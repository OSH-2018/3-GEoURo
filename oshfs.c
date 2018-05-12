#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>

struct filenode                         //文件节点以链表的形式存储
{
    char filename[128];
    int32_t filesize;
    int32_t place;
    int32_t content[16312];
    struct stat st;
    struct filenode *next;
};
struct head
{
    struct filenode *next;
    char map[64*1024];
};
static const size_t size = 1024 * 1024 * (size_t)1024;      //表示大小（总共1G)
static const size_t blocksize = 64 * (size_t)1024;
static const size_t blocknum = 16 * 1024;                   //blocknum = 16384
static void *mem[64* 1024];                                 //mem表示一个指针数组，保存每个block所对应的的指针

int blockcnt = 0;
int lastused_block = 0;
int alloc_block()
{
    int i;

    for(i = lastused_block;i < blocknum; i++)
    {
        if(((struct head*)mem[0])->map[i] == 0)
        {
            lastused_block = i + 1;
            mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            ((struct head*)mem[0])->map[i] = 1;
            blockcnt++;
            return i;
        }
    }
    for (i = 0; i < lastused_block; i++)
    {
        if (((struct head*)mem[0])->map[i] == 0)
        {
            lastused_block = i + 1;
            mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            ((struct head*)mem[0])->map[i] = 1;
            blockcnt++;
            return i;
        }
    }
    return -1;
}
int deleteblock(int i)//取消内存映射
{
    munmap(mem[i], blocksize);
    mem[i] = NULL;
    lastused_block = i;
    blockcnt--;
    return 1;
}
int realloc_block(struct filenode *node, int size)
{

    int num = (size - 1)/blocksize + 1;
    int i, temp;
    if(num > node->filesize)
    {
        if(blocknum - blockcnt < num - size)
        {
            printf("Not enough space!\n");
            return -1;
        }
        else
        {
            for(i = node->filesize; i < num; i++)
            {
                temp = alloc_block();
                node->content[i] = temp;
            }
        }
    }
    else
    {
        for(i = num; i < node->filesize; i++)
            deleteblock(node->content[i]);
    }
    node->filesize = num;
    node->st.st_size = size;
    return 0;
}
static struct filenode *get_filenode(const char *name)          //寻找和name名字一致的文件节点，并将其return，找不到，返回空
//在fileattr中调用
{
    struct filenode *node = ((struct head*)mem[0])->next;
    while(node)
    {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)        //创造一个新的文件节点（在mknod中调用）
{
    struct head *root = (struct head*)mem[0];
    int num = alloc_block();
    if(num < 0)
        return;
    struct filenode *new = (struct filenode *)mem[num];
    strcpy(new->filename, filename);
    memcpy(&(new->st), st, sizeof(struct stat));
    new->place = num;
    new->filesize = 0;
    new->next = root->next;
    root->next = new;
    return;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    struct head *root;
    mem[0] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    blockcnt = 1;
    root = (struct head*)mem[0];
    for(int i = 0; i < blocknum; i++)
        root->map[i] = 0;
    root->next = NULL;
    root->map[0] = 1;
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);         //调用get_filenode函数，寻找与路径一致的文件节点
    if(strcmp(path, "/") == 0)                          //????????
    {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;                //缓冲区中的文件保护模式设定为目录
    }
    else if(node)                                       //如果节点存在，那么就把节点的文件属性copy到属性缓冲区中
        memcpy(stbuf, &(node->st), sizeof(struct stat));
    else                                                //若不存在，那么就返回错误
        ret = -ENOENT;
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
//读出所有文件的信息
{
    struct filenode *node = ((struct head*)mem[0])->next;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node)
    {                                       //依次读出每个文件的信息
        filler(buf, node->filename, &(node->st), 0);
        node = node->next;
    }
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)            //创造一个节点
{
    struct stat st;                                                         //定义一个状态结构体
    st.st_mode = S_IFREG | 0644;                                            //保护模式定义为普通文件
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;                                                        //硬链接个数设置为1个
    st.st_size = 0;                                                         //初始的文件大小为0
    create_filenode(path + 1, &st);                                         //调用了创造文件节点的函数，并将路径和定义的st和传进去
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)          //打开文件
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//将缓冲区（buf）中的数据写到一个打开的文件中
{
    struct filenode *node = get_filenode(path);                     //打开文件
    int total = offset + size;
    if(total > blocknum - blockcnt)
    {
        printf("Not enough space!\n");
        return -1;
    }
    if(realloc_block(node, total) == -1)
        printf("Allocation Error!\n");
    int a, b, byte_cnt, op_size;
    a = offset/blocksize;
    b = offset%blocksize;
    byte_cnt = 0;
    while(byte_cnt < size)
    {
        if(b)
        {
            if(size - byte_cnt < blocksize - b)
                op_size = size - byte_cnt;
            else
                op_size = blocksize - b;
            memcpy(mem[node->content[a]] + b, buf + byte_cnt, op_size);
            b = 0;
        }
        else if(size - byte_cnt < blocksize)
        {
            op_size = size - byte_cnt;
            memcpy((mem[node->content[a]]), buf + byte_cnt, op_size);
        }
        else
        {
            op_size = blocksize;
            memcpy((mem[node->content[a]]), buf + byte_cnt, op_size);
        }
        byte_cnt += op_size;
        a++;
    }
    return size;
}
static int oshfs_truncate(const char *path, off_t size)     //用于修改文件的大小
{
    struct filenode *node = get_filenode(path);             //打开文件
    realloc_block(node, size);
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//从一个已经打开的文件中读出数据
{
    struct filenode *node = get_filenode(path);
    int readsize;
    if(offset + size > node->st.st_size)
        readsize = node->st.st_size - offset;
    else
        readsize = size;
    int a, b, byte_cnt = 0, op_size;
    a = offset/blocksize;
    b = offset%blocksize;
    while(byte_cnt < readsize)
    {
        if(b)
        {
            op_size = blocksize - b;
            memcpy(mem[node->content[a]] + b, buf + byte_cnt, op_size);
            b = 0;
        }
        else if(readsize - byte_cnt < blocksize)
        {
            op_size = readsize - byte_cnt;
            memcpy((mem[node->content[a]]), buf + byte_cnt, op_size);
        }
        else
        {
            op_size = blocksize;
            memcpy((mem[node->content[a]]), buf + byte_cnt, op_size);
        }
        byte_cnt += op_size;
        a++;
    }
    return size;                                                     //返回读取数据的大小
}

static int oshfs_unlink(const char *path)               //用于删除一个节点
{
    struct head *root = (struct head*)mem[0];
    struct filenode *node1 = get_filenode(path);
    struct filenode *node2 = root->next;
    if (node1 == node2)                        //特殊处理文件为链表头的情况
    {
        root->next=node1->next;
        node1->next=NULL;
    }
    else if (node1)                         //若node1存在
    {
        while(node2->next != node1 && node2)
            node2 = node2->next;
        node2->next=node1->next;
        node1->next=NULL;
    }
    else return -1;
    for(int i = 0; i < node1->filesize; i++)
        deleteblock(node1->content[i]);
    deleteblock(node1->place);
    return 0;

}

static const struct fuse_operations op = {              //不同的op所对应的函数
        .init = oshfs_init,
        .getattr = oshfs_getattr,
        .readdir = oshfs_readdir,
        .mknod = oshfs_mknod,
        .open = oshfs_open,
        .write = oshfs_write,
        .truncate = oshfs_truncate,
        .read = oshfs_read,
        .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);            //调用fuse函数
}
