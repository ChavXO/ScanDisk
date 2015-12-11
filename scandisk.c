#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
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
#include "scandisk.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


/*******************************************************************\
 * Project 5: Scan disk                                            * 
 * Contributors: Michael Chavinda, Katrina Gross, Katelyn Puhala   *
 * Description: a consistency checker for the FAT12 file system    *
 *                                                                 *
\*******************************************************************/

cluster_node* head = NULL; // saves us a lot of state passing

/* 
 * main - written collectively. traverses file 
 * and collects orphans
 */
 
int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
        usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    traverse_root(image_buf, bpb);
    foster_orphans(image_buf, bpb);

    free_clusters();
    free(bpb);
    unmmap_file(image_buf, &fd);
    return 0;
}

/*
 * pre-implemented function to inform user of proper usage
 */
void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}

/////////////////////////////////////////////////////////////////////
//                                                                 //
//          Functions taken from dos files and modified            //
//                                                                 //
/////////////////////////////////////////////////////////////////////
/* 
 * traverse root - taken from dos files. modified collectively
 * recursively follows directories from the root checking each file
 * for consistency as it goes
 */
void traverse_root(uint8_t *image_buf, struct bpb33* bpb) {

    uint16_t cluster = 0;
    struct direntry* dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);

    char buffer [MAXFILENAME]; //buffer for storing file names

    int i;
    for (i = 0; i < bpb->bpbRootDirEnts; i++) {
        uint16_t followclust = get_dirent(dirent, buffer);
        // deal with normal files
        if (dirent->deAttributes == ATTR_NORMAL) { 
            chkerr(dirent, buffer, image_buf, bpb);
        }

        append_clusters(followclust); // append file cluster

        if (is_valid_cluster(followclust, bpb)) {
            append_clusters(followclust);
            follow_dir(followclust, 1, image_buf, bpb);
        }

        dirent++;
    }
}

/* 
 * follow dir recursively scans through the file hierarchy
 * we check for consistency for every directory entry
 * taken from dos with some modifications.
 */
void follow_dir(uint16_t cluster, int indent,
               uint8_t *image_buf, struct bpb33* bpb) {   
    while (is_valid_cluster(cluster, bpb)) {   
        append_clusters(cluster);
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        char buffer[MAXFILENAME];
        int i = 0;
        for ( ; i < numDirEntries; i++) {
            append_clusters(cluster);
            uint16_t followclust = get_dirent(dirent, buffer);
   
            chkerr(dirent, buffer, image_buf, bpb);
            if (followclust) {
                follow_dir(followclust, indent+1, image_buf, bpb);
            }
            dirent++;
        }
        cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}

/*
 * taken from dos files
 */
uint16_t get_dirent(struct direntry *dirent, char *buffer) {
    uint16_t followclust = 0;
    memset(buffer, 0, MAXFILENAME);

    int i;
    char name[9];
    char extension[4];
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY) return followclust;

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED) return followclust;

    // dot entry ("." or "..")
    // skip it
    if (((uint8_t)name[0]) == 0x2E) return followclust;

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) {
        if (name[i] == ' ') name[i] = '\0';
        else break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) {
        if (extension[i] == ' ') extension[i] = '\0';
        else break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN) {
        // ignore any long file name extension entries
        //
        // printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
        if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN) {
            strcpy(buffer, name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    } else {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
        strcpy(buffer, name);
        if (strlen(extension))  {
            strcat(buffer, ".");
            strcat(buffer, extension);
        }
    }

    return followclust;
}

/*
 * taken from dos files
 */
void write_dirent(struct direntry *dirent, char *filename, 
          uint16_t start_cluster, uint32_t size) {
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
        if (p2[i] == '/' || p2[i] == '\\') uppername = p2+i+1;
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) 
        uppername[i] = toupper(uppername[i]);

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
        fprintf(stderr, "No filename extension given - defaulting to .___\n");
    } else {
        *p = '\0';
        p++;
        len = strlen(p);
        if (len > 3) len = 3;
        memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) {
        uppername[8]='\0';
    }
    
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* could also set time and date here if we really
       cared... we care*/
    time_t t;
    time(&t);
    // how do we set date and time?
}

/* create_dirent finds a free slot in the directory, and write the
   directory entry */

void create_dirent(struct direntry *dirent, char *filename, 
                   uint16_t start_cluster, uint32_t size,
                   uint8_t *image_buf, struct bpb33* bpb) {
    while (1) {
        if (dirent->deName[0] == SLOT_EMPTY) {
            /* we found an empty slot at the end of the directory */
            write_dirent(dirent, filename, start_cluster, size);
        dirent++;

        /* make sure the next dirent is set to be empty, just in
           case it wasn't before */
        memset((uint8_t*)dirent, 0, sizeof(struct direntry));
        dirent->deName[0] = SLOT_EMPTY;
        return;
        }

        if (dirent->deName[0] == SLOT_DELETED) {
            /* we found a deleted entry - we can just overwrite it */
            write_dirent(dirent, filename, start_cluster, size);
            return;
        }
        dirent++;
    }
}

/////////////////////////////////////////////////////////////////////
//                                                                 //
//          Error checking and reparation functions                //
//                                                                 //
/////////////////////////////////////////////////////////////////////

/*
 * worker function that checks and repairs incosistency errors
 */
void chkerr(struct direntry* dirent, char* filename, 
                   uint8_t *image_buf, struct bpb33* bpb) {
    int num_clusters = count_clusters(dirent, image_buf, bpb);
    uint32_t entry_size = getulong(dirent->deFileSize);

    // remove empty files
    if (entry_size == 0) {
        if (dirent->deAttributes == ATTR_NORMAL && dirent->deName[0] != 
            SLOT_EMPTY && dirent->deName[0] != SLOT_DELETED) {
            printf("Empty file ... rmeoving. \n");
            dirent->deName[0] = SLOT_DELETED;
        }
    }
    
    // fix size inconsistencies
    if (num_clusters != 0 && entry_size < num_clusters - 512 ) { // take entry to be right
        printf("OUT OF BOUNDS: \n\tFilename: %s \n\t\tsize in directory entry: %d, size in FAT chain: %d.) \n", filename, entry_size, num_clusters);
        repair(dirent, image_buf, bpb, entry_size);
    } else if (entry_size > num_clusters) { // take FAT to be right
        printf("OUT OF BOUNDS: \n\tFilename: %s \n\t\tsize in directory entry: %d, size in FAT chain: %d \n", filename, entry_size, num_clusters);
        putulong(dirent->deFileSize, num_clusters);
    }
}

/*
 * repair an inconsistent file
 */
void repair(struct direntry *dirent, uint8_t *image_buf, struct bpb33 *bpb, int actual_size) {
    
    uint16_t cluster = getushort(dirent->deStartCluster);
    uint16_t cluster_size = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;
    uint16_t prev_cluster = cluster;

    uint16_t num_bytes = 0;
    
    // accumulate all the related clusters in the FAT
    while (num_bytes < actual_size) {
        num_bytes += cluster_size;
        prev_cluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);
    }
    
    if (num_bytes != 0) {
        set_fat_entry(prev_cluster, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
    }
    
    // update all other clusters attached and mark them as free
    while (!is_end_of_file(cluster)) {
        uint16_t old_cluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);
        set_fat_entry(old_cluster, FAT12_MASK & CLUST_FREE, image_buf, bpb);
    }
}

/////////////////////////////////////////////////////////////////////
//                                                                 //
//                   Operations on clusters                        //
//                                                                 //
/////////////////////////////////////////////////////////////////////
/* 
 * given a directory entry the function counts the number of
 * clusters referred to by the entry.
 */
int count_clusters(struct direntry *dirent, 
                   uint8_t *image_buf, struct bpb33 *bpb) {
    uint16_t cluster = getushort(dirent->deStartCluster);
    uint16_t cluster_size = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;
    int num_bytes = 0; uint16_t prev_cluster = cluster;
    append_clusters(cluster);
    if (is_end_of_file(cluster)) num_bytes = 512;
    while (!is_end_of_file(cluster) && cluster < usable_clusters) {   
        if (cluster == (FAT12_MASK & CLUST_BAD)) {
            printf("Bad cluster: cluster number %d \n", cluster);
            set_fat_entry(prev_cluster, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
            break;
        }
        if (cluster == (FAT12_MASK & CLUST_FREE)) {
            set_fat_entry(prev_cluster, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
            break;   
        }
        num_bytes += cluster_size;
        prev_cluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);
        if (prev_cluster == cluster) {
            printf("Self eferential cluster. \n");
            set_fat_entry(prev_cluster, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
            break;   
        }
        append_clusters(cluster);
    }

    return num_bytes;
}

/*
 * calculate the size of a cluster chain
 */
uint32_t size_of_cluster(uint16_t cluster, uint8_t *image_buf, 
                         struct bpb33 *bpb) {
    uint16_t cluster_size = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;
    uint32_t num_bytes = 0;
    append_clusters(cluster);
    while (!is_end_of_file(cluster)) {   
        if (cluster == (FAT12_MASK & CLUST_BAD)) {
            printf("Bad cluster: cluster number %d \n", cluster);
        }
        num_bytes += cluster_size;
        cluster = get_fat_entry(cluster, image_buf, bpb);
        append_clusters(cluster);
    }
    return num_bytes;
}

/////////////////////////////////////////////////////////////////////
//                                                                 //
//                         Orphan clean up                         //
//                                                                 //
/////////////////////////////////////////////////////////////////////
/*
 * function that runs through the cluster list and stores orphans
 * in persistent memory
 */
void foster_orphans(uint8_t *image_buf, struct bpb33* bpb) {
    int orphan_count = 0;
    uint16_t curr_cluster = start_cluster;

    for ( ; curr_cluster < usable_clusters; curr_cluster++) {
        if ((get_fat_entry(curr_cluster, image_buf, bpb) != CLUST_FREE) 
            && !in_cluster_list(curr_cluster)) {
                orphan_count = foster_single_orphan(orphan_count, 
                                    curr_cluster, image_buf, bpb);
        }
    }
}

/* 
 * helper function that fixes a single orphan and makes
 * a DAT file associated with the cluster
 */
int foster_single_orphan(int orphan_count, uint16_t curr_cluster, 
                         uint8_t *image_buf, struct bpb33* bpb) {
    orphan_count++;
    int cluster = 0;
    struct direntry* dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    char filename[13]; char str[3];
    
    // make file name
    memset(filename, '\0', 13); strcat(filename, "found"); memset(str, '\0', 3);
    sprintf(str, "%d", orphan_count);
    strcat(filename, str); strcat(filename, ".dat");

    int clusters_size = size_of_cluster(curr_cluster, image_buf, bpb);
    append_clusters(cluster);
    create_dirent(dirent, filename, curr_cluster, clusters_size, image_buf, bpb);
    return orphan_count;
}

/////////////////////////////////////////////////////////////////////
//                                                                 //
//             Functions to manage linked list state               //
//                                                                 //
/////////////////////////////////////////////////////////////////////
/*
 * linear scan to check if the cluster is already in the list
 */
int in_cluster_list(uint16_t cluster) {
    bool found = false;
    cluster_node* curr = head;
    while (curr != NULL) {
        if (cluster == curr->cluster) {
            found = true;
            break;
        }
        curr = curr->next;
    }
    return found;
}

/*
 * append cluster to end of linked list
 * might consider keeping track of the tail.
 */
void append_clusters(uint16_t cluster) {
    if (in_cluster_list(cluster)) {
        return;
    }

    cluster_node* new_node = malloc(sizeof(cluster_node));
    new_node->cluster = cluster;
    new_node->next = NULL;
    cluster_node* curr = head;

    if (curr == NULL) {
        head = new_node;
        return;
    }

    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = new_node;
    new_node->next = NULL;
}

/*
 * free list of clusters when done
 */
void free_clusters() {
    while (head != NULL) {
        cluster_node* tmp = head;
        head = head->next;
        free(tmp);
    }
}
