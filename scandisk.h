#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"
#include <stdbool.h>
#include <time.h>

// http://stackoverflow.com/questions/1674032/static-const-vs-define-vs-enum-in-c
enum { usable_clusters = 2849, 
       start_cluster = FAT12_MASK & CLUST_FIRST, 
       eof_cluster = FAT12_MASK & CLUST_EOFS,
       free_cluster = FAT12_MASK & CLUST_FREE,
       bad_cluster = FAT12_MASK & CLUST_BAD}; // define constants for clusters

typedef struct _cluster_node {
    uint16_t cluster;
    struct _cluster_node* next;
} cluster_node;

void usage(char *progname);

uint16_t get_dirent(struct direntry *dirent, char *buffer);
void write_dirent(struct direntry *dirent, char *filename, 
          uint16_t start_cluster, uint32_t size);
void create_dirent(struct direntry *dirent, char *filename, 
           uint16_t start_cluster, uint32_t size,
           uint8_t *image_buf, struct bpb33* bpb);

void traverse_root(uint8_t *image_buf, struct bpb33* bpb);
void follow_dir(uint16_t cluster, int indent,
               uint8_t *image_buf, struct bpb33* bpb);

void repair(struct direntry *dirent, uint8_t *image_buf, struct bpb33 *bpb, int actual_size);
void chkerr(struct direntry* dirent, char* filename, 
                   uint8_t *image_buf, struct bpb33* bpb);

int count_clusters(struct direntry *dirent, 
                   uint8_t *image_buf, struct bpb33 *bpb);
uint32_t calculate_size(uint16_t cluster, uint8_t *image_buf, struct bpb33 *bpb);

void foster_orphans(uint8_t *image_buf, struct bpb33* bpb);
int foster_single_orphan(int orphan_count, uint16_t curr_cluster,uint8_t *image_buf, struct bpb33* bpb);


void append_clusters(uint16_t cluster);
int in_cluster_list(uint16_t cluster);
int in_cluster_list(uint16_t cluster);
void free_clusters(void);

void create_dirent(struct direntry *dirent, char *filename, 
           uint16_t start_cluster, uint32_t size,
           uint8_t *image_buf, struct bpb33* bpb);
void write_dirent(struct direntry *dirent, char *filename, 
          uint16_t start_cluster, uint32_t size);
uint16_t get_dirent(struct direntry *dirent, char *buffer);
