#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>
#include <unistd.h>

#define QUEUE_SIZE 10

// mpicc -o at4 at4.c -lpthread
// mpiexec -n 3 ./at4

typedef struct {
  int p[3]; // Vetor de relógios
  int owner; // Identificador do processo dono do relógio
} Clock;

typedef struct _Queue {
  Clock queue[QUEUE_SIZE]; // Fila de relógios
  int start, end; // Índices do início e do fim da fila
  pthread_mutex_t mutex; // Trava para sincronização de acesso à fila
  pthread_cond_t is_full, is_empty; // Condições para fila cheia e vazia
} Queue;

Queue receiveQueue, sendQueue; // Filas de recebimento e envio de relógios
Clock global_clock = { { 0, 0, 0 }, 0 }; // Relógio global

// Variáveis para controle do snapshot
int snapshot_started = 0;
int *snapshot_values;

// Cria uma fila de relógios
Queue create_queue() {
  Queue q;
  q.start = 0;
  q.end = 0;
  pthread_mutex_init(&q.mutex, NULL);
  pthread_cond_init(&q.is_full, NULL);
  pthread_cond_init(&q.is_empty, NULL);
  return q;
}

// Adiciona um relógio à fila
int add_to_queue(Queue *q, Clock c) {
  pthread_mutex_lock(&q->mutex);
  while ((q->end + 1) % QUEUE_SIZE == q->start % QUEUE_SIZE) {
    pthread_cond_wait(&q->is_full, &q->mutex);
  }
  q->queue[q->end % QUEUE_SIZE] = c;
  q->end++;
  pthread_cond_signal(&q->is_empty);
  pthread_mutex_unlock(&q->mutex);
  return 0;
}

// Remove um relógio da fila
Clock remove_from_queue(Queue *q) {
  pthread_mutex_lock(&q->mutex);
  while (q->start == q->end) {
    pthread_cond_wait(&q->is_empty, &q->mutex);
  }
  Clock c = q->queue[q->start % QUEUE_SIZE];
  q->start++;
  pthread_cond_signal(&q->is_full);
  pthread_mutex_unlock(&q->mutex);
  return c;
}

// Thread que recebe relógios
void* receive_thread(void* arg) {
  Clock c;
  while (1) {
    MPI_Recv(&c, sizeof(Clock) / sizeof(int), MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    add_to_queue(&receiveQueue, c);
  }
  return NULL;
}

// Thread que envia relógios
void* send_thread(void* arg) {
  Clock c;
  while (1) {
    c = remove_from_queue(&sendQueue);
    int target = c.owner;
    c.owner = global_clock.owner;
    MPI_Send(&c, sizeof(Clock) / sizeof(int), MPI_INT, target, 0, MPI_COMM_WORLD);
    printf("Send: %d, Clock: (%d, %d, %d)\n", global_clock.owner, global_clock.p[0], global_clock.p[1], global_clock.p[2]);
  }
  return NULL;
}

// Realiza um evento
void Event() {
  global_clock.p[global_clock.owner]++;
  printf("Event: %d, Clock: (%d, %d, %d)\n", global_clock.owner, global_clock.p[0], global_clock.p[1], global_clock.p[2]);
}

// Envia um relógio para um processo
void Send(int target) {
  global_clock.p[global_clock.owner]++;
  Clock c = global_clock;
  c.owner = target;
  add_to_queue(&sendQueue, c);
}

// Recebe um relógio de um processo
void Receive(Clock *c) {
  *c = remove_from_queue(&receiveQueue);
  global_clock.p[global_clock.owner]++;
  for (int i = 0; i < 3; i++) {
    if (global_clock.p[i] < c->p[i]) {
      global_clock.p[i] = c->p[i];
    }
  }
  printf("Receive: %d, Clock: (%d, %d, %d)\n", global_clock.owner, global_clock.p[0], global_clock.p[1], global_clock.p[2]);
}

// Função para iniciar o snapshot
void StartSnapshot() {
  snapshot_started = 1;
  snapshot_values = (int *)malloc(3 * sizeof(int));
  for (int i = 0; i < 3; i++) {
    snapshot_values[i] = global_clock.p[i];
  }
  printf("Snapshot iniciado pelo processo %d\n", global_clock.owner);
}

// Função para finalizar o snapshot
void EndSnapshot() {
  snapshot_started = 0;
  free(snapshot_values);
}

// Função para registrar o estado durante o snapshot
void RecordState() {
  if (snapshot_started) {
    printf("Snapshot value no processo %d: Clock (%d, %d, %d)\n",
           global_clock.owner, snapshot_values[0], snapshot_values[1], snapshot_values[2]);
  }
}

// Tira um snapshot do estado global do sistema
void TakeSnapshot() {
  StartSnapshot();
  RecordState();
  EndSnapshot();
}

void process0();
void process1();
void process2();


int main(int argc, char **argv) {
  int my_rank;
  MPI_Init(NULL, NULL);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  global_clock.owner = my_rank;

  receiveQueue = create_queue();
  sendQueue = create_queue();

  pthread_t threadRecv, threadSend;
  pthread_create(&threadRecv, NULL, receive_thread, NULL);
  pthread_create(&threadSend, NULL, send_thread, NULL);

  switch (my_rank) {
    case 0: process0(); break;
    case 1: process1(); break;
    case 2: process2(); break;
    default:
      printf("Invalid rank %d", my_rank);
      exit(1);
  }

  pthread_join(threadRecv, NULL);
  pthread_join(threadSend, NULL);

  // Tira um snapshot no final de cada processo
  TakeSnapshot();

  MPI_Finalize();
  return 0;
}

void process0() {
  Event();
  StartSnapshot();
  Send(1);
  Clock received_clock;
  Receive(&received_clock);
  Send(2);
  Receive(&received_clock);
  Send(1);
  Event();
  RecordState();
  EndSnapshot();
}

void process1() {
  Send(0);
  StartSnapshot();
  Clock received_clock;
  Receive(&received_clock);
  Receive(&received_clock);
  RecordState();
}

void process2() {
  Event();
  Send(0);
  StartSnapshot();
  Clock received_clock;
  Receive(&received_clock);
  RecordState();
}
