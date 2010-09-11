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

#if !defined(DTHREAD_H_)
#define DTHREAD_H_

#include <stdlib.h>

#if !defined(os_windows)
#define cap_pthreads
#include <pthread.h>
#else
#include <Windows.h>
#endif

class DThread {
#if defined(cap_pthreads)
   pthread_t thrd;
#elif defined(os_windows)
   HANDLE thrd;
#endif
   bool live;   
 public:
   DThread();
   ~DThread();
   typedef void (*initial_func_t)(void *);

   static long self();
   bool spawn(initial_func_t func, void *param);
   bool join();
   long id();
};

class Mutex {
   friend class CondVar;
#if defined(cap_pthreads)
   pthread_mutex_t mutex;
#elif defined(os_windows)
   CRITICAL_SECTION mutex;
#endif
 public:
   Mutex(bool recursive=false);
   ~Mutex();

   bool lock();
   bool unlock();
};

class CondVar {
#if defined(cap_pthreads)
   pthread_cond_t cond;
#elif defined(os_windows)
   CONDITION_VARIABLE cond;
#endif
   Mutex *mutex;
   bool created_mutex;
 public:
   CondVar(Mutex *m = NULL);
   ~CondVar();

   bool unlock();
   bool lock();
   bool signal();
   bool broadcast();
   bool wait();
};

#endif
