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

//array global variable

int* refs;


//check size first, if chain is 2 blocks long check size variable then size of dir entry should be between 513 - 1024, look at print dirent to extract size, if wrong -> make size max size, add ability to follow cluster chain


void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}

/* get_name retrieves the filename from a directory entry */
/* taken from dos_cp - modified for validity checking */
int get_name(char *fullname, struct direntry *dirent) {
    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN) return -1;
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    if (((u_int8_t) name[0]) == SLOT_DELETED) return -1;
    
    
    memcpy(extension, dirent->deExtension, 3);
    
    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) {
        if (name[i] == ' ') 
            name[i] = '\0';
        else 
            break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) {
        if (extension[i] == ' ') 
            extension[i] = '\0';
        else 
            break;
    }
    
    fullname[0]='\0';
    strcat(fullname, name);

    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) {
        if ((dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN) {
            return -1;
        }
        strcat(fullname, ".");
        strcat(fullname, extension);
    }
    
    return 0;
}

//TO BE MODIFIED - taken from dos.c
int is_valid_cluster(uint16_t cluster, struct bpb33 *bpb)
{
    uint16_t max_cluster = (bpb->bpbSectors / bpb->bpbSecPerClust) & FAT12_MASK; // edit max xluster?

    if (cluster >= (FAT12_MASK & CLUST_FIRST) && 
        cluster <= (FAT12_MASK & CLUST_LAST) &&
        cluster < max_cluster)
        return TRUE;
    return FALSE;
}

//taken from dos_cp
/* write the values into a directory entry */
void write_dirent(struct direntry *dirent, char *filename, 
		  uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) 
    {
	if (p2[i] == '/' || p2[i] == '\\') 
	{
	    uppername = p2+i+1;
	}
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
        uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
        fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else {
        *p = '\0';
        p++;
        len = strlen(p);
        if (len > 3) len = 3;
            memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* could also set time and date here if we really
       cared... */
}

void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb)
{
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

	if (dirent->deName[0] == SLOT_DELETED) 
	{
	    /* we found a deleted entry - we can just overwrite it */
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}

//taken from dos_ls
void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}

uint16_t print_dirent(struct direntry *dirent, int indent, struct bpb33* bpb, uint8_t * image_buf)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	    print_indent(indent);
    	    printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
	int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
	int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
	int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
	int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;

	size = getulong(dirent->deFileSize);
	print_indent(indent);
	printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');
               
        int chain = 0;
        uint16_t cluster = getushort(dirent->deStartCluster);
        while(is_valid_cluster(cluster, bpb)){
              refs[cluster]++;
              uint16_t previous = cluster;
              cluster = get_fat_entry(cluster, image_buf, bpb);
              if (previous == cluster){ //check pointing to itself
                  printf("pointing to self\n");
                  set_fat_entry(cluster, FAT12_MASK& CLUST_EOFS, image_buf, bpb);
                  chain ++;
                  break;
              }
              if(cluster == (FAT12_MASK & CLUST_BAD)){ //check bad cluster
                  printf("Bad Cluster\n");
                  set_fat_entry(cluster, FAT12_MASK & CLUST_FREE, image_buf, bpb);
                  set_fat_entry(previous, FAT12_MASK & CLUST_EOFS, image_buf, bpb);
                  chain++;
                  break;
              }
            chain++;
        
    }
    
          //check size for consistency  
              
              int max = chain * 512;
              int min = max - 512;
              
              if(size > max || size <= min)
              {
                printf("OUT OF BOUNDS: Size in directory entry is inconsistent");
                putulong(dirent->deFileSize,max);
              }
   }
    return followclust;
}

//taken from dos_ls
void follow_dir(uint16_t cluster, int indent,
		uint8_t *image_buf, struct bpb33* bpb)
{
    while (is_valid_cluster(cluster, bpb)) {
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++)
	{
            
            uint16_t followclust = print_dirent(dirent, indent,bpb,image_buf);
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb);
            dirent++;
	}

	cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}

bool is_valid_size(struct direntry* dirent, uint8_t image_buf, 
                   struct bpb33* bpb, int cluster) {
    int start = getulong(dirent->deStartCluster);
    int size = getulong(dirent->deFileSize);;
    int end = 0;
    end = start + size / 512;
    
    char name[15]; //realistic upper bound is 13 though
    
    // use name to see if has been deleted
    if(get_name(name, dirent) == -1) {
        // error message
    	return false;
    }
    
    // pass in cluster
    // int cluster = 0; // is this necessary or correct?
    
    // check if start cluster is valid
    if (!is_valid_cluster(cluster, bpb)) {
        // mark as deleted
        // error message
        dirent->deName[0] = SLOT_DELETED;
        return false;
    }
    
    // check other instances of cluster
    return true;
}

//taken from dos_ls
void traverse_root(uint8_t *image_buf, struct bpb33* bpb) {
    uint16_t cluster = 0;

    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++) {
        uint16_t followclust = print_dirent(dirent, 0,bpb,image_buf);
        
        // check size
        if (is_valid_cluster(followclust, bpb)) {   //update reference count
            refs[followclust]++;
            //no index of refs can contain a number greater than 1
            if(refs[followclust] > 1) {
            //deleting duplicate entry in directoty
               dirent->deName[0] = SLOT_DELETED;
               refs[followclust]--;
               printf("scan error: multiple references.");
            }
            follow_dir(followclust, 1, image_buf, bpb);
        }

        dirent++;
    }
}


/* create_dirent finds a free slot in the directory, and write the
   directory entry */

int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
        usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    
    refs = calloc(bpb->bpbSectors, sizeof(int));

    traverse_root(image_buf, bpb);
    unmmap_file(image_buf, &fd);
    return 0;
}
