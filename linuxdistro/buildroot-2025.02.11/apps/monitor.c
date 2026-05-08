#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>

// Função auxiliar para formatar segundos em dias, horas, minutos e segundos
void print_time_format(double total_seconds) {
    int d = (int)total_seconds / 86400;
    int h = ((int)total_seconds % 86400) / 3600;
    int m = ((int)total_seconds % 3600) / 60;
    int s = (int)total_seconds % 60;
    printf("%d dias, %d horas, %d minutos, %d segundos", d, h, m, s);
}

// Função auxiliar para imprimir o conteúdo de um arquivo em bloco <pre>
// CORRIGIDO: char *line (ponteiro sem buffer) -> char line[512]
void dump_file_to_html(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp) {
        char line[512];
        printf("<pre style='background:#f4f4f4; padding:10px;'>\n");
        while (fgets(line, sizeof(line), fp)) {
            for (int i = 0; line[i] != '\0'; i++) {
                if (line[i] == '<') printf("&lt;");
                else if (line[i] == '>') printf("&gt;");
                else putchar(line[i]);
            }
        }
        printf("</pre>\n");
        fclose(fp);
    } else {
        printf("<p><em>Não foi possível ler %s</em></p>\n", path);
    }
}

int main() {
    // CORRIGIDO: cabeçalho CGI obrigatório deve vir ANTES de qualquer saída HTML
    printf("Content-Type: text/html\r\n\r\n");
    fflush(stdout);

    // 1. Coletar tempos do /proc/stat com 1 segundo de intervalo para calcular CPU% real
    long u1=0, n1=0, s1=0, i1=0, iw1=0, ir1=0, sir1=0;
    long u2=0, n2=0, s2=0, i2=0, iw2=0, ir2=0, sir2=0;

    FILE *stat_fp = fopen("/proc/stat", "r");
    if (stat_fp) {
        fscanf(stat_fp, "cpu %ld %ld %ld %ld %ld %ld %ld", &u1, &n1, &s1, &i1, &iw1, &ir1, &sir1);
        fclose(stat_fp);
    }
    sleep(1);
    stat_fp = fopen("/proc/stat", "r");
    if (stat_fp) {
        fscanf(stat_fp, "cpu %ld %ld %ld %ld %ld %ld %ld", &u2, &n2, &s2, &i2, &iw2, &ir2, &sir2);
        fclose(stat_fp);
    }

    long total_diff = (u2+n2+s2+i2+iw2+ir2+sir2) - (u1+n1+s1+i1+iw1+ir1+sir1);
    long idle_diff  = (i2+iw2) - (i1+iw1);
    double cpu_usage = (total_diff > 0) ? (100.0 * (total_diff - idle_diff) / total_diff) : 0.0;

    // Cabeçalho HTML
    printf("<!DOCTYPE html>\n<html lang='pt-BR'>\n<head>\n");
    printf("<meta charset='UTF-8'>\n<title>Relatório do Sistema</title>\n");
    printf("<style>body{font-family: Arial, sans-serif; margin: 20px;} h2{color: #2c3e50; border-bottom: 1px solid #ccc; padding-bottom: 5px;}</style>\n");
    printf("</head>\n<body>\n");
    printf("<h1>Relatório de Informações do Sistema</h1>\n");

    // 1. Versão do sistema e kernel
    struct utsname uts;
    if (uname(&uts) == 0) {
        printf("<h2>1. Sistema e Kernel</h2>\n");
        printf("<p><strong>SO:</strong> %s %s</p>\n", uts.sysname, uts.machine);
        printf("<p><strong>Release (Kernel):</strong> %s</p>\n", uts.release);
        printf("<p><strong>Versão:</strong> %s</p>\n", uts.version);
    }

    // 2. Uptime e Tempo Ocioso
    FILE *uptime_fp = fopen("/proc/uptime", "r");
    if (uptime_fp) {
        double uptime_sec, idle_sec;
        if (fscanf(uptime_fp, "%lf %lf", &uptime_sec, &idle_sec) == 2) {
            printf("<h2>2. Tempo de Atividade (Uptime / Idle)</h2>\n");
            printf("<p><strong>Uptime:</strong> ");
            print_time_format(uptime_sec);
            printf("</p>\n");
            printf("<p><strong>Tempo Ocioso Agregado (todos os núcleos):</strong> ");
            print_time_format(idle_sec);
            printf("</p>\n");
        }
        fclose(uptime_fp);
    }

    // 3. Data e Hora
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%d/%m/%Y %H:%M:%S", tm_info);
    printf("<h2>3. Data e Hora do Sistema</h2>\n");
    printf("<p>%s</p>\n", time_buf);

    // 4. Processador
    // CORRIGIDO: cpu_model[13] e cpu_speed[13] eram pequenos demais -> 256 e 64
    int cores = 0;
    char cpu_model[256] = "Desconhecido";
    char cpu_speed[64]  = "Desconhecida";
    FILE *cpu_fp = fopen("/proc/cpuinfo", "r");
    if (cpu_fp) {
        char line[256];
        while (fgets(line, sizeof(line), cpu_fp)) {
            if (strncmp(line, "processor", 9) == 0) {
                cores++;
            } else if (strncmp(line, "model name", 10) == 0) {
                char *p = strchr(line, ':');
                if (p) {
                    strncpy(cpu_model, p + 2, sizeof(cpu_model) - 1);
                    cpu_model[sizeof(cpu_model) - 1] = '\0';
                    cpu_model[strcspn(cpu_model, "\n")] = '\0';
                }
            } else if (strncmp(line, "cpu MHz", 7) == 0) {
                char *p = strchr(line, ':');
                if (p) {
                    strncpy(cpu_speed, p + 2, sizeof(cpu_speed) - 1);
                    cpu_speed[sizeof(cpu_speed) - 1] = '\0';
                    cpu_speed[strcspn(cpu_speed, "\n")] = '\0';
                }
            }
        }
        fclose(cpu_fp);
    }
    printf("<h2>4. Processador (CPU)</h2>\n");
    printf("<ul>\n");
    printf("<li><strong>Modelo:</strong> %s</li>\n", cpu_model);
    printf("<li><strong>Velocidade:</strong> %s MHz</li>\n", cpu_speed);
    printf("<li><strong>Número de Núcleos:</strong> %d</li>\n", cores);
    printf("<li><strong>Capacidade Ocupada Atual:</strong> %.2f%%</li>\n", cpu_usage);
    printf("</ul>\n");

    // 5. Carga do Sistema e Memória RAM
    struct sysinfo s_info;
    if (sysinfo(&s_info) == 0) {
        printf("<h2>5. Carga do Sistema e Memória</h2>\n");
        float load1  = s_info.loads[0] / (float)(1 << 16);
        float load5  = s_info.loads[1] / (float)(1 << 16);
        float load15 = s_info.loads[2] / (float)(1 << 16);
        printf("<p><strong>Carga Média (1, 5, 15 min):</strong> %.2f, %.2f, %.2f</p>\n", load1, load5, load15);

        unsigned long total_ram = (s_info.totalram  * s_info.mem_unit) / (1024 * 1024);
        unsigned long free_ram  = (s_info.freeram   * s_info.mem_unit) / (1024 * 1024);
        unsigned long used_ram  = total_ram - free_ram;
        printf("<p><strong>Memória RAM:</strong> Total: %lu MB | Usada: %lu MB | Livre: %lu MB</p>\n",
               total_ram, used_ram, free_ram);
    }

    // 6. Operações de I/O
    // CORRIGIDO: char dev_name (1 byte) -> char dev_name[32]
    unsigned long long total_reads = 0, total_writes = 0;
    FILE *disk_fp = fopen("/proc/diskstats", "r");
    if (disk_fp) {
        char line[256];
        while (fgets(line, sizeof(line), disk_fp)) {
            int major, minor;
            char dev_name[32];
            unsigned long long r_io, w_io;
            if (sscanf(line, "%d %d %31s %llu %*u %*u %*u %llu",
                       &major, &minor, dev_name, &r_io, &w_io) == 5) {
                total_reads  += r_io;
                total_writes += w_io;
            }
        }
        fclose(disk_fp);
    }
    printf("<h2>6. Operações de Entrada e Saída (I/O)</h2>\n");
    printf("<p><strong>Total de Leituras concluídas:</strong> %llu</p>\n", total_reads);
    printf("<p><strong>Total de Escritas concluídas:</strong> %llu</p>\n", total_writes);

    // 7. Sistemas de Arquivos Suportados
    printf("<h2>7. Sistemas de Arquivos Suportados</h2>\n");
    dump_file_to_html("/proc/filesystems");

    // 8. Dispositivos (Caracteres e Blocos)
    printf("<h2>8. Dispositivos (Caracteres e Blocos)</h2>\n");
    dump_file_to_html("/proc/devices");

    // 9. Dispositivos de Rede
    printf("<h2>9. Dispositivos de Rede</h2>\n");
    dump_file_to_html("/proc/net/dev");

    // 10. Processos em Execução
    // CORRIGIDO: removido printf("CCCCC") de debug, buffers reduzidos para tamanhos razoáveis
    printf("<h2>10. Processos em Execução (PID e Nome)</h2>\n");
    printf("<div style='height: 300px; overflow-y: scroll; border: 1px solid #ccc; padding: 10px;'>\n");
    printf("<ul style='list-style-type: none; padding-left: 0;'>\n");

    DIR *dir = opendir("/proc");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (!isdigit(ent->d_name[0])) continue;

            char path[64];
            snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
            FILE *comm_fp = fopen(path, "r");
            if (comm_fp) {
                char comm[32];
                if (fgets(comm, sizeof(comm), comm_fp)) {
                    comm[strcspn(comm, "\n")] = '\0';
                    printf("<li><strong>PID %s:</strong> %s</li>\n", ent->d_name, comm);
                }
                fclose(comm_fp);
            }
        }
        closedir(dir);
    }
    printf("</ul>\n</div>\n");

    printf("</body>\n</html>\n");
    return 0;
}