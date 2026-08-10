#ifndef NFSMW_OBB_INDEX_H
#define NFSMW_OBB_INDEX_H

#include <stddef.h>
#include <stdint.h>

int nfsmw_obb_open(const char *path, char *error, size_t error_size);
void nfsmw_obb_close(void);
int64_t nfsmw_obb_asset_size(const char *path);
size_t nfsmw_obb_list(const char *directory, const char ***children);
const char *nfsmw_obb_path(void);

#endif
