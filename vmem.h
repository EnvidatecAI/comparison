/**
 * @file vmem.h
 * @author Wolfgang Fohl, HAW Hamburg 
 * @brief Global Definitions for TI BS A3 - model of virtual memory management

 * First Version : Wolfgang Fohl HAW Hamburg
 * Dec 2015 : Delete BitMap for free frames (Franz Korf, HAW Hamburg)
 * Dec 2015 : Set memory algorithm vi command line parameter 
 * Dec 2015 : Set define for PAGESIZE and VMEM_ALGO via compiler -D option (Franz Korf, HAW Hamburg)
 * Dec 2015 : Add some documentation (Franz Korf, HAW Hamburg)
 */

#ifndef VMEM_H
#define VMEM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "mytypes.h"

#define SHMKEY          "./vmem.h" //!< First paremater for shared memory generation via ftok function
#define SHMPROCID       1234       //!< Second paremater for shared memory generation via ftok function

#define NAMED_SEM       "sem_vm_simulation_OS_X" //!< For OS-X semaphore

/**
 * Constants for page replacement algorithms
 */

#define VMEM_ALGO_FIFO  0
#define VMEM_ALGO_AGING 1
#define VMEM_ALGO_CLOCK 2

// Following defines will be sets via compiler parameter / Makefile
// VMEM_PAGESIZE :                    values 8 16 32 64
// default values

/**
 * Constant VMEM_PAGESIZE will be sete via compiler -D option. 
 * Default value : 8
 * value range : 8 16 32 64 
 */
#ifndef VMEM_PAGESIZE
#define VMEM_PAGESIZE 8
#endif

/* Sizes */
#define VMEM_VIRTMEMSIZE 1024   //!< Size of virtual address space of the process
#define VMEM_PHYSMEMSIZE  128   //!< Size of physical memory
#define VMEM_NPAGES     (VMEM_VIRTMEMSIZE / VMEM_PAGESIZE)  //!< Total number of pages 
#define VMEM_NFRAMES (VMEM_PHYSMEMSIZE / VMEM_PAGESIZE)     //!< Total number of (page) frames 

/**
 * page table flags used by this simulation
 */
#define PTF_PRESENT     1
#define PTF_DIRTY       2 //!< store: need to write 
#define PTF_REF         4       

#define VOID_IDX -1       //!< Constant for invalid page or frame reference 

/**
 * Page table entry
 */
struct pt_entry {
   int flags;             //!< See definition of PTF_* flags 
   int frame;             //!< Frame idx; frame == VOID_IDX: unvalid reference  
   int count;             //!< Global counter as quasi-timestamp for LRU page replacement algorithm
   unsigned char age;     //!< 8 bit counter for aging page replacement algorithm
};

/**
 * Structure of all administration data stored in shared memory
 */
struct vmem_adm_struct {
    int size;                    //!< size of virtual memory supported by mmanage
    pid_t mmanage_pid;           //!< process id if mmanage - will be used for sending signals to mmanage
    int shm_id;                  //!< shared memory id. Will be used to destroy shared memory when mmanage terminates
    int req_pageno;              //!< number of requested page 
    int next_alloc_idx;          //!< next frame to allocate by FIFO and CLOCK page replacement algorithm
    int pf_count;                //!< page fault counter 
    int g_count;                 //!< global acces counter as quasi-timestamp - will be increment by each memory access
    unsigned char page_rep_algo; // !< page replacement algorithm
    char *program_name;          //!< program name
};

/**
 * This structure contains
   - page table 
   - mapping form frame idx to page idx (for each frame the page stored in this frame currently
 */
struct pt_struct {
    /* page table */
    struct pt_entry entries[VMEM_NPAGES]; //!< page table 
    int framepage[VMEM_NFRAMES];          //!< Gives for each fame the page stored in this frame.  VOID_IDX indicates an unused frame.A
};

/* This is to be located in shared memory */
/**
 * The data structure stored in shared memory
 */
struct vmem_struct {
    struct vmem_adm_struct adm;              //!< admin data
    struct pt_struct pt;                     //!< page table 
    int data[VMEM_NFRAMES * VMEM_PAGESIZE];  //!< main memory used by virtual memory simulation 
};

#define SHMSIZE (sizeof(struct vmem_struct)) //!< size of virtual memory 

// for aging algo

/**
 * UPDATE_AGE_COUNT will be used by aging page replacement algorithm. 
 * When (g_count % UPDATE_AGE_COUNT) == 0 : UPDATE_AGE_COUNT quasi time units has passed
 * and aging algorithm will be executed. 
 */
#define UPDATE_AGE_COUNT   20

#endif /* VMEM_H */
