/*
  pubsub-multi.c
 
  Demonstra múltiplos processos simultâneos usando /dev/pubsub.
 
  Fluxo:
    1. Filho abre o device, se inscreve em "esporte" e fica lendo
       mensagens em loop até receber "FIM".
    2. Pai aguarda um instante (garante que o filho já se inscreveu),
       depois publica 3 mensagens e então "FIM".
    3. Pai aguarda o filho terminar e exibe o status.
 
  como usar: pubsub-multi
    No Qemu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#define DEVICE   "/dev/pubsub"
#define BUF_SIZE 512
#define TOPIC    "esporte"

/* Envia um comando de escrita para o driver e verifica erros. */
static void send_cmd(int fd, const char *cmd)
{
    size_t len = strlen(cmd);
    ssize_t n = write(fd, cmd, len);
    if (n < 0) {
        perror("write");
        exit(1);
    }
    printf("[PID %d] >> %s\n", (int)getpid(), cmd);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Processo FILHO: inscreve-se e consome mensagens até receber "FIM"  */
/* ------------------------------------------------------------------ */
static void processo_leitor(void)
{
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("[leitor] open");
        exit(1);
    }

    printf("[PID %d] Leitor iniciado.\n", (int)getpid());
    fflush(stdout);

    /* Inscreve-se no tópico */
    send_cmd(fd, "/subscribe " TOPIC);

    /* Configura o tópico a ser lido */
    send_cmd(fd, "/fetch " TOPIC);

    printf("[PID %d] Aguardando mensagens em '%s'...\n",
           (int)getpid(), TOPIC);
    fflush(stdout);

    /* Lê mensagens em loop; sai ao receber "FIM" ou sinal de parada */
    while (1) {
        char buf[BUF_SIZE] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n > 0) {
            buf[n] = '\0';
            printf("[PID %d] [Mensagem recebida]: %s\n",
                   (int)getpid(), buf);
            fflush(stdout);

            if (strcmp(buf, "FIM") == 0)
                break;

        } else if (n == 0) {
            /* Sem mensagens no momento; aguarda um pouco e tenta de novo */
            usleep(100 * 1000); /* 100 ms */
        } else {
            perror("[leitor] read");
            break;
        }
    }

    printf("[PID %d] Leitor encerrando.\n", (int)getpid());
    fflush(stdout);

    close(fd); /* dispara release → desinscreve automaticamente */
    exit(0);
}

/* ------------------------------------------------------------------ */
/*  Processo PAI: aguarda o filho se inscrever, depois publica         */
/* ------------------------------------------------------------------ */
static void processo_publicador(void)
{
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("[publicador] open");
        exit(1);
    }

    printf("[PID %d] Publicador iniciado.\n", (int)getpid());

    /* Aguarda o filho se inscrever (simples, suficiente para demo) */
    sleep(1);

    const char *mensagens[] = {
        "/publish " TOPIC " \"Gol do Brasil!\"",
        "/publish " TOPIC " \"Falta no meio-campo\"",
        "/publish " TOPIC " \"Intervalo: 1 x 0\"",
        "/publish " TOPIC " \"FIM\"",  /* sinaliza fim ao leitor */
        NULL
    };

    for (int i = 0; mensagens[i] != NULL; i++) {
        send_cmd(fd, mensagens[i]);
        usleep(300 * 1000); /* 300 ms entre publicações */
    }

    printf("[PID %d] Publicador encerrando.\n", (int)getpid());
    close(fd);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== pubsub-multi: teste com múltiplos processos ===\n\n");
    fflush(stdout);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Filho vira o leitor */
        processo_leitor();
        /* nunca retorna (exit dentro) */
    }

    /* Pai vira o publicador */
    processo_publicador();

    /* Aguarda o filho terminar */
    int status;
    waitpid(pid, &status, 0);

    printf("\n=== Filho encerrou com status %d ===\n",
           WEXITSTATUS(status));

    /* Mostra as estatísticas do procfs */
    printf("\n--- /proc/pubsub ---\n");
    fflush(stdout);
    system("cat /proc/pubsub");

    printf("\n--- /sys/pubsub/" TOPIC " (max_subscribers) ---\n");
    fflush(stdout);
    system("cat /sys/pubsub/" TOPIC " 2>/dev/null || echo '(topico ja removido)'");

    return 0;
}