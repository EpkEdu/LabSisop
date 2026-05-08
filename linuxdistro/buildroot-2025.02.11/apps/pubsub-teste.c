#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
    FILE *fp = fopen("/dev/pubsub", "r+"); // r+ abre para leitura e escrita
    if (!fp) {
        perror("Falha ao abrir /dev/pubsub");
        return 1;
    }

    char cmd[512];
    printf("Interagindo com /dev/pubsub (PID: %d)...\n", getpid());
    printf("Comandos aceitos: \n /subscribe <topico>\n /unsubscribe <topico>\n /publish <topico> \"msg\"\n /fetch <topico>\n /read\n\n");

    while (1) {
        printf("> ");
        if (!fgets(cmd, sizeof(cmd), stdin)) break;
        
        // Remove a quebra de linha
        cmd[strcspn(cmd, "\n")] = 0;

        if (strcmp(cmd, "exit") == 0) {
            break;
        }

        if (strncmp(cmd, "/read", 5) == 0) {
            // Executa leitura no driver
            char buffer[256] = {0};
            // Força a leitura chamando fread ou fgets no arquivo aberto
            if (fgets(buffer, sizeof(buffer), fp) != NULL) {
                printf("[Mensagem Recebida]: %s\n", buffer);
            } else {
                printf("Nenhuma mensagem pendente no topico selecionado.\n");
            }
        } else {
            // Qualquer outro comando (/subscribe, /unsubscribe, /publish, /fetch) é enviado via escrita
            fprintf(fp, "%s", cmd);
            fflush(fp); // Garante o envio imediato ao driver
        }
    }

    fclose(fp); // Dispara a função fops->release no kernel para liberar memória
    return 0;
}