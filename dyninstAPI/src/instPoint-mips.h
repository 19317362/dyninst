/*
 * Copyright (c) 1998 Barton P. Miller
 * 
 * We provide the Paradyn Parallel Performance Tools (below
 * described as Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 * 
 * This license is for research uses.  For such uses, there is no
 * charge. We define "research use" to mean you may freely use it
 * inside your organization for whatever purposes you see fit. But you
 * may not re-distribute Paradyn or parts of Paradyn, in any form
 * source or binary (including derivatives), electronic or otherwise,
 * to any other organization or entity without our permission.
 * 
 * (for other uses, please contact us at paradyn@cs.wisc.edu)
 * 
 * All warranties, including without limitation, any warranty of
 * merchantability or fitness for a particular purpose, are hereby
 * excluded.
 * 
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 * 
 * Even if advised of the possibility of such damages, under no
 * circumstances shall we (or any other person or entity with
 * proprietary rights in the software licensed hereunder) be liable
 * to you or any third party for direct, indirect, or consequential
 * damages of any character regardless of type of action, including,
 * without limitation, loss of profits, loss of use, loss of good
 * will, or computer failure or malfunction.  You agree to indemnify
 * us (and any other person or entity with proprietary rights in the
 * software licensed hereunder) for any and all liability it may
 * incur to third parties resulting from your use of Paradyn.
 */

// $Id: instPoint-mips.h,v 1.14 2003/10/21 17:22:11 bernat Exp $
// MIPS-specific definition of class instPoint

#ifndef _INST_POINT_MIPS_H_
#define _INST_POINT_MIPS_H_

#include <stdio.h>
#include "common/h/Types.h" // Address
#include "arch-mips.h"    // instruction
#include "dyninstAPI/src/symtab.h"       // pd_Function, function_base, image

class process;

#ifdef BPATCH_LIBRARY
class BPatch_point;
#endif

typedef Address Offset;
typedef enum {
  IPT_NONE = 0,
  IPT_ENTRY,
  IPT_EXIT,
  IPT_CALL,
  IPT_OTHER
} instPointType;

/* instPoint flag bits */
#define IP_Relocated       (0x00000001)
#define IP_Overlap         (0x00000002)
#define IP_RecursiveBranch (0x80000000)

class instPoint {
 public:
  instPoint(pd_Function *fn, Offset offset, instPointType ipt, unsigned info) :
    flags(info),
    vectorId(-1),
    func_(fn),
    hint_got_(0),
    callee_(NULL),
    owner_(fn->file()->exec()),
    addr_(fn->getAddress(0) + offset),
    offset_(offset), 
    ipType_(ipt), 
    size_(2*INSN_SIZE),
    origOffset_(offset)
    {
      origInsn_.raw = owner_->get_instruction(addr_); 
      delayInsn_.raw = owner_->get_instruction(addr_+INSN_SIZE); 
    }
  ~instPoint() { /* TODO */ }

  const function_base *iPgetFunction() const { return func_;   }
  const function_base *iPgetCallee()   const { return callee_; }
  const image         *iPgetOwner()    const { return owner_;  }
  Address        iPgetAddress(process *p = 0)  const;
  pd_Function *func() const { return (pd_Function *)func_;}

  instPointType type() const { return ipType_; }
  int size() const { return size_; }
  unsigned code() const { return origInsn_.raw; }
  Offset offset() const { return offset_; }
  Address address(const process *p = NULL) const { 
    return func_->getAddress(p) + offset_; 
  }
  void setCallee(pd_Function *fn) { callee_ = fn; }

  void print(FILE *stream = stderr, char *pre = NULL) 
    {
      if (pre) fprintf(stream, pre);
      switch(ipType_) {
      case IPT_ENTRY: fprintf(stream, "entry "); break;
      case IPT_EXIT: fprintf(stream, "exit "); break;
      case IPT_CALL: fprintf(stream, "call "); break;
      case IPT_OTHER: fprintf(stream, "other "); break;
      default: fprintf(stream, "unknown "); break;
      }
      fprintf(stream, "point of \"%s\"\n", func_->prettyName().c_str());      
    }

  bool match(instPoint *p);

 public:
  unsigned       flags;
  int            vectorId;
  // protected:
  function_base *func_;      // function containing this instPoint
  Address        hint_got_;  // callee hint (relative vaddr of GOT entry) 
  function_base *callee_;    // function being called (if call point)
  image         *owner_;     // image containing "func_"
  Address        addr_;      // address of this instPoint
  Offset         offset_;    // offset of instPoint from function start
  instPointType  ipType_;
  int            size_;      // instPoint footprint
  instruction    origInsn_;  // instruction being instrumented

  instruction    delayInsn_;
  Offset         origOffset_;

  // VG(11/06/01): there is some common stuff amongst instPoint
  // classes on all platforms (like addr and the back pointer to
  // BPatch_point). 
  // TODO: Merge these classes and put ifdefs for platform-specific
  // fields.
 private:
  // We need this here because BPatch_point gets dropped before
  // we get to generate code from the AST, and we glue info needed
  // to generate code for the effective address snippet/node to the
  // BPatch_point rather than here.
  friend class BPatch_point;
  BPatch_point *bppoint; // unfortunately the correspondig BPatch_point
  			 // is created afterwards, so it needs to set this
 public:
  const BPatch_point* getBPatch_point() const { return bppoint; }
};

class entryPoint : public instPoint {
 public:
  entryPoint(pd_Function *func, Offset off, unsigned info = 0) 
    : instPoint(func, off, IPT_ENTRY, info) {}
};

class exitPoint : public instPoint {
 public:
  exitPoint(pd_Function *func, Offset off, unsigned info = 0) 
    : instPoint(func, off, IPT_EXIT, info) {}
};

class callPoint : public instPoint {
 public:
  callPoint(pd_Function *func, Offset off, unsigned info = 0) 
    : instPoint(func, off, IPT_CALL, info) {}
};


#endif /* _INST_POINT_MIPS_H_ */
