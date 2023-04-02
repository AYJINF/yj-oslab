#include "boot.h"
// DO NOT DEFINE ANY NON-LOCAL VARIBLE!

void load_kernel() {
  // char hello[] = {'\n', 'h', 'e', 'l', 'l', 'o', '\n', 0};
  // putstr(hello);
  // while (1) ;
  // remove both lines above before write codes below
  Elf32_Ehdr *elf = (void *)0x8000;
  copy_from_disk(elf, 255 * SECTSIZE, SECTSIZE);

  // assert(*(uint32_t *)elf->e_ident == 0x464c457f); // not sure 检查魔数

  Elf32_Phdr *ph, *eph;
  ph = (void*)((uint32_t)elf + elf->e_phoff); // 程序头表的偏移量
  eph = ph + elf->e_phnum; // 最后一个程序头表的末尾
  
  for (; ph < eph; ph++) {
    if (ph->p_type == PT_LOAD) {
      memcpy((void *)ph->p_vaddr, (void *)((uint32_t)elf + ph->p_offset), ph->p_filesz);
      memset((void *)ph->p_vaddr + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }
  }
  uint32_t entry = elf->e_entry; // change me
  ((void(*)())entry)();
}
