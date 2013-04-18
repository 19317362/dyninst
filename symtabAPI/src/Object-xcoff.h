/*
 * See the dyninst/COPYRIGHT file for copyright information.
 * 
 * We provide the Paradyn Tools (below described as "Paradyn")
 * on an AS IS basis, and do not warrant its validity or performance.
 * We reserve the right to update, modify, or discontinue this
 * software at any time.  We shall have no obligation to supply such
 * updates or modifications or any other form of support to you.
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
 * AIX object files.
 * $Id: Object-xcoff.h,v 1.24 2008/07/01 19:26:43 legendre Exp $
************************************************************************/


#if !defined(_Object_aix_h_)
#define _Object_aix_h_

/************************************************************************
 * header files.
************************************************************************/

#include "common/src/headers.h"
//#include <common/src/Line.h>
#include "symtabAPI/h/Symbol.h"
#include "symtabAPI/h/Symtab.h"
#include "common/src/Types.h"

#include <string>
#include <vector>

using namespace std;

extern "C" {
#include <a.out.h>
};

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define __AR_BIG__
#define __AR_SMALL__
#include <ar.h>

namespace Dyninst {
namespace SymtabAPI {

class fileOpener;

// Object to represent both the 32-bit and 64-bit archive headers
// for ar files (libraries)
class xcoffArchive {
 public:
  xcoffArchive (fileOpener *f) : member_name(0), fo_(f) {}
  virtual ~xcoffArchive () {}
  
  virtual int read_arhdr() = 0;
  virtual int read_mbrhdr() = 0;

  unsigned long long aout_offset;
  unsigned long long next_offset;
  char *member_name;
  int member_len;
 protected:
  unsigned long long first_offset;
  unsigned long long last_offset;

  fileOpener *fo_;
};

class xcoffArchive_32 : private xcoffArchive {
 public:
  xcoffArchive_32 (fileOpener *file) : xcoffArchive(file) {};
  ~xcoffArchive_32 () {if (member_name) free(member_name);};
  virtual int read_arhdr();
  virtual int read_mbrhdr();

 private:
  struct fl_hdr filehdr;
  struct ar_hdr memberhdr;
};

class xcoffArchive_64 : private xcoffArchive {
 public:
  xcoffArchive_64 (fileOpener *file) : xcoffArchive(file) {};
  ~xcoffArchive_64 () {if (member_name) free(member_name);};
  virtual int read_arhdr();
  virtual int read_mbrhdr();

 private:
  struct fl_hdr_big filehdr;
  struct ar_hdr_big memberhdr;
};

// We want to mmap a file once, then go poking around inside it. So we
// need to tie a few things together.

class fileOpener {
 public:
    static std::vector<fileOpener *> openedFiles;
    static fileOpener *openFile(const std::string &file);
    static fileOpener *openFile(void *ptr, unsigned size);
    
    void closeFile();

#if 0
    fileOpener(const std::string &file) : refcount_(1), 
        file_(file), fd_(0), 
        size_(0), mmapStart_(NULL),
        offset_(0) {}
#endif

    fileOpener(void *ptr, unsigned size) : refcount_(1), 
        file_(""), fd_(0), 
        size_(size), mmapStart_(ptr),
        offset_(0) {}
	
    ~fileOpener();

#if  0
    bool open();
    bool mmap();
    bool unmap();
    bool close();
#endif

    bool pread(void *buf, unsigned size, unsigned offset);
    bool read(void *buf, unsigned size);
    bool seek(int offset);
    bool set(unsigned addr);
    // No write :)
    // Get me a pointer into the mapped area
    void *ptr() const;
    void *getPtrAtOffset(unsigned offset) const;
    
    const std::string &file() const { return file_; }
    int fd() const { return fd_; }
    unsigned size() const { return size_; }
    void *mem_image() const { return mmapStart_; }
    void set_file(std::string f) { file_ = f; }

 private:
    int refcount_;
    std::string file_;
    int fd_;
    unsigned size_;
    void *mmapStart_;

    // Where were we last reading?
    unsigned offset_;
};


class Symtab;

/************************************************************************
 * class Object
************************************************************************/

class Object : public AObject {
   friend class Symtab;
 public:
    Object (const Object &);
    Object&   operator= (const Object &);
    Object(){}	
    Object(MappedFile *, MappedFile *, bool, void (*)(const char *) = log_msg, Offset off = 0, bool alloc_syms = true);
    Object(MappedFile *, MappedFile *, dyn_hash_map<std::string, LineInformation> &, std::vector<Region *> &, 
          void (*)(const char *) = log_msg, Offset off = 0);
    Object(MappedFile *, MappedFile *, std::string &member_name, Offset offset,	
            void (*)(const char *) = log_msg, void *base = NULL);
    virtual ~Object ()
    {
#if 0
        if (fo_)
    	    fo_->closeFile();
#endif
    }
    
    Offset getTOCoffset() const { return toc_offset_; }

    // AIX does some weirdness with addresses. We don't want to touch local
    // symbols to get addresses right, but that means that the base address
    // needs to have a small value added back in. On shared objects.
    Offset data_reloc () const { return data_reloc_; }
    Offset text_reloc () const { return text_reloc_; }
    Offset bss_size () const { return bss_size_; }
    
    void get_stab_info(char *&stabstr, int &nstabs, void *&stabs, char *&stringpool) const;
    
    void get_line_info(int& nlines, char*& lines,unsigned long& fdptr) const {
	nlines = nlines_;
	lines = (char*) linesptr_; 
	fdptr = linesfdptr_;
    }

    Offset getLoadAddress() const { return loadAddress_; }
    Offset getEntryAddress() const { return entryAddress_; }
    Offset getBaseAddress() const { return baseAddress_; }
    void getModuleLanguageInfo(dyn_hash_map<std::string, supportedLanguages> *mod_langs);
    const char *interpreter_name() const { return NULL; }
#if 0
    dyn_hash_map<std::string, LineInformation > &getLineInfo() { 
    	parseFileLineInfo();
    	return lineInfo_; 
    }
#endif

    Dyninst::Architecture getArch();
    ObjectType objType() const;
    bool isEEL() const { return false; }

    void parseTypeInfo(Symtab *obj);
    bool emitDriver(Symtab *obj, string fName, std::vector<Symbol *>&allSymbols, unsigned flag);

#if 0
    bool getRegValueAtFrame(Address pc, 
                            Dyninst::MachRegister reg, 
                            Dyninst::MachRegisterVal &reg_result,
                            MemRegReader *reader);
#endif

private:

    void load_object(bool alloc_syms);
    void load_archive(bool is_aout, bool alloc_syms);
	bool fillExceptionTable(struct exceptab *, unsigned int, bool, struct syment *);
    void parse_aout(int offset, bool is_aout, bool alloc_syms);
    void parseFileLineInfo(Symtab *, dyn_hash_map<std::string, LineInformation> &li);
    void parseLineInformation(dyn_hash_map<std::string, LineInformation> &li, std::string * currentSourceFile,
                                	char * symbolName,
                                	SYMENT * sym,
                                 	Offset linesfdptr,
                                 	char * lines,
                                 	int nlines );
																										 
    // We mmap big files and piece them out; this handles the mapping
    // and reading and pointerage and...
    fileOpener *fo_;

    std::string member_;
    Offset offset_;
    Offset toc_offset_;
    Offset data_reloc_;
    Offset text_reloc_;
    Offset bss_size_;

    Offset   loadAddress_;      // The object may specify a load address
                                    //   Set to 0 if it may load anywhere
    Offset entryAddress_;
    Offset baseAddress_;
    
	char *stringPool;

    int  nstabs_;
    int  nlines_;
    void *stabstr_;
    void *stabs_;
    void *stringpool_;
    void *linesptr_;
    Offset linesfdptr_;
    bool is64_;
    dyn_hash_map<std::string, LineInformation > lineInfo_;
};

/* This class is only used in symtab.C; the only reason it's in
   this header file is so that template0.C can include it to
   instantiate pdvector< IncludeFileInfo >. */
class IncludeFileInfo {
	public:
		unsigned int begin;
		unsigned int end;
		std::string name;

		IncludeFileInfo() : begin(0), end(0) {};
		IncludeFileInfo( int _begin, const char *_name ) : begin(_begin), end(0), name(_name) {};
	};

}//namespace SymtabAPI

}//namespace Dyninst

char *P_cplus_demangle( const char * symbol, bool nativeCompiler, bool includeTypes );

#endif /* !defined(_Object_aix_h_) */
