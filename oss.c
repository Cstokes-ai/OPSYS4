
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
#define CLOCK_INCREMENT_MS 250
#define TIME_LIMIT 60 // Termination after 60 seconds
#define MSGKEY 1234
#define SHMKEY 5678

// Define message structure
struct msgbuf {
    long mtype;
    int data;
};

// Define Process Control Block
struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int messagesSent;
};

// Global variables
struct PCB processTable[MAX_PROCESSES];
int shm_id, msq_id;
FILE *logfile;

time_t startTime;

// Shared memory clock
struct SharedClock {
    int seconds;
    int nanoseconds;
};

struct SharedClock *simClock;

// Function prototypes
void printProcessTable();
void cleanup();
void sigintHandler(int sig);
void incrementClock(int runningChildren);
void checkTimeLimit();

int main(int argc, char *argv[]) {
    int opt, maxChildren = 5, maxSimul = 3, maxTimeLimit = 10, interval = 100;
    char logFilename[256] = "oss.log";

    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-n maxChildren] [-s maxSimul] [-t timeLimit] [-i interval] [-f logfile]\n", argv[0]);
                exit(0);
            case 'n': maxChildren = atoi(optarg); break;
            case 's': maxSimul = atoi(optarg); break;
            case 't': maxTimeLimit = atoi(optarg); break;
            case 'i': interval = atoi(optarg); break;
            case 'f': strncpy(logFilename, optarg, 255); break;
            default:
                fprintf(stderr, "Invalid option. Use -h for help.\n");
                exit(EXIT_FAILURE);
        }
    }

    signal(SIGINT, sigintHandler);
    startTime = time(NULL);

    shm_id = shmget(SHMKEY, sizeof(struct SharedClock), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("shmget failed in oss");
        exit(EXIT_FAILURE);
    } else {
        printf("OSS: Shared memory created with ID %d\n", shm_id);
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

    logfile = fopen(logFilename, "w");
    if (!logfile) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    int runningChildren = 0, launchedChildren = 0;

    while (launchedChildren < maxChildren || runningChildren > 0) {
        checkTimeLimit();
        incrementClock(runningChildren);
        printProcessTable();

        if (runningChildren < maxSimul && launchedChildren < maxChildren) {
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (!processTable[i].occupied) {
                    int startSec = simClock->seconds;
                    int startNano = simClock->nanoseconds;

                    pid_t pid = fork();
                    if (pid == 0) {
                        execl("./worker", "worker", "5", "500000", NULL);
                        perror("execl failed");
                        exit(EXIT_FAILURE);
                    }

                    processTable[i] = (struct PCB){1, pid, startSec, startNano, 0};
                    launchedChildren++;
                    runningChildren++;
                    break;
                }
            }
        }

        struct msgbuf msg;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processTable[i].occupied) {
                msg.mtype = processTable[i].pid;
                msg.data = 1;
                if (msgsnd(msq_id, &msg, sizeof(msg.data), 0) == -1) {
                    perror("msgsnd failed");
                }
                processTable[i].messagesSent++;
                fprintf(logfile, "OSS: Sent message to worker %d PID %d at time %d:%d\n",
                        i, processTable[i].pid, simClock->seconds, simClock->nanoseconds);
            }
        }

        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processTable[i].occupied) {
                if (msgrcv(msq_id, &msg, sizeof(msg.data), processTable[i].pid, 0) == -1) {
                    perror("msgrcv failed");
                } else {
                    fprintf(logfile, "OSS: Received message from worker %d PID %d at time %d:%d\n",
                            i, processTable[i].pid, simClock->seconds, simClock->nanoseconds);
                    if (msg.data == 0) {
                        fprintf(logfile, "OSS: Worker %d PID %d terminating.\n", i, processTable[i].pid);
                        waitpid(processTable[i].pid, NULL, 0);
                        processTable[i].occupied = 0;
                        runningChildren--;
                    }
                }
            }
        }
    }

    fclose(logfile);
    cleanup();
    return 0;
}

void printProcessTable() {
    printf("OSS PID:%d SysClockS: %d SysClockNano: %d\n", getpid(), simClock->seconds, simClock->nanoseconds);
    printf("Process Table:\nEntry\tOccupied\tPID\tStartS\tStartN\tMessagesSent\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        printf("%d\t%d\t%d\t%d\t%d\t%d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano, processTable[i].messagesSent);
    }

    // Log to file
    fprintf(logfile, "OSS PID:%d SysClockS: %d SysClockNano: %d\n", getpid(), simClock->seconds, simClock->nanoseconds);
    fprintf(logfile, "Process Table:\nEntry\tOccupied\tPID\tStartS\tStartN\tMessagesSent\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        fprintf(logfile, "%d\t%d\t%d\t%d\t%d\t%d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano, processTable[i].messagesSent);
    }
}

void incrementClock(int runningChildren) {
    int increment = runningChildren > 0 ? CLOCK_INCREMENT_MS / runningChildren : CLOCK_INCREMENT_MS;
    simClock->nanoseconds += increment * 1000000;
    if (simClock->nanoseconds >= 1000000000) {
        simClock->seconds++;
        simClock->nanoseconds -= 1000000000;
    }
}

void checkTimeLimit() {
    if (time(NULL) - startTime >= TIME_LIMIT) {
        printf("Time limit reached. Terminating.\n");
        cleanup();
        exit(0);
    }
}

void cleanup() {
    shmdt(simClock);
}

void sigintHandler(int sig) {
    cleanup();
    exit(0);
}
//project 4 start