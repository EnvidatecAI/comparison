/**
 * @file mmanage.c
 * @author Prof. Dr. Wolfgang Fohl, HAW Hamburg
 * @date  2014

 * @brief Memory Manager module of TI BSP A3 virtual memory
 * 
 * This is the memory manager process that
 * works together with the vmaccess process to
 * manage virtual memory management.
 *
 * The memory manager process will be invoked
 * via a SIGUSR1 signal. It maintains the page table
 * and provides the data pages in shared memory.
 *
 * This process starts shared memory, so
 * it has to be started prior to the vmaccess process.
 *
 */

#include "mmanage.h"
#include "debug.h"
#include "pagefile.h"
#include "logger.h"
#include "vmem.h"

/*
 * Signatures of private / static functions
 */

/**
 *****************************************************************************************
 *  @brief      This function fetchs a page out of the pagefile.
 *
 * It is mainly a wrapper of the corresponding function of module pagefile.c
 *
 *  @param      pt_idx Index of the page that should be fetched.
 * 
 *  @return     void 
 ****************************************************************************************/
static void fetch_page(int pt_idx);

/**
 *****************************************************************************************
 *  @brief      This function writes a page into the pagefile.
 *
 * It is mainly a wrapper of the corresponding function of module pagefile.c
 *
 *  @param      pt_idx Index of the page that should be written into the pagefile.
 * 
 *  @return     void 
 ****************************************************************************************/
static void store_page(int pt_idx);

/**
 *****************************************************************************************
 *  @brief      This function initializes the virtual memory.
 *
 *  In particular it creates the shared memory. The application just attachs to the
 *  shared memory.
 *
 *  @return     void 
 ****************************************************************************************/
static void vmem_init(void);

/**
 *****************************************************************************************
 *  @brief      This function finds an unused frame.
 *
 *  The framepage array of pagetable marks unused frames with VOID_IDX. 
 *  Based on this information find_free_frame searchs in vmem->pt.framepage for the 
 *  free frame with the smallest frame number.
 *
 *  @return     idx of the unused frame with the smallest idx. 
 *              If all frames are in use, VOID_IDX will be returned.
 ****************************************************************************************/
static int find_free_frame();

/**
 *****************************************************************************************
 *  @brief      This function update the page table for page vmem->adm.req_pageno.
 *              It will be stored in frame.
 *
 *  @param      frame The frame that stores the now allocated page vmem->adm.req_pageno.
 *
 *  @return     void 
 ****************************************************************************************/
static void update_pt(int frame);

/**
 *****************************************************************************************
 *  @brief      This function allocates a new page into memory. If all frames are in 
 *              use the corresponding page replacement algorithm will be called.
 *
 *  allocate_page gets the requested page via vmem->adm.req_pageno. Please take into
 *  account that allocate_page must update the page table and log the page fault 
 *  as well.
 *  allocate_page does all actions that must be down when the SIGUSR1 signal 
 *  indicates a page fault.
 *
 *  @return     void 
 ****************************************************************************************/
static void allocate_page(void);

/**
 *****************************************************************************************
 *  @brief      This function is the signal handler attached to system call sigaction
 *              for signal SIGUSR1, SIGUSR2 aund SIGINT.
 *
 * These three signals have the same signal handler. Based on the parameter signo the 
 * corresponding action will be started.
 *
 *  @param      signo Current signal that has be be handled.
 * 
 *  @return     void 
 ****************************************************************************************/
static void sighandler(int signo);

/**
 *****************************************************************************************
 *  @brief      This function dumps the page table to stderr.
 *
 *  @return     void 
 ****************************************************************************************/
static void dump_pt(void);

/**
 *****************************************************************************************
 *  @brief      This function implements page replacement algorithm aging.
 *
 *  @return     idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_aging(void);

/**
 *****************************************************************************************
 *  @brief      This function implements page replacement algorithm fifo.
 *
 *  @return     idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_fifo(void);

/**
 *****************************************************************************************
 *  @brief      This function implements page replacement algorithm clock.
 *
 *  @return     idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_clock(void);

/**
 *****************************************************************************************
 *  @brief      This function selects and starts a page replacement algorithm.
 *
 *  It is just a wrapper for the three page replacement algorithms.
 *
 *  @return     The idx of the page that should be replaced.
 ****************************************************************************************/
static int find_remove_frame(void);

/**
 *****************************************************************************************
 *  @brief      This function cleans up when mmange runs out.
 *
 *  @return     void 
 ****************************************************************************************/
static void cleanup(void);

/**
 *****************************************************************************************
 *  @brief      This function scans all parameters of the porgram.
 *              The corresponding global variables vmem->adm.page_rep_algo will be set.
 * 
 *  @param      argc number of parameter 
 *
 *  @param      argv parameter list 
 *
 *  @return     void 
 ****************************************************************************************/
static void scan_params(int argc, char **argv);

/**
 *****************************************************************************************
 *  @brief      This function prints an error message and the usage information of 
 *              this program.
 *
 *  @param      err_str pointer to the error string that should be printed.
 *
 *  @return     void 
 ****************************************************************************************/
static void print_usage_info_and_exit(char *err_str);

/*
 * variables
 */

static struct vmem_struct *vmem = NULL; //!< Reference to shared memory
static int signal_number = 0;           //!< Number of signal received last
static sem_t *local_sem; //!< OS-X Named semaphores will be stored locally due to pointer
static int oldestFrame = -1;
struct logevent event;
int clockpointer=0;
int sharedMemoryId;

int main(int argc, char **argv) {
	struct sigaction sigact;

	init_pagefile(); // init page file
	open_logger();   // open logfile

	/* Create shared memory and init vmem structure */
	vmem_init();
	TEST_AND_EXIT_ERRNO(!vmem, "Error initialising vmem");
	PRINT_DEBUG((stderr, "vmem successfully created\n"));

	// scan parameter
	vmem->adm.program_name = argv[0];
	vmem->adm.page_rep_algo = VMEM_ALGO_AGING;
	scan_params(argc, argv);

	/* Setup signal handler */
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;

	TEST_AND_EXIT_ERRNO(sigaction(SIGUSR1, &sigact, NULL) == -1,
			"Error installing signal handler for USR1");
	PRINT_DEBUG((stderr, "USR1 handler successfully installed\n"));

	TEST_AND_EXIT_ERRNO(sigaction(SIGUSR2, &sigact, NULL) == -1,
			"Error installing signal handler for USR2");
	PRINT_DEBUG((stderr, "USR2 handler successfully installed\n"));

	TEST_AND_EXIT_ERRNO(sigaction(SIGINT, &sigact, NULL) == -1,
			"Error installing signal handler for INT");
	PRINT_DEBUG((stderr, "INT handler successfully installed\n"));

	/* Signal processing loop */
	while (1) {
		signal_number = 0;
		pause();
		if (signal_number == SIGUSR1) { /* Page fault */
			PRINT_DEBUG((stderr, "Processed SIGUSR1\n"));
			signal_number = 0;
		} else if (signal_number == SIGUSR2) { /* PT dump */
			PRINT_DEBUG((stderr, "Processed SIGUSR2\n"));
			signal_number = 0;
		} else if (signal_number == SIGINT) {
			PRINT_DEBUG((stderr, "Processed SIGINT\n"));
		}
	}

	return 0;
}

void scan_params(int argc, char **argv) {
	int i = 0;
	unsigned char param_ok = FALSE;

	// scan all parameters (argv[0] points to program name)
	if (argc > 2)
		print_usage_info_and_exit("Wrong number of parameters.\n");

	for (i = 1; i < argc; i++) {
		param_ok = FALSE;
		if (0 == strcasecmp("-fifo", argv[i])) {
			// page replacement strategies fifo selected
			vmem->adm.page_rep_algo = VMEM_ALGO_FIFO;
			param_ok = TRUE;
		}
		if (0 == strcasecmp("-clock", argv[i])) {
			// page replacement strategies clock selected
			vmem->adm.page_rep_algo = VMEM_ALGO_CLOCK;
			param_ok = TRUE;
		}
		if (0 == strcasecmp("-aging", argv[i])) {
			// page replacement strategies aging selected
			vmem->adm.page_rep_algo = VMEM_ALGO_AGING;
			param_ok = TRUE;
		}
		if (!param_ok)
			print_usage_info_and_exit("Undefined parameter.\n"); // undefined parameter found
	} // for loop
}

void print_usage_info_and_exit(char *err_str) {
	fprintf(stderr, "Wrong parameter: %s\n", err_str);
	fprintf(stderr, "Usage : %s [OPTIONS]\n", vmem->adm.program_name);
	fprintf(stderr, " -fifo     : Fifo page replacement algorithm.\n");
	fprintf(stderr, " -clock    : Clock page replacement algorithm.\n");
	fprintf(stderr, " -aging    : Aging page replacement algorithm.\n");
	fprintf(stderr, " -pagesize=[8,16,32,64] : Page size.\n");
	fflush(stderr);
	exit(EXIT_FAILURE);
}

void sighandler(int signo) {
	signal_number = signo;
	if (signo == SIGUSR1) {
		allocate_page();
		sem_post(local_sem);
	} else if (signo == SIGUSR2) {
		dump_pt();
	} else if (signo == SIGINT) {
		cleanup();
		exit(EXIT_SUCCESS);
	}
}

/* Your code goes here... */
///////////////////////////////////////////////////////////////////////////////
void vmem_init(void) {
	int i;
	// initialisiere Shared Memory
	int sharedMemoryId = shmget(ftok(SHMKEY, SHMPROCID), sizeof(struct vmem_struct), IPC_CREAT | 0666);
	if (sharedMemoryId < 0) {
		perror("mmanage.c kann shared memory nicht erstellen");
		exit(1);
	}
	vmem = (struct vmem_struct *) shmat(sharedMemoryId, 0, 0);
	if (vmem < 0) {
		perror("mmanage.c kann shared memory nicht erstellen");
		exit(1);
	}
	
	// initialisiere die Admin Struktur
	vmem->adm.size = VMEM_VIRTMEMSIZE;
	vmem->adm.mmanage_pid = getpid();
	vmem->adm.shm_id = sharedMemoryId;
	vmem->adm.req_pageno = 0;
	vmem->adm.next_alloc_idx = 0;
	vmem->adm.pf_count = 0;
	vmem->adm.g_count = 0;
	
	// initialisiere Page-Frame-Tabelle
	for (i = 0; i < VMEM_NPAGES; i++) {
		vmem->pt.entries[i].flags = 0;
		vmem->pt.entries[i].frame = VOID_IDX;		
		vmem->pt.entries[i].count = 0;
		vmem->pt.entries[i].age = 0;
	}

	// initialisiere Frames
	for (i = 0; i < VMEM_NPAGES; i++) {
		vmem->pt.framepage[i] = VOID_IDX;
	}

	// initialisiere Frames Hauptspeicher
	for (i = 0; i < VMEM_NFRAMES * VMEM_PAGESIZE; i++) {
		vmem->data[i] = 0;
	}

	//initialisiere Semaphore
	local_sem = sem_open(NAMED_SEM, O_CREAT, 0777, 0);
	if (local_sem < 0) {
		perror("mmanage.c kann Semaphore nicht erstellen");
		exit(1);
	}
}

///////////////////////////////////////////////////////////////////////////////
int find_free_frame() {
	int freeFrame = VOID_IDX;
	int i;
	for (i=0; i<VMEM_NFRAMES; i++) {	
		if (vmem->pt.framepage[i] == VOID_IDX) { // suche das erste freie Frame in der Tabelle
			freeFrame = i;
			break;
		}
	}
	return freeFrame;
}

//////Zusammen Arbeit zwischen allocate_page() und update_pt() kann bestimmt besser implementiert sein 
void allocate_page(void) {
	int framenummer;
	
	vmem->adm.pf_count++;
	framenummer = find_free_frame();
  
	// wenn alle Frames schon belegt sind, muss etwas loeschen
	if (framenummer == VOID_IDX) {	
		framenummer = find_remove_frame();
	}	
	vmem->adm.next_alloc_idx = framenummer;

	update_pt(framenummer);

	// notiere Pagefault 
	event.alloc_frame=vmem->adm.next_alloc_idx;
	event.g_count=vmem->adm.g_count;
	event.pf_count=vmem->adm.pf_count;
	event.req_pageno=vmem->adm.req_pageno;
	logger(event);
}

///////////////////////////////////////////////////////////////////////////////
void fetch_page(int pt_idx) {
	int* dataStart = &(vmem->data[vmem->adm.next_alloc_idx * VMEM_PAGESIZE]);
	fetch_page_from_pagefile(pt_idx, dataStart);
}

///////////////////////////////////////////////////////////////////////////////
void store_page(int pt_idx) {
	int* dataStart = &(vmem->data[vmem->adm.next_alloc_idx * VMEM_PAGESIZE]);
	store_page_to_pagefile(pt_idx, dataStart);
}

//////Zusammen Arbeit zwischen allocate_page() und update_pt() kann bestimmt besser implementiert sein 
void update_pt(int frame) {
	int mBit;
	int seitenummer = vmem->adm.req_pageno;
	// alte seite aus dem Frame loeschen
	int alteseite = vmem->pt.framepage[frame];
	event.replaced_page=alteseite;
	if(alteseite != VOID_IDX){
		vmem->pt.entries[alteseite].frame = VOID_IDX;
	}
	// schreibe die Seite zurueck ins File, wenn noetig
	mBit = vmem->pt.entries[alteseite].flags & PTF_DIRTY;
	if (alteseite != VOID_IDX && mBit == PTF_DIRTY) {
		store_page(alteseite);
		vmem->pt.entries[alteseite].flags -= PTF_DIRTY;
	}
	//neu seite laden
	fetch_page(seitenummer);
	vmem->pt.entries[seitenummer].frame = frame;
	vmem->pt.entries[seitenummer].age=AGE_DEFAULT;
	vmem->pt.framepage[frame] = seitenummer;
}

///////////////////////////////////////////////////////////////////////////////
int find_remove_frame(void) {
	int framenummer = VOID_IDX;
	switch (vmem->adm.page_rep_algo) {	// suche mit dem gewaehlten Algorithmus 
		case VMEM_ALGO_FIFO:
			framenummer= find_remove_fifo();
			break;
		case VMEM_ALGO_AGING:
			framenummer=find_remove_aging();
			break;
		case VMEM_ALGO_CLOCK:
			framenummer=find_remove_clock();
			break;
	}
	return framenummer;
}

///////////////////////////////////////////////////////////////////////////////
int find_remove_fifo(void) {
	oldestFrame = (oldestFrame + 1) % VMEM_NFRAMES;
	return oldestFrame;
}

///////////////////////////////////////////////////////////////////////////////
int find_remove_aging(void) {
	int frameToRemove = 0;		
	int smallestAge = AGE_MAX;	// max Wert fuer char
	int pageInFrame;		// Seite, die im Frame liegt
	int i;
	for (i=0; i<VMEM_NFRAMES; i++) {
		pageInFrame = vmem->pt.framepage[i];
		if (vmem->pt.entries[pageInFrame].age <= smallestAge) {	// suche ein Frame mit dem kleinsten Age
			smallestAge = vmem->pt.entries[pageInFrame].age;
			frameToRemove = i;
		}
	}
	return frameToRemove;
}

///////////////////////////////////////////////////////////////////////////////
int find_remove_clock(void) {
	int pageInFrame;  
	int rBit;
	while(1) {	// suche zyklisch bis ein zum loeschen passendes Frame gefunden wird
		oldestFrame = (oldestFrame + 1) % VMEM_NFRAMES;
		pageInFrame = vmem->pt.framepage[oldestFrame];
		rBit = vmem->pt.entries[pageInFrame].flags & PTF_REF;
		if (rBit == 0) {
			break;	// gefunden
		} else {
			vmem->pt.entries[pageInFrame].flags = vmem->pt.entries[pageInFrame].flags ^ rBit; // setze das R Bit
		}
	}
	return oldestFrame;
}

void cleanup(void) {
	sem_close(local_sem);
	shmctl(vmem->adm.shm_id, IPC_RMID, 0);	//shm zerstören
}

void dump_pt(void) {
	int i;
	printf("%d. Pagefault\n", vmem->adm.pf_count);
	for (i=0; i<VMEM_NFRAMES; i++) {
		printf("Frame; %d, Page: %d\n", i, vmem->pt.framepage[i]);
	}
}
// EOF
