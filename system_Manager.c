//Pedro Tiago Gomes Ramalho 2019248594
//André Rodrigues Costa Pinto 2021213497
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

#define SEM_NAME "semaforo_worker"
#define SEM_NAME_QUEUE "semaforo_queue"
#define SEM_NAME_WATCHER "semaforo_watcher"
#define SEM_NAME_SHARE "semaforo_share_memory"
#define SEM_NAME_LOG "semaforo_log"
#define SEM_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)
#define SHM_SIZE MAX_SENSORS * (sizeof(Sensor)) + MAX_KEYS * (sizeof(Info_chave)) + MAX_ALERTS * (sizeof(Alerta)) + N_WORKERS * (sizeof(int))
#define LOG "log.txt"
#define MESSAGE_QUEUE_KEY 1234
#define MAX_MSG_SIZE 256

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS, fd_sensor, fd_console;

//message queue
int queue;

//contador do numero de elementos da internal queue
int internal_queue_size=0;

//inicialização do unnamed pipe
int (*un_pipe)[2];

FILE* log_file;

// Inicializa as threads
pthread_t sensor_reader_thread, console_reader_thread, dispatcher_thread;

// Inicializa as mutexs
pthread_mutex_t internal_queue_mutex, msg_to_send_mutex, mutex_console_reader_thread, mutex_sensor_reader_thread;

sem_t * sem, * sem_queue, *sem_watcher, *sem_share, *sem_log;// semaforos

typedef struct sensor{
    char sensor[33];
}Sensor;

// Estrutura de dados para as regras de alerta
typedef struct alert {
    char id[33], chave[33]; // identificador do alerta, chave a ser monitorada
    int min, max, console; // valor mínimo aceitável, valor máximo aceitável
} Alerta;

typedef struct info_chave {
    char chave[33];
    int min_val;
    int max_val;
    int num_update;
    double media;
    int ultimo_val;
}Info_chave;

//message queue buffer
struct msg_queue {
    long msg_type;
    char msg_text[MAX_MSG_SIZE];
}msg_queue;

typedef struct internal_queue {
    // dados que precisam ser processados pelos Workers
    char *data;
    // ponteiro para o próximo nó da lista
    struct internal_queue* next;
} Internal_queue;

Internal_queue* internal_queue_head;


//Memória partilhada
int shmid;
char *shm;
Sensor *sensors;
int *worker_valid_dispacher, *worker_valid_worker, *worker_teste;
Info_chave *chaves;
Alerta *alerts;

void mensagem_erro(const char *mensagem) {
    fprintf(stderr, "%s\n", mensagem);
    exit(EXIT_FAILURE);
}

int write_log_file_and_print(const char* frase, FILE* log){ // estar sempre a abriri o ficheiro e semaforos
    sem_wait(sem_log);
    time_t t;
    t = time(NULL);
    struct tm tm;
    tm = *localtime(&t);

    printf("%d:%d:%d %s",tm.tm_hour, tm.tm_min, tm.tm_sec, frase);
    fprintf(log,"%d:%d:%d %s",tm.tm_hour, tm.tm_min, tm.tm_sec, frase);
    sem_post(sem_log);
    return 0;
}

int read_config_file(const char* filename, int* QUEUE_SZ, int* N_WORKERS, int* MAX_KEYS, int* MAX_SENSORS, int* MAX_ALERTS) {
    FILE* config_file = fopen(filename, "r");

    if (config_file == NULL) {
        printf("Erro ao abrir o ficheiro de configurações.\n");
        return 1;
    }

    if (fscanf(config_file, "%d", QUEUE_SZ) != 1 || *QUEUE_SZ < 1) {
        printf("O valor QUEUE_SZ do ficheiro de configurações tem que ser >=1!\n");
        fclose(config_file);
        return 1;
    }

    if (fscanf(config_file, "%d", N_WORKERS) != 1 || *N_WORKERS < 1) {
        printf("O valor N_WORKERS do ficheiro de configurações tem que ser >=1!\n");
        fclose(config_file);
        return 1;
    }

    if (fscanf(config_file, "%d", MAX_KEYS) != 1 || *MAX_KEYS < 1) {
        printf("O valor MAX_KEYS do ficheiro de configurações tem que ser >=1!\n");
        fclose(config_file);
        return 1;
    }

    if (fscanf(config_file, "%d", MAX_SENSORS) != 1 || *MAX_SENSORS < 1) {
        printf("O valor MAX_SENSORS do ficheiro de configurações tem que ser >=1!\n");
        fclose(config_file);
        return 1;
    }

    if (fscanf(config_file, "%d", MAX_ALERTS) != 1 || *MAX_ALERTS < 0) {
        printf("O valor MAX_ALERTS do ficheiro de configurações tem que ser >=0!\n");
        fclose(config_file);
        return 1;
    }
    fclose(config_file);
    return 0;
}

void enviar_mensagem_queue(const char* mensagem, int type) {
    // Cria o buffer da message queue
    struct msg_queue msg;
    msg.msg_type = type; // Define o tipo da mensagem
    strcpy(msg.msg_text, mensagem); // Copia a mensagem para o buffer

    // Envia a mensagem pela message queue
    if (msgsnd(queue, &msg, strlen(msg.msg_text) + 1, 0) == -1) {
        perror("Erro ao enviar mensagem pela message queue");
    }
}

void add_node_to_internal_queue(char* buffer) {
    //printf("Entrou na add internal_queue\n");
    // verificar se a lista ligada está no tamanho máximo
    if (internal_queue_size >= QUEUE_SZ) {
        //printf("A internal queue já atingiu o tamanho máximo.\n");
        write_log_file_and_print("A internal queue já atingiu o tamanho máximo.\n",log_file);
        return;
    }

    // criar um novo nó na lista ligada
    Internal_queue* new_node = malloc(sizeof(Internal_queue));
    new_node->data = strdup(buffer); // copiar a mensagem lida para o novo nó
    new_node->next = NULL;

    //printf("novo nó criado\n");

    // adquirir o mutex da lista encadeada
    pthread_mutex_lock(&internal_queue_mutex);

    //printf("mutex criado\n");

    // adicionar o novo nó ao final da lista encadeada
    if (internal_queue_head == NULL) {
        // se a lista estiver vazia, o novo nó será a cabeça da lista
        internal_queue_head = new_node;
        //printf("adicionou o primeiro no à lista\n");
    } else {
        // caso contrário, percorremos a lista até encontrar o último nó e adicionamos o novo nó lá
        Internal_queue* current_node = internal_queue_head;

        while (current_node->next != NULL) {
            //printf("%s\n",current_node->data);
            current_node = current_node->next;
        }
        current_node->next = new_node;
    }

    // atualizar o tamanho da lista
    internal_queue_size++;
    //printf("antes de libertar o mutex\n");
    // liberar o mutex da lista encadeada
    pthread_mutex_unlock(&internal_queue_mutex);
    sem_post(sem_queue);
}


void* sensor_reader(void* arg) {
    // cria o named pipe
    if (mkfifo("SENSOR_PIPE", 0777) == -1) {
        if (errno != EEXIST) {
            mensagem_erro("Erro ao criar SENSOR_PIPE");
        }
    }

    // abre o named pipe em modo de leitura

    if ((fd_sensor = open("SENSOR_PIPE", O_RDONLY))==-1) {
        printf("Erro ao abrir o sensor_pipe.%d",errno);
        exit(EXIT_FAILURE);
    }

    // loop principal da thread
    size_t nbytes = 80; // tamanho máximo da mensagem
    char buffer[nbytes]; // buffer para armazenar a mensagem lida
    ssize_t bytes_read; // número de bytes lidos pela função read
    while (1) {
        // espera e lê a informação vinda do sensor pipe
        bytes_read = read(fd_sensor, buffer, nbytes);
        buffer[bytes_read] = '\0';
        if (bytes_read == -1) {
            // Handle read error
            printf("Erro ao ler do sensor_pipe.\n");
            continue; // Skip the current iteration and continue with the next iteration
        }
        // bloqueia o acesso ao mutex antes de acessar a lista encadeada
        int lock_result = pthread_mutex_lock(&mutex_sensor_reader_thread);
        if (lock_result != 0) {
            // Handle mutex lock error
            mensagem_erro("Erro ao bloquear o mutex na função sensor_reader.");
            continue; // Skip the current iteration and continue with the next iteration
        }
        //printf("%s\n",buffer);
        add_node_to_internal_queue(buffer);
        // libera o acesso ao mutex após concluir a operação na lista ligada
        int unlock_result = pthread_mutex_unlock(&mutex_sensor_reader_thread);
        if (unlock_result != 0) {
            // Handle mutex unlock error
            mensagem_erro("Erro ao desbloquear o mutex na função sensor_reader.");
            continue; // Skip the current iteration and continue with the next iteration
        }
    }
}


void* console_reader(void* arg) {
    // cria o named pipe
    if (mkfifo("CONSOLE_PIPE", 0777) == -1) {
        if (errno != EEXIST) {
            mensagem_erro("Erro ao criar CONSOLE_PIPE");
        }
    }
    //printf("Criou console pipe\n");
    // abre o named pipe em modo de leitura

    if ((fd_console = open("CONSOLE_PIPE", O_RDONLY))== -1) {
        perror("Erro ao abrir o console_pipe.");
    }
    //printf("Abriu console pipe\n");

    // loop principal do thread
    char buffer[80];
    ssize_t bytes_read;
    while(1) {
        // espera e lê a informação vinda do console pipe
        bytes_read = read(fd_console, buffer, 80);
        buffer[bytes_read] = '\0';
        if (bytes_read == -1) {
            printf("Erro ao ler do console_pipe.\n");
            continue;
        }
        // bloqueia o acesso ao mutex antes de acessar a lista encadeada
        if (pthread_mutex_lock(&mutex_console_reader_thread) != 0) {
            // Handle mutex lock error
            printf("Erro ao bloquear o mutex na função console_reader.\n");
            continue;
        }
        //printf("Recebido do console pipe: %s\n", buffer);
        add_node_to_internal_queue(buffer);
        // libera o acesso ao mutex após concluir a operação na lista encadeada
        if (pthread_mutex_unlock(&mutex_console_reader_thread) != 0) {
            // Handle mutex unlock error
            printf("Erro ao desbloquear o mutex na função console_reader.\n");
            continue;
        }
    }
}


void *dispatcher(void *arg) {

    char mensagem[100];
    while(1) {

        // verifica se há mensagens na fila
        sem_wait(sem_queue);

        Internal_queue *atual = internal_queue_head;
        Internal_queue *anterior = internal_queue_head;
        Internal_queue *msg_to_send = NULL;
        // procurando pela primeira mensagem do User Console na fila
        while (atual != NULL) {
            char dados[50];// obter a mensagem armazenada no nó atual
            strcpy(dados, atual->data);
            char *first_word = strtok(dados, "#"); // extrair a primeira palavra da mensagem
            if (strcmp(first_word, "SENSOR") != 0) { // se a primeira palavra não for "SENSOR", quer dizer que é do User Console
                msg_to_send = atual;
                if (atual == internal_queue_head) internal_queue_head = internal_queue_head->next;
                else anterior->next = atual->next;
                break;
            }
            anterior = atual;
            atual = atual->next;
        }

        // se não encontrou mensagem do User Console, envia a primeira mensagem da fila, que é do Sensor
        if (msg_to_send == NULL) {
            msg_to_send = internal_queue_head;
            //printf("%s\n",msg_to_send->data);
            internal_queue_head = internal_queue_head->next;
        }

        strcpy(mensagem, msg_to_send->data);
        sem_wait(sem);
        int i;
        /*
        for (i = 0; i < N_WORKERS; i++) {
            printf("%d\n",*worker_valid_dispacher);
            worker_valid_dispacher++;
        }
        worker_valid_dispacher -= i;
         */
        for (i = 0; i < N_WORKERS; i++) {
            if (*worker_valid_dispacher == 0) {
                write(un_pipe[i][1], mensagem, 100 * sizeof(char));
                //printf("Dispatcher enviou para o Worker %d a frase: %s\n", i, mensagem);
                break;
            }
            worker_valid_dispacher++;
        }
        worker_valid_dispacher -= i;
        sem_post(sem);
        internal_queue_size--;

    }
}

void cleanup() {
    write_log_file_and_print("HOME_IOT SIMULATOR CLOSING...\n",log_file);

    // Fechar o descritor de arquivo do pipe do sensor
    close(fd_sensor);

    // Fechar o descritor de arquivo do pipe do console
    close(fd_console);

    // Fechar e remover o pipe SENSOR_PIPE
    unlink("SENSOR_PIPE");

    // Fechar e remover o pipe CONSOLE_PIPE
    unlink("CONSOLE_PIPE");

    // Fechar os unnamed pipes
    for (int i = 0; i < 10; i++) {
        close(un_pipe[i][0]);
        close(un_pipe[i][1]);
    }

    // Desalocar a matriz dos unnamed pipes
    // Não é necessário chamar free(un_pipe), pois não foi alocado dinamicamente
    free(un_pipe);

    pthread_cancel(dispatcher_thread);
    pthread_cancel(console_reader_thread);
    pthread_cancel(sensor_reader_thread);

    pthread_join(dispatcher_thread, NULL);
    pthread_join(console_reader_thread, NULL);
    pthread_join(sensor_reader_thread, NULL);


    // Destruir o mutex da thread console e sensor reader
    pthread_mutex_destroy(&mutex_console_reader_thread);
    pthread_mutex_destroy(&mutex_sensor_reader_thread);

    //fecha message queue
    msgctl(queue, IPC_RMID, NULL);

    // Fechar o semáforo worker
    sem_close(sem);
    sem_unlink(SEM_NAME);

    // Fechar o semáforo queue
    sem_close(sem_queue);
    sem_unlink(SEM_NAME_QUEUE);

    // Fechar o semáforo watcher
    sem_close(sem_watcher);
    sem_unlink(SEM_NAME_WATCHER);

    // Fechar o semáforo share memory
    sem_close(sem_share);
    sem_unlink(SEM_NAME_SHARE);

    // Fechar o semáforo log
    sem_close(sem_log);
    sem_unlink(SEM_NAME_LOG);

    // Fechar a shared memory
    shmdt(shm);

    // Fechar o arquivo de log
    fclose(log_file);

    exit(EXIT_SUCCESS);
}


void sig_handler(int sig) {
    if (sig == SIGINT) {
        //As threads Dispatcher, Sensor Reader e Console Reader param de funcionar, deixando
        //assim o sistema de receber dados dos processos Sensor ou User Console.
        cleanup();
    }
}



int main(int argc, char *argv[]) {
    //abre ficheiro log
    log_file = fopen(LOG  , "a");
    if (log_file == NULL) {
        mensagem_erro("Erro ao abrir o ficheiro log.\n");
        fclose(log_file);
        return 1;
    }
    //inicializa semaforo log
    sem_log = sem_open(SEM_NAME_LOG, O_CREAT, SEM_PERMS, 1);
    if (sem_log == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }
    write_log_file_and_print("HOME_IOT SIMULATOR STARTING\n",log_file);

    if (read_config_file(argv[1], &QUEUE_SZ, &N_WORKERS, &MAX_KEYS, &MAX_SENSORS, &MAX_ALERTS) != 0) {
        return 1;
    }
    un_pipe = malloc(N_WORKERS * sizeof(*un_pipe));

    //cria a memória partilhada
    if ((shmid = shmget(123, SHM_SIZE, IPC_CREAT | 0666 )) < 0) {
        perror("erro no shmget");
        exit(EXIT_FAILURE);
    }
    write_log_file_and_print("SHARED MEMORY CREATED\n",log_file);

    //adiciona a memória partilhada ao espaço de endereço do processo
    if ((shm = shmat(shmid, NULL, 0)) == (char *) -1) {
        perror("erro no shmat");
        exit(1);
    }

    // ponteiros para a shared memory
    sensors = (Sensor *) shm;
    chaves = (Info_chave *)(shm + MAX_SENSORS);
    alerts = (Alerta*)(shm + MAX_SENSORS + MAX_KEYS);
    worker_valid_dispacher = (int *)(shm + MAX_SENSORS + MAX_KEYS + MAX_ALERTS);
    worker_valid_worker = worker_valid_dispacher;
    worker_teste = worker_valid_dispacher;


    //coloca o array dos workers a zero
    int j = 0;
    for (int *s = worker_valid_dispacher;; s++) {
        if(j==N_WORKERS) break;
        *s = 0;
        j++;
    }
    j = 0;
    //coloca o array das chaves a zero
    for (Info_chave *s = chaves;; s++) {
        if(j==MAX_KEYS) break;
        strcpy(s->chave," ");
        j++;
    }
    j = 0;
    //coloca o array dos alertas a zero
    for (Alerta *s = alerts;; s++) {
        if(j==MAX_ALERTS) break;
        strcpy(s->id," ");
        j++;
    }
    j = 0;
    //coloca o array dos sensores a zero
    for (Sensor *s = sensors;; s++) {
        if(j==MAX_SENSORS) break;
        strcpy(s->sensor," ");
        j++;
    }

    //inicializa os unnamed pipes
    for(int i = 0; i < N_WORKERS; i++){
        if(pipe(un_pipe[i]) == -1) {
            mensagem_erro("Erro ao inicializar o unnamed pipe\n");
        }
    }

    // cria semafero alerts watcher
    sem_watcher = sem_open(SEM_NAME_WATCHER, O_CREAT, SEM_PERMS, 0);
    if (sem_watcher == SEM_FAILED) {
        perror("sem_open(1) failed");
        exit(EXIT_FAILURE);
    }

    //inicializa semaforo workers
    sem = sem_open(SEM_NAME, O_CREAT, SEM_PERMS, N_WORKERS);
    if (sem == SEM_FAILED) {
        perror("sem_open(2) failed");
        exit(EXIT_FAILURE);
    }

    //inicializa semaforo internal queue
    sem_queue = sem_open(SEM_NAME_QUEUE, O_CREAT, SEM_PERMS, 0);
    if (sem_queue == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

    //inicializa semaforo share memory
    sem_share = sem_open(SEM_NAME_SHARE, O_CREAT, SEM_PERMS, 1);
    if (sem_share == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

    internal_queue_head = NULL;


    if (pthread_create(&sensor_reader_thread, NULL, sensor_reader, NULL) != 0){
        mensagem_erro("Erro na thread sensor_reader");
    }
    write_log_file_and_print("THREAD SENSOR_READER CREATED\n",log_file);
    if (pthread_create(&console_reader_thread, NULL, console_reader, NULL) != 0){
        mensagem_erro("Erro na thread console_reader");
    }
    write_log_file_and_print("THREAD CONSOLE_READER CREATED\n",log_file);
    if(pthread_create(&dispatcher_thread, NULL, dispatcher, NULL) != 0){
        mensagem_erro("Erro na thread dispatcher");
    }
    write_log_file_and_print("THREAD DISPATCHER CREATED\n",log_file);


    //criação da message queue
    queue = msgget(MESSAGE_QUEUE_KEY, IPC_CREAT | 0666);
    if (queue == -1) {
        perror("ERROR CREATING MESSAGE QUEUE");
        return -1;
    }

    //cleanup();
    // cria processo Alerts Watcher
    pid_t alerts_watcher_pid = fork();
    if (alerts_watcher_pid == 0) {
        // código do processo Alerts Watcher
        printf("PROCESS ALETRS_WACTHER CREATED\n");

        while (1) {
            sem_wait(sem_watcher);
            char aviso[150];
            Alerta *dados_alerta = alerts;
            Info_chave *dados_chave;
            int repeticao = -1;
            for(int i = 0; i<MAX_ALERTS; i++) {
                dados_chave = chaves;

                for(int e = 0; e<MAX_KEYS; e++) {

                    if ((strcmp(dados_alerta->id, " ") > 0) && (strcmp(dados_chave->chave, " ") > 0)) {
                        if (strcmp(dados_alerta->chave, dados_chave->chave) == 0) {
                            if (dados_chave->ultimo_val != repeticao){
                                if ((dados_chave->ultimo_val > dados_alerta->max) || (dados_chave->ultimo_val < dados_alerta->min)){
                                    sprintf(aviso, "Alerta %s acionado, chave %s passou dos limites no valor %d\n",
                                            dados_alerta->id, dados_chave->chave, dados_chave->ultimo_val);
                                    write_log_file_and_print(aviso, log_file);
                                    enviar_mensagem_queue(aviso, dados_alerta->console);
                                    repeticao = dados_chave->ultimo_val;
                                }
                            }
                        }
                    }
                    dados_chave++;
                }
                dados_alerta++;
            }
        }


    } else if (alerts_watcher_pid > 0) {
        // processo pai
        for (int i = 0; i < N_WORKERS; i++) {
            pid_t worker_pid = fork(); // Cria um novo processo
            if (worker_pid == -1) {
                // Se ocorrer um erro ao criar o processo
                write_log_file_and_print("ERROR CREATING WORKER\n",log_file);
                exit(EXIT_FAILURE);
            } else if (worker_pid == 0) {
                char msg_recebida[100], msg_enviar[150];
                // processo worker
                char frase[50];
                int *work = worker_valid_worker;

                sprintf(frase, "WORKER %d READY\n", i);
                write_log_file_and_print(frase,log_file);

                while(1) {
                    ssize_t msg_size = read(un_pipe[i][0], msg_recebida, 100 * sizeof(char));
                    msg_recebida[msg_size] = '\0';
                    sem_wait(sem);
                    sem_wait(sem_share);
                    *work = 1;
                    sem_post(sem_share);
                    sem_post(sem_watcher);

                    //printf("msg recebida unamedpipe: %s\n",msg_recebida);
                    //printf("worker %d ocupado\n",i);
                    char *token = strtok(msg_recebida, "#"), token_cpy[50];
                    strcpy(token_cpy,token);

                    int identificador = atoi(token);

                    token = strtok(NULL, "#");
                    if(token == NULL) strcpy(token,token_cpy);
                    // verificar se a primeira palavra é "SENSOR"
                    if (strcmp(token_cpy, "SENSOR") == 0 ) {
                        char palavras[3][33];
                        int k = 0;



                        // extrair as palavras usando strtok
                        while (k < 3) {
                            strcpy(palavras[k],token);
                            token = strtok(NULL, "#");
                            k++;
                        }

                        // atribuir as palavras às variáveis desejadas
                        char sensor[33];
                        strcpy(sensor,palavras[0]);
                        char chave[33];
                        strcpy(chave,palavras[1]);

                        char *ptr;
                        int valor = (int)strtol(palavras[2], &ptr, 10);

                        //printf("%d\n",valor);

                        // ponteiro para o sensor e chave da sharememory
                        Info_chave *dados_chave = chaves;
                        Sensor *dados_sensor = sensors;

                        //verifica se a lista sensor ou chave chegou ao seu limite
                        int chave_nao_existe = 0;

                        // encontra o sensor correspondente na share memory caso ele não exista adiciona-o

                        for (int e = 0; e < MAX_SENSORS; e++) {
                            if (strcmp(dados_sensor->sensor, sensor) == 0) break;

                            if(strcmp(dados_sensor->sensor, " ")==0){
                                sem_wait(sem_share);
                                strcpy(dados_sensor->sensor,sensor);
                                sem_post(sem_share);
                                break;
                            }
                            if(e==MAX_SENSORS-1) chave_nao_existe = 1;
                            dados_sensor++;
                        }

                        /*
                        for (int e = 0; e < MAX_SENSORS; e++) {
                            printf("sensor: %s %lu\n",dados_sensor2->sensor, strlen(dados_sensor2->sensor));
                            dados_sensor2++;
                        }
                         */

                        // caso a lista de sensors tenha chegado ao limite, envia-se uma msg de erro
                        if(chave_nao_existe){
                            sprintf(msg_enviar,"Erro: numero maximo de sensors alcancado, sensor %s descartado.\n",sensor);
                            write_log_file_and_print(msg_enviar,log_file);
                            break;
                        }

                        // encontra a chave correspondente na share memory
                        for (int e = 0; e < MAX_KEYS; e++) {
                            // se a chave já existir na memória partilhada, atualiza seus dados
                            if (strcmp(dados_chave->chave, chave) == 0) {
                                // atualiza os valores
                                sem_wait(sem_share);
                                dados_chave->ultimo_val = valor;
                                dados_chave->num_update++;
                                dados_chave->media = (dados_chave->media * (dados_chave->num_update-1) + valor) / dados_chave->num_update;
                                dados_chave->min_val = (valor < dados_chave->min_val) ? valor : dados_chave->min_val;
                                dados_chave->max_val = (valor > dados_chave->max_val) ? valor : dados_chave->max_val;
                                sem_post(sem_share);
                                break;
                            }

                            // se a chave não existir na memória compartilhada, adiciona-a
                            if(strcmp(dados_chave->chave, " ")==0){
                                // adiciona os valores
                                sem_wait(sem_share);
                                strcpy(dados_chave->chave,chave);
                                dados_chave->ultimo_val = valor;
                                dados_chave->num_update = 1;
                                dados_chave->media = valor;
                                dados_chave->min_val = valor;
                                dados_chave->max_val = valor;
                                sem_post(sem_share);
                                break;
                            }

                            if(e==MAX_KEYS-1) chave_nao_existe = 1;
                            dados_chave++;
                        }


                        // caso a lista de chaves tenha chegado ao limite, envia-se uma msg de erro
                        if(chave_nao_existe){
                            sprintf(msg_enviar,"Erro: numero maximo de chaves alcancado, chave %s descartada.\n",chave);
                            write_log_file_and_print(msg_enviar,log_file);
                        }
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Dados processados corretamente\n", i);
                        write_log_file_and_print(msg_log, log_file);



                    } else if(strcmp(token, "stats") == 0){
                        Info_chave *dados_chave = chaves;
                        char msg_cat[100];
                        //Apresenta estatísticas referentes aos dados nas chaves, incluindo os valores máximo e mínimo obtidos até esse momento.
                        strcpy(msg_enviar,"[KEY] [LAST] [Min] [Max] [UPDATE] [MEDIA]\n");
                        for (int e = 0; e < MAX_KEYS; e++) {
                            // se a chave existir na memória partilhada concatena na mensagem
                            if (strcmp(dados_chave->chave, " ") == 0) break;
                            if (strcmp(dados_chave->chave, " ") > 0) {
                                sprintf(msg_cat,"%s %d %d %d %d %lf\n",dados_chave->chave,dados_chave->ultimo_val,dados_chave->min_val,dados_chave->max_val,dados_chave->num_update,dados_chave->media);
                                strcat(msg_enviar,msg_cat);
                            }
                            dados_chave++;
                        }
                        enviar_mensagem_queue(msg_enviar,identificador);
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Stats enviados para a consola %d\n", i, identificador);
                        write_log_file_and_print(msg_log, log_file);


                    }else if(strcmp(token, "reset") == 0) {
                        Info_chave *dados_chave = chaves;
                        Sensor *dados_sensor = sensors;

                        for (int e = 0; e < MAX_KEYS; e++) {
                            sem_wait(sem_share);
                            strcpy(dados_chave->chave," ");
                            sem_post(sem_share);
                            dados_chave++;
                        }
                        for (int e = 0; e < MAX_SENSORS; e++) {
                            sem_wait(sem_share);
                            strcpy(dados_sensor->sensor," ");
                            sem_post(sem_share);
                            dados_sensor++;
                        }
                        strcpy(msg_enviar,"OK\n");
                        enviar_mensagem_queue(msg_enviar,identificador);
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Dados eliminados\n", i);
                        write_log_file_and_print(msg_log, log_file);


                    }else if(strcmp(token, "sensors") == 0) {
                        Sensor *dados_sensor = sensors;
                        size_t size_msg = 0;
                        //Apresenta todos os sensores que enviaram dados ao sistema
                        strcpy(msg_enviar,"[ID]\n");
                        size_msg += 5;

                        for (int e = 0; e < MAX_SENSORS; e++) {
                            // se o sensor existir na memória partilhada concatena na mensagem
                            if (strcmp(dados_sensor->sensor, " ") == 0) break;
                            if (strcmp(dados_sensor->sensor, " ") > 0) {
                                strcat(msg_enviar,dados_sensor->sensor);
                                size_msg += strlen(dados_sensor->sensor) + 1;
                                strcat(msg_enviar,"\n");
                            }
                            dados_sensor++;
                        }

                        msg_enviar[size_msg] = '\0';
                        enviar_mensagem_queue(msg_enviar,identificador);
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Sensores enviados para a consola %d\n", i, identificador);
                        write_log_file_and_print(msg_log, log_file);


                    }else if(strcmp(token, "add_alert") == 0) {
                        //Adiciona uma nova regra de alerta ao sistema
                        char palavras[4][50];
                        int k = 0;

                        token = strtok(NULL, "#");
                        // extrair as palavras usando strtok
                        while (k < 4) {
                            strcpy(palavras[k],token);
                            token = strtok(NULL, "#");
                            k++;
                        }
                        char id[33];
                        strcpy(id,palavras[0]);
                        char chave[33];
                        strcpy(chave,palavras[1]);
                        int min = atoi(palavras[2]);
                        int max = atoi(palavras[3]);

                        Alerta *dados_alerta = alerts;

                        int verifica_erros = 1;
                        // encontra o alerta correspondente na share memory
                        for (int e = 0; e < MAX_ALERTS; e++) {
                            if (strcmp(dados_alerta->id, id) == 0) {
                                // verifica se o alerta ja existe
                                verifica_erros = 2;
                                break;
                            }
                        }


                        if(verifica_erros == 2){
                            sprintf(msg_enviar,"Erro: Alerta de ID %s ja existente, operacao descartada\n",id);
                            write_log_file_and_print(msg_enviar,log_file);
                            enviar_mensagem_queue(msg_enviar,identificador);
                            break;
                        }

                        for (int e = 0; e < MAX_ALERTS; e++) {
                            if (strcmp(dados_alerta->id, " ") == 0) {
                                // adiciona o alerta
                                sem_wait(sem_share);
                                strcpy(dados_alerta->id, id);
                                strcpy(dados_alerta->chave, chave);
                                printf("%s\n",dados_alerta->chave);
                                dados_alerta->max = max;
                                dados_alerta->min = min;
                                dados_alerta->console = identificador;
                                sem_post(sem_share);
                                verifica_erros = 0;
                                break;
                            }
                            dados_alerta++;
                        }

                        if(verifica_erros){
                            sprintf(msg_enviar,"Erro: numero maximo de alertas alcancado, alerta de ID %s descartado.\n",id);
                            write_log_file_and_print(msg_enviar,log_file);
                            enviar_mensagem_queue(msg_enviar,identificador);
                            break;
                        }

                        strcpy(msg_enviar,"OK\n");
                        enviar_mensagem_queue(msg_enviar,identificador);
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Add_alert %s chave %s\n", i, id, chave);
                        write_log_file_and_print(msg_log, log_file);



                    }else if(strcmp(token, "remove_alert") == 0) {
                        token = strtok(NULL, "#");

                        char id[50];
                        strcpy(id,token);

                        Alerta *dados_alerta = alerts;

                        int existe = 0;
                        for (int e = 0; e < MAX_ALERTS; e++) {

                            if (strcmp(dados_alerta->id,id) == 0) {
                                // remove o alerta
                                existe = 1;
                                sem_wait(sem_share);
                                strcpy(dados_alerta->id," ");
                                sem_post(sem_share);
                                break;
                            }
                            dados_alerta++;
                        }
                        if(existe) strcpy(msg_enviar,"OK\n");
                        else strcpy(msg_enviar,"ERROR\n");
                        enviar_mensagem_queue(msg_enviar,identificador);
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Alerta %s removido\n", i, id);
                        write_log_file_and_print(msg_log, log_file);



                    }else if(strcmp(token, "list_alerts") == 0) {
                        Alerta *dados_alerta = alerts;
                        char msg_cat[100];

                        //Apresenta estatísticas referentes aos dados dos alertas, incluindo os valores máximo e mínimo.
                        strcpy(msg_enviar,"[ID] [Key] [MIN] [MAX]\n");
                        for (int e = 0; e < MAX_ALERTS; e++) {
                            // se o alerta existir na memória partilhada concatena na mensagem
                            if (strcmp(dados_alerta->id, " ") > 0) {
                                sprintf(msg_cat,"%s %s %d %d\n",dados_alerta->id,dados_alerta->chave,dados_alerta->min,dados_alerta->max);
                                strcat(msg_enviar,msg_cat);
                            }
                            dados_alerta++;
                        }
                        enviar_mensagem_queue(msg_enviar,identificador);
                        char msg_log[100];
                        sprintf(msg_log, "Worker %d: Lista de alertas enviados para a consola %d\n", i, identificador);
                        write_log_file_and_print(msg_log, log_file);



                    }else{
                        write_log_file_and_print("mensagem invalida no pipe\n",log_file);
                    }
                    /*
                    int p;
                    for (p = 0; p < N_WORKERS; p++) {
                        printf("%d\n",*worker_teste);
                        worker_teste++;
                    }
                    worker_teste -= p;
                    */
                    sem_wait(sem_share);
                    *work = 0;
                    sem_post(sem_share);
                    //printf("worker %d livre\n",i);
                    sem_post(sem);
                }

                exit(EXIT_SUCCESS);
            } else {
                // processo pai
                worker_valid_worker++;
            }
        }
        // Definir os handlers de sinais
        struct sigaction sa;
        sa.sa_handler = sig_handler;
        sigfillset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
    } else {
        // erro ao criar processo Alerts Watcher
        write_log_file_and_print("ERROR CREATING ALERTS WATCHER",log_file);
        exit(EXIT_FAILURE);
    }
    pthread_join(sensor_reader_thread, NULL);
    pthread_join(console_reader_thread, NULL);
    pthread_join(dispatcher_thread, NULL);
    wait(NULL);
    cleanup();
    return 0;
}
