#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#define MAX_PROCESSES 20
#define SHMKEY 5678
#define MSGKEY 1234
#define QUEUE_LEVELS 3
#define CLOCK_INCREMENT_MS 100
#define TIME_LIMIT 3

struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int serviceTimeSeconds;
    int serviceTimeNano;
    int eventWaitSec;
    int eventWaitNano;
    int blocked;
};

struct PCB processTable[MAX_PROCESSES];

struct SharedClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

struct SharedClock *simClock;
int shm_id, msq_id;
FILE *logfile;
time_t startTime;

struct msgbuf {
    long mtype;
    int data;
};

struct Queue {
    int front, rear, size;
    unsigned capacity;
    int* array;
};

struct Queue* createQueue(unsigned capacity) {
    struct Queue* queue = (struct Queue*) malloc(sizeof(struct Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->array = (int*) malloc(queue->capacity * sizeof(int));
    return queue;
}

int isFull(struct Queue* queue) {
    return (queue->size == queue->capacity);
}

int isEmpty(struct Queue* queue) {
    return (queue->size == 0);
}

void enqueue(struct Queue* queue, int item) {
    if (isFull(queue))
        return;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
}

int dequeue(struct Queue* queue) {
    if (isEmpty(queue))
        return -1;
    int item = queue->array[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

struct Queue* queues[QUEUE_LEVELS];

void initializeQueues() {
    for (int i = 0; i < QUEUE_LEVELS; i++) {
        queues[i] = createQueue(MAX_PROCESSES);
    }
}

void cleanup() {
    shmdt(simClock);
    shmctl(shm_id, IPC_RMID, NULL);
    msgctl(msq_id, IPC_RMID, NULL);
}

void sigintHandler(int sig) {
    cleanup();
    exit(0);
}

void incrementClock(int nanoseconds) {
    simClock->nanoseconds += nanoseconds;
    if (simClock->nanoseconds >= 1000000000) {
        simClock->seconds++;
        simClock->nanoseconds -= 1000000000;
    }
}

void createProcess(int index) {
    int startSec = simClock->seconds;
    int startNano = simClock->nanoseconds;

    pid_t pid = fork();
    if (pid == 0) {
        execl("./worker", "worker", "5", "500000", NULL);
        perror("execl failed");
        exit(EXIT_FAILURE);
    }

    processTable[index] = (struct PCB){1, pid, startSec, startNano, 0, 0, 0, 0, 0};
    enqueue(queues[0], index);
}

void createProcess(int index) {
    int startSec = simClock->seconds;
    int startNano = simClock->nanoseconds;

    pid_t pid = fork();
    if (pid == 0) {
        execl("./worker", "worker", "5", "500000", NULL);
        perror("execl failed");
        exit(EXIT_FAILURE);
    }

    processTable[index] = (struct PCB){1, pid, startSec, startNano, 0, 0, 0, 0, 0};
    enqueue(queues[0], index);

    fprintf(logfile, "OSS: Generating process with PID %d and putting it in queue 0 at time %d:%d\n", pid, simClock->seconds, simClock->nanoseconds);
}

void scheduleProcess() {
    for (int i = 0; i < QUEUE_LEVELS; i++) {
        if (!isEmpty(queues[i])) {
            int index = dequeue(queues[i]);
            struct PCB *pcb = &processTable[index];

            struct msgbuf msg;
            msg.mtype = pcb->pid;
            msg.data = (i + 1) * 10000; // Time quantum

            int dispatchStartSec = simClock->seconds;
            int dispatchStartNano = simClock->nanoseconds;

            if (msgsnd(msq_id, &msg, sizeof(msg.data), 0) == -1) {
                perror("msgsnd failed");
            }

            if (msgrcv(msq_id, &msg, sizeof(msg.data), pcb->pid, 0) == -1) {
                perror("msgrcv failed");
            } else {
                int dispatchEndSec = simClock->seconds;
                int dispatchEndNano = simClock->nanoseconds;
                int dispatchTime = (dispatchEndSec - dispatchStartSec) * 1000000000 + (dispatchEndNano - dispatchStartNano);

                fprintf(logfile, "OSS: Dispatching process with PID %d from queue %d at time %d:%d,\n", pcb->pid, i, dispatchStartSec, dispatchStartNano);
                fprintf(logfile, "OSS: total time this dispatch was %d nanoseconds\n", dispatchTime);
                fprintf(logfile, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", pcb->pid, msg.data);

                if (msg.data < 0) {
                    pcb->occupied = 0;
                    waitpid(pcb->pid, NULL, 0);
                } else {
                    incrementClock(msg.data);
                    if (msg.data < (i + 1) * 10000) {
                        pcb->blocked = 1;
                        fprintf(logfile, "OSS: Putting process with PID %d into blocked queue\n", pcb->pid);
                    } else {
                        if (i < QUEUE_LEVELS - 1) {
                            enqueue(queues[i + 1], index);
                            fprintf(logfile, "OSS: Putting process with PID %d into queue %d\n", pcb->pid, i + 1);
                        } else {
                            enqueue(queues[i], index);
                            fprintf(logfile, "OSS: Putting process with PID %d into queue %d\n", pcb->pid, i);
                        }
                    }
                }
            }
            break;
        }
    }
}

void logStatus() {
    fprintf(logfile, "OSS PID:%d SysClockS: %d SysClockNano: %d\n", getpid(), simClock->seconds, simClock->nanoseconds);
    fprintf(logfile, "Process Table:\nEntry\tOccupied\tPID\tStartS\tStartN\tServiceS\tServiceN\tBlocked\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        fprintf(logfile, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano, processTable[i].serviceTimeSeconds, processTable[i].serviceTimeNano, processTable[i].blocked);
    }
}

void logQueues() {
    for (int i = 0; i < QUEUE_LEVELS; i++) {
        fprintf(logfile, "Queue %d: ", i);
        for (int j = queues[i]->front; j != queues[i]->rear; j = (j + 1) % queues[i]->capacity) {
            fprintf(logfile, "%d ", queues[i]->array[j]);
        }
        fprintf(logfile, "\n");
    }
}

void checkTimeLimit() {
    if (time(NULL) - startTime >= TIME_LIMIT) {
        printf("Time limit reached. Terminating.\n");
        cleanup();
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigintHandler);
    startTime = time(NULL);

    shm_id = shmget(SHMKEY, sizeof(struct SharedClock), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    simClock = (struct SharedClock *) shmat(shm_id, NULL, 0);
    if (simClock == (void *) -1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    msq_id = msgget(MSGKEY, IPC_CREAT | 0666);
    if (msq_id == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }

    logfile = fopen("oss.log", "w");
    if (!logfile) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    initializeQueues();

    int runningChildren = 0, launchedChildren = 0;

    while (launchedChildren < MAX_PROCESSES || runningChildren > 0) {
        checkTimeLimit();
        incrementClock(CLOCK_INCREMENT_MS);
        logStatus();
        logQueues();

        if (runningChildren < MAX_PROCESSES && launchedChildren < MAX_PROCESSES) {
            launchProcesses();
            launchedChildren++;
            runningChildren++;
        }

        scheduleProcess();
    }

    fclose(logfile);
    cleanup();
    return 0;
}