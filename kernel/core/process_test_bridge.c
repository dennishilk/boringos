#include <boring/preemption_test.h>
#include <boring/process_test.h>

void run_preemptive_and_process_test(void);

void run_preemptive_and_process_test(void) {
    run_preemptive_task_test();
    run_process_address_space_test();
}
