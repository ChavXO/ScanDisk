#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}

struct direntry *traverse_root(char *searchpath, uint8_t *image_buf, struct bpb33* bpb)
{
    uint16_t cluster = 0;
    struct direntry *rv = NULL;

    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    char *next_path_component = index(searchpath, '/');
    int root_entry_len = strlen(searchpath);
    if (next_path_component != NULL)
    {
        root_entry_len = next_path_component - searchpath;
        *next_path_component = '\0';
        next_path_component++;
    }

    char buffer[MAXFILENAME];

    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = get_dirent(dirent, buffer);

        if (strncasecmp(searchpath, buffer, strlen(searchpath)) == 0)
        {
            if (!next_path_component)
                rv = dirent;
            else if (is_valid_cluster(followclust, bpb))
                rv = follow_dir(next_path_component, followclust, image_buf, bpb);
        }

        if (rv)
            break;

        dirent++;
    }

    return rv;
}

int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
        usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);

    // your code should start here...
    int i;
    while (





    unmmap_file(image_buf, &fd);
    return 0;
}
