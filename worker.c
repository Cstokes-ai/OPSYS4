//
// Created by corne on 3/25/2025.
//
// Worker

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
/*
 * /*worker, the children
The worker takes in two command line arguments, this time corresponding to how many seconds and nanoseconds it should
decide to stay around in the system. For example, if you were running it directly you might call it like (note that oss launches
this when you are actually running the project):
Linux System Calls 2
./worker 5 500000
The worker will start by attaching to shared memory. However, it will not examine it. It will then go into a loop.
do {
msgrcv(from oss);
check the clock
determine if it is time to terminate
msgsnd(to oss, saying if we are done or not)
} while (not done);
It determines the termination time by adding up the system clock time and the time passed to it in the initial call through
command line arguments (in our simulated system clock, not actual time). This is when the process should decide to leave the
system and terminate.
For example, if the system clock was showing 6 seconds and 100 nanoseconds and the worker was passed 5 and 500000 as
above, the target time to terminate in the system would be 11 seconds and 500100 nanoseconds. The worker would check this
termination time to see if it has passed each iteration of the loop. If it ever looks at the system clock and sees values over the
ones when it should terminate, it should output some information, send a message to oss and then terminate.
So what output should the worker send? Upon starting up, it should output the following information:
WORKER PID:6577 PPID:6576 SysClockS: 5 SysclockNano: 1000 TermTimeS: 11 TermTimeNano: 500100
--Just Starting
The worker should then go into a loop, waiting for a message from oss, checking the clcok and then sending a message back. It
should also do some periodic output.
In this project, unlike previous projects, you should output a message every iteration of the loop. Something like the following:
WORKER PID:6577 PPID:6576 SysClockS: 6 SysclockNano: 45000000 TermTimeS: 11 TermTimeNano: 500100
--1 iteration has passed since it started
and then the next iteration it would output:
WORKER PID:6577 PPID:6576 SysClockS: 7 SysclockNano: 500000 TermTimeS: 11 TermTimeNano: 500100
--2 iterations have passed since starting
Once its time has elapsed, supposing it ran for 10 iterations, it would send out one final message:
WORKER PID:6577 PPID:6576 SysClockS: 11 SysclockNano: 700000 TermTimeS: 11 TermTimeNano: 500100
--Terminating after sending message back to oss after 10 iterations.*/


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