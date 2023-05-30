#include "klib.h"
#include "cte.h"
#include "sysnum.h"
#include "vme.h"
#include "serial.h"
#include "loader.h"
#include "proc.h"
#include "timer.h"
#include "file.h"

typedef int (*syshandle_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

extern void *syscall_handle[NR_SYS];

void do_syscall(Context *ctx) {
  // TODO: Lab1-5 call specific syscall handle and set ctx register
  int sysnum = ctx->eax;
  uint32_t arg1 = ctx->ebx;
  uint32_t arg2 = ctx->ecx;
  uint32_t arg3 = ctx->edx;
  uint32_t arg4 = ctx->esi;
  uint32_t arg5 = ctx->edi;
  int res;
  if (sysnum < 0 || sysnum >= NR_SYS) {
    res = -1;
  } else {
    res = ((syshandle_t)(syscall_handle[sysnum]))(arg1, arg2, arg3, arg4, arg5);
  }
  ctx->eax = res;
}

// 从buf写count字节到fd表示的文件，返回写的字节数（或-1如果失败）
int sys_write(int fd, const void *buf, size_t count) {
  // TODO: rewrite me at Lab3-1
  proc_t *proc_cur = proc_curr();
  file_t *file = proc_getfile(proc_cur, fd);
  if(file == NULL) return -1;
  int ret = fwrite(file, buf, count);
  return ret;
}

// 从fd表示的文件读count字节到buf，返回读的字节数（或-1如果失败）
int sys_read(int fd, void *buf, size_t count) {
  // TODO: rewrite me at Lab3-1
  proc_t *proc_cur = proc_curr();
  file_t *file = proc_getfile(proc_cur, fd);
  if(file == NULL) return -1;
  int ret = fread(file, buf, count);
  // Log("rrrrrrrrrrrret=%d\n", ret);
  return ret;
}

int sys_brk(void *addr) {
  // TODO: Lab1-5
  proc_t *proc_cur = proc_curr();
  // static size_t brk = 0; // use brk of proc instead of this in Lab2-1
  size_t new_brk = PAGE_UP(addr);

  // Log("brk=%x, new_brk=%x\n", brk, new_brk);

  if (proc_cur->brk == 0) {
    proc_cur->brk = new_brk;
  } 
  else if (new_brk > proc_cur->brk) {
    // TODO(); 
    vm_map(vm_curr(), proc_cur->brk, new_brk - proc_cur->brk, 7);
    proc_cur->brk = new_brk;
  } 
  else if (new_brk < proc_cur->brk) {
    // can just do nothing
    vm_unmap(vm_curr(), new_brk, proc_cur->brk - new_brk); // not sure
    proc_cur->brk = new_brk;
  }
  return 0;
}

void sys_sleep(int ticks) {
  // TODO(); // Lab1-7
  uint32_t pos_tick = get_tick();
  uint32_t cur_tick = get_tick();
  while(cur_tick < pos_tick + ticks){
    proc_yield();
    cur_tick = get_tick();
  }
}

int sys_exec(const char *path, char *const argv[]) {
  // TODO(); // Lab1-8, Lab2-1
  PD *pgdir = vm_alloc();
  Context ctx;
  if (load_user(pgdir, &ctx, path, argv) != 0){
    kfree((void *)pgdir);
    return -1;
  }
  PD *old_pgdir = vm_curr();
  set_cr3(pgdir);

  proc_t * proc_cur = proc_curr(); // lab2-1
  proc_cur->pgdir = pgdir;

  kfree(old_pgdir);
  irq_iret(&ctx);
}

int sys_getpid() {
  // TODO(); // Lab2-1
  proc_t * proc_cur = proc_curr(); // lab2-1
  return proc_cur->pid;
}

void sys_yield() {
  proc_yield();
}

int sys_fork() {
  // TODO(); // Lab2-2
  proc_t *proc = proc_alloc();
  if(!proc) return -1;
  proc_copycurr(proc);
  proc_addready(proc);
  // Log("fork pid=%d\n", proc->pid);
  return proc->pid;
}

void sys_exit(int status) {
  // TODO(); // Lab2-3
  proc_t *proc_cur = proc_curr();
  proc_makezombie(proc_cur, status);
  INT(0x81);
  assert(0);
}

int sys_wait(int *status) {
  // TODO(); // Lab2-3, Lab2-4
  proc_t *proc_cur = proc_curr();
  if(proc_cur->child_num == 0) return -1;

  sem_p(&(proc_cur->zombie_sem));
  proc_t *proc_child = proc_findzombie(proc_cur);
  assert(proc_child != NULL);
  if(status) *status = proc_child->exit_code;
  int child_pid = proc_child->pid;
  proc_free(proc_child);
  proc_cur->child_num--;
  return child_pid;
  // while(1){
  //   proc_t *proc_child = proc_findzombie(proc_cur);
  //   if(proc_child){
  //   // Log("proc_pid=%d, child_pid=%d, child_num=%d, child_exitcode=%d\n", proc_cur->pid, proc_child->pid, proc_cur->child_num, proc_child->exit_code);
  //     if(status) *status = proc_child->exit_code;
  //     int child_pid = proc_child->pid;
  //     // Log("child_pid=%d\n", child_pid);
  //     proc_free(proc_child);
  //     proc_cur->child_num--;
  //     return child_pid;
  //   }
  //   else proc_yield();
  // }
  // return 0;
}

// 打开一个初值为value的用户信号量，成功返回其编号，失败返回-1
int sys_sem_open(int value) {
  // TODO(); // Lab2-5
  proc_t *proc_cur = proc_curr();
  int id_sem = proc_allocusem(proc_cur);
  if(id_sem == -1) return -1;
  // Log("open value=%d, id=%d\n", value, id_sem);
  usem_t *usem = usem_alloc(value);
  if(usem == NULL) return -1;
  proc_cur->usems[id_sem] = usem;
  return id_sem;
}

// P编号为sem_id对应的信号量，成功返回0，失败（信号量不存在）返回-1
int sys_sem_p(int sem_id) {
  // TODO(); // Lab2-5
  proc_t *proc_cur = proc_curr();
  usem_t *tmp_usem = proc_getusem(proc_cur, sem_id);
  if(tmp_usem == NULL) return -1;
  // Log("sem value=%d\n", tmp_usem->sem.value);
  sem_p(&(tmp_usem->sem));
  return 0;
}

// V编号为sem_id对应的信号量，成功返回0，失败（信号量不存在）返回-1
int sys_sem_v(int sem_id) {
  // TODO(); // Lab2-5
  proc_t *proc_cur = proc_curr();
  usem_t *tmp_usem = proc_getusem(proc_cur, sem_id);
  if(tmp_usem == NULL) return -1;
  sem_v(&(tmp_usem->sem));
  return 0;
}

// 关闭编号为sem_id对应的信号量，成功返回0，失败（信号量不存在）返回-1
int sys_sem_close(int sem_id) {
  TODO(); // Lab2-5
  proc_t *proc_cur = proc_curr();
  usem_t *tmp_usem = proc_getusem(proc_cur, sem_id);
  if(tmp_usem == NULL) return -1;
  usem_close(tmp_usem);
  proc_cur->usems[sem_id] = NULL;
  return 0;
}

// 打开path代表的文件，返回其文件描述符（要求为可用中最小的），失败返回-1，mode的意义同前面介绍的fopen
int sys_open(const char *path, int mode) {
  // TODO(); // Lab3-1
  proc_t *proc_cur = proc_curr();
  int fd = proc_allocfile(proc_cur);
  if(fd == -1) return -1;
  file_t *file = fopen(path, mode);
  if(file == NULL) return -1;
  // Log("----------type=%d\n", file->type);
  // if(file->type != 3) Log("sys_open size=%d\n", isize(file->inode));
  proc_cur->files[fd] = file;
  return fd;
}

// 关闭fd表示的文件，成功返回0，失败返回-1
int sys_close(int fd) {
  // TODO(); // Lab3-1
  proc_t *proc_cur = proc_curr();
  file_t *file = proc_getfile(proc_cur, fd);
  if(file == NULL) return -1;
  fclose(file);
  proc_cur->files[fd] = NULL;
  return 0;
}

// 复制fd表示的file_t指针到新的文件描述符并返回（要求为可用中最小的），失败返回-1
int sys_dup(int fd) {
  // TODO(); // Lab3-1
  proc_t *proc_cur = proc_curr();
  int new_fd = proc_allocfile(proc_cur);
  if(new_fd == -1) return -1;
  file_t *file = proc_getfile(proc_cur, fd);
  if(file == NULL) return -1;
  proc_cur->files[new_fd] = fdup(file);
  return new_fd;
}

// 调整fd指向的文件的文件的偏移量并返回，whence的意义同前面介绍的fseek，失败返回-1
uint32_t sys_lseek(int fd, uint32_t off, int whence) {
  // TODO(); // Lab3-1
  proc_t *proc_cur = proc_curr();
  file_t *file = proc_getfile(proc_cur, fd);
  if(file == NULL) return -1;
  uint32_t ret = fseek(file, off, whence);
  return ret; // not sure
}

// 记录fd指向的文件的信息于st结构体中，成功返回0，失败返回-1
int sys_fstat(int fd, struct stat *st) {
  // TODO(); // Lab3-1
  proc_t *proc_cur = proc_curr();
  file_t *file = proc_getfile(proc_cur, fd);
  if(file == NULL) return -1;
  int type = file->type;
  if(type == TYPE_FILE || type == TYPE_DIR){
    inode_t *inode = file->inode;
    st->type = itype(inode);
    st->size = isize(inode);
    st->node = ino(inode);
    return 0;
  }
  else if(type == TYPE_DEV){
    st->type = TYPE_DEV;
    st->size = 0;
    st->node = 0;
    return 0;
  }
  return -1;
}

// 改变当前进程的cwd为path这一路径指向的目录，成功返回0，失败返回-1
int sys_chdir(const char *path) {
  // TODO(); // Lab3-2
  inode_t *file_inode = iopen(path, TYPE_NONE);
  if(file_inode == NULL) return -1;
  int type = itype(file_inode);
  if(type != TYPE_DIR){
    iclose(file_inode);
    return -1;
  }
  proc_t *proc_cur = proc_curr();
  iclose(proc_cur->cwd);
  proc_cur->cwd = file_inode;
  return 0;
}

// 删除这个文件
int sys_unlink(const char *path) {
  return iremove(path);
}

// optional syscall

void *sys_mmap() {
  TODO();
}

void sys_munmap(void *addr) {
  TODO();
}

int sys_clone(void (*entry)(void*), void *stack, void *arg) {
  TODO();
}

int sys_kill(int pid) {
  TODO();
}

int sys_cv_open() {
  TODO();
}

int sys_cv_wait(int cv_id, int sem_id) {
  TODO();
}

int sys_cv_sig(int cv_id) {
  TODO();
}

int sys_cv_sigall(int cv_id) {
  TODO();
}

int sys_cv_close(int cv_id) {
  TODO();
}

int sys_pipe(int fd[2]) {
  TODO();
}

int sys_link(const char *oldpath, const char *newpath) {
  TODO();
}

int sys_symlink(const char *oldpath, const char *newpath) {
  TODO();
}

void *syscall_handle[NR_SYS] = {
  [SYS_write] = sys_write,
  [SYS_read] = sys_read,
  [SYS_brk] = sys_brk,
  [SYS_sleep] = sys_sleep,
  [SYS_exec] = sys_exec,
  [SYS_getpid] = sys_getpid,
  [SYS_yield] = sys_yield,
  [SYS_fork] = sys_fork,
  [SYS_exit] = sys_exit,
  [SYS_wait] = sys_wait,
  [SYS_sem_open] = sys_sem_open,
  [SYS_sem_p] = sys_sem_p,
  [SYS_sem_v] = sys_sem_v,
  [SYS_sem_close] = sys_sem_close,
  [SYS_open] = sys_open,
  [SYS_close] = sys_close,
  [SYS_dup] = sys_dup,
  [SYS_lseek] = sys_lseek,
  [SYS_fstat] = sys_fstat,
  [SYS_chdir] = sys_chdir,
  [SYS_unlink] = sys_unlink,
  [SYS_mmap] = sys_mmap,
  [SYS_munmap] = sys_munmap,
  [SYS_clone] = sys_clone,
  [SYS_kill] = sys_kill,
  [SYS_cv_open] = sys_cv_open,
  [SYS_cv_wait] = sys_cv_wait,
  [SYS_cv_sig] = sys_cv_sig,
  [SYS_cv_sigall] = sys_cv_sigall,
  [SYS_cv_close] = sys_cv_close,
  [SYS_pipe] = sys_pipe,
  [SYS_link] = sys_link,
  [SYS_symlink] = sys_symlink};
