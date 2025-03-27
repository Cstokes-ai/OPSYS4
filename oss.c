#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>

#define MAX_PROCS 18
#define MAX_LOG_LINES 10000
#define BASE_TIME_QUANTUM 10000000 // 10ms in nanoseconds
#define NUM_QUEUES 3

// Shared memory for clock
typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} Clock;

Clock *simulated_clock;

// Process Control Block (PCB)
typedef struct {
    int occupied;
    pid_t pid;
    unsigned int start_seconds;
    unsigned int start_nanoseconds;
    unsigned int service_time_seconds;
    unsigned int service_time_nanoseconds;
    unsigned int event_wait_sec;
    unsigned int event_wait_nanoseconds;
    int blocked;
    int queue_level;
} PCB;

PCB process_table[MAX_PROCS];

// Message queue structure
struct msg_buffer {
    long msg_type;
    int quantum;  // Time slice allocated to the process
};

// Queue structure
typedef struct Queue {
    int pids[MAX_PROCS];
    int front;
    int rear;
} Queue;

Queue queues[NUM_QUEUES];

// Function to initialize queues
void initialize_queues() {
    for (int i = 0; i < NUM_QUEUES; i++) {
        queues[i].front = queues[i].rear = -1;
    }
}

// Function to enqueue a process
void enqueue(Queue *queue, int pid) {
    if (queue->rear == MAX_PROCS - 1) return;
    if (queue->front == -1) queue->front = 0;
    queue->rear++;
    queue->pids[queue->rear] = pid;
}

// Function to dequeue a process
int dequeue(Queue *queue) {
    if (queue->front == -1) return -1;
    int pid = queue->pids[queue->front];
    queue->front++;
    if (queue->front > queue->rear) queue->front = queue->rear = -1;
    return pid;
}
// Function to get the current time in "seconds:nanoseconds" format
void get_time_str(char *time_str, size_t len) {
    snprintf(time_str, len, "%u:%09u", simulated_clock->seconds, simulated_clock->nanoseconds);
}

// Function to log the events to the file
void log_event(FILE *log_file, const char *event_msg) {
    char time_str[20];
    get_time_str(time_str, sizeof(time_str));
    fprintf(log_file, "OSS: %s at time %s\n", event_msg, time_str);
    fflush(log_file);
}

// Function to simulate time increment for OSS
void increment_time(unsigned int seconds, unsigned int nanoseconds) {
    simulated_clock->nanoseconds += nanoseconds;
    if (simulated_clock->nanoseconds >= 1000000000) { // 1 second in nanoseconds
        simulated_clock->nanoseconds -= 1000000000;
        simulated_clock->seconds += seconds + 1;
    } else {
        simulated_clock->seconds += seconds;
    }
}

// Function to spawn a new process and log it
void generate_process(FILE *log_file) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (process_table[i].occupied == 0) {
            process_table[i].occupied = 1;
            process_table[i].pid = fork();

            if (process_table[i].pid == 0) {
                // Child process logic
                char msg[100];
                snprintf(msg, sizeof(msg), "Running process with PID %d", getpid());
                log_event(log_file, msg);
                exit(0);  // End the child process after logging
            } else {
                // Parent process logic
                process_table[i].start_seconds = simulated_clock->seconds;
                process_table[i].start_nanoseconds = simulated_clock->nanoseconds;
                process_table[i].queue_level = 0; // Start in queue 0 (highest priority)

                char msg[100];
                snprintf(msg, sizeof(msg), "Generating process with PID %d and putting it in queue 0", process_table[i].pid);
                log_event(log_file, msg);
                enqueue(&queues[0], process_table[i].pid);
                return; // Process generated, exit loop
            }
        }
    }
}

// Function to dispatch a process from the highest priority queue and log
void dispatch_process(FILE *log_file) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        int pid = dequeue(&queues[i]);
        if (pid != -1) {
            for (int j = 0; j < MAX_PROCS; j++) {
                if (process_table[j].pid == pid && process_table[j].blocked == 0) {
                    char msg[100];
                    snprintf(msg, sizeof(msg), "Dispatching process with PID %d from queue %d", process_table[j].pid, i);
                    log_event(log_file, msg);

                    // Simulate the dispatch time
                    int dispatch_time = rand() % 10000 + 500; // Random dispatch time
                    increment_time(0, dispatch_time); // Update clock
                    snprintf(msg, sizeof(msg), "total time this dispatch was %d nanoseconds", dispatch_time);
                    log_event(log_file, msg);

                    // Simulate process execution
                    int ran = (rand() % 2) ? BASE_TIME_QUANTUM : BASE_TIME_QUANTUM / 2;

                    snprintf(msg, sizeof(msg), "Receiving that process with PID %d ran for %d nanoseconds", process_table[j].pid, ran_for);
                    log_event(log_file, msg);

                    // Process transitions
                    if (ran_for >= BASE_TIME_QUANTUM) {
                        if (process_table[j].queue_level < NUM_QUEUES - 1) {
                            process_table[j].queue_level++;
                        }
                        snprintf(msg, sizeof(msg), "Putting process with PID %d into queue %d", process_table[j].pid, process_table[j].queue_level);
                        log_event(log_file, msg);
                        enqueue(&queues[process_table[j].queue_level], process_table[j].pid);
                    } else if (ran_for < BASE_TIME_QUANTUM / 2) {
                        snprintf(msg, sizeof(msg), "Putting process with PID %d into blocked queue", process_table[j].pid);
                        log_event(log_file, msg);
                        process_table[j].blocked = 1; // Block the process
                    } else {
                        snprintf(msg, sizeof(msg), "Process with PID %d did not use its entire time quantum", process_table[j].pid);
                        log_event(log_file, msg);
                        enqueue(&queues[process_table[j].queue_level], process_table[j].pid);
                    }
                    return; // Dispatch one process and return
                }
            }
        }
    }
}
// Main function
int main() {
    srand(time(NULL));

    // Create shared memory for simulated clock
    int shm_id = shmget(IPC_PRIVATE, sizeof(Clock), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget failed");
        exit(1);
    }
    simulated_clock = (Clock*)shmat(shm_id, NULL, 0);
    if (simulated_clock == (void *) -1) {
        perror("shmat failed");
        exit(1);
    }
    simulated_clock->seconds = 0;
    simulated_clock->nanoseconds = 0;

    // Open log file
    FILE *log_file = fopen("oss_log.txt", "w");
    if (log_file == NULL) {
        perror("Error opening log file");
        exit(1);
    }

    // Initialize queues
    initialize_queues();

    // Main loop
    int log_lines = 0;
    while (log_lines < MAX_LOG_LINES) {
        // Generate a process and log
        generate_process(log_file);
        log_lines++;

        // Dispatch a process from the queue and log
        dispatch_process(log_file);
        log_lines++;

        // Output the process table every 0.5 seconds
        if (simulated_clock->nanoseconds % 500000000 == 0) { // 0.5 seconds
            // Log the process table and queue status
            fprintf(log_file, "Process Table and Queue status at time %u:%09u\n", simulated_clock->seconds, simulated_clock->nanoseconds);
            log_lines++;
        }

        if (log_lines >= MAX_LOG_LINES) break; // Stop after exceeding max log lines
    }

    fclose(log_file);
    return 0;
}
