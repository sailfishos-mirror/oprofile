/**
 * @file op_bfd.cpp
 * Encapsulation of bfd objects
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie <phil_el@wanadoo.fr>
 * @author John Levon <moz@compsoc.man.ac.uk>
 */

#include <algorithm>

#include "op_file.h"
#include "oprofpp.h"

op_bfd::op_bfd(opp_samples_files& samples, const std::string & filename)
	:
	ibfd(0),
	bfd_syms(0),
	sect_offset(0)
{
	if (filename.length() == 0) {
		fprintf(stderr,"oprofpp: oppp_bfd() empty image filename.\n");
		exit(EXIT_FAILURE);
	}

	nr_samples = op_get_fsize(filename.c_str(), 0);

	open_bfd_image(filename, samples.first_header()->is_kernel);

	time_t newmtime = op_get_mtime(filename.c_str());
	if (newmtime != samples.first_header()->mtime) {
		fprintf(stderr, "oprofpp: WARNING: the last modified time of the binary file %s does not match\n"
			"that of the sample file. Either this is the wrong binary or the binary\n"
			"has been modified since the sample file was created.\n", filename.c_str());
	}

	samples.set_sect_offset(sect_offset);
}

op_bfd::~op_bfd()
{
	delete bfd_syms;
	bfd_close(ibfd);
}

/**
 * open_bfd_image - op_bfd ctor helper
 * @param file name of a valid image file
 * @param is_kernel true if the image is the kernel or a module
 *
 * This function will open a bfd image and process symbols
 * within this image file
 *
 * Failure to open the image a fatal
 * gettings zero symbols from the image is not an error
 */
void op_bfd::open_bfd_image(const std::string & filename, bool is_kernel)
{
	char **matching;

	ibfd = bfd_openr(filename.c_str(), NULL);
 
	if (!ibfd) {
		fprintf(stderr,"oprofpp: bfd_openr of %s failed.\n", filename.c_str());
		exit(EXIT_FAILURE);
	}
	 
	if (!bfd_check_format_matches(ibfd, bfd_object, &matching)) { 
		fprintf(stderr,"oprofpp: BFD format failure for %s.\n", filename.c_str());
		exit(EXIT_FAILURE);
	}

	/* Kernel / kernel modules are calculated as offsets against 
	 * the .text section, so they need special handling
	 */
	if (is_kernel) {
		asection *sect;
		sect = bfd_get_section_by_name(ibfd, ".text");
		sect_offset = sect->filepos;
		verbprintf("Adjusting kernel samples by 0x%x, .text filepos 0x%lx\n", 
			sect_offset, sect->filepos); 
	}

	get_symbols();
}

/**
 * symcomp - comparator
 *
 */
static bool symcomp(const op_bfd_symbol & a, const op_bfd_symbol & b)
{
	return a.vma < b.vma;
}

namespace { 
 
// only add symbols that would /never/ be
// worth examining
static char const * boring_symbols[] = {
	"gcc2_compiled.",
	"_init"
};

static const size_t nr_boring_symbols =
			sizeof(boring_symbols) / sizeof(boring_symbols[0]);
 
/**
 * Return true if the symbol is worth looking at
 */
static bool interesting_symbol(asymbol *sym)
{
	if (!(sym->section->flags & SEC_CODE))
		return 0;

	if (!sym->name || sym->name[0] == '\0')
		return 0;

	// C++ exception stuff
	if (sym->name[0] == '.' && sym->name[1] == 'L')
		return 0;

	for (size_t i = 0; i < nr_boring_symbols; ++i) {
		if (!strcmp(boring_symbols[i], sym->name))
			return 0;
	}
	 
	return 1;
}

} // namespace anon
 
/**
 * get_symbols - op_bfd ctor helper
 *
 * Parse and sort in ascending order all symbols
 * in the file pointed to by abfd that reside in
 * a %SEC_CODE section. Returns true if symbol(s)
 * are found. The symbols are filtered through
 * the interesting_symbol() predicate and sorted
 * with the symcomp() comparator.
 */
bool op_bfd::get_symbols()
{
	uint nr_all_syms;
	symbol_index_t i; 
	size_t size;

	if (!(bfd_get_file_flags(ibfd) & HAS_SYMS))
		return false;

	size = bfd_get_symtab_upper_bound(ibfd);

	/* HAS_SYMS can be set with no symbols */
	if (size < 1)
		return false;

	bfd_syms = new asymbol * [size];
	nr_all_syms = bfd_canonicalize_symtab(ibfd, bfd_syms);
	if (nr_all_syms < 1) {
		return false;
	}

	for (i = 0; i < nr_all_syms; i++) {
		if (interesting_symbol(bfd_syms[i])) {
			// we can't fill the size member for now, because in
			// some case it is calculated from the vma of the
			// next symbol
			struct op_bfd_symbol symb = { bfd_syms[i], 
			  bfd_syms[i]->value + bfd_syms[i]->section->vma, 0 };
			syms.push_back(symb);
		}
	}

	std::stable_sort(syms.begin(), syms.end(), symcomp);

	// now we can calculate the symbol size
	for (i = 0 ; i < syms.size() ; ++i) {
		syms[i].size = symbol_size(i);
	}

	// we need to ensure than for a given vma only one symbol exist else
	// we read more than one time some samples. Fix #526098
	// ELF symbols size : potential bogosity here because when using
	// elf symbol size we need to check than two symbols does not overlap.
	for (i =  1 ; i < syms.size() ; ++i) {
		if (syms[i].vma == syms[i-1].vma) {
			// TODO: choose more carefully the symbol we drop.
			// If once have FUNCTION flag and not the other keep
			// it etc.
			syms.erase(syms.begin() + i);
			i--;
		}
	}

	verbprintf("nr symbols before excluding symbols%u\n", syms.size());

	// it's time to remove the excluded symbol.
	for (i = 0 ; i < syms.size() ; ) {
		if (is_excluded_symbol(syms[i].symbol->name)) {
			printf("excluding symbold %s\n", syms[i].symbol->name);
			syms.erase(syms.begin() + i);
		} else {
			++i;
		}
	}

	verbprintf("nr symbols %u\n", syms.size());

	if (syms.empty())
		return false;

	return true;
}

u32 op_bfd::sym_offset(symbol_index_t sym_index, u32 num) const
{
	/* take off section offset */
	num -= syms[sym_index].symbol->section->filepos;
	/* and take off symbol offset from section */
	num -= syms[sym_index].symbol->value;

	return num;
}

bool op_bfd::have_debug_info() const
{
	sec* section;
	for (section = ibfd->sections; section; section = section->next)
		if (section->flags & SEC_DEBUGGING)
			break;

	return section != NULL;
}
 
bool op_bfd::get_linenr(symbol_index_t sym_idx, uint offset, 
			char const * & filename, unsigned int & linenr) const
{
	char const * functionname;
	bfd_vma pc;

	filename = 0;
	linenr = 0;

	asection* section = syms[sym_idx].symbol->section;

	if ((bfd_get_section_flags (ibfd, section) & SEC_ALLOC) == 0)
		return false;

	pc = sym_offset(sym_idx, offset) + syms[sym_idx].symbol->value;

	if (pc >= bfd_section_size(ibfd, section))
		return false;

	bool ret = bfd_find_nearest_line(ibfd, section, bfd_syms, pc,
					 &filename, &functionname, &linenr);

	if (filename == NULL || ret == false) {
		filename = "";
		linenr = 0;
		ret = false;
	}

	// functioname and symbol name can be different if we query linenr info
	// if we accept it we can get samples for the wrong symbol (#484660)
	if (ret == true && functionname && 
	    strcmp(functionname, syms[sym_idx].symbol->name)) {
		ret = false;
	}

	/* binutils 2.12 and below have a small bug where functions without a
	 * debug entry at the prologue start do not give a useful line number
	 * from bfd_find_nearest_line(). This can happen with certain gcc
	 * versions such as 2.95.
	 *
	 * We work around this problem by scanning forward for a vma with 
	 * valid linenr info, if we can't get a valid line number.
	 * Problem uncovered by Norbert Kaufmann. The work-around decreases,
	 * on the tincas application, the number of failure to retrieve linenr
	 * info from 835 to 173. Most of the remaining are c++ inline functions
	 * mainly from the STL library. Fix #529622
	 */
	if (/*ret == false || */linenr == 0) {
		// FIXME: looking at debug info for all gcc version shows
		// than the same problems can -perhaps- occur for epilog code:
		// find a samples files with samples in epilog and try oprofpp
		// -L -o on it, check it also with op_to_source.

		// first restrict the search on a sensible range of vma,
		// 16 is an intuitive value based on epilog code look
		size_t max_search = 16;
		size_t section_size = bfd_section_size(ibfd, section);
		if (pc + max_search > section_size)
			max_search = section_size - pc;

		for (size_t i = 1 ; i < max_search ; ++i) {
			bool ret = bfd_find_nearest_line(ibfd, section,
							 bfd_syms, pc+i,
							 &filename,
							 &functionname,
							 &linenr);

			if (ret == true && linenr != 0 &&
			    strcmp(functionname,
				   syms[sym_idx].symbol->name) == 0) {
				return ret;	// we win
			}
		}

		// We lose it's worthwhile to try more.

		// bfd_find_nearest_line clobber the memory pointed by filename
		// from a previous call when the filename change across
		// multiple calls. The more easy way to recover is to reissue
		// the first call, we don't need to recheck return value, we
		// know that the call will succeed.
		bfd_find_nearest_line(ibfd, section, bfd_syms, pc,
				      &filename, &functionname, &linenr);
	}

	return ret;
}

// #define USE_ELF_INTERNAL

#ifdef USE_ELF_INTERNAL
struct elf_internal_sym {
  bfd_vma	st_value;		/* Value of the symbol */
  bfd_vma	st_size;		/* Associated symbol size */
  unsigned long	st_name;		/* Symbol name, index in string tbl */
  unsigned char	st_info;		/* Type and binding attributes */
  unsigned char	st_other;		/* No defined meaning, 0 */
  unsigned short st_shndx;		/* Associated section index */
};

typedef struct elf_internal_sym Elf_Internal_Sym;

typedef struct
{
  /* The BFD symbol.  */
  asymbol symbol;
  /* ELF symbol information.  */
  Elf_Internal_Sym internal_elf_sym;
} elf_symbol_type;

#endif /* USE_ELF_INTERNAL */

size_t op_bfd::symbol_size(symbol_index_t sym_idx) const
{
	asymbol * next, *sym;

	sym = syms[sym_idx].symbol;
	next = (sym_idx == syms.size() - 1) ? NULL : syms[sym_idx + 1].symbol;

	u32 start = sym->section->filepos + sym->value;
	size_t length;

#ifndef USE_ELF_INTERNAL
	u32 end;
	if (next) {
		end = next->value;
		/* offset of section */
		end += next->section->filepos;
	} else
		end = nr_samples;

	length = end - start;
#else /* !USE_ELF_INTERNAL */
	size_t length =
		((elf_symbol_type *)sym)->internal_elf_sym.st_size;

	// some asm symbol can have a zero length such system_call
	// entry point in vmlinux. Calculate the length from the next
	// symbol vma
	if (length == 0) {
		u32 next_offset = start;
		if (next) {
			next_offset = next->value + next->section->filepos;
		} else {
			next_offset = nr_samples;
		}
		length = next_offset - start;
	}
#endif /* USE_ELF_INTERNAL */

	return length;
}

void op_bfd::get_symbol_range(symbol_index_t sym_idx,
			      u32 & start, u32 & end) const
{
	asymbol *sym = syms[sym_idx].symbol;

	verbprintf("Symbol %s, value 0x%lx\n", sym->name, sym->value); 
	start = sym->value;
	/* offset of section */
	start += sym->section->filepos;
	verbprintf("in section %s, filepos 0x%lx\n", sym->section->name, sym->section->filepos);

	end = start + syms[sym_idx].size;
	verbprintf("start 0x%x, end 0x%x\n", start, end); 

	if (start >= nr_samples + sect_offset) {
		fprintf(stderr,"oprofpp: start 0x%x out of range (max 0x%x)\n", start, nr_samples);
		exit(EXIT_FAILURE);
	}

	if (end > nr_samples + sect_offset) {
		fprintf(stderr,"oprofpp: end 0x%x out of range (max 0x%x)\n", end, nr_samples);
		exit(EXIT_FAILURE);
	}

	if (start > end) {
		fprintf(stderr,"oprofpp: start 0x%x overflow or end 0x%x underflow\n", start, end);
		exit(EXIT_FAILURE);
	}
}

/**
 * symbol_index - find a symbol
 * @param name the symbol name
 *
 * find and return the index of a symbol.
 * if the name is not found -1 is returned
 */
symbol_index_t op_bfd::symbol_index(char const * symbol) const
{
	for (symbol_index_t i = 0; i < syms.size(); i++) {
		if (!strcmp(syms[i].symbol->name, symbol))
			return i;
	}

	return nil_symbol_index;
}
