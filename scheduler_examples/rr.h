#ifndef RR_H
#define RR_H

#include "queue.h"

#define RR_TIME_SLICE_MS 500

void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task);

#endif //RR_H