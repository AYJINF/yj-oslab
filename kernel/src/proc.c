#include "klib.h"
#include "cte.h"
#include "proc.h"

#define PROC_NUM 64

static __attribute__((used)) int next_pid = 1;

proc_t pcb[PROC_NUM];
static proc_t *curr = &pcb[0];

void init_proc() {
  // Lab2-1, set status and pgdir
  curr->status = RUNNING;
  curr->pgdir = vm_curr();
  curr->kstack = (void*)(KER_MEM-PGSIZE);
  // Lab2-4, init zombie_sem
  // Lab3-2, set cwd
}

proc_t *proc_alloc() {
  // Lab2-1: find a unused pcb from pcb[1..PROC_NUM-1], return NULL if no such one
  // TODO();
  proc_t *free_pcb = NULL;
  for(int i = 1; i < PROC_NUM; i++){
    proc_t *tmp_pcb = &pcb[i];
    if(tmp_pcb->status == UNUSED) {
      free_pcb = tmp_pcb;
      free_pcb->pid = next_pid;
      next_pid = (next_pid % 32767) + 1;
      free_pcb->status = UNINIT;
      free_pcb->pgdir = vm_alloc();
      free_pcb->brk = 0;
      free_pcb->kstack = (kstack_t *)kalloc();
      free_pcb->ctx = &(free_pcb->kstack->ctx);
      free_pcb->parent = NULL;
      free_pcb->child_num = 0;
      break;
    }
  }
  return free_pcb;  
  // init ALL attributes of the pcb
}

void proc_free(proc_t *proc) {
  // Lab2-1: free proc's pgdir and kstack and mark it UNUSED
  // TODO();
  if(proc->status != RUNNING){
    vm_teardown(proc->pgdir);
    kfree(proc->kstack);
    proc->status = UNUSED;
  }
}

proc_t *proc_curr() {
  return curr;
}

void proc_run(proc_t *proc) {
  proc->status = RUNNING;
  curr = proc;
  set_cr3(proc->pgdir);
  set_tss(KSEL(SEG_KDATA), (uint32_t)STACK_TOP(proc->kstack));
  irq_iret(proc->ctx);
}

// 把proc进程标记为READY
void proc_addready(proc_t *proc) {
  // Lab2-1: mark proc READY
  proc->status = READY;
}

void proc_yield() {
  // Lab2-1: mark curr proc READY, then int $0x81
  curr->status = READY;
  INT(0x81);
}

// 复制当前进程的地址空间和上下文的状态到proc这个进程中，要求proc是刚proc_alloc出来，还没进一步初始化的PCB
void proc_copycurr(proc_t *proc) {
  // Lab2-2: copy curr proc
  // Lab2-5: dup opened usems
  // Lab3-1: dup opened files
  // Lab3-2: dup cwd
  // TODO();
  proc_t *proc_cur = proc_curr();
  PD *pgdir = vm_alloc();
  vm_copycurr(pgdir);
  proc->pgdir = pgdir;
  proc->brk = proc_cur->brk;
  proc->kstack->ctx = proc_cur->kstack->ctx;
  proc->kstack->ctx.eax = 0;
  proc->parent = proc_cur;
  proc_cur->child_num++;
}

// 将proc的状态标记为ZOMBIE，退出状态记录为exitcode
void proc_makezombie(proc_t *proc, int exitcode) {
  // Lab2-3: mark proc ZOMBIE and record exitcode, set children's parent to NULL
  // Lab2-5: close opened usem
  // Lab3-1: close opened files
  // Lab3-2: close cwd
  // TODO();
  proc->status = ZOMBIE;
  proc->exit_code = exitcode;
  for(int i = 0; i < PROC_NUM; i++){
    proc_t *tmp_pcb = &pcb[i];
    if(tmp_pcb->parent == proc){
      tmp_pcb->parent = NULL;
    }
  }
}

// 遍历pcb找一个proc的子僵尸进程，如果不存在就返回NULL
proc_t *proc_findzombie(proc_t *proc) {
  // Lab2-3: find a ZOMBIE whose parent is proc, return NULL if none
  // TODO();
  if(proc->child_num == 0) return NULL;
  for(int i = 1; i < PROC_NUM; i++){
    proc_t *tmp_pcb = &pcb[i];
    if(tmp_pcb->status == ZOMBIE && tmp_pcb->parent == proc){
      return tmp_pcb;
    }
  }
  return NULL;
}

void proc_block() {
  // Lab2-4: mark curr proc BLOCKED, then int $0x81
  curr->status = BLOCKED;
  INT(0x81);
}

int proc_allocusem(proc_t *proc) {
  // Lab2-5: find a free slot in proc->usems, return its index, or -1 if none
  TODO();
}

usem_t *proc_getusem(proc_t *proc, int sem_id) {
  // Lab2-5: return proc->usems[sem_id], or NULL if sem_id out of bound
  TODO();
}

int proc_allocfile(proc_t *proc) {
  // Lab3-1: find a free slot in proc->files, return its index, or -1 if none
  TODO();
}

file_t *proc_getfile(proc_t *proc, int fd) {
  // Lab3-1: return proc->files[fd], or NULL if fd out of bound
  TODO();
}

void schedule(Context *ctx) {
  // Lab2-1: save ctx to curr->ctx, then find a READY proc and run it
  // TODO();
  proc_t *proc_cur = proc_curr();
  proc_cur->ctx = ctx;
  proc_t *tmp_pcb = proc_cur + 1;
  while(tmp_pcb <= &pcb[PROC_NUM-1]){
    if(tmp_pcb->status == READY){
      proc_run(tmp_pcb);
    }
    tmp_pcb++;
  }

  tmp_pcb = &pcb[0]; // not sure
  while(tmp_pcb <= proc_cur){
    if(tmp_pcb->status == READY){
      proc_run(tmp_pcb);
    }
    tmp_pcb++;
  }
}
