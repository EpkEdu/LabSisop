// SPDX-License-Identifier: GPL-2.0
/*
 * sstf.c - Escalonador de disco com politica SSTF (Shortest Seek Time First)
 *
 * Trabalho 3 - Laboratorio de Sistemas Operacionais
 *
 * O modulo enfileira requisicoes ate que (a) a fila atinja o tamanho maximo
 * configurado ou (b) o tempo maximo de espera (apos a ultima requisicao)
 * expire. Quando uma dessas condicoes ocorre, as requisicoes sao despachadas
 * na ordem definida pela politica SSTF: sempre a requisicao cujo setor esta
 * mais proximo da posicao atual do cabecote.
 *
 * Parametros (definidos em tempo de carga):
 *   queue_size  - numero de requisicoes a enfileirar antes de despachar
 *   max_wait_ms - tempo maximo de espera em milissegundos
 *   debug       - se != 0, gera mensagens no log do kernel (dmesg)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>

#include "elevator.h"

/* ------------------------------------------------------------------ */
/* Parametros do modulo                                               */
/* ------------------------------------------------------------------ */

static int queue_size = 32;
module_param(queue_size, int, 0644);
MODULE_PARM_DESC(queue_size, "Numero de requisicoes enfileiradas antes do despacho (20-100)");

static int max_wait_ms = 50;
module_param(max_wait_ms, int, 0644);
MODULE_PARM_DESC(max_wait_ms, "Tempo maximo de espera em milissegundos (20-100)");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Habilita mensagens de depuracao no log do kernel");

#define sstf_dbg(fmt, ...)                                             \
	do {                                                          \
		if (debug)                                           \
			printk(KERN_INFO "sstf: " fmt, ##__VA_ARGS__); \
	} while (0)

/* ------------------------------------------------------------------ */
/* Estrutura de dados do escalonador                                  */
/* ------------------------------------------------------------------ */

struct sstf_data {
	struct list_head queue;     /* fila interna de requisicoes pendentes */
	int nr_queued;              /* quantidade de requisicoes na fila     */
	sector_t head_pos;          /* posicao atual do cabecote             */
	bool flushing;              /* batch liberado para despacho?         */

	spinlock_t lock;            /* protege a estrutura (uso em IRQ)      */
	struct hrtimer timer;       /* temporizador de espera maxima         */
	struct request_queue *q;    /* fila de requisicoes do dispositivo    */

	/* estatisticas acumuladas para avaliacao de desempenho */
	unsigned long long total_sstf;  /* setores percorridos com SSTF      */
	unsigned long long total_fifo;  /* setores percorridos sem reordenar */
	unsigned long batch_id;         /* contador de lotes despachados     */
};

/* ------------------------------------------------------------------ */
/* Auxiliares                                                          */
/* ------------------------------------------------------------------ */

static inline sector_t sstf_abs_diff(sector_t a, sector_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

/*
 * Calcula o custo (total de setores percorridos) caso o lote atual fosse
 * atendido na ordem de chegada (FIFO), partindo da posicao do cabecote.
 * Tambem registra no log a ordem de chegada quando debug esta ativo.
 */
static unsigned long long sstf_fifo_cost(struct sstf_data *sd)
{
	struct request *rq;
	sector_t pos = sd->head_pos;
	unsigned long long cost = 0;

	list_for_each_entry(rq, &sd->queue, queuelist) {
		sector_t s = blk_rq_pos(rq);
		cost += sstf_abs_diff(pos, s);
		sstf_dbg("  [chegada] setor=%llu op=%s\n",
			 (unsigned long long)s,
			 (rq_data_dir(rq) == WRITE) ? "W" : "R");
		pos = s;
	}
	return cost;
}

/* ------------------------------------------------------------------ */
/* Timer de espera maxima                                             */
/* ------------------------------------------------------------------ */

static enum hrtimer_restart sstf_timer_fn(struct hrtimer *timer)
{
	struct sstf_data *sd = container_of(timer, struct sstf_data, timer);
	unsigned long flags;
	bool run = false;

	spin_lock_irqsave(&sd->lock, flags);
	if (sd->nr_queued > 0 && !sd->flushing) {
		sd->flushing = true;
		run = true;
		sstf_dbg("timer expirou: liberando %d requisicoes (lote %lu)\n",
			 sd->nr_queued, sd->batch_id + 1);
		sd->total_fifo += sstf_fifo_cost(sd);
	}
	spin_unlock_irqrestore(&sd->lock, flags);

	if (run)
		blk_mq_run_hw_queues(sd->q, true);

	return HRTIMER_NORESTART;
}

/* ------------------------------------------------------------------ */
/* Callbacks do elevator                                              */
/* ------------------------------------------------------------------ */

static int sstf_init_sched(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data *sd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	sd = kzalloc_node(sizeof(*sd), GFP_KERNEL, q->node);
	if (!sd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&sd->queue);
	sd->nr_queued = 0;
	sd->head_pos = 0;
	sd->flushing = false;
	sd->total_sstf = 0;
	sd->total_fifo = 0;
	sd->batch_id = 0;
	sd->q = q;
	spin_lock_init(&sd->lock);

	hrtimer_init(&sd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sd->timer.function = sstf_timer_fn;

	eq->elevator_data = sd;

	/* Sinaliza que trabalhamos como um escalonador de fila unica */
	blk_queue_flag_set(QUEUE_FLAG_SQ_SCHED, q);
	q->elevator = eq;

	sstf_dbg("init: queue_size=%d max_wait_ms=%d\n",
		 queue_size, max_wait_ms);
	return 0;
}

static void sstf_exit_sched(struct elevator_queue *e)
{
	struct sstf_data *sd = e->elevator_data;

	hrtimer_cancel(&sd->timer);

	if (debug) {
		unsigned long long fifo = sd->total_fifo;
		unsigned long long sstf = sd->total_sstf;
		long long economia = (long long)fifo - (long long)sstf;

		printk(KERN_INFO "sstf: ===== RESUMO FINAL =====\n");
		printk(KERN_INFO "sstf: lotes despachados=%lu\n", sd->batch_id);
		printk(KERN_INFO "sstf: setores percorridos sem reordenar (FIFO)=%llu\n",
		       fifo);
		printk(KERN_INFO "sstf: setores percorridos com SSTF=%llu\n", sstf);
		printk(KERN_INFO "sstf: reducao=%lld setores\n", economia);
	}

	WARN_ON_ONCE(!list_empty(&sd->queue));
	kfree(sd);
}

static bool sstf_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long flags;
	bool work;

	spin_lock_irqsave(&sd->lock, flags);
	/* So ha trabalho quando o lote foi liberado (timer ou fila cheia) */
	work = sd->flushing && sd->nr_queued > 0;
	spin_unlock_irqrestore(&sd->lock, flags);

	return work;
}

static void sstf_insert_requests(struct blk_mq_hw_ctx *hctx,
				 struct list_head *list,
				 blk_insert_t flags)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long irqflags;
	bool full = false;

	spin_lock_irqsave(&sd->lock, irqflags);

	while (!list_empty(list)) {
		struct request *rq = list_first_entry(list, struct request,
						      queuelist);
		list_del_init(&rq->queuelist);
		list_add_tail(&rq->queuelist, &sd->queue);
		sd->nr_queued++;

		sstf_dbg("insercao: setor=%llu op=%s (fila=%d)\n",
			 (unsigned long long)blk_rq_pos(rq),
			 (rq_data_dir(rq) == WRITE) ? "W" : "R",
			 sd->nr_queued);
	}

	if (sd->nr_queued >= queue_size && !sd->flushing) {
		sd->flushing = true;
		full = true;
		sstf_dbg("fila cheia (%d): liberando lote %lu\n",
			 sd->nr_queued, sd->batch_id + 1);
		sd->total_fifo += sstf_fifo_cost(sd);
	}

	if (full) {
		/* fila cheia: cancela timer, despacharemos agora */
		hrtimer_cancel(&sd->timer);
	} else if (!sd->flushing) {
		/* reinicia o temporizador a cada nova requisicao */
		hrtimer_start(&sd->timer,
			      ms_to_ktime(max_wait_ms),
			      HRTIMER_MODE_REL);
	}

	spin_unlock_irqrestore(&sd->lock, irqflags);

	if (full)
		blk_mq_run_hw_queues(sd->q, true);
}

static struct request *sstf_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct sstf_data *sd = hctx->queue->elevator->elevator_data;
	unsigned long flags;
	struct request *rq, *best = NULL;
	sector_t best_diff = 0;
	char dir;

	spin_lock_irqsave(&sd->lock, flags);

	if (!sd->flushing || list_empty(&sd->queue)) {
		spin_unlock_irqrestore(&sd->lock, flags);
		return NULL;
	}

	/* Seleciona a requisicao mais proxima da posicao atual do cabecote */
	list_for_each_entry(rq, &sd->queue, queuelist) {
		sector_t diff = sstf_abs_diff(sd->head_pos, blk_rq_pos(rq));

		if (!best || diff < best_diff) {
			best = rq;
			best_diff = diff;
		}
	}

	if (!best) {
		spin_unlock_irqrestore(&sd->lock, flags);
		return NULL;
	}

	dir = (blk_rq_pos(best) < sd->head_pos) ? 'L' : 'R';
	sd->total_sstf += best_diff;

	sstf_dbg("despacho: setor=%llu dir=%c op=%s dist=%llu\n",
		 (unsigned long long)blk_rq_pos(best), dir,
		 (rq_data_dir(best) == WRITE) ? "W" : "R",
		 (unsigned long long)best_diff);

	sd->head_pos = blk_rq_pos(best);
	list_del_init(&best->queuelist);
	sd->nr_queued--;

	/* Lote esgotado: encerra o flush e aguarda novas requisicoes */
	if (sd->nr_queued == 0) {
		sd->flushing = false;
		sd->batch_id++;
		sstf_dbg("lote %lu concluido\n", sd->batch_id);
	}

	spin_unlock_irqrestore(&sd->lock, flags);
	return best;
}

static void sstf_finish_request(struct request *rq)
{
	return;
}

/* ------------------------------------------------------------------ */
/* Registro do escalonador                                            */
/* ------------------------------------------------------------------ */

static struct elevator_type sstf = {
	.ops = {
		.init_sched       = sstf_init_sched,
		.exit_sched       = sstf_exit_sched,
		.insert_requests  = sstf_insert_requests,
		.dispatch_request = sstf_dispatch_request,
		.has_work         = sstf_has_work,
		.finish_request   = sstf_finish_request,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_mod_init(void)
{
	printk(KERN_INFO "sstf: registrando escalonador SSTF\n");
	return elv_register(&sstf);
}

static void __exit sstf_mod_exit(void)
{
	printk(KERN_INFO "sstf: removendo escalonador SSTF\n");
	elv_unregister(&sstf);
}

module_init(sstf_mod_init);
module_exit(sstf_mod_exit);

MODULE_AUTHOR("Lorran Marques");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Escalonador de disco SSTF (Shortest Seek Time First)");
