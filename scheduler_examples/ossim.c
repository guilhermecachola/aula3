#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "debug.h"

#define MAX_CLIENTS 128

#include <stdlib.h>
#include <sys/errno.h>

#include "fifo.h"
#include "sjf.h"
#include "rr.h"
#include "mlfq.h"

#include "msg.h"
#include "queue.h"

static uint32_t PID = 0;

int setup_server_socket(const char *socket_path) {
    int server_fd;
    struct sockaddr_un addr;

    unlink(socket_path);

    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags != -1) {
        if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl: set non-blocking");
        }
    }
    return server_fd;
}

void check_new_commands(queue_t *command_queue, queue_t *blocked_queue, queue_t *ready_queue, int server_fd, uint32_t current_time_ms) {
    int client_fd;
    do {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EMFILE || errno == ENFILE) {
                perror("accept: too many fds");
                break;
            }
            if (errno == EINTR) continue;
            if (errno == ECONNABORTED) continue;
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                perror("accept");
            }
            break;
        }
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags != -1) {
            if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                perror("fcntl: set non-blocking");
            }
        }
        int fdflags = fcntl(client_fd, F_GETFD, 0);
        if (fdflags != -1) {
            fcntl(client_fd, F_SETFD, fdflags | FD_CLOEXEC);
        }
        DBG("[Scheduler] New client connected: fd=%d\n", client_fd);
        pcb_t *pcb = new_pcb(++PID, client_fd, 0);
        enqueue_pcb(command_queue, pcb);
    } while (client_fd > 0);

    queue_elem_t * elem = command_queue->head;
    while (elem != NULL) {
        pcb_t *current_pcb = elem->pcb;
        msg_t msg;
        int n = read(current_pcb->sockfd, &msg, sizeof(msg_t));
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                elem = elem->next;
            } else {
                if (n < 0) {
                    perror("read");
                } else {
                    DBG("Connection closed by remote host\n");
                }
                queue_elem_t *tmp = elem;
                elem = elem->next;
                free(current_pcb);
                free(tmp);
            }
            continue;
        }
        if (msg.request == PROCESS_REQUEST_RUN) {
            current_pcb->pid = msg.pid;
            current_pcb->time_ms = msg.time_ms;
            current_pcb->ellapsed_time_ms = 0;
            current_pcb->status = TASK_RUNNING;
            if (ready_queue) {
                enqueue_pcb(ready_queue, current_pcb);
            }
            DBG("Process %d requested RUN for %d ms\n", current_pcb->pid, current_pcb->time_ms);
        } else if (msg.request == PROCESS_REQUEST_BLOCK) {
            current_pcb->pid = msg.pid;
            current_pcb->time_ms = msg.time_ms;
            current_pcb->status = TASK_BLOCKED;
            enqueue_pcb(blocked_queue, current_pcb);
            DBG("Process %d requested BLOCK for %d ms\n", current_pcb->pid);
        } else {
            printf("Unexpected message received from client\n");
            continue;
        }
        remove_queue_elem(command_queue, elem);
        queue_elem_t *tmp = elem;
        elem = elem->next;
        free(tmp);

        msg_t ack_msg = {
            .pid = current_pcb->pid,
            .request = PROCESS_REQUEST_ACK,
            .time_ms = current_time_ms
        };
        if (write(current_pcb->sockfd, &ack_msg, sizeof(msg_t)) != sizeof(msg_t)) {
            perror("write");
        }
        DBG("Send ACK message to process %d with time %d\n", current_pcb->pid, current_time_ms);
    }
}

void check_blocked_queue(queue_t * blocked_queue, queue_t * command_queue, uint32_t current_time_ms) {
    queue_elem_t * elem = blocked_queue->head;
    while (elem != NULL) {
        pcb_t *pcb = elem->pcb;

        if (pcb->last_update_time_ms < current_time_ms) {
            if (pcb->time_ms > TICKS_MS) {
                pcb->time_ms -= TICKS_MS;
            } else {
                pcb->time_ms = 0;
            }
        }

        if (pcb->time_ms == 0) {
            msg_t msg = {
                .pid = pcb->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write(pcb->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            DBG("Process %d finished BLOCK, sending DONE\n", pcb->pid);
            pcb->status = TASK_COMMAND;
            pcb->last_update_time_ms = current_time_ms;
            enqueue_pcb(command_queue, pcb);

            remove_queue_elem(blocked_queue, elem);
            queue_elem_t *tmp = elem;
            elem = elem->next;
            free(tmp);
        } else {
            elem = elem->next;
        }
    }
}

static const char *SCHEDULER_NAMES[] = {
    "FIFO",
    "SJF",
    "RR",
    "MLFQ",
    NULL
};

typedef enum  {
    NULL_SCHEDULER = -1,
    SCHED_FIFO = 0,
    SCHED_SJF,
    SCHED_RR,
    SCHED_MLFQ
} scheduler_en;

scheduler_en get_scheduler(const char *name) {
    for (int i = 0; SCHEDULER_NAMES[i] != NULL; i++) {
        if (strcmp(name, SCHEDULER_NAMES[i]) == 0) {
            return (scheduler_en)i;
        }
    }
    printf("Scheduler %s not recognized. Available options are:\n", name);
    for (int i = 0; SCHEDULER_NAMES[i] != NULL; i++) {
        printf(" - %s\n", SCHEDULER_NAMES[i]);
    }
    return NULL_SCHEDULER;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <scheduler>\nScheduler options: FIFO, SJF, RR, MLFQ\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    scheduler_en scheduler_type = get_scheduler(argv[1]);
    if (scheduler_type == NULL_SCHEDULER) {
        return EXIT_FAILURE;
    }

    queue_t command_queue = {.head = NULL, .tail = NULL};
    queue_t ready_queue = {.head = NULL, .tail = NULL};
    queue_t blocked_queue = {.head = NULL, .tail = NULL};

    mlfq_t mlfq;
    if (scheduler_type == SCHED_MLFQ) {
        mlfq_init(&mlfq);
    }

    pcb_t *CPU = NULL;

    int server_fd = setup_server_socket(SOCKET_PATH);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to set up server socket\n");
        return 1;
    }
    printf("Scheduler server listening on %s...\n", SOCKET_PATH);
    uint32_t current_time_ms = 0;
    while (1) {
        check_new_commands(&command_queue, &blocked_queue, &ready_queue, server_fd, current_time_ms);

        if (current_time_ms%1000 == 0) {
            printf("Current time: %d s\n", current_time_ms/1000);
        }

        check_blocked_queue(&blocked_queue, &command_queue, current_time_ms);

        usleep(TICKS_MS * 1000/2);
        check_new_commands(&command_queue, &blocked_queue, &ready_queue, server_fd, current_time_ms);

        if (scheduler_type == SCHED_MLFQ) {
            pcb_t *task;
            while ((task = dequeue_pcb(&ready_queue)) != NULL) {
                mlfq_add_task(&mlfq, task);
            }
        }

        switch (scheduler_type) {
            case SCHED_FIFO:
                fifo_scheduler(current_time_ms, &ready_queue, &CPU);
                break;

            case SCHED_SJF:
                sjf_scheduler(current_time_ms, &ready_queue, &CPU);
                break;

            case SCHED_RR:
                rr_scheduler(current_time_ms, &ready_queue, &CPU);
                break;

            case SCHED_MLFQ:
                mlfq_scheduler(current_time_ms, &mlfq, &CPU);
                break;

            default:
                printf("Unknown scheduler type\n");
                break;
        }

        usleep(TICKS_MS * 1000/2);
        current_time_ms += TICKS_MS;
    }

    return 0;
}