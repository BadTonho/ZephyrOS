#ifndef KERNEL_HOST_TEST_H
#define KERNEL_HOST_TEST_H

int kernel_host_test_run_finite_routes(void);
int kernel_host_test_should_wake_shell_for_event(int shell_job_active,
                                                 int app_loader_active);

#endif
