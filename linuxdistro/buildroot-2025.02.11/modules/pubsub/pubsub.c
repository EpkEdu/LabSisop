// SPDX-License-Identifier: GPL-2.0
/*
 * pubsub.c - Loadable Kernel Module: Publish/Subscribe broker
 *
 * Expõe /dev/pubsub. Comandos via write():
 *   /subscribe <topico>
 *   /unsubscribe <topico>
 *   /publish <topico> <mensagem ou "mensagem com espacos">
 *   /fetch <topico>
 *
 * Após /fetch, read() retorna uma mensagem por chamada (0 = sem mensagens).
 * fclose() desinscreve o processo de todos os tópicos.
 *
 * sysfs:  /sys/pubsub/<topico>     → leitura/escrita do max_subscribers
 * procfs: /proc/pubsub             → contagem de publicações por tópico
 *
 * Parâmetros de módulo:
 *   max_topics           (padrão 16)
 *   default_max_subscribers (padrão 10)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grupo T2 - Lab. Sistemas Operacionais");
MODULE_DESCRIPTION("Publish/Subscribe broker como device driver");

/* ------------------------------------------------------------------ */
/*  Parâmetros                                                          */
/* ------------------------------------------------------------------ */

static int max_topics = 16;
module_param(max_topics, int, 0444);
MODULE_PARM_DESC(max_topics, "Número máximo de tópicos simultâneos");

static int default_max_subscribers = 10;
module_param(default_max_subscribers, int, 0444);
MODULE_PARM_DESC(default_max_subscribers, "Max. de inscritos padrão por tópico");

/* ------------------------------------------------------------------ */
/*  Dispositivo de caractere                                            */
/* ------------------------------------------------------------------ */

#define DEVICE_NAME   "pubsub"
#define MAX_CMD_LEN   4096
#define TOPIC_NAME_LEN 64

static dev_t         dev_num;
static struct cdev   pubsub_cdev;
static struct class *pubsub_class;

/* ------------------------------------------------------------------ */
/*  Estruturas de dados                                                 */
/* ------------------------------------------------------------------ */

/** Mensagem pendente para um processo inscrito */
struct pubsub_message {
	char            *data;
	size_t           len;
	struct list_head list;
};

/** Entrada de processo inscrito num tópico */
struct pubsub_subscriber {
	pid_t            pid;
	struct list_head messages;   /* lista de pubsub_message */
	struct mutex     msg_mutex;  /* protege a lista de mensagens */
	int              msg_count;
	struct list_head list;
};

/** Tópico */
struct pubsub_topic {
	char             name[TOPIC_NAME_LEN];
	struct list_head subscribers;  /* lista de pubsub_subscriber */
	struct mutex     sub_mutex;    /* protege a lista de inscritos */
	int              sub_count;
	int              max_subscribers;
	unsigned long    published_count;
	struct kobj_attribute kattr;   /* /sys/pubsub/<name> */
	struct list_head list;
};

/** Dados privados por fd aberto */
struct pubsub_private {
	char fetch_topic[TOPIC_NAME_LEN]; /* tópico configurado pelo /fetch */
};

/* ------------------------------------------------------------------ */
/*  Estado global                                                       */
/* ------------------------------------------------------------------ */

static LIST_HEAD(topics_list);
static DEFINE_MUTEX(topics_mutex);
static int topic_count = 0;

static struct kobject     *pubsub_kobj;   /* /sys/pubsub/ */
static struct proc_dir_entry *pubsub_proc; /* /proc/pubsub */

/* ------------------------------------------------------------------ */
/*  Funções auxiliares internas                                         */
/* ------------------------------------------------------------------ */

/* Caller DEVE segurar topics_mutex */
static struct pubsub_topic *find_topic_locked(const char *name)
{
	struct pubsub_topic *t;
	list_for_each_entry(t, &topics_list, list) {
		if (strncmp(t->name, name, TOPIC_NAME_LEN) == 0)
			return t;
	}
	return NULL;
}

/* Caller DEVE segurar topic->sub_mutex */
static struct pubsub_subscriber *find_subscriber_locked(
		struct pubsub_topic *topic, pid_t pid)
{
	struct pubsub_subscriber *s;
	list_for_each_entry(s, &topic->subscribers, list) {
		if (s->pid == pid)
			return s;
	}
	return NULL;
}

/* Libera todas as mensagens de um inscrito. Não requer nenhum lock externo;
 * adquire sub->msg_mutex internamente. */
static void free_subscriber_messages(struct pubsub_subscriber *sub)
{
	struct pubsub_message *msg, *tmp;

	mutex_lock(&sub->msg_mutex);
	list_for_each_entry_safe(msg, tmp, &sub->messages, list) {
		list_del(&msg->list);
		kfree(msg->data);
		kfree(msg);
	}
	sub->msg_count = 0;
	mutex_unlock(&sub->msg_mutex);
}

/*
 * Remove o processo 'pid' do tópico.
 * Se o tópico ficar vazio, remove-o da lista global e libera.
 *
 * Caller DEVE segurar topics_mutex.
 * Ordem de locks: topics_mutex → sub_mutex → msg_mutex  (sem inversão)
 */
static void remove_subscriber_from_topic(struct pubsub_topic *topic, pid_t pid)
{
	struct pubsub_subscriber *sub = NULL;
	bool topic_empty = false;

	mutex_lock(&topic->sub_mutex);
	sub = find_subscriber_locked(topic, pid);
	if (sub) {
		list_del(&sub->list);
		topic->sub_count--;
		if (topic->sub_count == 0)
			topic_empty = true;
	}
	mutex_unlock(&topic->sub_mutex);

	if (!sub)
		return;

	/* Libera mensagens e estrutura do inscrito */
	free_subscriber_messages(sub);
	kfree(sub);

	/* Se o tópico ficou vazio, remove da lista global e destrói */
	if (topic_empty) {
		list_del(&topic->list);
		topic_count--;
		sysfs_remove_file(pubsub_kobj, &topic->kattr.attr);
		kfree(topic);
	}
}

/* ------------------------------------------------------------------ */
/*  sysfs – /sys/pubsub/<nome_do_topico>                               */
/*  Controla o max_subscribers de cada tópico em tempo de execução.    */
/* ------------------------------------------------------------------ */

static ssize_t topic_max_sub_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct pubsub_topic *topic =
		container_of(attr, struct pubsub_topic, kattr);
	return sysfs_emit(buf, "%d\n", topic->max_subscribers);
}

static ssize_t topic_max_sub_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	struct pubsub_topic *topic =
		container_of(attr, struct pubsub_topic, kattr);
	int val;

	if (kstrtoint(buf, 10, &val) || val <= 0)
		return -EINVAL;

	/*
	 * Spec: "não devem ser excluídos processos inscritos" se o novo
	 * valor for menor que o atual sub_count.
	 * Simplesmente atualizamos o limite; futuras inscrições serão
	 * bloqueadas se sub_count >= max_subscribers.
	 */
	topic->max_subscribers = val;
	return count;
}

/* ------------------------------------------------------------------ */
/*  procfs – /proc/pubsub                                              */
/* ------------------------------------------------------------------ */

static int pubsub_proc_show(struct seq_file *m, void *v)
{
	struct pubsub_topic *t;

	mutex_lock(&topics_mutex);
	list_for_each_entry(t, &topics_list, list)
		seq_printf(m, "%s: %lu\n", t->name, t->published_count);
	mutex_unlock(&topics_mutex);
	return 0;
}

static int pubsub_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pubsub_proc_show, NULL);
}

static const struct proc_ops pubsub_proc_ops = {
	.proc_open    = pubsub_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  Handlers dos comandos de escrita                                    */
/* ------------------------------------------------------------------ */

/*
 * /subscribe <topico>
 * Cria o tópico se não existir (respeitando max_topics).
 * Ignora silenciosamente se já inscrito ou se max_subscribers atingido.
 */
static ssize_t handle_subscribe(struct file *file,
				 const char *topic_name, ssize_t orig_count)
{
	pid_t pid = task_pid_nr(current);
	struct pubsub_topic *topic;
	struct pubsub_subscriber *sub;
	int ret;

	if (strnlen(topic_name, TOPIC_NAME_LEN) >= TOPIC_NAME_LEN)
		return -EINVAL;

	mutex_lock(&topics_mutex);

	topic = find_topic_locked(topic_name);
	if (!topic) {
		/* Criar novo tópico */
		if (topic_count >= max_topics) {
			mutex_unlock(&topics_mutex);
			return orig_count; /* limite atingido, ignora */
		}

		topic = kzalloc(sizeof(*topic), GFP_KERNEL);
		if (!topic) {
			mutex_unlock(&topics_mutex);
			return -ENOMEM;
		}

		strscpy(topic->name, topic_name, TOPIC_NAME_LEN);
		INIT_LIST_HEAD(&topic->subscribers);
		mutex_init(&topic->sub_mutex);
		topic->sub_count       = 0;
		topic->max_subscribers = default_max_subscribers;
		topic->published_count = 0;

		/* Configura o atributo sysfs */
		topic->kattr.attr.name = topic->name;
		topic->kattr.attr.mode = 0644;
		topic->kattr.show      = topic_max_sub_show;
		topic->kattr.store     = topic_max_sub_store;
		sysfs_attr_init(&topic->kattr.attr);

		ret = sysfs_create_file(pubsub_kobj, &topic->kattr.attr);
		if (ret) {
			kfree(topic);
			mutex_unlock(&topics_mutex);
			return ret;
		}

		list_add_tail(&topic->list, &topics_list);
		topic_count++;
	}

	/* Verifica inscrição duplicada e limite de inscritos */
	mutex_lock(&topic->sub_mutex);

	if (find_subscriber_locked(topic, pid)) {
		/* Já inscrito: ignora silenciosamente */
		mutex_unlock(&topic->sub_mutex);
		mutex_unlock(&topics_mutex);
		return orig_count;
	}

	if (topic->sub_count >= topic->max_subscribers) {
		mutex_unlock(&topic->sub_mutex);
		mutex_unlock(&topics_mutex);
		return orig_count; /* limite de inscritos, ignora */
	}

	sub = kzalloc(sizeof(*sub), GFP_KERNEL);
	if (!sub) {
		mutex_unlock(&topic->sub_mutex);
		mutex_unlock(&topics_mutex);
		return -ENOMEM;
	}

	sub->pid = pid;
	INIT_LIST_HEAD(&sub->messages);
	mutex_init(&sub->msg_mutex);
	sub->msg_count = 0;

	list_add_tail(&sub->list, &topic->subscribers);
	topic->sub_count++;

	mutex_unlock(&topic->sub_mutex);
	mutex_unlock(&topics_mutex);

	return orig_count;
}

/*
 * /unsubscribe <topico>
 * Remove o processo e suas mensagens pendentes.
 * Remove o tópico se ficar sem inscritos.
 */
static ssize_t handle_unsubscribe(struct file *file,
				   const char *topic_name, ssize_t orig_count)
{
	pid_t pid = task_pid_nr(current);
	struct pubsub_topic *topic;

	mutex_lock(&topics_mutex);
	topic = find_topic_locked(topic_name);
	if (topic)
		remove_subscriber_from_topic(topic, pid);
	mutex_unlock(&topics_mutex);

	return orig_count;
}

/*
 * /publish <topico> <mensagem>
 * Enfileira uma cópia da mensagem para cada inscrito no tópico.
 * Ignora se o tópico não existir ou não tiver inscritos.
 */
static ssize_t handle_publish(struct file *file,
			       const char *args, ssize_t orig_count)
{
	char topic_name[TOPIC_NAME_LEN];
	const char *space;
	const char *msg_start;
	size_t name_len, msg_len;
	struct pubsub_topic *topic;
	struct pubsub_subscriber *sub;

	/* Extrai nome do tópico (primeira palavra) */
	space = strchr(args, ' ');
	if (!space)
		return -EINVAL;

	name_len = space - args;
	if (name_len == 0 || name_len >= TOPIC_NAME_LEN)
		return -EINVAL;

	memcpy(topic_name, args, name_len);
	topic_name[name_len] = '\0';

	/* Conteúdo da mensagem (restante após o espaço) */
	msg_start = space + 1;

	/* Remove aspas externas se presentes */
	if (*msg_start == '"') {
		msg_start++;
		msg_len = strlen(msg_start);
		if (msg_len > 0 && msg_start[msg_len - 1] == '"')
			msg_len--;
	} else {
		msg_len = strlen(msg_start);
	}

	if (msg_len == 0)
		return orig_count;

	mutex_lock(&topics_mutex);

	topic = find_topic_locked(topic_name);
	if (!topic) {
		mutex_unlock(&topics_mutex);
		return orig_count; /* tópico inexistente, ignora */
	}

	mutex_lock(&topic->sub_mutex);

	if (topic->sub_count == 0) {
		mutex_unlock(&topic->sub_mutex);
		mutex_unlock(&topics_mutex);
		return orig_count; /* nenhum inscrito, ignora */
	}

	topic->published_count++;

	/* Distribui uma cópia para cada inscrito */
	list_for_each_entry(sub, &topic->subscribers, list) {
		struct pubsub_message *msg;

		msg = kzalloc(sizeof(*msg), GFP_KERNEL);
		if (!msg)
			continue; /* OOM: pula este inscrito */

		msg->data = kmalloc(msg_len + 1, GFP_KERNEL);
		if (!msg->data) {
			kfree(msg);
			continue;
		}

		memcpy(msg->data, msg_start, msg_len);
		msg->data[msg_len] = '\0';
		msg->len = msg_len;

		mutex_lock(&sub->msg_mutex);
		list_add_tail(&msg->list, &sub->messages);
		sub->msg_count++;
		mutex_unlock(&sub->msg_mutex);
	}

	mutex_unlock(&topic->sub_mutex);
	mutex_unlock(&topics_mutex);

	return orig_count;
}

/*
 * /fetch <topico>
 * Configura qual tópico será lido nas próximas chamadas a read().
 * Persiste em filep->private_data durante toda a vida do fd.
 */
static ssize_t handle_fetch(struct file *file,
			     const char *topic_name, ssize_t orig_count)
{
	struct pubsub_private *priv = file->private_data;

	strscpy(priv->fetch_topic, topic_name, TOPIC_NAME_LEN);
	return orig_count;
}

/* ------------------------------------------------------------------ */
/*  file_operations                                                     */
/* ------------------------------------------------------------------ */

static int pubsub_open(struct inode *inode, struct file *file)
{
	struct pubsub_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	file->private_data = priv;
	return 0;
}

static int pubsub_release(struct inode *inode, struct file *file)
{
	struct pubsub_private *priv = file->private_data;
	pid_t pid = task_pid_nr(current);
	struct pubsub_topic *t, *ttmp;

	/*
	 * Desinscreve o processo de todos os tópicos em que estava inscrito.
	 * list_for_each_entry_safe é necessário pois remove_subscriber_from_topic
	 * pode deletar 't' da lista quando o tópico fica vazio.
	 */
	mutex_lock(&topics_mutex);
	list_for_each_entry_safe(t, ttmp, &topics_list, list)
		remove_subscriber_from_topic(t, pid);
	mutex_unlock(&topics_mutex);

	kfree(priv);
	file->private_data = NULL;
	return 0;
}

static ssize_t pubsub_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret;

	if (count == 0 || count > MAX_CMD_LEN)
		return -EINVAL;

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, ubuf, count)) {
		kfree(kbuf);
		return -EFAULT;
	}
	kbuf[count] = '\0';

	/* Remove newline final */
	if (count > 0 && kbuf[count - 1] == '\n')
		kbuf[count - 1] = '\0';

	/* Despacha para o handler correto */
	if (strncmp(kbuf, "/subscribe ", 11) == 0)
		ret = handle_subscribe(file, kbuf + 11, (ssize_t)count);
	else if (strncmp(kbuf, "/unsubscribe ", 13) == 0)
		ret = handle_unsubscribe(file, kbuf + 13, (ssize_t)count);
	else if (strncmp(kbuf, "/publish ", 9) == 0)
		ret = handle_publish(file, kbuf + 9, (ssize_t)count);
	else if (strncmp(kbuf, "/fetch ", 7) == 0)
		ret = handle_fetch(file, kbuf + 7, (ssize_t)count);
	else
		ret = -EINVAL;

	kfree(kbuf);
	return ret;
}

static ssize_t pubsub_read(struct file *file, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct pubsub_private *priv = file->private_data;
	pid_t pid = task_pid_nr(current);
	struct pubsub_topic *topic;
	struct pubsub_subscriber *sub;
	struct pubsub_message *msg = NULL;
	ssize_t ret;

	if (priv->fetch_topic[0] == '\0')
		return 0; /* nenhum tópico selecionado */

	/*
	 * Ordem de aquisição: topics_mutex → sub_mutex → msg_mutex
	 * (mesma em toda a implementação → sem deadlock)
	 */
	mutex_lock(&topics_mutex);

	topic = find_topic_locked(priv->fetch_topic);
	if (!topic) {
		mutex_unlock(&topics_mutex);
		return 0;
	}

	mutex_lock(&topic->sub_mutex);
	sub = find_subscriber_locked(topic, pid);
	if (!sub) {
		mutex_unlock(&topic->sub_mutex);
		mutex_unlock(&topics_mutex);
		return 0;
	}

	mutex_lock(&sub->msg_mutex);
	if (!list_empty(&sub->messages)) {
		msg = list_first_entry(&sub->messages,
				       struct pubsub_message, list);
		list_del(&msg->list);
		sub->msg_count--;
	}
	mutex_unlock(&sub->msg_mutex);

	mutex_unlock(&topic->sub_mutex);
	mutex_unlock(&topics_mutex);

	if (!msg)
		return 0; /* sem mensagens */

	ret = (ssize_t)min(count, msg->len);
	if (copy_to_user(ubuf, msg->data, (unsigned long)ret))
		ret = -EFAULT;

	kfree(msg->data);
	kfree(msg);
	return ret;
}

static const struct file_operations pubsub_fops = {
	.owner   = THIS_MODULE,
	.open    = pubsub_open,
	.release = pubsub_release,
	.read    = pubsub_read,
	.write   = pubsub_write,
};

/* ------------------------------------------------------------------ */
/*  Inicialização e finalização do módulo                              */
/* ------------------------------------------------------------------ */

static int __init pubsub_init(void)
{
	struct device *dev;
	int ret;

	/* Registra intervalo de números de dispositivo */
	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("pubsub: alloc_chrdev_region falhou: %d\n", ret);
		return ret;
	}

	/* Registra o cdev */
	cdev_init(&pubsub_cdev, &pubsub_fops);
	ret = cdev_add(&pubsub_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("pubsub: cdev_add falhou: %d\n", ret);
		goto err_unreg;
	}

	/* Cria a classe do dispositivo (kernel >= 6.4 não recebe THIS_MODULE) */
	pubsub_class = class_create(DEVICE_NAME);
	if (IS_ERR(pubsub_class)) {
		ret = PTR_ERR(pubsub_class);
		pr_err("pubsub: class_create falhou: %d\n", ret);
		goto err_cdev;
	}

	/* Cria /dev/pubsub */
	dev = device_create(pubsub_class, NULL, dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		pr_err("pubsub: device_create falhou: %d\n", ret);
		goto err_class;
	}

	/* Cria kobject pai em /sys/pubsub/ */
	pubsub_kobj = kobject_create_and_add("pubsub", NULL);
	if (!pubsub_kobj) {
		ret = -ENOMEM;
		pr_err("pubsub: kobject_create_and_add falhou\n");
		goto err_device;
	}

	/* Cria /proc/pubsub */
	pubsub_proc = proc_create("pubsub", 0444, NULL, &pubsub_proc_ops);
	if (!pubsub_proc) {
		ret = -ENOMEM;
		pr_err("pubsub: proc_create falhou\n");
		goto err_kobj;
	}

	pr_info("pubsub: modulo carregado. major=%d max_topics=%d default_max_sub=%d\n",
		MAJOR(dev_num), max_topics, default_max_subscribers);
	return 0;

err_kobj:
	kobject_put(pubsub_kobj);
err_device:
	device_destroy(pubsub_class, dev_num);
err_class:
	class_destroy(pubsub_class);
err_cdev:
	cdev_del(&pubsub_cdev);
err_unreg:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit pubsub_exit(void)
{
	struct pubsub_topic      *t,    *ttmp;
	struct pubsub_subscriber *s,    *stmp;
	struct pubsub_message    *m,    *mtmp;

	proc_remove(pubsub_proc);

	/* Libera todos os tópicos, inscritos e mensagens */
	mutex_lock(&topics_mutex);
	list_for_each_entry_safe(t, ttmp, &topics_list, list) {
		sysfs_remove_file(pubsub_kobj, &t->kattr.attr);

		mutex_lock(&t->sub_mutex);
		list_for_each_entry_safe(s, stmp, &t->subscribers, list) {
			list_del(&s->list);

			mutex_lock(&s->msg_mutex);
			list_for_each_entry_safe(m, mtmp, &s->messages, list) {
				list_del(&m->list);
				kfree(m->data);
				kfree(m);
			}
			mutex_unlock(&s->msg_mutex);

			kfree(s);
		}
		mutex_unlock(&t->sub_mutex);

		list_del(&t->list);
		kfree(t);
	}
	topic_count = 0;
	mutex_unlock(&topics_mutex);

	kobject_put(pubsub_kobj);
	device_destroy(pubsub_class, dev_num);
	class_destroy(pubsub_class);
	cdev_del(&pubsub_cdev);
	unregister_chrdev_region(dev_num, 1);

	pr_info("pubsub: modulo removido\n");
}

module_init(pubsub_init);
module_exit(pubsub_exit);