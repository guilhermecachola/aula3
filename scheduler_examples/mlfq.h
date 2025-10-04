#ifndef MLFQ_H
#define MLFQ_H

#include "queue.h"

#define MLFQ_NUM_QUEUES 3
#define MLFQ_TIME_SLICE_MS 500

typedef struct {
    queue_t queues[MLFQ_NUM_QUEUES];
} mlfq_t;

void mlfq_init(mlfq_t *mlfq);
void mlfq_scheduler(uint32_t current_time_ms, mlfq_t *mlfq, pcb_t **cpu_task);
void mlfq_add_task(mlfq_t *mlfq, pcb_t *task);

#endif //MLFQ_H