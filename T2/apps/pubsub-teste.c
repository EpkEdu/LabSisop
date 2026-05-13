/*
  como usar: pubsub-teste
    No Qemu

    Exemplos

    /subscribe esporte
    /publish esporte "Gol do Brasil!"
    /fetch esporte
    /read
    exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEVICE   "/dev/pubsub"
#define BUF_SIZE 512

int main(void)
{
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Falha ao abrir " DEVICE);
        return 1;
    }

    char cmd[BUF_SIZE];
    printf("Interagindo com /dev/pubsub (PID: %d)...\n", (int)getpid());
    printf("Comandos:\n");
    printf("  /subscribe <topico>\n");
    printf("  /unsubscribe <topico>\n");
    printf("  /publish <topico> \"msg\"\n");
    printf("  /fetch <topico>\n");
    printf("  /read\n");
    printf("  exit\n\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(cmd, sizeof(cmd), stdin))
            break;

        /* remove newline */
        cmd[strcspn(cmd, "\n")] = '\0';

        if (strcmp(cmd, "exit") == 0)
            break;

        if (strcmp(cmd, "/read") == 0) {
            char buf[BUF_SIZE] = {0};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                printf("[Mensagem]: %s\n", buf);
            } else if (n == 0) {
                printf("Sem mensagens pendentes.\n");
            } else {
                perror("read");
            }
       } else {
    size_t len = strlen(cmd);
    printf("[debug] enviando %zu bytes: '%s'\n", len, cmd);
    if (len == 0) {
        printf("comando vazio, ignorado\n");
        continue;
    }
    ssize_t n = write(fd, cmd, len);
    if (n < 0)
        perror("write");
}
    }

    close(fd);
    printf("Encerrando.\n");
    return 0;
}