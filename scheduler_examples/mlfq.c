#include "mlfq.h"
#include <stdio.h>
#include <stdlib.h>
#include "msg.h"
#include <unistd.h>


void mlfq_init(mlfq_t *mlfq) {
    // Inicializa as 3 filas vazias
    for (int i = 0; i < MLFQ_NUM_QUEUES; i++) {
        mlfq->queues[i].head = NULL;
        mlfq->queues[i].tail = NULL;
    }
}

void mlfq_add_task(mlfq_t *mlfq, pcb_t *task) {
    // REGRA 3: Novos processos começam na fila 0 (prioridade máxima)
    if (task->status == TASK_RUNNING) {
        enqueue_pcb(&mlfq->queues[0], task);
    }
}

void mlfq_scheduler(uint32_t current_time_ms, mlfq_t *mlfq, pcb_t **cpu_task) {
    // Variável estática para lembrar de qual fila veio o processo atual
    static int fila_atual = 0;

    // ===== PARTE 1: Se há processo no CPU =====
    if (*cpu_task) {
        // Atualiza o tempo executado
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Calcula tempo desde início do time slice
        uint32_t tempo_slice = current_time_ms - (*cpu_task)->slice_start_ms;

        // Verifica se TERMINOU
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Envia DONE
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
            fila_atual = 0;  // Reset para começar a procurar de Q0
        }
        // Verifica se TIME SLICE expirou
        else if (tempo_slice >= MLFQ_TIME_SLICE_MS) {
            // REGRA 4: Usou time slice completo → desce uma fila
            int nova_fila = fila_atual;

            // Se não está na fila mais baixa, desce
            if (fila_atual < MLFQ_NUM_QUEUES - 1) {
                nova_fila = fila_atual + 1;
            }

            // Coloca o processo na nova fila (pode ser a mesma se já está em Q2)
            (*cpu_task)->status = TASK_RUNNING;
            enqueue_pcb(&mlfq->queues[nova_fila], *cpu_task);

            // Liberta o CPU
            (*cpu_task) = NULL;
            fila_atual = 0;  // Reset: próxima busca começa em Q0
        }
        // Se não terminou nem expirou: continua no CPU
    }

    // ===== PARTE 2: Se CPU livre, escolher próximo processo =====
    if (*cpu_task == NULL) {
        // REGRA 1 e 2: Procura da fila de MAIOR prioridade para MENOR
        for (int i = 0; i < MLFQ_NUM_QUEUES; i++) {
            if (mlfq->queues[i].head != NULL) {
                // Encontrou um processo na fila i
                *cpu_task = dequeue_pcb(&mlfq->queues[i]);

                if (*cpu_task) {
                    // Marca início do novo time slice
                    (*cpu_task)->slice_start_ms = current_time_ms;
                    // Guarda de qual fila veio
                    fila_atual = i;
                    break;  // Sai do loop, encontrou processo
                }
            }
        }
    }
}