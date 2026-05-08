#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>       /* para kmalloc e kfree */
#include <linux/list.h>       /* para struct list_head */
#include <linux/mutex.h>      /* para struct mutex */
#include <linux/proc_fs.h>    /* para procfs */
#include <linux/kobject.h>    /* para sysfs */
#include <linux/sched.h>      /* para o macro 'current' */
#include <linux/uaccess.h>    /* para copy_from_user e copy_to_user */


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Estudante");
MODULE_DESCRIPTION("Proc Driver");
MODULE_VERSION("0.0.1");

static unsigned int max_topics = 10; // Valor padrão
module_param(max_topics, uint, S_IRUGO); // Parâmetro de carga

static LIST_HEAD(topics_list);
static DEFINE_MUTEX(topics_mutex); // Mutex global de tópicos

static int major_number;
static struct class* pubsub_class = NULL;
static struct device* pubsub_device = NULL;

// Estrutura de Mensagem
struct pubsub_msg {
    char text[256];
    struct list_head list;
};

// Estrutura de Processo Inscrito (Subscriber)
struct pubsub_sub {
    pid_t pid;
    struct list_head messages;
    struct mutex msg_mutex;
    struct list_head list;
};

// Estrutura de Tópico
struct pubsub_topic {
    char name[64];
    struct list_head subscribers;
    unsigned int max_subscribers;
    unsigned int current_subscribers;
    unsigned long published_count;
    struct mutex sub_mutex;
    struct kobject *kobj;
    struct list_head list;
};

// Contexto privado de arquivo para cada processo aberto
struct pubsub_file_context {
    char current_topic[64];
};