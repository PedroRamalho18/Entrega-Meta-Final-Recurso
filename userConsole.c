//Pedro Tiago Gomes Ramalho 2019248594
//André Rodrigues Costa Pinto 2021213497
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/msg.h>
#include <errno.h>

#define MESSAGE_QUEUE_KEY 1234
#define MAX_MSG_SIZE 256

int fd, queue;
//message queue buffer
typedef struct msg_queue {
    long msg_type;
    char msg_text[MAX_MSG_SIZE];
}msg_queue;

// Estrutura de dados para as regras de alerta
typedef struct alert {
    char id[33], chave[33]; // identificador do alerta, chave a ser monitorada
    int min, max; // valor mínimo aceitável, valor máximo aceitável
} Alerta;

int verifica_id(char *id) {
    int id_alphanumeric = 1;
    size_t len = strlen(id);

    if ((len < 3) || (len > 32)) {
        printf("Erro: Ip precisa de conter entre 3 a 32 caracteres.\n");
        return 0;
    }
    for (int j = 0; j < len; j++) {
        if (!isalpha(id[j]) && !isdigit(id[j])) {
            id_alphanumeric = 0;
            break;
        }
    }
    if (id_alphanumeric) {
        return 1;
    } else{
        printf("Erro: Ip precisa de ser alfanumerico.\n");
        return 0;
    }
}



// Função para imprimir as opções do menu
void print_menu() {
    printf("\nSelecione uma opcao:\n");
    printf("  stats - Apresenta estatisticas referentes aos dados enviados pelos sensores\n");
    printf("  reset - Limpa todas as estatisticas calculadas ate ao momento pelo sistema (relativa a todos os sensores, criados por qualquer User Console)\n");
    printf("  sensors - Lista todos os Sensors que enviaram dados ao sistema\n");
    printf("  add_alert [id] [chave] [min] [max] - Adiciona uma nova regra de alerta ao sistema\n");
    printf("  remove_alert [id] - Remove uma regra de alerta do sistema\n");
    printf("  list_alerts - Lista todas as regras de alerta que existem no sistema\n");
    printf("  exit - Sai do User Console\n");
}
void cleanup() {
    /*// fechar o descritor de arquivo associado ao named pipe
    if (close(fd) == -1) {
        printf("Erro ao fechar o descritor de arquivo do named pipe: %d\n",errno);
    }*/
    // fechar message queue
    msgctl(queue, IPC_RMID, NULL);

    exit(0);
}


void sig_handler(int sig) {
    if (sig == SIGINT) {
        cleanup();
        exit(EXIT_SUCCESS);
    } else {
        cleanup();
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Erro: user_console {identificador da consola}\n");
        exit(EXIT_FAILURE);
    }

    print_menu();
    int invalido, contador, identificador = atoi(argv[1]);

    // Definir os handlers de sinais
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    //abre o name pipe para escrever
    fd = open("CONSOLE_PIPE", O_WRONLY);
    if (fd == -1) {
        printf("Erro ao abrir o console pipe");
        return 1;
    }

    struct msg_queue message;

    queue = msgget(MESSAGE_QUEUE_KEY, IPC_CREAT | 0666);
    if (queue == -1) {
        perror("ERROR OPENING MESSAGE QUEUE");
        return -1;
    }

    while (1) {
        invalido = 1;
        contador = 0;

        char op[100], op_copy[50], op_copy2[50], dados[100];

        scanf("%[^\n]%*c", op);
        strcpy(op_copy, op);
        strcpy(op_copy2, op);


        char *token = strtok(op_copy, " ");

        if (strcmp(op, "exit") == 0) {
            printf("O programa foi encerrado!\n");
            cleanup();
        }

        if (strcmp(op, "stats") == 0) {
            //pede as estatísticas	referentes	aos	dados	enviados	pelos	sensores
            sprintf(op, "%s#%s", argv[1], op_copy2);
            write(fd, op, strlen(argv[1]) + 7);
            if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, 0) == -1) {
                perror("ERROR READING THE MESSAGE FROM MESSAGE QUEUE");
                return -1;
            }
            printf("%s\n", message.msg_text);
            invalido = 0;
        }

        if (strcmp(op, "reset") == 0) {
            sprintf(op, "%s#%s", argv[1], op_copy2);
            write(fd, op, strlen(argv[1]) + 7);
            if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, 0) == -1) {
                perror("ERROR READING THE MESSAGE FROM MESSAGE QUEUE");
                return -1;
            }
            printf("%s\n", message.msg_text);
            invalido = 0;
        }

        if (strcmp(op, "sensors") == 0) {
            //pede  a lista de	todos	os	Sensors que	enviaram	dados	ao	sistema
            sprintf(op, "%s#%s", argv[1], op_copy2);
            write(fd, op, strlen(argv[1]) + 9);
            if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, 0) == -1) {
                perror("ERROR READING THE MESSAGE FROM MESSAGE QUEUE");
                return -1;
            }
            printf("%s\n", message.msg_text);
            invalido = 0;
        }

        if (strcmp(token, "add_alert") == 0) {
            token = strtok(NULL, " ");

            sprintf(op, "%s#%s", argv[1], op_copy2);

            char *id = token;

            int i = 0;

            while (op[i] != '\0') {
                if (op[i] == ' ') {
                    op[i] = '#';
                    contador++;
                }
                i++;
            }
            if (contador != 4) {
                printf("Comando errado, insira: add_alert [id] [chave] [min] [max]\n");
                continue;
            }
            if (verifica_id(id)) {
                if (write(fd, op, i + 1) == -1) {
                    printf("%d\n", errno);
                }
                if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, 0) == -1) {
                    perror("ERROR READING THE MESSAGE FROM MESSAGE QUEUE");
                    return -1;
                }
                printf("%s\n", message.msg_text);
            }
            invalido = 0;
        }

        if (strcmp(token, "remove_alert") == 0) {
            sprintf(op, "%s#%s", argv[1], op_copy2);

            int i = 1;
            while (op[i] != '\0') {
                if (op[i] == ' ') contador++;
                i++;
            }

            if (contador != 1) {
                printf("Comando errado, insira: remove_alert [id]\n");
                continue;
            }

            token = strtok(NULL, " ");
            op[strlen(argv[1]) + 13] = '#';
            write(fd, op, strlen(argv[1]) + 14 + sizeof(token));
            if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, 0) == -1) {
                perror("ERROR READING THE MESSAGE FROM MESSAGE QUEUE");
                return -1;
            }
            printf("%s\n", message.msg_text);
            invalido = 0;
        }

        if (strcmp(op, "list_alerts") == 0) {
            //pede a lista	de todas	as	regras	de	alerta	que	existem	no	sistema
            sprintf(op, "%s#%s", argv[1], op_copy2);
            write(fd, op, strlen(argv[1]) + 13);
            if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, 0) == -1) {
                perror("ERROR READING THE MESSAGE FROM MESSAGE QUEUE");
                return -1;
            }
            printf("%s\n", message.msg_text);
            invalido = 0;
        }

        if (invalido) {
            printf("Comando invalido\n");
        }

        // receber alertas
        while (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, IPC_NOWAIT) > 0) {
            strcpy(dados, message.msg_text);
            char *first_word = strtok(dados, " "); // extrair a primeira palavra da mensagem
            if (strcmp(first_word, "Alerta") == 0) printf("%s\n", message.msg_text);
        }

        if (msgrcv(queue, &message, MAX_MSG_SIZE, identificador, IPC_NOWAIT) > 0) {
            printf("%s\n", message.msg_text);
        }
    }
}