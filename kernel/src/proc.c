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
  sem_init(&(curr->zombie_sem), 0);
  // curr->zombie_sem.value = 0;
  // list_init(&(curr->zombie_sem.wait_list));
  // Lab2-5, init usem (not sure)
  for(int i = 0; i < MAX_USEM; i++){
    curr->usems[i] = NULL;
  }
  // Lab3-2, set cwd
  inode_t *pcb_cwd = iopen("/", TYPE_NONE);
  pcb[0].cwd = pcb_cwd;
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
      // lab 2-4-2
      sem_init(&(free_pcb->zombie_sem), 0);
      // lab 2-5
      for(int j = 0; j < MAX_USEM; j++){
        free_pcb->usems[j] = NULL;
      }
      //lab 3-1
      for(int k = 0; k < MAX_UFILE; k++){
        free_pcb->files[k] = NULL;
      }
      //lab 3-2-4
      free_pcb->cwd = NULL;
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
  //lab 3-2-4
  proc->cwd = idup(proc_cur->cwd); // not sure 

  for(int i = 0; i < MAX_USEM; i++){
    proc->usems[i] = proc_cur->usems[i];
    if(proc_cur->usems[i] != NULL){ // not sure
      proc_cur->usems[i] = usem_dup(proc_cur->usems[i]);
    }
  }

  for(int j = 0; j < MAX_UFILE; j++){
    proc->files[j] = proc_cur->files[j];
    if(proc_cur->files[j] != NULL){
      proc_cur->files[j] = fdup(proc_cur->files[j]);
    }
  }
}

// 将proc的状态标记为ZOMBIE，退出状态记录为exitcode
void proc_makezombie(proc_t *proc, int exitcode) {
  // Lab2-3: mark proc ZOMBIE and record exitcode, set children's parent to NULL
  // Lab2-5: close opened usem
  // Lab3-1: close opened files
  // Lab3-2: close cwd
  // TODO();
  if(proc->parent != NULL){
    sem_v(&(proc->parent->zombie_sem));
  }
  proc->status = ZOMBIE;
  proc->exit_code = exitcode;

  for(int i = 0; i < MAX_USEM; i++){
    usem_t *tmp_usem = proc->usems[i];
    if(tmp_usem != NULL) usem_close(tmp_usem); // not sure
  }

  for(int i = 0; i < MAX_UFILE; i++){
    file_t *tmp_file = proc->files[i];
    if(tmp_file != NULL) fclose(tmp_file);
  }

  for(int i = 0; i < PROC_NUM; i++){
    proc_t *tmp_pcb = &pcb[i];
    if(tmp_pcb->parent == proc){
      tmp_pcb->parent = NULL;
    }
  }
  iclose(proc->cwd);
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

// Lab2-4: mark curr proc BLOCKED, then int $0x81
void proc_block() {
  curr->status = BLOCKED;
  INT(0x81);
}

// 遍历proc的用户信号量表，找到一个空的（即为NULL）的下标并返回，没有空的返回-1
int proc_allocusem(proc_t *proc) {
// Lab2-5: find a free slot in proc->usems, return its index, or -1 if none
  // TODO();
  for(int i = 0; i < MAX_USEM; i++){
    if(proc->usems[i] == NULL) return i;
  }
  return -1;
}

// 返回proc用户信号量表中第sem_id项对应的用户信号量，如果下标越界返回NULL. 
usem_t *proc_getusem(proc_t *proc, int sem_id) {
  // Lab2-5: return proc->usems[sem_id], or NULL if sem_id out of bound
  // TODO();
  if(sem_id >= MAX_USEM) return NULL;
  return proc->usems[sem_id];
}

// 遍历用户打开文件表，找到空闲的最小下标并返回，没有空的返回-1
int proc_allocfile(proc_t *proc) {
  // Lab3-1: find a free slot in proc->files, return its index, or -1 if none
  // TODO();
  for(int i = 0; i < MAX_UFILE; i++){
    if(proc->files[i] == NULL) return i;
  }
  return -1;
}

// 返回用户打开文件表第fd项对应文件，或NULL如果fd越界
file_t *proc_getfile(proc_t *proc, int fd) {
  // Lab3-1: return proc->files[fd], or NULL if fd out of bound
  // TODO();
  if(fd >= MAX_UFILE) return NULL;
  return proc->files[fd];
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
