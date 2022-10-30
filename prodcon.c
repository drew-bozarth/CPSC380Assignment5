#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct item {
  unsigned int seqn; 
  uint8_t data[22];  
  time_t timestamp;  
  uint16_t checksum; 
} ITEM;  

uint16_t checksum(char *address, uint32_t tally)
{
    register uint32_t sum = 0;

    uint16_t *buf = (uint16_t *) address;

    // summing loop
    while(tally > 1)
    {
        sum = sum + *(buf)++;
        tally = tally - 2;
    }

    // if there is left-over byte, add
    if (tally > 0)
        sum = sum + *address;

    // fold 32-bit sum to 16 bits
    while (sum>>16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return(~sum);

}

sem_t *empty;
sem_t *full;
pthread_mutex_t mutex;

char* shm_name = "shm";
struct stat buf;
uint8_t  *shm_ptr;
int shm_fd;

int in = 0;
int out = 0;

void* producer();
void* consumer();

int n_items;

int main(int argc, char *argv[]){
  // error handle to check if there was not a command line argument given
  if (argc != 2){
    printf("Incorrect args");
    return -1;
  }

  // set size of n_items to what the command line arg was
  n_items = atoi(argv[1]);

  sem_unlink("empty");
  sem_unlink("full");
  shm_unlink(shm_name);

  // create mutex and semaphores
  pthread_mutex_init(&mutex, NULL);
  empty = sem_open("/empty", O_CREAT, 0644, n_items);
  full = sem_open("/full", O_CREAT, 0644, 0);

  // create shared memory buffer
  shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0644);
  if (shm_fd == -1) {
      fprintf(stderr, "ERROR - shared memory not created, '%s, errno = %d (%s)\n", shm_name,
        errno, strerror(errno));
      return -1;
  }

  // configure the size of the shared memory segment
  if (ftruncate(shm_fd, n_items*sizeof(ITEM)) == -1) {
      fprintf(stderr, "ERROR - not able to configure shared memory segment, '%s, errno = %d (%s)\n", shm_name,
        errno, strerror(errno));
      shm_unlink(shm_name);
      return -1;
  }

  // get configuration of shared memory segment
  if (fstat(shm_fd, &buf) == -1) {
      fprintf(stderr, "ERROR - not able to get status of shared memory segment, fd = %d, errno = %d (%s)\n", shm_fd,
              errno, strerror(errno));
      return -1;
  }

  // attach to shared memory region
  shm_ptr = (uint8_t *)mmap(0, buf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (shm_ptr == MAP_FAILED) {
      fprintf(stderr, "ERROR - not able to map to shared memory segment, errno = %d (%s) \n",
              errno, strerror(errno));
      return -1;
  }

  pthread_t a_thread[2];
  void *thread_result;
  int res;

  res = pthread_create(&a_thread[0], NULL, producer, NULL);
  if (res != 0) {
      perror("Producer thread not able to be created");
      exit(EXIT_FAILURE);
  }

  res = pthread_create(&a_thread[1], NULL, consumer, NULL);
  if (res != 0) {
      perror("Consumer thread not able to be created");
      exit(EXIT_FAILURE);
  }

  pthread_join(a_thread[0],NULL);
  pthread_join(a_thread[1],NULL);

  // remove the shared memory segment
  if (shm_unlink(shm_name) == -1) {
      fprintf(stderr, "ERROR - not able to remove shared memory segment '%s', errno = %d (%s) \n", shm_name,
              errno, strerror(errno));
      return -1;
  }

}

// producer thread function
void* producer(void* arg){

  ITEM item;
  item.data[0] = (uint8_t)rand() % 256;
  item.checksum = checksum((char*) item.data, 22);

  srand (time(0));
  int index = 0;
  while(index<n_items){
    item.seqn = index++;
    item.timestamp = time(NULL);
    for(int j = 0; j <22; ++j){
      item.data[j] = (uint8_t)rand() % 256;
    }
    item.checksum = checksum((char*) item.data, 22);

    printf("Sum = %s\n", item.data);
    fflush(stdout);

    sem_wait(empty);
    pthread_mutex_lock(&mutex);
      // critical section
      memcpy((void*) &shm_ptr[in], (void*) &item, sizeof(ITEM));
      in = (in + 1) % n_items;

    pthread_mutex_unlock(&mutex);
    sem_post(full);
    printf("Producer item: %s\n", item.data);
    fflush(stdout);
  }
  return arg;
}

// consumer thread function
void *consumer(void* arg){

  ITEM item;
  unsigned int seqn;

  while(1) {
    sem_wait(full);
    pthread_mutex_lock(&mutex);
      //critical section
      memcpy((void*) &item, (void*) &shm_ptr[out], sizeof(ITEM));
      out = (out + 1) % n_items;

    pthread_mutex_unlock(&mutex);
    sem_post(empty);

    //check seqn
    seqn = item.seqn;

    uint16_t cksum = checksum((char*) item.data, 22);
    if (item.checksum != cksum) {
        printf("failed %u %s\n", cksum, item.data);
        fflush(stdout);
    }
  }

  return arg;
}
