#include "rr.h"
#include <stdio.h>
#include <stdlib.h>
#include "msg.h"
#include <unistd.h>


void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {

    // ===== PARTE 1: Se há processo no CPU, atualizar o seu tempo =====
    if (*cpu_task) {
        // Incrementa o tempo que o processo já executou
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Calcula há quanto tempo este processo está no CPU
        uint32_t tempo_no_cpu = current_time_ms - (*cpu_task)->slice_start_ms;

        // Verifica se o processo TERMINOU completamente
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Envia mensagem DONE
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }

            // Remove o processo do sistema
            free((*cpu_task));
            (*cpu_task) = NULL;
        }
        // Verifica se o TIME SLICE expirou (PREEMPÇÃO)
        else if (tempo_no_cpu >= RR_TIME_SLICE_MS) {
            // Time slice expirou! Fazer preempção
            // Coloca o processo no fim da fila Ready
            (*cpu_task)->status = TASK_RUNNING;
            enqueue_pcb(rq, *cpu_task);

            // Liberta o CPU
            (*cpu_task) = NULL;
        }
        // Se não terminou E não expirou o time slice:
        // continua a executar (não faz nada aqui)
    }

    // ===== PARTE 2: Se CPU está livre, pegar no próximo processo =====
    if (*cpu_task == NULL) {
        // Pega no PRIMEIRO processo da fila (FIFO dentro do RR)
        *cpu_task = dequeue_pcb(rq);

        if (*cpu_task) {
            // Marca o início deste time slice
            (*cpu_task)->slice_start_ms = current_time_ms;
        }
    }
}