#include "fishhook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

// This is a compact/adapted subset of fishhook suitable for our use.
// It will only handle rebinding in __DATA,__la_symbol_ptr and __DATA,__nl_symbol_ptr
// in 32/64-bit Mach-O for iOS. It's not exhaustive but works for common apps.

#if defined(__LP64__)
typedef struct mach_header_64 mach_header_t;
typedef struct nlist_64 nlist_t;
#define LC_SEGMENT_OTHER LC_SEGMENT_64
#else
typedef struct mach_header mach_header_t;
typedef struct nlist nlist_t;
#endif

// Simple dynamic array for rebindings
struct rebindings_entry {
    struct rebinding *rebindings;
    size_t rebindings_nel;
    struct rebindings_entry *next;
};

static struct rebindings_entry *_rebindings_head;

static int prepend_rebindings(struct rebindings_entry **head, struct rebinding rebindings[], size_t nel) {
    struct rebindings_entry *new_entry = (struct rebindings_entry*)malloc(sizeof(struct rebindings_entry));
    if (!new_entry) return -1;
    new_entry->rebindings = (struct rebinding*)malloc(sizeof(struct rebinding)*nel);
    if (!new_entry->rebindings) { free(new_entry); return -1; }
    memcpy(new_entry->rebindings, rebindings, sizeof(struct rebinding)*nel);
    new_entry->rebindings_nel = nel;
    new_entry->next = *head;
    *head = new_entry;
    return 0;
}

#include <mach-o/loader.h>
#include <mach-o/dyld.h>

static void perform_rebinding_with_section(struct rebindings_entry *rebindings, section_t *section, intptr_t slide, nlist_t *symtab, char *strtab, void **indirect_symbol_table, uint32_t *indirectsym) {
    uint32_t *indirect_symbol_indices = (uint32_t*)(indirect_symbol_table);
    // number of pointers
    uint64_t indirect_count = section->size / sizeof(void*);
    void **indirect_ptr = (void**)((uintptr_t)slide + section->addr);
    for (uint64_t i = 0; i < indirect_count; i++) {
        uint32_t sym_index = indirectsym[i + section->reserved1];
        if (sym_index == INDIRECT_SYMBOL_ABS || sym_index == INDIRECT_SYMBOL_LOCAL || sym_index == (uint32_t)UINT32_MAX) {
            // skip
        } else {
            uint32_t strtab_offset = symtab[sym_index].n_un.n_strx;
            char *name = &strtab[strtab_offset];
            struct rebindings_entry *cur = rebindings;
            for (; cur != NULL; cur = cur->next) {
                for (size_t j = 0; j < cur->rebindings_nel; j++) {
                    if (strcmp(name, cur->rebindings[j].name) == 0) {
                        // change pointer
                        // make pages writable
                        uintptr_t page = (uintptr_t)&indirect_ptr[i] & ~(getpagesize() - 1);
                        mprotect((void*)page, getpagesize(), PROT_READ | PROT_WRITE);
                        void *old = indirect_ptr[i];
                        if (cur->rebindings[j].replaced) {
                            *(cur->rebindings[j].replaced) = old;
                        }
                        indirect_ptr[i] = cur->rebindings[j].replacement;
                        // restore pages read-only
                        mprotect((void*)page, getpagesize(), PROT_READ);
                    }
                }
            }
        }
    }
}

int rebind_symbols(struct rebinding rebindings[], size_t rebindings_nel) {
    int retval = prepend_rebindings(&_rebindings_head, rebindings, rebindings_nel);
    if (retval < 0) return retval;

    uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; i++) {
        const mach_header_t *header = (const mach_header_t *)_dyld_get_image_header(i);
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);

#if defined(__LP64__)
        struct load_command *cmd = (struct load_command *)((char *)header + sizeof(mach_header_t));
#else
        struct load_command *cmd = (struct load_command *)((char *)header + sizeof(mach_header_t));
#endif
        for (uint32_t cmd_i = 0; cmd_i < header->ncmds; cmd_i++, cmd = (struct load_command *)((char *)cmd + cmd->cmdsize)) {
            if (cmd->cmd == LC_SEGMENT) {
                struct segment_command *seg_cmd = (struct segment_command*)cmd;
                if (strcmp(seg_cmd->segname, "__DATA") != 0) continue;
                struct section *sect = (struct section *)((char *)seg_cmd + sizeof(struct segment_command));
                for (uint32_t sect_i=0; sect_i < seg_cmd->nsects; sect_i++, sect = (struct section *)((char *)sect + sizeof(struct section))) {
                    if (strcmp(sect->sectname, "__la_symbol_ptr") != 0 && strcmp(sect->sectname, "__nl_symbol_ptr") != 0) continue;
                    // get symbol table and strtab from LC_SYMTAB
                    struct symtab_command *symtab_cmd = NULL;
                    struct dysymtab_command *dysymtab_cmd = NULL;
                    struct load_command *lc = (struct load_command *)((char *)header + sizeof(mach_header_t));
                    for (uint32_t k = 0; k < header->ncmds; k++, lc = (struct load_command *)((char *)lc + lc->cmdsize)) {
                        if (lc->cmd == LC_SYMTAB) symtab_cmd = (struct symtab_command*)lc;
                        if (lc->cmd == LC_DYSYMTAB) dysymtab_cmd = (struct dysymtab_command*)lc;
                    }
                    if (!symtab_cmd || !dysymtab_cmd) continue;
                    nlist_t *symtab = (nlist_t *)((char *)header + symtab_cmd->symoff);
                    char *strtab = (char *)header + symtab_cmd->stroff;
                    // indirect symbol table
                    uint32_t *indirect_symbol_table = (uint32_t *)((char *)header + dysymtab_cmd->indirectsymoff);
                    uint32_t *indirectsym = (uint32_t *)((char *)header + sect->reserved2);
                    perform_rebinding_with_section(_rebindings_head, (section_t*)sect, slide, symtab, strtab, (void**)indirect_symbol_table, indirectsym);
                }
            }
        }
    }
    return 0;
}