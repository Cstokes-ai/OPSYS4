#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <string.h>

#define MSGKEY 1234
#define SHMKEY 5678

typedef struct {
    long mtype;
    int data;
} message;

typedef struct {
    int seconds;
    int nanoseconds;
} system_clock;

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <seconds> <nanoseconds>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int termSeconds = atoi(argv[1]);
    int termNanoseconds = atoi(argv[2]);

    int shmid = shmget(SHMKEY, sizeof(system_clock), 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    system_clock *sim_clock = (system_clock *)shmat(shmid, NULL, 0);
    if (sim_clock == (void *) -1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    int msgid = msgget(MSGKEY, 0666);
    if (msgid == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }

    int startSeconds = sim_clock->seconds;
    int startNanoseconds = sim_clock->nanoseconds;
    int termTimeSeconds = startSeconds + termSeconds;
    int termTimeNanoseconds = startNanoseconds + termNanoseconds;
    if (termTimeNanoseconds >= 1000000000) {
        termTimeSeconds++;
        termTimeNanoseconds -= 1000000000;
    }

    printf("WORKER PID:%d PPID:%d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n--Just Starting\n",
           getpid(), getppid(), startSeconds, startNanoseconds, termTimeSeconds, termTimeNanoseconds);

    int iterations = 0;
    message msg;
    msg.mtype = getpid();

    do {
        if (msgrcv(msgid, &msg, sizeof(msg.data), getpid(), 0) == -1) {
            perror("msgrcv failed");
            exit(EXIT_FAILURE);
        }

        int currentSeconds = sim_clock->seconds;
        int currentNanoseconds = sim_clock->nanoseconds;

        printf("WORKER PID:%d PPID:%d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n--%d iteration(s) has passed since starting\n",
               getpid(), getppid(), currentSeconds, currentNanoseconds, termTimeSeconds, termTimeNanoseconds, iterations);

        if (currentSeconds > termTimeSeconds || (currentSeconds == termTimeSeconds && currentNanoseconds >= termTimeNanoseconds)) {
            printf("WORKER PID:%d PPID:%d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n--Terminating after sending message back to oss after %d iterations.\n",
                   getpid(), getppid(), currentSeconds, currentNanoseconds, termTimeSeconds, termTimeNanoseconds, iterations);
            msg.data = 0; // Indicate termination
            if (msgsnd(msgid, &msg, sizeof(msg.data), 0) == -1) {
                perror("msgsnd failed");
            }
            break;
        } else {
            msg.data = 1; // Indicate still running
            if (msgsnd(msgid, &msg, sizeof(msg.data), 0) == -1) {
                perror("msgsnd failed");
            }
        }

        iterations++;
    } while (1);

    shmdt(sim_clock);
    return 0;
}