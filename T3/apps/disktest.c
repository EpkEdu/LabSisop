/*
 * disktest.c - Aplicacao de teste para o escalonador de disco SSTF
 *
 * Trabalho 3 - Laboratorio de Sistemas Operacionais
 *
 * Gera um grande numero de requisicoes de leitura e escrita em regioes
 * aleatorias de um dispositivo de bloco (ou arquivo), usando fork() para
 * criar varios processos concorrentes. O objetivo e manter o escalonador
 * de disco ocupado para que a politica SSTF possa reordenar as requisicoes.
 *
 * Para que as requisicoes cheguem efetivamente ao escalonador, as operacoes
 * usam O_DIRECT (ignora a cache de paginas) sempre que possivel.
 *
 * Uso:
 *   disktest <dispositivo> <bloco_bytes> <disco_blocos> <num_ops> \
 *            <pct_escritas> <req_min> <req_max> <num_procs>
 *
 *   dispositivo    caminho do dispositivo de bloco (ex.: /dev/sda)
 *   bloco_bytes    tamanho do bloco em bytes (potencia de 2)
 *   disco_blocos   tamanho do disco em blocos
 *   num_ops        numero total de operacoes de E/S (por processo)
 *   pct_escritas   percentual de escritas (0-100); o resto sao leituras
 *   req_min        tamanho minimo de cada requisicao em bytes (<= bloco)
 *   req_max        tamanho maximo de cada requisicao em bytes (<= bloco)
 *   num_procs      numero de processos concorrentes (fork)
 *
 * Exemplo (disco de 4MB, blocos de 4096B -> blocos 0..1023):
 *   ./disktest /dev/sda 4096 1024 1000 50 512 4096 8
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

struct config {
	const char *device;
	long block_bytes;
	long disk_blocks;
	long num_ops;
	int pct_writes;
	long req_min;
	long req_max;
	int num_procs;
};

static long rand_range(unsigned int *seed, long min, long max)
{
	if (max <= min)
		return min;
	return min + (rand_r(seed) % (max - min + 1));
}

/*
 * Arredonda um valor para baixo ao multiplo de 'align'.
 * O_DIRECT exige que offset e tamanho sejam alinhados ao tamanho do bloco.
 */
static long align_down(long v, long align)
{
	return (v / align) * align;
}

static void worker(const struct config *c, int proc_idx)
{
	unsigned int seed = (unsigned int)(time(NULL) ^ (getpid() << 8) ^ proc_idx);
	long alignment = c->block_bytes;
	void *buf = NULL;
	int fd;
	long i;

	/* O_DIRECT envia a E/S direto ao dispositivo, sem cache de paginas */
	fd = open(c->device, O_RDWR | O_DIRECT);
	if (fd < 0) {
		/* fallback: sem O_DIRECT (passa pela cache, mas ainda gera E/S) */
		fd = open(c->device, O_RDWR);
		if (fd < 0) {
			fprintf(stderr, "[proc %d] erro ao abrir %s: %s\n",
				proc_idx, c->device, strerror(errno));
			_exit(1);
		}
	}

	/* buffer alinhado ao tamanho do bloco (necessario para O_DIRECT) */
	if (posix_memalign(&buf, alignment, c->block_bytes) != 0) {
		fprintf(stderr, "[proc %d] posix_memalign falhou\n", proc_idx);
		close(fd);
		_exit(1);
	}
	memset(buf, 0xAB, c->block_bytes);

	for (i = 0; i < c->num_ops; i++) {
		long block = rand_range(&seed, 0, c->disk_blocks - 1);
		off_t offset = (off_t)block * c->block_bytes;
		long size = rand_range(&seed, c->req_min, c->req_max);
		int is_write = (rand_range(&seed, 1, 100) <= c->pct_writes);
		ssize_t r;

		/* alinhamento exigido por O_DIRECT */
		size = align_down(size, alignment);
		if (size < alignment)
			size = alignment;
		if (size > c->block_bytes)
			size = c->block_bytes;

		if (is_write)
			r = pwrite(fd, buf, size, offset);
		else
			r = pread(fd, buf, size, offset);

		if (r < 0) {
			fprintf(stderr, "[proc %d] %s no bloco %ld: %s\n",
				proc_idx, is_write ? "escrita" : "leitura",
				block, strerror(errno));
		}
	}

	/* garante que as escritas pendentes cheguem ao dispositivo */
	fsync(fd);
	free(buf);
	close(fd);
	_exit(0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Uso: %s <dispositivo> <bloco_bytes> <disco_blocos> <num_ops>"
		" <pct_escritas> <req_min> <req_max> <num_procs>\n",
		prog);
}

int main(int argc, char **argv)
{
	struct config c;
	int i;
	pid_t pid;

	if (argc != 9) {
		usage(argv[0]);
		return 1;
	}

	c.device       = argv[1];
	c.block_bytes  = atol(argv[2]);
	c.disk_blocks  = atol(argv[3]);
	c.num_ops      = atol(argv[4]);
	c.pct_writes   = atoi(argv[5]);
	c.req_min      = atol(argv[6]);
	c.req_max      = atol(argv[7]);
	c.num_procs    = atoi(argv[8]);

	if (c.block_bytes <= 0 || (c.block_bytes & (c.block_bytes - 1)) != 0) {
		fprintf(stderr, "bloco_bytes deve ser potencia de 2 positiva\n");
		return 1;
	}
	if (c.disk_blocks <= 0 || c.num_ops <= 0 || c.num_procs <= 0) {
		fprintf(stderr, "disco_blocos, num_ops e num_procs devem ser > 0\n");
		return 1;
	}
	if (c.pct_writes < 0 || c.pct_writes > 100) {
		fprintf(stderr, "pct_escritas deve estar entre 0 e 100\n");
		return 1;
	}
	if (c.req_min <= 0 || c.req_max <= 0 ||
	    c.req_min > c.block_bytes || c.req_max > c.block_bytes ||
	    c.req_min > c.req_max) {
		fprintf(stderr, "req_min/req_max invalidos (0 < min <= max <= bloco)\n");
		return 1;
	}

	printf("disktest: %s, bloco=%ld B, disco=%ld blocos, ops/proc=%ld,"
	       " escritas=%d%%, req=[%ld,%ld] B, procs=%d\n",
	       c.device, c.block_bytes, c.disk_blocks, c.num_ops,
	       c.pct_writes, c.req_min, c.req_max, c.num_procs);

	for (i = 0; i < c.num_procs; i++) {
		pid = fork();
		if (pid < 0) {
			perror("fork");
			return 1;
		}
		if (pid == 0)
			worker(&c, i); /* nao retorna */
	}

	/* processo pai aguarda todos os filhos */
	for (i = 0; i < c.num_procs; i++)
		wait(NULL);

	printf("disktest: concluido (%ld operacoes totais)\n",
	       c.num_ops * c.num_procs);
	return 0;
}
