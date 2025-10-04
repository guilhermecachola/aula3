#include "sjf.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "msg.h"
#include <unistd.h>


void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {

    // ===== PARTE 1: Verificar se há processo no CPU =====
    if (*cpu_task) {
        // Incrementa o tempo decorrido do processo
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Verifica se o processo terminou
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Envia mensagem DONE ao processo
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }

            // Liberta a memória e deixa o CPU livre
            free((*cpu_task));
            (*cpu_task) = NULL;
        }
    }

    // ===== PARTE 2: Se CPU livre, escolher processo mais curto =====
    if (*cpu_task == NULL && rq->head != NULL) {

        // Procurar o processo com menor time_ms
        queue_elem_t *atual = rq->head;
        queue_elem_t *mais_curto = rq->head;

        while (atual != NULL) {
            // Se este processo é mais curto que o atual mais_curto
            if (atual->pcb->time_ms < mais_curto->pcb->time_ms) {
                mais_curto = atual;
            }
            atual = atual->next;
        }

        // Remove o processo mais curto da fila
        remove_queue_elem(rq, mais_curto);
        *cpu_task = mais_curto->pcb;
        free(mais_curto);  // Liberta o nó da lista (não o PCB)
    }
}