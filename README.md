#实验三
##文件节点
- char filename[128]：文件名，最长127
- int32_t filesize：文件占用的块数
- int32_t place：该文件节点在mem中的位置
- int32_t content[16312]：文件内容映射表
- struct stat st：文件相关信息
- struct filenode *next：下一个文件节点指针

##分块操作
- 文件系统总共1G，每块64KB，共16384块
- blockcnt为记录当前所用的块数
- lastused_block记录最后一次使用的块的编号
- alloc\_block()和deleteblock()都会修改lastused\_block的值。alloc\_block()将分配出去的块的编号值加一再对总块数取模赋值给lastused\_block，deleteblock则将删除的块的编号赋值给lastused\_block。
- 分配块时alloc\_bloick()总是先从lastused\_block开始向后查找可用块，若无可用块再从头开始查找至lastused\_block停止。经过上述调整可以最快的找到可用块。
- realloc\_block()根据新的size值重新给文件分配块。与原文件大小比较后，大于则再分配新的块，小于则删除offset+size后的块。

##write和read函数
- write函数通过offset+size调用realloc\_block()实现块的再分配，再根据块偏移量和块内偏移量完成按块写入操作，块内偏移量在完成第一次有偏移写入后置零。
- read函数通过offset+size并于文件大小比较获得实际读取数据的大小，再根据块偏移量和块内偏移量完成按块读取操作，块内偏移量在完成第一次有偏移读取后置零。

##truncate和unlink
- unlink的操作即为有头节点的链表的删除操作，头节点为mem[0]如果删除的文件节点mem[0]->next，则特殊操作修改mem[0]的next指针。
- truncate在计算出新的文件大小后直接调用realloc\_block()即可。realloc\_block()函数会完成对文件大小和占用块数信息的修改。