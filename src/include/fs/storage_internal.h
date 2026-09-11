#ifndef STORAGE_INTERNAL_H
#define STORAGE_INTERNAL_H

#include "types.h"

typedef struct {
    uint32_t structures_verified;
    uint32_t files_verified;
    uint32_t directories_verified;
    uint32_t referenced_clusters;
    uint32_t free_clusters;
    uint32_t errors;
    uint32_t warnings;
    int last_error;
} storage_check_report_t;

int storage_check_get_last_report(storage_check_report_t* out_report);

#endif
