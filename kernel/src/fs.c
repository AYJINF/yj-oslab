#include "klib.h"
#include "fs.h"
#include "disk.h"
#include "proc.h"

#ifdef EASY_FS

#define MAX_FILE  (SECTSIZE / sizeof(dinode_t))
#define MAX_DEV   16
#define MAX_INODE (MAX_FILE + MAX_DEV)

// On disk inode
typedef struct dinode {
  uint32_t start_sect;
  uint32_t length;
  char name[MAX_NAME + 1];
} dinode_t;

// On OS inode, dinode with special info
struct inode {
  int valid;
  int type;
  int dev; // dev_id if type==TYPE_DEV
  dinode_t dinode;
};

static inode_t inodes[MAX_INODE];

void init_fs() {
  dinode_t buf[MAX_FILE];
  read_disk(buf, 256);
  for (int i = 0; i < MAX_FILE; ++i) {
    inodes[i].valid = 1;
    inodes[i].type = TYPE_FILE;
    inodes[i].dinode = buf[i];
  }
}

inode_t *iopen(const char *path, int type) {
  for (int i = 0; i < MAX_INODE; ++i) {
    if (!inodes[i].valid) continue;
    if (strcmp(path, inodes[i].dinode.name) == 0) {
      return &inodes[i];
    }
  }
  return NULL;
}

int iread(inode_t *inode, uint32_t off, void *buf, uint32_t len) {
  assert(inode);
  char *cbuf = buf;
  char dbuf[SECTSIZE];
  uint32_t curr = -1;
  uint32_t total_len = inode->dinode.length;
  uint32_t st_sect = inode->dinode.start_sect;
  int i;
  for (i = 0; i < len && off < total_len; ++i, ++off) {
    if (curr != off / SECTSIZE) {
      read_disk(dbuf, st_sect + off / SECTSIZE);
      curr = off / SECTSIZE;
    }
    *cbuf++ = dbuf[off % SECTSIZE];
  }
  return i;
}

void iadddev(const char *name, int id) {
  assert(id < MAX_DEV);
  inode_t *inode = &inodes[MAX_FILE + id];
  inode->valid = 1;
  inode->type = TYPE_DEV;
  inode->dev = id;
  strcpy(inode->dinode.name, name);
}

uint32_t isize(inode_t *inode) {
  return inode->dinode.length;
}

int itype(inode_t *inode) {
  return inode->type;
}

uint32_t ino(inode_t *inode) {
  return inode - inodes;
}

int idevid(inode_t *inode) {
  return inode->type == TYPE_DEV ? inode->dev : -1;
}

int iwrite(inode_t *inode, uint32_t off, const void *buf, uint32_t len) {
  panic("write doesn't support");
}

void itrunc(inode_t *inode) {
  panic("trunc doesn't support");
}

inode_t *idup(inode_t *inode) {
  return inode;
}

void iclose(inode_t *inode) { /* do nothing */ }

int iremove(const char *path) {
  panic("remove doesn't support");
}

#else

#define DISK_SIZE (128 * 1024 * 1024)
#define BLK_NUM   (DISK_SIZE / BLK_SIZE)

#define NDIRECT   12
#define NINDIRECT (BLK_SIZE / sizeof(uint32_t))

#define IPERBLK   (BLK_SIZE / sizeof(dinode_t)) // inode num per blk

// super block
typedef struct super_block {
  uint32_t bitmap; // block num of bitmap
  uint32_t istart; // start block no of inode blocks
  uint32_t inum;   // total inode num
  uint32_t root;   // inode no of root dir
} sb_t;

// On disk inode
typedef struct dinode {
  uint32_t type;   // file type
  uint32_t device; // if it is a dev, its dev_id
  uint32_t size;   // file size
  uint32_t addrs[NDIRECT + 1]; // data block addresses, 12 direct and 1 indirect
} dinode_t;

struct inode {
  int no;
  int ref;
  int del;
  dinode_t dinode;
};

#define SUPER_BLOCK 32
static sb_t sb;

void init_fs() {
  bread(&sb, sizeof(sb), SUPER_BLOCK, 0);
}

#define I2BLKNO(no)  (sb.istart + no / IPERBLK)
#define I2BLKOFF(no) ((no % IPERBLK) * sizeof(dinode_t))

// 把第no号的inode读到内存中
static void diread(dinode_t *di, uint32_t no) {
  bread(di, sizeof(dinode_t), I2BLKNO(no), I2BLKOFF(no));
}

// 把第no号的inode写到逻辑块中(not sure)
static void diwrite(const dinode_t *di, uint32_t no) {
  bwrite(di, sizeof(dinode_t), I2BLKNO(no), I2BLKOFF(no));
}

// 申请一个空闲的磁盘inode，设置其type，并返回其编号
static uint32_t dialloc(int type) {
  // Lab3-2: iterate all dinode, find a empty one (type==TYPE_NONE)
  // set type, clean other infos and return its no (remember to write back)
  // if no empty one, just abort
  // note that first (0th) inode always unused, because dirent's inode 0 mark invalid
  dinode_t dinode;
  for (uint32_t i = 1; i < sb.inum; ++i) {
    memset(&dinode, 0, sizeof dinode);
    diread(&dinode, i);
    // TODO();
    if(dinode.type == TYPE_NONE){
      dinode.type = type;
      diwrite(&dinode, i);
      return i;
    }
  }
  assert(0);
}

// 回收编号为no的磁盘inode
static void difree(uint32_t no) {
  dinode_t dinode;
  memset(&dinode, 0, sizeof dinode);
  diwrite(&dinode, no);
}

// 申请一个空闲的逻辑块，将其清零，并返回其编号
static uint32_t balloc() {
  // Lab3-2: iterate bitmap, find one free block
  // set the bit, clean the blk (can call bzero) and return its no
  // if no free block, just abort
  uint32_t byte = 0;
  for (int i = 0; i < BLK_NUM / 32; ++i) {
    bread(&byte, 4, sb.bitmap, i * 4);

    if (byte != 0xffffffff) {
      uint32_t t = 1;
      for(int j = 0; j < 32; j++){
        if((byte & t) == 0){
          bzero(i*32+j);
          byte |= t;
          bwrite(&byte, 4, sb.bitmap, i * 4);
          return i*32+j;
        }
        t *= 2;
      }
    }
  }
  assert(0);
}

// 回收第blkno号逻辑块
static void bfree(uint32_t blkno) {
  // Lab3-2: clean the bit of blkno in bitmap
  assert(blkno >= 64); // cannot free first 64 block
  // TODO();
  uint8_t byte = 0;
  bread(&byte, 1, sb.bitmap, blkno / 8);
  uint8_t t = blkno % 8;
  uint8_t tt = 1;
  for(uint8_t i = 0; i < t; i++) tt *= 2;
  byte &= (~tt);
  bwrite(&byte, 1, sb.bitmap, blkno / 8); // not sure
  bzero(blkno);
}

#define INODE_NUM 128
static inode_t inodes[INODE_NUM];

// 在活动inode表中打开编号为no的inode
static inode_t *iget(uint32_t no) {
  // Lab3-2
  // if there exist one inode whose no is just no, inc its ref and return it
  // otherwise, find a empty inode slot, init it and return it
  // if no empty inode slot, just abort
  // TODO();
  for(int i = 0; i < INODE_NUM; i++){
    if(inodes[i].no == no){
      inodes[i].ref++; // not sure
      return &inodes[i];
    }
  }
  for(int i = 0; i < INODE_NUM; i++){  // waiting to be perfected
    if(inodes[i].ref == 0){
      inodes[i].no = no;
      inodes[i].ref = 1;
      inodes[i].del = 0;
      diread(&(inodes[i].dinode), no);
      return &inodes[i];
    }
  }
  assert(0);
  return NULL;
}

// 将inode中的磁盘inode部分写回磁盘
static void iupdate(inode_t *inode) {
  // Lab3-2: sync the inode->dinode to disk
  // call me EVERYTIME after you edit inode->dinode
  diwrite(&inode->dinode, inode->no);
}


// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return NULL.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = NULL
// 返回指向path的下一级路径的指针，并将这一级的名字存到name中
static const char* skipelem(const char *path, char *name) {
  const char *s;
  int len;
  while (*path == '/') path++;
  if (*path == 0) return 0;
  s = path;
  while(*path != '/' && *path != 0) path++;
  len = path - s;
  if (len >= MAX_NAME) {
    memcpy(name, s, MAX_NAME);
    name[MAX_NAME] = 0;
  } else {
    memcpy(name, s, len);
    name[len] = 0;
  }
  while (*path == '/') path++;
  return path;
}

// 给inode对应的文件（一定是目录）创建.和..的两个目录项
static void idirinit(inode_t *inode, inode_t *parent) {
  // Lab3-2: init the dir inode, i.e. create . and .. dirent
  assert(inode->dinode.type == TYPE_DIR);
  assert(parent->dinode.type == TYPE_DIR); // both should be dir
  assert(inode->dinode.size == 0); // inode shoule be empty
  dirent_t dirent;
  memset(&dirent, 0, sizeof dirent);
  // set .
  dirent.inode = inode->no;
  strcpy(dirent.name, ".");
  iwrite(inode, 0, &dirent, sizeof dirent);
  // set ..
  dirent.inode = parent->no;
  strcpy(dirent.name, "..");
  iwrite(inode, sizeof dirent, &dirent, sizeof dirent);
}

// 遍历parent这个目录，找到其中名字为name的文件并打开，返回打开的inode
static inode_t *ilookup(inode_t *parent, const char *name, uint32_t *off, int type) {
  // Lab3-2: iterate the parent dir, find a file whose name is name
  // if off is not NULL, store the offset of the dirent_t to it
  // if no such file and type == TYPE_NONE, return NULL
  // if no such file and type != TYPE_NONE, create the file with the type
  assert(parent->dinode.type == TYPE_DIR); // parent must be a dir
  dirent_t dirent;
  memset(&dirent, 0, sizeof dirent);
  uint32_t size = parent->dinode.size, empty = size;
  for (uint32_t i = 0; i < size; i += sizeof dirent) {
    // directory is a file containing a sequence of dirent structures
    memset(&dirent, 0, sizeof dirent);
    iread(parent, i, &dirent, sizeof dirent);
    if (dirent.inode == 0) {
      // a invalid dirent, record the offset (used in create file), then skip
      if (empty == size) empty = i;
      continue;
    }
    // a valid dirent, compare the name
    // if(dirent.name[0] == 's')
      // Log("strlen=%u, dirent.name=%s\n", strlen(dirent.name), dirent.name);
    else if(strcmp(name, dirent.name) == 0){
      inode_t *inode_ret = iget(dirent.inode);
      if(off != NULL) memcpy(off, &i, sizeof(i));
      return inode_ret;
    }
  }
  // not found
  if (type == TYPE_NONE) return NULL;
  // need to create the file, first alloc inode, then init dirent, write it to parent
  // if you create a dir, remember to init it's . and ..
  // TODO();

  // Log("new name=%s\n", name);

  uint32_t inode_no = dialloc(type);
  inode_t *inode_ret = iget(inode_no);
  if(type == TYPE_DIR) idirinit(inode_ret, parent);

  dirent_t dirent_tmp;
  memset(&dirent_tmp, 0, sizeof dirent_tmp);
  dirent_tmp.inode = inode_no;
  memcpy(&dirent_tmp.name, name, strlen(name));
  iwrite(parent, empty, &dirent_tmp, sizeof dirent_tmp);
  if(off != NULL) memcpy(off, &empty, sizeof(empty));
  return inode_ret;
}

// 打开path路径指向的文件所在的目录，然后将文件名（即path的最后一部分）记录在name中
static inode_t *iopen_parent(const char *path, char *name) {
  // Lab3-2: open the parent dir of path, store the basename to name
  // if no such parent, return NULL
  inode_t *ip, *next;
  // set search starting inode

  if (path[0] == '/') {
    ip = iget(sb.root);
  } else {
    ip = idup(proc_curr()->cwd);
  }
  assert(ip);
  while ((path = skipelem(path, name))) {
    // curr round: need to search name in ip
    if (ip->dinode.type != TYPE_DIR) {
      // not dir, cannot search
      iclose(ip);
      return NULL;
    }
    if (*path == 0) {
      // last search, return ip because we just need parent
      return ip;
    }
    // not last search, need to continue to find parent
    next = ilookup(ip, name, NULL, 0);
    if (next == NULL) {
      // name not exist
      iclose(ip);
      return NULL;
    }
    iclose(ip);
    ip = next;
  }
  iclose(ip);
  return NULL;
}

// 打开path路径指向的文件本身
inode_t *iopen(const char *path, int type) {
  // Lab3-2: if file exist, open and return it
  // if file not exist and type==TYPE_NONE, return NULL
  // if file not exist and type!=TYPE_NONE, create the file as type
  // printf("path=%s\n", path);
  char name[MAX_NAME + 1];
  if (skipelem(path, name) == NULL) { 
    // no parent dir for path, path is "" or "/"
    // "" is an invalid path, "/" is root dir
    return path[0] == '/' ? iget(sb.root) : NULL;
  }
  // path do have parent, use iopen_parent and ilookup to open it
  // remember to close the parent inode after you ilookup it
  // TODO();
  memset(name, 0, MAX_NAME + 1);
  inode_t *parent_inode = iopen_parent(path, name);
  if(parent_inode == NULL) return NULL;
  inode_t *inode_ret = ilookup(parent_inode, name, NULL, type);

  if(inode_ret == NULL){
    iclose(parent_inode);
    return NULL;
  }

  // Log("iopen name=%s, size=%d\n", name, inode_ret->dinode.size);

  iclose(parent_inode);
  return inode_ret;
}

// 返回inode对应的文件数据使用的第no个逻辑块的编号，如果不存在这个逻辑块，就申请一个
static uint32_t iwalk(inode_t *inode, uint32_t no) {
  // return the blkno of the file's data's no th block, if no, alloc it
  if (no < NDIRECT) {
    // direct address
    // TODO();
    if(inode->dinode.addrs[no] == 0){
      inode->dinode.addrs[no] = balloc();
      iupdate(inode);
    }

    // Log("ret=%u\n", inode->dinode.addrs[no]);

    return inode->dinode.addrs[no];
  }
  no -= NDIRECT;
  if (no < NINDIRECT) {
    // indirect address
    // TODO();
    if(inode->dinode.addrs[NDIRECT] == 0){
      inode->dinode.addrs[NDIRECT] = balloc();
      iupdate(inode);
    }
    uint32_t blk_no;
    bread(&blk_no, sizeof(uint32_t), inode->dinode.addrs[NDIRECT], no * 4);
    if(blk_no == 0) {
      blk_no = balloc();
      bwrite(&blk_no, sizeof(uint32_t), inode->dinode.addrs[NDIRECT], no * 4);
    }

    // Log("ret=%u\n", blk_no);

    return blk_no;
  }
  assert(0); // file too big, not need to handle this case
}

// 从inode代表的文件的off偏移量处，读取len字节到内存的buf里，返回读取的字节数（或-1如果失败）
int iread(inode_t *inode, uint32_t off, void *buf, uint32_t len) {
  // Lab3-2: read the inode's data [off, MIN(off+len, size)) to buf
  // use iwalk to get the blkno and read blk by blk
  // TODO();
  // Log("iread off=%d, size=%d\n", off, inode->dinode.size);
  uint32_t max_blk = inode->dinode.size / BLK_SIZE; // 文件的最后一个逻辑块
  uint32_t max_off = inode->dinode.size % BLK_SIZE; // 文件最后一个逻辑块的最大off

  uint32_t len_tmp = len;
  int ret = 0;

  uint32_t blk_start = off / BLK_SIZE; // 开始读取的第一个逻辑块
  if(blk_start > max_blk) return 0;

  uint32_t blk_no = iwalk(inode, blk_start); // 开始读取的第一个逻辑块编号
  uint32_t off_start = off % BLK_SIZE; // 读取第一个逻辑块的off

  if(blk_start == max_blk){
    if(off_start >= max_off) return 0;
    ret = (off_start + len_tmp >= max_off) ? max_off - off_start : len_tmp;
    bread(buf, ret, blk_no, off_start);
    return ret;
  }

  int empty = BLK_SIZE - off_start; // 第一个逻辑块剩下的可读大小

  if(empty >= len_tmp){
    ret += len_tmp;
    bread(buf, ret, blk_no, off_start);
    return ret;
  }

  len_tmp -= empty;
  ret += empty;
  bread(buf, ret, blk_no, off_start);

  while(len_tmp != 0){
    blk_start++;
    blk_no = iwalk(inode, blk_start);

    if(blk_start == max_blk){
      int tmp = (len_tmp >= max_off) ? max_off : len_tmp;
      bread(buf + ret, tmp, blk_no, 0);
      ret += tmp;
      return ret;
    }

    if(len_tmp >= BLK_SIZE){
      len_tmp -= BLK_SIZE;
      bread(buf + ret, BLK_SIZE, blk_no, 0);
      ret += BLK_SIZE;
    }
    else{
      bread(buf + ret, len_tmp, blk_no, 0);
      ret += len_tmp;
      return ret;
    }
  }
  return ret;
}

/* 从内存的buf里，写len字节到inode代表的文件的off偏移量处，返回写入的字节数（或-1如果失败) 
允许off+len超过写之前文件的大小，此时会更新文件新大小为off+len */
int iwrite(inode_t *inode, uint32_t off, const void *buf, uint32_t len) {
  // Lab3-2: write buf to the inode's data [off, off+len)
  // if off>size, return -1 (can not cross size before write)
  // if off+len>size, update it as new size (but can cross size after write)
  // use iwalk to get the blkno and read blk by blk
  // TODO();
  uint32_t max_blk = inode->dinode.size / BLK_SIZE; // 文件的最后一个逻辑块
  uint32_t max_off = inode->dinode.size % BLK_SIZE; // 文件最后一个逻辑块的最大off

  uint32_t blk_start = off / BLK_SIZE; // 开始写入的第一个逻辑块

  if(blk_start > max_blk){ // 不处理写的范围的开头off超过文件的大小的情况
    assert(0);
    return -1;
  }

  uint32_t blk_no = iwalk(inode, blk_start); // 开始写入的第一个逻辑块编号
  uint32_t off_start = off % BLK_SIZE; // 写入的第一个逻辑块的off
  uint32_t len_tmp = len;
  int ret = 0;
  
  if(blk_start == max_blk){ // 特判写最后一块的情况

    if(max_off > off_start + len){
      bwrite(buf, len, blk_no, off_start);
      return len;
    }

    if(off_start + len <= BLK_SIZE){
      bwrite(buf, len, blk_no, off_start);
      inode->dinode.size += (off_start + len - max_off);
      iupdate(inode);
      return len;
    }
    bwrite(buf, BLK_SIZE - off_start, blk_no, off_start);
    inode->dinode.size += (BLK_SIZE - max_off);
    iupdate(inode);
    len_tmp -= (BLK_SIZE - off_start);
    ret += (BLK_SIZE - off_start);
    max_blk++;
    iwalk(inode, max_blk);
    max_off = 0;
  }

  else{
    if((off_start + len_tmp) <= BLK_SIZE){
      bwrite(buf, len_tmp, blk_no, off_start);
      return len;
    }
    len_tmp -= (BLK_SIZE - off_start);
    ret += (BLK_SIZE - off_start);
    bwrite(buf, ret, blk_no, off_start);
  }

  while(len_tmp != 0){
    blk_start++;
    blk_no = iwalk(inode, blk_start);

    if(blk_start == max_blk){
      if(max_off > len_tmp){
        bwrite(buf + ret, len_tmp, blk_no, 0);
        ret += len_tmp;
        return ret;
      }
      if(len_tmp <= BLK_SIZE){
        bwrite(buf + ret, len_tmp, blk_no, 0);
        inode->dinode.size += (len_tmp - max_off);
        iupdate(inode);
        ret += len_tmp;
        return ret;
      }
      bwrite(buf + ret, BLK_SIZE, blk_no, 0);
      inode->dinode.size += (BLK_SIZE - max_off);
      iupdate(inode);
      len_tmp -= BLK_SIZE;
      ret += BLK_SIZE;
      max_blk++;
      iwalk(inode, max_blk);
      max_off = 0;
      continue;
    }

    else{
      if(len_tmp <= BLK_SIZE){
        bwrite(buf + ret, len_tmp, blk_no, 0);
        ret += len_tmp;
        return ret;
      }
      len_tmp -= BLK_SIZE;
      bwrite(buf + ret, BLK_SIZE, blk_no, 0);
      ret += BLK_SIZE;
    }
  }
  return len;
}

// 清空inode代表的文件的所有数据
void itrunc(inode_t *inode) {
  // Lab3-2: free all data block used by inode (direct and indirect)
  // mark all address of inode 0 and mark its size 0
  // TODO();
  uint32_t blk_num = inode->dinode.size / BLK_SIZE;
  for(int i = 0; i <= blk_num; i++){

    // Log("blk_no=%u\n", iwalk(inode, i));

    bfree(iwalk(inode, i));
  }

  if(inode->dinode.addrs[NDIRECT] != 0){

    // Log("blk_no=%u\n", inode->dinode.addrs[NDIRECT]);

    bfree(inode->dinode.addrs[NDIRECT]);
  }

  for(int i = 0; i < NDIRECT + 1; i++){
    inode->dinode.addrs[i] = 0;
    iupdate(inode); // waiting to be perfected
  }
  inode->dinode.size = 0;
  iupdate(inode);
}

// 自增inode的引用计数并返回自身
inode_t *idup(inode_t *inode) {
  assert(inode);
  inode->ref += 1;
  return inode;
}

// 自减inode的引用计数，如果为0且设置了del，清空该文件的内容并回收磁盘inode（删除文件）
void iclose(inode_t *inode) {
  assert(inode);
  if (inode->ref == 1 && inode->del) {
    itrunc(inode);
    difree(inode->no);
  }
  inode->ref -= 1;
}

// 返回inode代表的文件的大小
uint32_t isize(inode_t *inode) {
  return inode->dinode.size;
}

// 返回inode代表的文件的类型
int itype(inode_t *inode) {
  return inode->dinode.type;
}

// 返回inode代表的文件的inode标号
uint32_t ino(inode_t *inode) {
  return inode->no;
}

// 如果inode代表的文件是设备文件，返回其设备号，否则返回-1
int idevid(inode_t *inode) {
  return itype(inode) == TYPE_DEV ? inode->dinode.device : -1;
}

// 向文件系统中注册一个设备，名字为name，设备号为id
void iadddev(const char *name, int id) {
  inode_t *ip = iopen(name, TYPE_DEV);
  assert(ip);
  ip->dinode.device = id;
  iupdate(ip);
  iclose(ip);
}

// 监测目录是否为空，要求inode必须是一个目录
static int idirempty(inode_t *inode) {
  // Lab3-2: return whether the dir of inode is empty
  // the first two dirent of dir must be . and ..
  // you just need to check whether other dirent are all invalid
  assert(inode->dinode.type == TYPE_DIR);
  // TODO();
  dirent_t dirent;
  memset(&dirent, 0, sizeof dirent);
  uint32_t size = inode->dinode.size;
  for(uint32_t i = 2 * (sizeof dirent); i < size; i += sizeof dirent){
    memset(&dirent, 0, sizeof dirent);
    iread(inode, i, &dirent, sizeof dirent);
    if(dirent.inode != 0) return 0; // 非空
  }
  return 1; // 空
}

// 删除path路径指向的文件，成功返回0，失败返回-1
int iremove(const char *path) {
  // Lab3-2: remove the file, return 0 on success, otherwise -1
  // first open its parent, if no parent, return -1
  // then find file in parent, if not exist, return -1
  // if the file need to remove is a dir, only remove it when it's empty
  // . and .. cannot be remove, so check name set by iopen_parent
  // remove a file just need to clean the dirent points to it and set its inode's del
  // the real remove will be done at iclose, after everyone close it
  // TODO();
  char name[MAX_NAME + 1];
  memset(name, 0, MAX_NAME + 1);
  inode_t *parent_inode = iopen_parent(path, name);
  if(parent_inode == NULL) return -1;
  if((strcmp(".", name) == 0) || (strcmp("..", name) == 0)){
    iclose(parent_inode);
    return -1;
  }
  uint32_t off = 0;
  inode_t *file_inode = ilookup(parent_inode, name, &off, TYPE_NONE);
  if(file_inode == NULL){
    iclose(parent_inode);
    return -1;
  }

  if(itype(file_inode) == TYPE_DIR){
    if(idirempty(file_inode) == 0){ // 非空
      iclose(parent_inode);
      iclose(file_inode);
      return -1;
    }
  }
  
  file_inode->del = 1;
  dirent_t dirent;
  memset(&dirent, 0, sizeof dirent);
  iwrite(parent_inode, off, &dirent, sizeof dirent);
  iclose(parent_inode); // not sure
  return 0;
}

#endif
