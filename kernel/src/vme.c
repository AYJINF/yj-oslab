#include "klib.h"
#include "vme.h"
#include "proc.h"

static TSS32 tss;

void init_gdt() {
  static SegDesc gdt[NR_SEG];
  gdt[SEG_KCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_KERN);
  gdt[SEG_KDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_KERN);
  gdt[SEG_UCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_USER);
  gdt[SEG_UDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_USER);
  gdt[SEG_TSS]   = SEG16(STS_T32A,     &tss,  sizeof(tss)-1, DPL_KERN);
  set_gdt(gdt, sizeof(gdt[0]) * NR_SEG);
  set_tr(KSEL(SEG_TSS));
}

void set_tss(uint32_t ss0, uint32_t esp0) {
  tss.ss0 = ss0;
  tss.esp0 = esp0;
}

static PD kpd;
static PT kpt[PHY_MEM / PT_SIZE] __attribute__((used));

typedef union free_page {
  union free_page *next;
} page_t;

page_t *free_page_list;

// 设置pte的p位
void set_pte_p(PTE *pte, uint32_t p){
  pte->present &= 0;
  pte->present |= p;
  set_cr3(vm_curr());
}

void init_page() {
  extern char end;
  panic_on((size_t)(&end) >= KER_MEM - PGSIZE, "Kernel too big (MLE)");
  static_assert(sizeof(PTE) == 4, "PTE must be 4 bytes");
  static_assert(sizeof(PDE) == 4, "PDE must be 4 bytes");
  static_assert(sizeof(PT) == PGSIZE, "PT must be one page");
  static_assert(sizeof(PD) == PGSIZE, "PD must be one page");
  // Lab1-4: init kpd and kpt, identity mapping of [0 (or 4096), PHY_MEM)
  // TODO(); // DONE 初始化内核页目录和页表项
  for(int i = 0; i < 32; i++){
    kpd.pde[i].val = MAKE_PDE(&kpt[i], 3);
    for(int j = 0; j < NR_PTE; j++){
      kpt[i].pte[j].val = MAKE_PTE((i << DIR_SHIFT) | (j << TBL_SHIFT), 3);
    }
  }
  kpt[0].pte[0].val = 0;
  set_cr3(&kpd);
  set_cr0(get_cr0() | CR0_PG);
  
  // Lab1-4: init free memory at [KER_MEM, PHY_MEM), a heap for kernel
  // TODO(); // DONE 初始化[KER_MEM, PHY_MEM)的物理空闲页链表
  uint32_t ptr_page = KER_MEM; // 空闲页地址
  free_page_list = (void *)ptr_page; // 初始化第一页空闲页
  page_t *free_pages = free_page_list;
  // int page_number = 1; // 初始化空闲页数目

  while(ptr_page < PHY_MEM - PGSIZE){
    ptr_page += PGSIZE;
    page_t *new_page = (void *)ptr_page;
    // Log("free_pages=%p, new_pages=%p\n", free_pages, new_page);
    free_pages->next = new_page;
    // Log("num=%u\n", page_number);
    free_pages = new_page;
    // page_number++;
  }
  free_pages->next = NULL;
  // Log("page_number=%u\n", page_number);
}

// Lab1-4: 分配一页（4KiB）内存并返回其地址，要求分配出的内存按页对齐
void *kalloc() {
  // TODO();
  // Log("kalloc %x------>%x\n", free_page_list, free_page_list->next);
  if(free_page_list == NULL){
    Log("There are no free pages!\n");
    assert(0);
  }
  void *ret = free_page_list;
  // Log("kallllllllllllllllllllllllllll\n");

  kpt[ADDR2DIR(ret)].pte[ADDR2TBL(ret)].present = 1;

  free_page_list = free_page_list->next;

  // Log("before %x------>%x\n", free_page_list, free_page_list->next);
  memset(ret, 0, PGSIZE);
  // Log("afterr %x------>%x\n", free_page_list, free_page_list->next);
  // Log("ret %x\n", ret);

  return ret;
}

// Lab1-4: 回收ptr指向的那一页内存，要求ptr指向之前分配出去的某一页
void kfree(void *ptr) {
  //TODO();
  assert(ptr >= (void *)KER_MEM);
  assert(ptr < (void *)PHY_MEM);
  page_t *page = (void *)(PAGE_DOWN(ptr));
  // Log("kfree %x------------------------->%x\n", page, free_page_list);
  // Log("kfree %x------------------------->%x\n", free_page_list, free_page_list->next);

  page_t *free_pg = free_page_list;
  // int number = 0;
  while(free_pg){
    if((void *)free_pg == (void *)page) {
      return;
    }
    else {
      kpt[ADDR2DIR(free_pg)].pte[ADDR2TBL(free_pg)].present = 1;
      free_pg = free_pg->next;
      kpt[ADDR2DIR(free_pg)].pte[ADDR2TBL(free_pg)].present = 0;
    }
  }
  // Log("!!!!!!!!!free_p=%x\n", page);
  memset((void *)page, 0, PGSIZE);

  page->next = free_page_list;
  free_page_list = page;

  // Log("free_page_list=%x\n", free_page_list);
  // Log("free_page_list->next=%x\n", free_page_list->next);
  kpt[ADDR2DIR(page)].pte[ADDR2TBL(page)].present = 0;
  set_cr3(vm_curr());
}

// Lab1-4: 返回一个用户页目录，并映射[0, PHY_MEM)的恒等映射
PD *vm_alloc() {
  // TODO(); 返回一个用户页目录
  PD *ret = kalloc();
  for(int i = 0; i < 32; i++){
    ret->pde[i].val = MAKE_PDE(&kpt[i], 3);
  }
  memset(&(ret->pde[32]), 0, PGSIZE - 32 * 4); // not sure
  
  set_cr3(vm_curr());
  return ret;
}

// Lab1-4: 把pgdir这一页目录下下辖的所有页表，和所映射到的所有物理页全部kfree（除了内核区）用的时候要改有效位
void vm_teardown(PD *pgdir) {
  // //TODO();
  for(int i = 32; i < NR_PDE; i++){
    if(pgdir->pde[i].present == 1){
      for(int j = 0; j < NR_PTE; j++){
        if(PDE2PT(pgdir->pde[i])->pte[j].present == 1){
          kfree((void *)PTE2PG(PDE2PT(pgdir->pde[i])->pte[j])); // 释放物理页 not sure
        }
      }
      kfree((void *)PDE2PT(pgdir->pde[i])); // 释放pde[i]对应的页表
    }
  }
  kfree((void *)pgdir); // 释放页目录页
}

PD *vm_curr() {
  return (PD*)PAGE_DOWN(get_cr3());
}

// Lab1-4: 返回pgdir这一页目录下，指向va这一虚拟地址对应的PTE的指针
PTE *vm_walkpte(PD *pgdir, size_t va, int prot) {
  assert((prot & ~7) == 0);
  int pd_index = ADDR2DIR(va); // 计算“页目录号”
  PDE *pde = &(pgdir->pde[pd_index]); // 找到对应的页目录项
  if(pde->present == 0){
    if((prot&1) == 0) return NULL;
    PT *new_pt = kalloc();
    pde->val = MAKE_PDE(new_pt, prot);
    pde->present |= 1;
    int pt_index = ADDR2TBL(va); // 计算“页表号”
    return &(new_pt->pte[pt_index]); // 返回对应的页表项
  }
  if(prot != 0) pde->val |= prot; // not sure
  PT *pt = PDE2PT(*pde); // 根据PDE找页表的地址
  int pt_index = ADDR2TBL(va); // 计算“页表号”
  return &(pt->pte[pt_index]); // 返回对应的页表项
}

// Lab1-4: 返回pgdir这一页目录下，va这一虚拟地址对应的物理地址，如果没有被映射返回NULL
void *vm_walk(PD *pgdir, size_t va, int prot) {
  // if prot&1 and prot voilation ((pte->val & prot & 7) != prot), call vm_pgfault
  // if va is not mapped and !(prot&1), return NULL
  // TODO(); // to be perfected
  PTE *va_pte = vm_walkpte(pgdir, va, prot);
  if(va_pte == NULL || va_pte->present == 0) return NULL;
  void *page = PTE2PG(*va_pte); // 根据PTE找物理页的地址
  if(!(va_pte->page_frame) || !(prot&1)) return NULL;
  void *pa = (void*)((uint32_t)page | ADDR2OFF(va)); // 补上页内偏移量
  
  return pa;
}

// Lab1-4: 在pgdir这一页目录下，添加虚拟地址[PAGE_DOWN(va), PAGE_UP(va+len))这一范围的映射，设置这些页的权限为prot
void vm_map(PD *pgdir, size_t va, size_t len, int prot) {
  // if have already mapped pages, just let pte->prot |= prot
  assert(prot & PTE_P);
  assert((prot & ~7) == 0);
  size_t start = PAGE_DOWN(va);
  size_t end = PAGE_UP(va + len);
  assert(start >= PHY_MEM);
  assert(end >= start);
  // TODO(); // not sure 

  // Log("wm_map va=%x, len=%x\n", va, len); 

  while(start < end){
    PTE *pte = vm_walkpte(pgdir, start, prot);
    // Log("----------------------------------vm_map pte=%x\n", pte);
    if(pte == NULL){
      start += PGSIZE;
      continue;
    }
    if(vm_walk(pgdir, start, prot) != NULL){
      pte->val |= prot;
      start += PGSIZE;
      continue;
    }
    // Log("--------------------------------------------list=%x\n", free_page_list);
    page_t *new_page = kalloc();
    pte->val = MAKE_PTE(new_page, prot);
    set_pte_p(pte, 1);
    set_cr3(vm_curr());

    // set_pte_P((void *)new_page, 1);
    start += PGSIZE;
  }
}
 
// Lab1-4: 在pgdir这一页目录下，取消[va, va+len)这一范围的映射，并且kfree掉它们映射的物理页
void vm_unmap(PD *pgdir, size_t va, size_t len) { // not sure
  // you can just do nothing :)
  assert(ADDR2OFF(va) == 0);
  assert(ADDR2OFF(len) == 0);
  size_t start = PAGE_DOWN(va);
  size_t end = PAGE_UP(va + len);
  while(start < end){
    PTE *pte = vm_walkpte(pgdir, start, 3); // not sure
    if(pte == NULL){
      // int pd_index = ADDR2DIR(start); // 计算“页目录号”
      // kfree((void *)PDE2PT(pgdir->pde[pd_index])); // 释放pde对应的页表
      start += PGSIZE;
      continue;
    }
    else{
    page_t *paddr_page = vm_walk(pgdir, start, 3);
    // Log("paddr_page=%x\n", paddr_page);
    if(paddr_page != NULL) kfree(paddr_page); // 释放物理页
    pte->page_frame = 0;
    set_pte_p(pte, 0);
    set_cr3(vm_curr());
    // int pd_index = ADDR2DIR(start); // 计算“页目录号”
    // kfree((void *)PDE2PT(pgdir->pde[pd_index])); // 释放pde对应的页表
    start += PGSIZE;
    }
  }
  // Log("unmap free_page_list=%x\n", free_page_list);
  // Log("unmap free_page_list->next=%x\n", free_page_list->next);
  //TODO();
}

// 复制当前的虚拟地址空间到pgdir这个页目录，调用时要求pgdir刚vm_alloc出来，只有[0, PHY_MEM)的恒等映射
void vm_copycurr(PD *pgdir) {
  // Lab2-2: copy memory mapped in curr pd to pgdir
  // TODO();
  PD *cur_pgdir = vm_curr();
  for(size_t old_va = PHY_MEM; old_va < USR_MEM; old_va += PGSIZE){
    PTE *va_pte = vm_walkpte(cur_pgdir, old_va, 0);
    if(!va_pte) continue;
    if(va_pte->present){
      int prot = (va_pte->val)&0x7;
      vm_map(pgdir, old_va, PGSIZE, prot);
      page_t *new_pa = vm_walk(pgdir, old_va, prot);
      memcpy((void *)new_pa, (void *)old_va, PGSIZE);
    }
  }
}

void vm_pgfault(size_t va, int errcode) {
  printf("pagefault @ 0x%p, errcode = %d\n", va, errcode);
  panic("pgfault");
}
