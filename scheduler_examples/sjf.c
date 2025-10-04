#include "sjf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "msg.h"
#include <unistd.h>


void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {

    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

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
        }
    }

    if (*cpu_task == NULL && rq->head != NULL) {


        queue_elem_t *atual = rq->head;
        queue_elem_t *mais_curto = rq->head;

        while (atual != NULL) {

            if (atual->pcb->time_ms < mais_curto->pcb->time_ms) {
                mais_curto = atual;
            }
            atual = atual->next;
        }

        remove_queue_elem(rq, mais_curto);
        *cpu_task = mais_curto->pcb;
        free(mais_curto);
    }
}