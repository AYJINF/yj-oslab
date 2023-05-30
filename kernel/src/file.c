#include "klib.h"
#include "file.h"

#define TOTAL_FILE 128

file_t files[TOTAL_FILE];

static file_t *falloc() {
  // Lab3-1: find a file whose ref==0, init it, inc ref and return it, return NULL if none
  // TODO();
  for(int i = 0; i < TOTAL_FILE; i++){
    if(files[i].ref == 0){
      files[i].type = TYPE_NONE;
      files[i].ref++;
      return &files[i];
    }
  }
  return NULL;
}

// 打开文件名为path的文件，打开模式为mode，返回打开后的file_t指针（或NULL如果不存在）
file_t *fopen(const char *path, int mode) {
  file_t *fp = falloc();
  inode_t *ip = NULL;
  if (!fp) goto bad;
  // TODO: Lab3-2, determine type according to mode
  // iopen in Lab3-2: if file exist, open and return it
  //       if file not exist and type==TYPE_NONE, return NULL
  //       if file not exist and type!=TYPE_NONE, create the file as type
  // you can ignore this in Lab3-1

  int open_type = 114514;
  if((mode & O_CREATE) == 0){
    open_type = TYPE_NONE;
  }
  else{
    if((mode & O_DIR) == 0){
      open_type = TYPE_FILE;
    }
    else open_type = TYPE_DIR;
  }

  ip = iopen(path, open_type);

  if (!ip) goto bad;
  int type = itype(ip);
  // if(type != 3) Log("ip size=========%x\n", isize(ip));
  
  if (type == TYPE_FILE || type == TYPE_DIR) {
    // TODO: Lab3-2, if type is not DIR, go bad if mode&O_DIR
    if((type != TYPE_DIR) && ((mode & O_DIR) != 0))
      goto bad; 

    // TODO: Lab3-2, if type is DIR, go bad if mode WRITE or TRUNC
    if((type == TYPE_DIR) && (((mode & O_WRONLY) != 0) || ((mode & O_RDWR) != 0) || ((mode & O_TRUNC) != 0)))
      goto bad;

    // TODO: Lab3-2, if mode&O_TRUNC, trunc the file
    if((type == TYPE_FILE) && ((mode & O_TRUNC) != 0))
      itrunc(ip);

    fp->type = TYPE_FILE; // file_t don't and needn't distingush between file and dir
    fp->inode = ip;
    fp->offset = 0;
  } 
  
  else if (type == TYPE_DEV) {
    // Log("file.c path=%s\n", path);
    if(((mode & O_DIR) != 0)) goto bad; 
    fp->type = TYPE_DEV;
    fp->dev_op = dev_get(idevid(ip));
    iclose(ip);
    ip = NULL;
  } else assert(0);
  fp->readable = !(mode & O_WRONLY);
  fp->writable = (mode & O_WRONLY) || (mode & O_RDWR);
  return fp;
bad:
  if (fp) fclose(fp);
  if (ip) iclose(ip);
  return NULL;
}

// 从file文件中读取size字节到内存的buf里，返回读取的字节数（或-1如果失败）
int fread(file_t *file, void *buf, uint32_t size) {
  // Lab3-1, distribute read operation by file's type
  // remember to add offset if type is FILE (check if iread return value >= 0!)
  if (!file->readable) return -1;
  // int len = 0;
  int file_type = file->type;
  int ret = 0;
  
  if(file_type == TYPE_FILE || file_type == TYPE_DIR){
    ret = iread(file->inode, file->offset, buf, size);
    if(ret == -1) return -1;
    file->offset += ret;
    // Log("fread   file->size=%d, file->offset=%d, fread ret=%d\n", isize(file->inode), file->offset, ret);
  }
  else if(file_type == TYPE_DEV){
    ret = file->dev_op->read(buf, size);
  }
  else assert(0);
  return ret;
}

// 从内存的buf处写size字节到file文件，返回读取的字节数（或-1如果失败）
int fwrite(file_t *file, const void *buf, uint32_t size) {
  // Lab3-1, distribute write operation by file's type
  // remember to add offset if type is FILE (check if iwrite return value >= 0!)
  if (!file->writable) return -1;
  // TODO();
  int file_type = file->type;
  int ret = 0;
  if(file_type == TYPE_FILE || file_type == TYPE_DIR){
    ret = iwrite(file->inode, file->offset, buf, size);
    if(ret == -1) return -1;
    file->offset += ret;
  }
  else if(file_type == TYPE_DEV){
    ret = file->dev_op->write(buf, size);
  }
  return ret;
}

// 如果file是磁盘文件，改变其偏移量，whence = SEEK_SET/SEEK_CUR/SEEK_END
uint32_t fseek(file_t *file, uint32_t off, int whence) {
  // Lab3-1, change file's offset, do not let it cross file's size
  int off_t = (int)off;
  if (file->type == TYPE_FILE || file->type == TYPE_DIR){
    // TODO();
    uint32_t file_size = isize(file->inode);
    if(whence == SEEK_SET){
      file->offset = off_t;
    }
    else if(whence == SEEK_CUR){
      file->offset += off_t;
    }
    else if(whence == SEEK_END){
      file->offset = file_size + off_t; // not sure
    }
    uint32_t ret = file->offset;
    // Log("whence=%d, off=%d, file_size = %d, ret = %d\n", whence, off_t, file_size, ret);
    assert(file_size >= ret); // not sure
    return ret;
  }
  return -1;
}

// 自增file的引用计数，并返回自身
file_t *fdup(file_t *file) {
  // Lab3-1, inc file's ref, then return itself
  // TODO();
  file->ref++;
  return file;
}

// 自减file的引用计数，代表该进程关闭此文件，如果引用计数减到0且该文件是磁盘文件还要调用iclose关闭这个文件对应的磁盘文件
void fclose(file_t *file) {
  // Lab3-1, dec file's ref, if ref==0 and it's a file, call iclose
  // TODO();
  file->ref--;
  if(file->ref == 0 && ((file->type == TYPE_FILE) || (file->type == TYPE_DIR))){
    iclose(file->inode);
  }
}
