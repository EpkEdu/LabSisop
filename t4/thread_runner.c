/*
 * thread_runner.c - Avaliacao de escalonamento de threads
 *
 * Trabalho 4 - Laboratorio de Sistemas Operacionais
 *
 * Uso:
 *   thread_runner <num_threads> <buffer_kb> <politica> <prioridade>
 *
 * Politicas:
 *   OTHER, BATCH, IDLE, RR, FIFO
 *
 * Exemplos:
 # Outras políticas normais
thread_runner 4 1 BATCH 0
thread_runner 4 1 IDLE 0

thread_runner 4 1 RR 1
thread_runner 4 1 RR 99
thread_runner 4 1 FIFO 1
thread_runner 4 1 FIFO 99
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Configuracao global                                                 */
/* ------------------------------------------------------------------ */

static char          *buffer;       /* buffer global compartilhado    */
static char          *buf_ptr;      /* ponteiro corrente no buffer     */
static char          *buf_end;      /* fim do buffer                   */
static atomic_flag    lock = ATOMIC_FLAG_INIT; /* mutex spinlock       */

static int            num_threads;
static long           buf_size;     /* tamanho em bytes                */

/* contagem de quantas vezes cada thread escreveu (foi escalonada) */
static long          *counts;

/* ------------------------------------------------------------------ */
/* Espera ocupada de ~1 ms usando CLOCK_MONOTONIC_RAW                 */
/* ------------------------------------------------------------------ */
static void busy_wait_ms(long ms)
{
    struct timespec start, now;
    long elapsed_ns;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    do {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        elapsed_ns = (now.tv_sec - start.tv_sec) * 1000000000L
                   + (now.tv_nsec - start.tv_nsec);
    } while (elapsed_ns < ms * 1000000L);
}

/* ------------------------------------------------------------------ */
/* Funcao de cada worker thread                                        */
/* ------------------------------------------------------------------ */
static void *worker(void *arg)
{
    int   idx  = (int)(long)arg;
    char  ch   = 'A' + idx;

    while (1) {
        /* --- adquire spinlock (espera ocupada) --- */
        while (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire))
            ; /* gira ate adquirir */

        /* --- secao critica --- */
        if (buf_ptr >= buf_end) {
            /* buffer cheio: libera e encerra */
            atomic_flag_clear_explicit(&lock, memory_order_release);
            break;
        }
        *buf_ptr = ch;
        buf_ptr++;
        counts[idx]++;

        /* --- libera spinlock --- */
        atomic_flag_clear_explicit(&lock, memory_order_release);

        /* --- delay com espera ocupada (~1 ms) --- */
        busy_wait_ms(1);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Traducao do nome da politica para constante do kernel              */
/* ------------------------------------------------------------------ */
static int parse_policy(const char *name)
{
    if (strcmp(name, "OTHER") == 0) return SCHED_OTHER;
    if (strcmp(name, "BATCH") == 0) return SCHED_BATCH;
    if (strcmp(name, "IDLE")  == 0) return SCHED_IDLE;
    if (strcmp(name, "RR")    == 0) return SCHED_RR;
    if (strcmp(name, "FIFO")  == 0) return SCHED_FIFO;
    fprintf(stderr, "Politica invalida: %s\n", name);
    fprintf(stderr, "Use: OTHER, BATCH, IDLE, RR ou FIFO\n");
    exit(1);
}

static const char *policy_name(int policy)
{
    switch (policy) {
        case SCHED_OTHER: return "SCHED_OTHER";
        case SCHED_BATCH: return "SCHED_BATCH";
        case SCHED_IDLE:  return "SCHED_IDLE";
        case SCHED_RR:    return "SCHED_RR";
        case SCHED_FIFO:  return "SCHED_FIFO";
        default:          return "DESCONHECIDA";
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr,
            "Uso: %s <num_threads> <buffer_kb> <politica> <prioridade>\n"
            "  Politicas: OTHER, BATCH, IDLE, RR, FIFO\n"
            "  Exemplo:   %s 4 64 OTHER 0\n"
            "             %s 4 64 RR 50\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    num_threads      = atoi(argv[1]);
    long buf_kb      = atol(argv[2]);
    int  policy      = parse_policy(argv[3]);
    int  priority    = atoi(argv[4]);

    if (num_threads < 1 || num_threads > 26) {
        fprintf(stderr, "num_threads deve ser entre 1 e 26\n");
        return 1;
    }
    if (buf_kb < 1) {
        fprintf(stderr, "buffer_kb deve ser >= 1\n");
        return 1;
    }

    buf_size = buf_kb * 1024L;

    /* --- aplica politica e prioridade ao processo atual --- */
    struct sched_param sp = { .sched_priority = priority };

    /* Para OTHER/BATCH/IDLE, prioridade estatica deve ser 0 */
    if (policy == SCHED_OTHER || policy == SCHED_BATCH || policy == SCHED_IDLE)
        sp.sched_priority = 0;

    if (sched_setscheduler(0, policy, &sp) < 0) {
        perror("sched_setscheduler");
        fprintf(stderr,
            "Dica: politicas de tempo real (RR/FIFO) requerem root.\n");
        return 1;
    }

    /* Para OTHER/BATCH/IDLE, aplica nice value se prioridade != 0 */
    if ((policy == SCHED_OTHER || policy == SCHED_BATCH || policy == SCHED_IDLE)
        && priority != 0) {
        if (nice(priority) == -1 && errno != 0)
            perror("nice (aviso)");
    }

    fprintf(stderr,
        "Configuracao: threads=%d buffer=%ld KB politica=%s prioridade=%d\n",
        num_threads, buf_kb, policy_name(policy), priority);

    /* --- aloca buffer e contadores --- */
    buffer   = malloc(buf_size + 1);
    counts   = calloc(num_threads, sizeof(long));
    if (!buffer || !counts) {
        perror("malloc");
        return 1;
    }
    memset(buffer, 0, buf_size + 1);
    buf_ptr = buffer;
    buf_end = buffer + buf_size;

    /* --- cria as worker threads --- */
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker, (void *)(long)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    /* --- aguarda todas terminarem --- */
    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    /* --- imprime buffer formatado em linhas de 72 chars --- */
    printf("\n");
    long line_width = 72;
    for (long i = 0; i < buf_size; i++) {
        putchar(buffer[i]);
        if ((i + 1) % line_width == 0)
            putchar('\n');
    }
    printf("\n");

    /* --- sequencia de escalonamento --- */
    printf("\nSequencia: ");
    char prev = 0;
    for (long i = 0; i < buf_size; i++) {
        if (buffer[i] != prev) {
            putchar(buffer[i]);
            prev = buffer[i];
        }
    }
    printf("\n");

    /* --- contagem por thread --- */
    printf("\nContagem de escalonamentos:\n");
    for (int i = 0; i < num_threads; i++)
        printf("  %c = %ld\n", 'A' + i, counts[i]);

    free(buffer);
    free(counts);
    free(threads);
    return 0;
}