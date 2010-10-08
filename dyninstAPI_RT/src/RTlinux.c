/*
 * Copyright (c) 1996-2009 Barton P. Miller
 * 
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 * 
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/************************************************************************
 * $Id: RTlinux.c,v 1.54 2008/04/11 23:30:44 legendre Exp $
 * RTlinux.c: mutatee-side library function specific to Linux
 ************************************************************************/

#include "dyninstAPI_RT/h/dyninstAPI_RT.h"
#include "dyninstAPI_RT/src/RTthread.h"
#include "dyninstAPI_RT/src/RTcommon.h"
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#if !defined(DYNINST_RT_STATIC_LIB)
#include <dlfcn.h>
#endif

#include <sys/types.h>
#include <sys/syscall.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <link.h>

#if defined(DYNINST_RT_STATIC_LIB)
/* 
 * The weak symbol here removes the dependence of the static version of this
 * library on pthread_self. If pthread_self is available, then it will be
 * linked.  Otherwise, the linker will ignore it.
 */
#pragma weak pthread_self
extern pthread_t pthread_self(void);
#else
#include <pthread.h>
#endif

extern double DYNINSTstaticHeap_512K_lowmemHeap_1[];
extern double DYNINSTstaticHeap_16M_anyHeap_1[];
extern unsigned long sizeOfLowMemHeap1;
extern unsigned long sizeOfAnyHeap1;

static struct trap_mapping_header *getStaticTrapMap(unsigned long addr);

#if defined(arch_power) && defined(arch_64bit) && defined(os_linux)
unsigned long DYNINSTlinkSave;
unsigned long DYNINSTtocSave;
#endif

/************************************************************************
 * void DYNINSTbreakPoint(void)
 *
 * stop oneself.
************************************************************************/

void DYNINSTbreakPoint()
{
   /* We set a global flag here so that we can tell
      if we're ever in a call to this when we get a 
      SIGBUS */
   if (DYNINSTstaticMode)
      return;

   DYNINST_break_point_event = 1;
   while (DYNINST_break_point_event)  {
      kill(dyn_lwp_self(), DYNINST_BREAKPOINT_SIGNUM);
   }
   /* Mutator resets to 0... */
}

static int failed_breakpoint = 0;
void uncaught_breakpoint(int sig)
{
   failed_breakpoint = 1;
}

void DYNINSTlinuxBreakPoint()
{
   struct sigaction act, oldact;
   int result;
   
   if (DYNINSTstaticMode)
      return;

   memset(&oldact, 0, sizeof(struct sigaction));
   memset(&act, 0, sizeof(struct sigaction));

   result = sigaction(DYNINST_BREAKPOINT_SIGNUM, NULL, &act);
   if (result == -1) {
      perror("DyninstRT library failed sigaction1");
      return;
   }
   act.sa_handler = uncaught_breakpoint;

   result = sigaction(DYNINST_BREAKPOINT_SIGNUM, &act, &oldact);
   if (result == -1) {
      perror("DyninstRT library failed sigaction2");
      return;
   }

   DYNINST_break_point_event = 1;
   failed_breakpoint = 0;
   kill(dyn_lwp_self(), DYNINST_BREAKPOINT_SIGNUM);
   if (failed_breakpoint) {
      DYNINST_break_point_event = 0;
      failed_breakpoint = 0;
   }

   result = sigaction(DYNINST_BREAKPOINT_SIGNUM, &oldact, NULL);
   if (result == -1) {
      perror("DyninstRT library failed sigaction3");
      return;
   }
}

void DYNINSTsafeBreakPoint()
{
   if (DYNINSTstaticMode)
      return;

    DYNINST_break_point_event = 2; /* Not the same as above */
    //    while (DYNINST_break_point_event)
    kill(dyn_lwp_self(), SIGSTOP);
}

void mark_heaps_exec() {
	RTprintf( "*** Initializing dyninstAPI runtime.\n" );

	/* Grab the page size, to align the heap pointer. */
	long int pageSize = sysconf( _SC_PAGESIZE );
	if( pageSize == 0 || pageSize == - 1 ) {
		fprintf( stderr, "*** Failed to obtain page size, guessing 16K.\n" );
		perror( "mark_heaps_exec" );
		pageSize = 1024 * 16;
		} /* end pageSize initialization */

	/* Align the heap pointer. */
	unsigned long int alignedHeapPointer = (unsigned long int) DYNINSTstaticHeap_16M_anyHeap_1;
	alignedHeapPointer = (alignedHeapPointer) & ~(pageSize - 1);
	unsigned long int adjustedSize = (unsigned long int) DYNINSTstaticHeap_16M_anyHeap_1 - alignedHeapPointer + sizeOfAnyHeap1;

	/* Make the heap's page executable. */
	int result = mprotect( (void *) alignedHeapPointer, (size_t) adjustedSize, PROT_READ | PROT_WRITE | PROT_EXEC );
	if( result != 0 ) {
		fprintf( stderr, "%s[%d]: Couldn't make DYNINSTstaticHeap_16M_anyHeap_1 executable!\n", __FILE__, __LINE__);
		perror( "mark_heaps_exec" );
		}
	RTprintf( "*** Marked memory from 0x%lx to 0x%lx executable.\n", alignedHeapPointer, alignedHeapPointer + adjustedSize );

	/* Mark _both_ heaps executable. */
	alignedHeapPointer = (unsigned long int) DYNINSTstaticHeap_512K_lowmemHeap_1;
	alignedHeapPointer = (alignedHeapPointer) & ~(pageSize - 1);
	adjustedSize = (unsigned long int) DYNINSTstaticHeap_512K_lowmemHeap_1 - alignedHeapPointer + sizeOfLowMemHeap1;

	/* Make the heap's page executable. */
	result = mprotect( (void *) alignedHeapPointer, (size_t) adjustedSize, PROT_READ | PROT_WRITE | PROT_EXEC );
	if( result != 0 ) {
		fprintf( stderr, "%s[%d]: Couldn't make DYNINSTstaticHeap_512K_lowmemHeap_1 executable!\n", __FILE__, __LINE__ );
		perror( "mark_heaps_exec" );
		}
	RTprintf( "*** Marked memory from 0x%lx to 0x%lx executable.\n", alignedHeapPointer, alignedHeapPointer + adjustedSize );
	} /* end mark_heaps_exec() */

/************************************************************************
 * void DYNINSTos_init(void)
 *
 * OS initialization function
************************************************************************/
int DYNINST_sysEntry;
void DYNINSTos_init(int calledByFork, int calledByAttach)
{
   RTprintf("DYNINSTos_init(%d,%d)\n", calledByFork, calledByAttach);
#if defined(arch_x86)
   /**
    * The following line reads the vsyscall entry point directly from
    * it's stored, which can then be used by the mutator to locate
    * the vsyscall page.
    * The problem is, I don't know if this memory read is valid on
    * older systems--It could cause a segfault.  I'm going to leave
    * it commented out for now, until further investigation.
    * -Matt 1/18/06
    **/
   //__asm("movl %%gs:0x10, %%eax\n" : "=r" (DYNINST_sysEntry))
#endif
}

#if !defined(DYNINST_RT_STATIC_LIB)
/*
 * For now, removing dependence of static version of this library
 * on libdl.
 */
typedef struct dlopen_args {
  const char *libname;
  int mode;
  void *result;
  void *caller;
} dlopen_args_t;

void *(*DYNINST_do_dlopen)(dlopen_args_t *) = NULL;

static int get_dlopen_error() {
   char *err_str;
   err_str = dlerror();
   if (err_str) {
      strncpy(gLoadLibraryErrorString, err_str, (size_t) ERROR_STRING_LENGTH);
      return 1;
   }
   else {
      sprintf(gLoadLibraryErrorString,"unknown error with dlopen");
      return 0;
   }
   return 0;
}

int DYNINSTloadLibrary(char *libname)
{
   void *res;
   gLoadLibraryErrorString[0]='\0';
   res = dlopen(libname, RTLD_NOW | RTLD_GLOBAL);
   if (res)
   {
      return 1;
   }
 
   get_dlopen_error();
#if defined(arch_x86)
   /* dlopen on recent glibcs has a "security check" so that
      only registered modules can call it. Unfortunately, progs
      that don't include libdl break this check, so that we
      can only call _dl_open (the dlopen worker function) from
      within glibc. We do this by calling do_dlopen
      We fool this check by calling an addr written by the
      mutator */
   if (strstr(gLoadLibraryErrorString, "invalid caller") != NULL &&
       DYNINST_do_dlopen != NULL) {
      dlopen_args_t args;
      args.libname = libname;
      args.mode = RTLD_NOW | RTLD_GLOBAL;
      args.result = 0;
      args.caller = (void *)DYNINST_do_dlopen;
      // There's a do_dlopen function in glibc. However, it's _not_
      // exported; thus, getting the address is a bit of a pain. 
      
      (*DYNINST_do_dlopen)(&args);
      // Duplicate the above
      if (args.result != NULL)
      {
         return 1;
      }
      else
         get_dlopen_error();
   }
#endif
   return 0;
}
#endif

//Define this value so that we can compile on a system that doesn't have
// gettid and still run on one that does.
#if !defined(SYS_gettid)

#if defined(arch_x86)
#define SYS_gettid 224
#elif defined(arch_x86_64)
#define SYS_gettid 186
#endif

#endif

int dyn_lwp_self()
{
   static int gettid_not_valid = 0;
   int result;

   if (gettid_not_valid)
      return getpid();

   result = syscall((long int) SYS_gettid);
   if (result == -1 && errno == ENOSYS)
   {
      gettid_not_valid = 1;
      return getpid();
   }
   return result;  
}

int dyn_pid_self()
{
   return getpid();
}

dyntid_t (*DYNINST_pthread_self)(void);

dyntid_t dyn_pthread_self()
{
   dyntid_t me;
   if (DYNINSTstaticMode) {
#if defined(DYNINST_RT_STATIC_LIB)
       /* This special case is necessary because the static
        * version of libc doesn't define a version of pthread_self
        * unlike the shared version of the library.
        */
       if( !pthread_self ) {
           return (dyntid_t) DYNINST_SINGLETHREADED;
       }
#endif
      return (dyntid_t) pthread_self();
   }
   if (!DYNINST_pthread_self) {
      return (dyntid_t) DYNINST_SINGLETHREADED;
   }
   me = (*DYNINST_pthread_self)();
   return (dyntid_t) me;
}

/* 
   We reserve index 0 for the initial thread. This value varies by
   platform but is always constant for that platform. Wrap that
   platform-ness here. 
*/
int DYNINST_am_initial_thread( dyntid_t tid ) {
	if( dyn_lwp_self() == getpid() ) {
		return 1;
   }
	return 0;
} /* end DYNINST_am_initial_thread() */

/**
 * We need to extract certain pieces of information from the usually opaque 
 * type pthread_t.  Unfortunately, libc doesn't export this information so 
 * we're reverse engineering it out of pthread_t.  The following structure
 * tells us at what offsets it look off of the return value of pthread_self
 * for the lwp, pid, initial start function (the one passed to pthread_create)
 * and the top of this thread's stack.  We have multiple entries in positions, 
 * one for each different version of libc we've seen.
 **/
#define READ_FROM_BUF(pos, type) *((type *)(buffer+pos))

typedef struct pthread_offset_t
{
   unsigned lwp_pos;
   unsigned pid_pos;
   unsigned start_func_pos;
   unsigned stck_start_pos;
} pthread_offset_t;

#if defined(arch_x86_64) && defined(MUTATEE_32)
//x86_64 32 bit mode
#define POS_ENTRIES 4
static pthread_offset_t positions[POS_ENTRIES] = { { 72, 476, 516, 576 },
                                                   { 72, 76, 516, 84 },
                                                   { 72, 476, 516, 80 },
                                                   { 72, 76, 532, 96} }; 

#elif defined(arch_x86_64) 
#define POS_ENTRIES 4
static pthread_offset_t positions[POS_ENTRIES] = { { 144, 952, 1008, 160 },
                                                   { 144, 148, 1000, 160 },
                                                   { 144, 148, 1000, 688 },
                                                   { 144, 148, 1024, 192 } };

#elif defined(arch_x86)
//x86 32
#define POS_ENTRIES 4
static pthread_offset_t positions[POS_ENTRIES] = { { 72, 476, 516, 576 },
                                                   { 72, 76, 516, 84 },
                                                   { 72, 76, 532, 592 },
                                                   { 72, 476, 516, 80 } };

#elif defined(arch_power)
//Power
#define POS_ENTRIES 3
static pthread_offset_t positions[POS_ENTRIES] = { { 72, 76, 508, 576 },
						   { 144, 148, 992, 160 }, // PPC64
                                                   { 144, 148, 992, 1072 } };
#else
#error Need to define thread structures here
#endif

int DYNINSTthreadInfo(BPatch_newThreadEventRecord *ev)
{
   static int err_printed = 0;
   int i;
   char *buffer;

   ev->stack_addr = 0x0;
   ev->start_pc = 0x0;
   buffer = (char *) ev->tid;

   for (i = 0; i < POS_ENTRIES; i++)
   {
      pid_t pid = READ_FROM_BUF(positions[i].pid_pos, pid_t);
      int lwp = READ_FROM_BUF(positions[i].lwp_pos, int);

      if( pid != ev->ppid || lwp != ev->lwp ) {
         continue;
      }

      void *stack_addr;
#if defined(target_ppc32)
	assert("Fix me.");
#elif defined(arch_x86_64) && defined(MUTATEE_32)
      asm("movl %%esp,%0" : "=r" (stack_addr));
#elif defined(arch_x86_64)
      asm("mov %%rsp,%0" : "=r" (stack_addr));
#elif defined(arch_x86)
      asm("movl %%esp,%0" : "=r" (stack_addr));
#else
      stack_addr = READ_FROM_BUF(positions[i].stck_start_pos, void *);
#endif
      void *start_pc = READ_FROM_BUF(positions[i].start_func_pos, void *);

#if defined(arch_power)
      /* 64-bit POWER architectures also use function descriptors instead of directly
       * pointing at the function code.  Find the actual address of the function.
       *
       * Actually, a process can't read its own OPD section.  Punt for now, but remember
       * that we're storing the function descriptor address, not the actual function address!
       */
      //uint64_t actualAddr = *((uint64_t *)start_pc);
      //fprintf(stderr, "*** Redirecting start_pc from 0x%lx to 0x%lx\n", start_pc, actualAddr);
      //start_pc = (void *)actualAddr;
#endif

      // Sanity checking. There are multiple different places that we have
      // found the stack address for a given pair of pid/lwpid locations,
      // so we need to do the best job we can of verifying that we've
      // identified the real stack. 
      //
      // Currently we just check for known incorrect values. We should
      // generalize this to check for whether the address is in a valid
      // memory region, but it is not clear whether that information is
      // available at this point.
      //        
      if(stack_addr == (void*)0 || stack_addr == (void*)0xffffffec) {
         continue;
      }

      ev->stack_addr = stack_addr;
      ev->start_pc = start_pc;

      return 1;
   }

   if (!err_printed)
   {
      //If you get this error, then Dyninst is having trouble figuring out
      //how to read the information from the positions structure above.
      //It needs a new entry filled in.  Running the commented out program
      //at the end of this file can help you collect the necessary data.
      RTprintf( "[%s:%d] Unable to parse the pthread_t structure for this version of libpthread.  Making a best guess effort.\n",  __FILE__, __LINE__ );
      err_printed = 1;
   }
   
   return 1;
}

#if defined(cap_mutatee_traps)

#include <ucontext.h>

#if defined(arch_x86) || defined(MUTATEE_32)

#if !defined(REG_EIP)
#define REG_EIP 14
#endif
#if !defined(REG_ESP)
#define REG_ESP 7
#endif
#define REG_IP REG_EIP
#define REG_SP REG_ESP
#define MAX_REG 18

#elif defined(arch_x86_64)

#if !defined(REG_RIP)
#define REG_RIP 16
#endif
#if !defined(REG_RSP)
#define REG_RSP 15
#endif
#define REG_IP REG_RIP
#define REG_SP REG_RSP
#define MAX_REG 15
#if !defined(REG_RAX)
#define REG_RAX 13
#endif
#if !defined(REG_R10)
#define REG_R10 2
#endif
#if !defined(REG_R11)
#define REG_R11 3
#endif

#endif

extern unsigned long dyninstTrapTableUsed;
extern unsigned long dyninstTrapTableVersion;
extern trapMapping_t *dyninstTrapTable;
extern unsigned long dyninstTrapTableIsSorted;

/**
 * Called by the SIGTRAP handler, dyninstTrapHandler.  This function is 
 * closly intwined with dyninstTrapHandler, don't modify one without 
 * understanding the other.
 *
 * This function sets up the calling context that was passed to the
 * SIGTRAP handler so that control will be redirected to our instrumentation
 * when we do the setcontext call.
 * 
 * There are a couple things that make this more difficult than it should be:
 *   1. The OS provided calling context is similar to the GLIBC calling context,
 *      but not compatible.  We'll create a new GLIBC compatible context and
 *      copy the possibly stomped registers from the OS context into it.  The
 *      incompatiblities seem to deal with FP and other special purpose registers.
 *   2. setcontext doesn't restore the flags register.  Thus dyninstTrapHandler
 *      will save the flags register first thing and pass us its value in the
 *      flags parameter.  We'll then push the instrumentation entry and flags
 *      onto the context's stack.  Instead of transfering control straight to the
 *      instrumentation, we'll actually go back to dyninstTrapHandler, which will
 *      do a popf/ret to restore flags and go to instrumentation.  The 'retPoint'
 *      parameter is the address in dyninstTrapHandler the popf/ret can be found.
 **/

void dyninstTrapHandler(int sig, siginfo_t *sg, ucontext_t *context)
{
   void *orig_ip;
   void *trap_to;

   orig_ip = (void *) context->uc_mcontext.gregs[REG_IP];
   assert(orig_ip);

   // Find the new IP we're going to and substitute. Leave everything else untouched.
   if (DYNINSTstaticMode) {
      unsigned long zero = 0;
      unsigned long one = 1;
      struct trap_mapping_header *hdr = getStaticTrapMap((unsigned long) orig_ip);
      assert(hdr);
      trapMapping_t *mapping = &(hdr->traps[0]);
      trap_to = dyninstTrapTranslate(orig_ip,
                                     (unsigned long *) &hdr->num_entries, 
                                     &zero, 
                                     (volatile trapMapping_t **) &mapping,
                                     &one);
   }
   else {
      trap_to = dyninstTrapTranslate(orig_ip, 
                                     &dyninstTrapTableUsed,
                                     &dyninstTrapTableVersion,
                                     (volatile trapMapping_t **) &dyninstTrapTable,
                                     &dyninstTrapTableIsSorted);
                                     
   }
   context->uc_mcontext.gregs[REG_IP] = (long) trap_to;
}

#if defined(cap_binary_rewriter)

extern struct r_debug _r_debug;
struct r_debug _r_debug __attribute__ ((weak));

#define NUM_LIBRARIES 512 //Important, max number of rewritten libraries

#define WORD_SIZE (8 * sizeof(unsigned))
#define NUM_LIBRARIES_BITMASK_SIZE (1 + NUM_LIBRARIES / WORD_SIZE)
struct trap_mapping_header *all_headers[NUM_LIBRARIES];

static unsigned all_headers_current[NUM_LIBRARIES_BITMASK_SIZE];
static unsigned all_headers_last[NUM_LIBRARIES_BITMASK_SIZE];

#if !defined(arch_x86_64) || defined(MUTATEE_32)
typedef Elf32_Dyn ElfX_Dyn;
#else
typedef Elf64_Dyn ElfX_Dyn;
#endif

struct trap_mapping_header *getStaticTrapMap(unsigned long addr);

static int parse_libs();
static int parse_link_map(struct link_map *l);
static void clear_unloaded_libs();

static void set_bit(unsigned *bit_mask, int bit, char value);
//static char get_bit(unsigned *bit_mask, int bit);
static void clear_bitmask(unsigned *bit_mask);
static unsigned get_next_free_bitmask(unsigned *bit_mask, int last_pos);
static unsigned get_next_set_bitmask(unsigned *bit_mask, int last_pos);

static tc_lock_t trap_mapping_lock;

static struct trap_mapping_header *getStaticTrapMap(unsigned long addr)
{
   struct trap_mapping_header *header;
   int i;
   
   tc_lock_lock(&trap_mapping_lock);
   parse_libs();

   i = -1;
   for (;;) {
      i = get_next_set_bitmask(all_headers_current, i);
      assert(i >= 0 && i <= NUM_LIBRARIES);
      if (i == NUM_LIBRARIES) {
         header = NULL;
         goto done;
      }
      header = all_headers[i];
      if (addr >= header->low_entry && addr <= header->high_entry) {
         goto done;
      }
   }  
 done:
   tc_lock_unlock(&trap_mapping_lock);
   return header;
}

static int parse_libs()
{
   struct link_map *l_current;

   l_current = _r_debug.r_map;
   if (!l_current)
      return -1;

   clear_bitmask(all_headers_current);
   while (l_current) {
      parse_link_map(l_current);
      l_current = l_current->l_next;
   }
   clear_unloaded_libs();

   return 0;
}

//parse_link_map return values
#define PARSED 0
#define NOT_REWRITTEN 1
#define ALREADY_PARSED 2
#define ERROR_INTERNAL -1
#define ERROR_FULL -2
static int parse_link_map(struct link_map *l) 
{
   ElfX_Dyn *dynamic_ptr;
   struct trap_mapping_header *header;
   unsigned int i, new_pos;

   dynamic_ptr = (ElfX_Dyn *) l->l_ld;
   if (!dynamic_ptr)
      return -1;

   assert(sizeof(dynamic_ptr->d_un.d_ptr) == sizeof(void *));
   for (; dynamic_ptr->d_tag != DT_NULL && dynamic_ptr->d_tag != DT_DYNINST; dynamic_ptr++);
   if (dynamic_ptr->d_tag == DT_NULL) {
      return NOT_REWRITTEN;
   }

   header = (struct trap_mapping_header *) (dynamic_ptr->d_un.d_val + l->l_addr);
   
   if (header->signature != TRAP_HEADER_SIG)
      return ERROR_INTERNAL;
   if (header->pos != -1) {
      set_bit(all_headers_current, header->pos, 1);
      assert(all_headers[header->pos] == header);
      return ALREADY_PARSED;
   }
 
   for (i = 0; i < header->num_entries; i++)
   {
      header->traps[i].source = (void *) (((unsigned long) header->traps[i].source) + l->l_addr);
      header->traps[i].target = (void *) (((unsigned long) header->traps[i].target) + l->l_addr);
      if (!header->low_entry || header->low_entry > (unsigned long) header->traps[i].source)
         header->low_entry = (unsigned long) header->traps[i].source;
      if (!header->high_entry || header->high_entry < (unsigned long) header->traps[i].source)
         header->high_entry = (unsigned long) header->traps[i].source;
   }

   new_pos = get_next_free_bitmask(all_headers_last, -1);
   assert(new_pos >= 0 && new_pos < NUM_LIBRARIES);
   if (new_pos == NUM_LIBRARIES)
      return ERROR_FULL;

   header->pos = new_pos;
   all_headers[new_pos] = header;
   set_bit(all_headers_current, new_pos, 1);
   set_bit(all_headers_last, new_pos, 1);

   return PARSED;
}

static void clear_unloaded_libs()
{
   unsigned i;
   for (i = 0; i<NUM_LIBRARIES_BITMASK_SIZE; i++)
   {
      all_headers_last[i] = all_headers_current[i];
   }
}

static void set_bit(unsigned *bit_mask, int bit, char value) {
   assert(bit < NUM_LIBRARIES);
   unsigned *word = bit_mask + bit / WORD_SIZE;
   unsigned shift = bit % WORD_SIZE;
   if (value) {
      *word |= (1 << shift);
   }
   else {
      *word &= ~(1 << shift);
   }
}

//Wasn't actually needed
/*
static char get_bit(unsigned *bit_mask, int bit) {
   assert(bit < NUM_LIBRARIES);
   unsigned *word = bit_mask + bit / WORD_SIZE;
   unsigned shift = bit % WORD_SIZE;
   return (*word & (1 << shift)) ? 1 : 0;
}
*/

static void clear_bitmask(unsigned *bit_mask) {
   unsigned i;
   for (i = 0; i < NUM_LIBRARIES_BITMASK_SIZE; i++) {
      bit_mask[i] = 0;
   }
}

static unsigned get_next_free_bitmask(unsigned *bit_mask, int last_pos) {
   unsigned i, j;
   j = last_pos+1;
   i = j / WORD_SIZE;
   for (; j < NUM_LIBRARIES; i++) {
      if (bit_mask[i] == (unsigned) -1) {
         j += WORD_SIZE;
         continue;
      }
      for (;;) {
         if (!((1 << (j % WORD_SIZE) & bit_mask[i]))) {
            return j;
         }
         j++;
         if (j % WORD_SIZE == 0) {
            break;
         }
      }
   }
   return NUM_LIBRARIES;
}

static unsigned get_next_set_bitmask(unsigned *bit_mask, int last_pos) {
   unsigned i, j;
   j = last_pos+1;
   i = j / WORD_SIZE;
   for (; j < NUM_LIBRARIES; i++) {
      if (bit_mask[i] == (unsigned) 0) {
         j += WORD_SIZE;
         continue;
      }
      for (;;) {
         if ((1 << (j % WORD_SIZE) & bit_mask[i])) {
            return j;
         }
         j++;
         if (j % WORD_SIZE == 0) {
            break;
         }
      }
   }
   return NUM_LIBRARIES;
}

#endif


#endif /* cap_mutatee_traps */

int dyn_var = 0;

#if defined(cap_binary_rewriter) && !defined(DYNINST_RT_STATIC_LIB)
/* For a static binary, all global constructors are combined in an undefined
 * order. Also, DYNINSTBaseInit must be run after all global constructors have
 * been run. Since the order of global constructors is undefined, DYNINSTBaseInit
 * cannot be run as a constructor in static binaries. Instead, it is run from a
 * special constructor handler that processes all the global constructors in
 * the binary. Leaving this code in would create a global constructor for the
 * function runDYNINSTBaseInit(). See DYNINSTglobal_ctors_handler.
 */ 
extern void DYNINSTBaseInit();
void runDYNINSTBaseInit() __attribute__((constructor));
void runDYNINSTBaseInit()
{
   DYNINSTBaseInit();
}
#endif


/*
//Small program for finding the correct values to fill in pos_in_pthreadt
// above
#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define gettid() syscall(SYS_gettid)

pthread_attr_t attr;

void *foo(void *f) {
  pid_t pid, tid;
  unsigned stack_addr;
  unsigned best_stack = 0xffffffff;
  int best_stack_pos = 0;
  void *start_func;
  int *p;
  int i = 0;
  pid = getpid();
  tid = gettid();
  start_func = foo;
  //x86 only.  
  asm("movl %%ebp,%0" : "=r" (stack_addr));
  p = (int *) pthread_self();
  while (i < 1000)
  {
    if (*p == (unsigned) pid)
      printf("pid @ %d\n", i);
    if (*p == (unsigned) tid)
      printf("lwp @ %d\n", i);
    if (*p > stack_addr && *p < best_stack)
    {
      best_stack = *p;
      best_stack_pos = i;
    }
    if (*p == (unsigned) start_func)
      printf("func @ %d\n", i);
    i += sizeof(int);
    p++;
  }  
  printf("stack @ %d\n", best_stack_pos);
  return NULL;
}

int main(int argc, char *argv[])
{
  pthread_t t;
  void *result;
  pthread_attr_init(&attr);
  pthread_create(&t, &attr, foo, NULL);
  pthread_join(t, &result);
  return 0;
}
*/

