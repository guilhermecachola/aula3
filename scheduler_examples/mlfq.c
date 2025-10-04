#include "mlfq.h"
#include <stdio.h>
#include <stdlib.h>
#include "msg.h"
#include <unistd.h>


void mlfq_init(mlfq_t *mlfq) {

    for (int i = 0; i < MLFQ_NUM_QUEUES; i++) {
        mlfq->queues[i].head = NULL;
        mlfq->queues[i].tail = NULL;
    }
}

void mlfq_add_task(mlfq_t *mlfq, pcb_t *task) {

    if (task->status == TASK_RUNNING) {
        enqueue_pcb(&mlfq->queues[0], task);
    }
}

void mlfq_scheduler(uint32_t current_time_ms, mlfq_t *mlfq, pcb_t **cpu_task) {

    static int fila_atual = 0;


    if (*cpu_task) {

        (*cpu_task)->ellapsed_time_ms += TICKS_MS;


        uint32_t tempo_slice = current_time_ms - (*cpu_task)->slice_start_ms;


        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {

            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }

            free((*cpu_task));
            (*cpu_task) = NULL;
            fila_atual = 0;
        }

        else if (tempo_slice >= MLFQ_TIME_SLICE_MS) {

            int nova_fila = fila_atual;


            if (fila_atual < MLFQ_NUM_QUEUES - 1) {
                nova_fila = fila_atual + 1;
            }


            (*cpu_task)->status = TASK_RUNNING;
            enqueue_pcb(&mlfq->queues[nova_fila], *cpu_task);


            (*cpu_task) = NULL;
            fila_atual = 0;
        }

    }


    if (*cpu_task == NULL) {

        for (int i = 0; i < MLFQ_NUM_QUEUES; i++) {
            if (mlfq->queues[i].head != NULL) {

                        *cpu_task = dequeue_pcb(&mlfq->queues[i]);


                if (*cpu_task) {

                    (*cpu_task)->slice_start_ms = current_time_ms;

                    fila_atual = i;
                    break;
                }
            }
        }
    }
}