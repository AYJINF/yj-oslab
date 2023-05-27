#ifndef __FS_H__
#define __FS_H__

#include <stdint.h>

typedef struct inode inode_t;

// #define EASY_FS // TODO: comment me at Lab3-2

void init_fs();

// 打开名字为path的文件，返回代表这个文件的inode（或NULL如果不存在）
inode_t *iopen(const char *path, int type);

// 从inode代表的文件的off偏移量处，读取len字节到内存的buf里，返回读取的字节数（或-1如果失败）
int iread(inode_t *inode, uint32_t off, void *buf, uint32_t len);

// 从内存的buf里，写len字节到inode代表的文件的off偏移量处，返回写入的字节数（或-1如果失败）
int iwrite(inode_t *inode, uint32_t off, const void *buf, uint32_t len);

void itrunc(inode_t *inode);
inode_t *idup(inode_t *inode);

// 关闭inode代表的文件
void iclose(inode_t *inode);

// 返回inode代表的文件的大小
uint32_t isize(inode_t *inode);

// 返回inode代表的文件的类型
int itype(inode_t *inode);

// 返回inode代表的文件的inode标号
uint32_t ino(inode_t *inode);

// 如果inode代表的文件是设备文件，返回其设备号，否则返回-1
int idevid(inode_t *inode);

// 向文件系统中注册一个设备，名字为name，设备号为id
void iadddev(const char *name, int id);

int iremove(const char *path);

#ifdef EASY_FS

#define MAX_NAME  (31 - 2 * sizeof(uint32_t))

#else

#define MAX_NAME  (31 - sizeof(uint32_t))

typedef struct dirent {
  uint32_t inode;
  char name[MAX_NAME + 1];
} dirent_t;

#endif

#endif
