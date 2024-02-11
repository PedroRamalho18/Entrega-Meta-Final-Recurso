//Pedro Tiago Gomes Ramalho 2019248594
//André Rodrigues Costa Pinto 2021213497
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#define MAX_SENSOR_ID_SIZE 32
#define MIN_SENSOR_ID_SIZE 3
#define MAX_KEY_SIZE 32
#define MIN_KEY_SIZE 3

typedef struct sensor{
    char *id_sensor;
    int valor_min, valor_max;
    double intervalo;
    char *chave;
    struct sensor *prox;
}Sensor;

int pipe_fd, NUM_MSG=0;

void mensagem_erro(const char *mensagem) {
    fprintf(stderr, "%s\n", mensagem);
    exit(EXIT_FAILURE);
}

void validar_sensor_id(const char *id_sensor) {
    size_t len = strlen(id_sensor);
    if (len < MIN_SENSOR_ID_SIZE || len > MAX_SENSOR_ID_SIZE) {
        mensagem_erro("Erro: O tamanho do ID do sensor deve ter entre 3 e 32 caracteres.");
    } else {
        for (size_t i = 0; i < len; i++) {
            if (!isalnum(id_sensor[i]) && id_sensor[i] != '_') {
                mensagem_erro("Erro: O ID do sensor deve ser composto apenas por caracteres alfanuméricos e underscore (_).");
                break;
            }
        }
    }
}


void validar_chave(const char *chave) {
    size_t len = strlen(chave);
    if (len < MIN_KEY_SIZE || len > MAX_KEY_SIZE) {
        mensagem_erro("Erro: O tamanho da Chave deve ter entre 3 e 32 caracteres.");
    }
    for (int i = 0; i < len; i++) {
        if (!(isdigit(chave[i]) || isalpha(chave[i]) || chave[i] == '_')) {
            mensagem_erro("Erro: a chave deve ser formada por uma combinação de dígitos, caracteres alfabéticos e underscore!");
        }
    }
}

void validar_valores(int valor_min, int valor_max) {
    if (valor_min > valor_max) {
        mensagem_erro("Erro: o valor minimo não pode ser maior que o máximo!!");
    }
}

void cleanup() {
    /*// fechar o descritor de arquivo associado ao named pipe
    if (close(pipe_fd) == -1) {
        fprintf(stderr, "Erro ao fechar o descritor de arquivo do named pipe!\n");
    }*/

    /*
    // remover o arquivo do named pipe
    if (unlink("SENSOR_PIPE") == -1) {
        fprintf(stderr, "Erro ao remover o named pipe!\n");
    }
     */
}

void sig_handler(int sig) {
    if (sig == SIGINT) {
        cleanup();
        exit(EXIT_SUCCESS);
    } else if (sig == SIGTSTP) {
        printf("NUMBER OF MESSAGES SENT: %d\n", NUM_MSG);
    } else {
        cleanup();
        exit(EXIT_FAILURE);
    }
}


int main(int argc, char *argv[]) {
    // Definir os handlers de sinais
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);




    // Verificar o nº de argumentos
    if (argc != 6) {
        mensagem_erro(
                "Erro: Sintaxe do comando:\n$ sensor {identificador do sensor} {intervalo entre envios em segundos\n"
                "(>=0)} {chave} {valor inteiro mínimo a ser enviado} {valor inteiro\n"
                "máximo a ser enviado}");
    }
    Sensor sensor;
    // Parse dos argumentos
    sensor.id_sensor = argv[1];
    sensor.intervalo = atoi(argv[2]);
    sensor.chave = argv[3];
    sensor.valor_min = atoi(argv[4]);
    sensor.valor_max = atoi(argv[5]);

    // Validar os argumentos
    validar_sensor_id(sensor.id_sensor);
    validar_chave(sensor.chave);
    validar_valores(sensor.valor_min, sensor.valor_max);

    // Abrir o named pipe para escrita
    pipe_fd = open("SENSOR_PIPE", O_WRONLY);
    if (pipe_fd == -1) {
        printf("Erro ao abrir o named pipe!%d\n",errno);
        return -1;
    }

    int valor;
    srand(time(NULL) + getpid());

    while (1) {

        valor = rand() % (sensor.valor_max - sensor.valor_min + 1) + sensor.valor_min;
        NUM_MSG+=1;
        printf("Valor gerado: %d\n", valor);

        char msg[100];
        snprintf(msg, sizeof(msg), "SENSOR#%s#%s#%d", sensor.id_sensor, sensor.chave, valor);
        printf("%s\n",msg);
        if (write(pipe_fd, msg, strlen(msg)) == -1) {
            mensagem_erro("Erro ao escrever no named pipe!");
        }

        sleep(sensor.intervalo);
    }

    return 0;
}
