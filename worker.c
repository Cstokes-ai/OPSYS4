#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <signal.h>

#define MAX_TIME_QUANTUM_NS 10000000 // 10ms
#define MAX_RUNNING_TIME_NS 5000000000 // 5 seconds max for a process to run

typedef struct {
    long mtype;
    int quantum;  // Time quantum in nanoseconds
} Message;

int msgqid;

void signal_handler(int signum) {
    // Cleanup on termination signal
    msgctl(msgqid, IPC_RMID, NULL);
    exit(0);
}

void init_msg_queue() {
    key_t key = ftok("oss", 65);
    msgqid = msgget(key, 0666 | IPC_CREAT);
    if (msgqid == -1) {
        perror("msgget");
        exit(1);
    }
}

void process_work(int quantum) {
    // Simulate work
    usleep(quantum / 1000);  // Convert ns to microseconds for sleep
    printf("Worker: Process completed with %d ns of work.\n", quantum);
}

void receive_message_and_run() {
    Message msg;
    if (msgrcv(msgqid, &msg, sizeof(msg.quantum), getpid(), 0) == -1) {
        perror("msgrcv");
        exit(1);
    }

    process_work(msg.quantum);

    // Send message back to OSS
    msg.quantum = msg.quantum;  // Just echoing the received quantum
    if (msgsnd(msgqid, &msg, sizeof(msg.quantum), 0) == -1) {
        perror("msgsnd");
        exit(1);
    }
}

int main() {
    signal(SIGINT, signal_handler);  // Handle cleanup on interrupt

    init_msg_queue();

    // Main worker loop
    while (1) {
        receive_message_and_run();  // Wait for time quantum from oss
    }

    return 0;
}
