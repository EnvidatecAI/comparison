/**
 * @file vmaccess.c
 * @author Prof. Dr. Wolfgang Fohl, HAW Hamburg
 * @date 2010
 * @brief The access functions to virtual memory.
 */

#include "vmem.h"
#include "debug.h"


/*
 * static variables
 */
static sem_t *local_sem; //lokale semaphore
static struct vmem_struct *vmem = NULL; //!< Reference to virtual memory

/**
 *****************************************************************************************
 *  @brief      This function setup the connection to virtual memory.
 *              The virtual memory has to be created by mmanage.c module.
 *
 *  @return     void
 ****************************************************************************************/
static void vmem_init(void) {
	// initialisiere shared memory
	key_t key = ftok(SHMKEY, SHMPROCID); // key zum shm generieren
	if (key == -1) {
		  perror("ftok");
		  exit(1);
	}
	int sharedMemoryId = shmget(key, sizeof(struct vmem_struct), IPC_CREAT | 0666);
	if (sharedMemoryId == -1) {
		  perror("vmaccess.c kann shared memory nicht erstellen");
		  exit(1);
	}
	vmem = (struct vmem_struct *) shmat( sharedMemoryId, 0, 0);

	local_sem=sem_open(NAMED_SEM, 0, 0777, 0);
}

/**
 *****************************************************************************************
 *  @brief      This function does aging for aging page replacement algorithm.
 *              It will be called periodic based on g_count.
 *              This function must be used only when aging page replacement algorithm is activ.
 *              Otherwise update_age_reset_ref may interfere with other page replacement 
 *              alogrithms that base on PTF_REF bit.
 *
 *  @return     void
 ****************************************************************************************/
static void update_age_reset_ref(void) {
	int i;
	int pageInFrame;
	int rBit;
	if ((vmem->adm.g_count % UPDATE_AGE_COUNT) == 0) {
		for (i=0; i<VMEM_NFRAMES; i++) {
			pageInFrame = vmem->pt.framepage[i];
			if (pageInFrame != VOID_IDX) {
				vmem->pt.entries[pageInFrame].age /= 2;	// right shift
				rBit = vmem->pt.entries[pageInFrame].flags & PTF_REF;
				if (rBit == PTF_REF) {
					vmem->pt.entries[pageInFrame].age |= AGE_DEFAULT;
					vmem->pt.entries[pageInFrame].flags &= ~PTF_REF;
				}
			}
		}
	}
}

/**
 *****************************************************************************************
 *  @brief      This function puts a page into memory (if required).
 *              It must be called by vmem_read and vmem_write
 *
 *  @param      pageNr The page that will be put in (if required).
 * 
 *  @return     The int value read from virtual memory.
 ****************************************************************************************/
static void vmem_put_page_into_mem(int pageNr) {
	vmem->adm.req_pageno = pageNr;
	int framenummer =vmem->pt.entries[pageNr].frame;

	// lade Seite wenn sie fehlt
	if(VOID_IDX == framenummer){
		kill(vmem->adm.mmanage_pid,SIGUSR1);
		sem_wait(local_sem);
	}
	// aktualisiere flags
	vmem->pt.entries[pageNr].flags |= PTF_PRESENT;	// die Seite ist geladen
	vmem->pt.entries[pageNr].flags |= PTF_REF;	// auf die Seite wurde zugegriffen
	
	vmem->adm.g_count++;	// Speicherzugriff Zaehler
}

///////////////////////////////////////////////////////////////////////////////
int vmem_read(int address) {
	if(vmem==NULL){
		vmem_init();
	}

	int offset = address & (VMEM_PAGESIZE - 1);
	int seitenummer= (address - offset) / VMEM_PAGESIZE;

	vmem_put_page_into_mem(seitenummer);
	
	int framenummer = vmem->pt.entries[seitenummer].frame;
	int datas = vmem->data[(framenummer * VMEM_PAGESIZE) | offset];
	
	if(vmem->adm.page_rep_algo==VMEM_ALGO_AGING){
		update_age_reset_ref();
	}
	return datas;
}

///////////////////////////////////////////////////////////////////////////////
void vmem_write(int address, int data) {
	if(vmem==NULL){
		vmem_init();
	}

	int offset = address & (VMEM_PAGESIZE - 1);
	int seitenummer= (address - offset) / VMEM_PAGESIZE;

	vmem_put_page_into_mem(seitenummer);
	vmem->pt.entries[seitenummer].flags |= PTF_DIRTY;	// Seite wurde geaendert
	
	int framenummer = vmem->pt.entries[seitenummer].frame;
	vmem->data[(framenummer * VMEM_PAGESIZE) | offset] = data;
	
	if(vmem->adm.page_rep_algo==VMEM_ALGO_AGING){
		update_age_reset_ref();
	}
}

// EOF
