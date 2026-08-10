#include "obb_index.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char error[256];
    const char **children = NULL;
    size_t count;
    size_t index;
    int saw_published = 0;
    int saw_1x = 0;

    if (argc != 2 || nfsmw_obb_open(argv[1], error, sizeof(error)) != 0) {
        (void)fprintf(stderr, "FAIL: %s\n", argc == 2 ? error : "usage");
        return 1;
    }
    if (nfsmw_obb_asset_size("published/CC_SeedData.bin") != 638946 ||
        nfsmw_obb_asset_size("published.1x/texturepacks_ui/menu.sba") !=
            29961332 ||
        nfsmw_obb_asset_size("published") != -1 ||
        nfsmw_obb_asset_size("not-present") != -2) {
        (void)fprintf(stderr, "FAIL: OBB size/directory contract\n");
        return 1;
    }
    count = nfsmw_obb_list("", &children);
    for (index = 0U; index < count; ++index) {
        const char *slash = strchr(children[index], '/');
        size_t length = slash != NULL ?
            (size_t)(slash - children[index]) : strlen(children[index]);
        saw_published |= length == strlen("published") &&
                         memcmp(children[index], "published", length) == 0;
        saw_1x |= length == strlen("published.1x") &&
                  memcmp(children[index], "published.1x", length) == 0;
    }
    nfsmw_obb_close();
    if (count != 4U || saw_published == 0 || saw_1x == 0) {
        (void)fprintf(stderr, "FAIL: root listing count=%zu\n", count);
        return 1;
    }
    (void)printf("PASS: OBB archive index, size and listing contract\n");
    return 0;
}
