#ifndef _zip_H
#define _zip_H

#include <Windows.h>

// ZIP 压缩接口：用于创建 ZIP 文件。本轮修改原因：统一中文说明与 UTF-8 BOM 编码；变更内容仅限注释和编码。
// 本文件由 Lucian Wischik 对 Info-ZIP 源码重新封装，
// 原始源码位于 www.info-zip.org，完整版权声明保留在 Zip.cpp 中。
// 封装的目的是简化并扩展 Windows/C++ 下的使用方式，
// 同时增加加密与 Unicode 支持。


#ifndef _unzip_H
DECLARE_HANDLE(HZIP);
#endif
// HZIP 标识正在创建的 ZIP 文件。

typedef DWORD ZRESULT;
// ZRESULT 为 ZIP 接口的返回码类型，具体定义见下文。



HZIP CreateZip(const TCHAR *fn, const char *password);
HZIP CreateZip(void *buf,unsigned int len, const char *password);
HZIP CreateZipHandle(HANDLE h, const char *password);
// CreateZip：开始创建 ZIP 文件，并返回操作句柄。
// 创建过程支持以下存储位置：
// 写入管道：CreateZipHandle(hpipe_write);
// 写入文件句柄：CreateZipHandle(hfile);
// 写入指定文件：CreateZip("c:\\test.zip");
// 写入已有内存：CreateZip(buf, len);
// 写入分页文件支持的内存：CreateZip(0, len);
// 最后一种方式使用系统分页文件支持的内存，
// ZIP 数据最多占用 len 字节。相比直接使用 new[] 分配内存，
// 它可以减少内存碎片，而且只在需要时访问相应内存页，
// 因此可以为最大归档大小预留较宽裕的上限，
// 而不必立即占用全部实际内存。
// password 用于加密归档中的每个文件。
// 该接口不支持逐文件单独设置加密密码。
// 注意：管道不支持随机访问，因此写入管道的 ZIP 结构
// 与写入普通文件或内存时略有不同。
// 具体来说，条目的压缩后大小必须等到条目数据写完后
// 才能存入归档。如果条目输入本身也来自管道，
// 其未压缩大小也可能只有在输入结束后才能确定。
// 通常这不会造成问题；但如果解压输入也来自管道，
// 解压器同样要等到条目解压完成后才能获知这些大小。
// 因此，如果解压器需要预先分配输出内存，
// 而不是直接把条目写入磁盘或管道，
// 则不应把归档输出到管道，或者不应通过管道添加条目；
// 若必须从管道添加条目，
// 至少应在添加时明确提供输入长度。
// 注意：Windows CE 下必须在 CloseZip 之后才能关闭传入的句柄。
// 桌面 Windows 下库会复制该句柄，
// 因此调用方可以自行关闭原句柄。


ZRESULT ZipAdd(HZIP hz,const TCHAR *dstzn, const TCHAR *fn);
ZRESULT ZipAdd(HZIP hz,const TCHAR *dstzn, void *src,unsigned int len);
ZRESULT ZipAddHandle(HZIP hz,const TCHAR *dstzn, HANDLE h);
ZRESULT ZipAddHandle(HZIP hz,const TCHAR *dstzn, HANDLE h, unsigned int len);
ZRESULT ZipAddFolder(HZIP hz,const TCHAR *dstzn);
// ZipAdd：将一个文件或目录条目添加到 ZIP 归档。
// dstzn 指定条目在 ZIP 文件内部保存的名称。
// 条目内容可以来自以下来源：
// 管道：ZipAddHandle(hz,"file.dat", hpipe_read);
// 文件句柄：ZipAddHandle(hz,"file.dat", hfile);
// 指定文件：ZipAdd(hz,"file.dat", "c:\\docs\\origfile.dat");
// 内存：ZipAdd(hz,"subdir\\file.dat", buf,len);
// 目录：ZipAddFolder(hz,"subdir");
// 注意：当条目输入和 ZIP 输出都使用管道时，
// 可通过 ZipAddHandle 的长度参数明确提供非零输入长度。
// 这样归档就能在压缩数据之前写入条目的原始大小，
// 使后续从管道解压该归档时，
// 能够提前获知输出空间的需求。

ZRESULT ZipGetMemory(HZIP hz, void **buf, unsigned long *len);
// ZipGetMemory：获取通过 CreateZip(0,len) 创建的内存归档信息。
// 该函数返回归档所在的内存块信息。
// buf 接收内存起始地址，len 接收归档数据长度。
// 注意：调用后不能继续向归档添加条目。

ZRESULT CloseZip(HZIP hz);
// CloseZip：必须通过该函数关闭 ZIP 句柄。

unsigned int FormatZipMessage(ZRESULT code, TCHAR *buf,unsigned int len);
// FormatZipMessage：将返回码格式化为错误说明字符串。
// 返回错误说明的长度；如果 buf 和 len 指定了有效缓冲区，
// 还会在容量允许的范围内将说明写入该缓冲区。



// 以下为接口返回码：
#define ZR_OK         0x00000000     // 操作成功。注意：ZR_RECENT 是查询最近错误的伪返回码，
#define ZR_RECENT     0x00000001     // 接口不会直接返回它，但可将其传入 FormatZipMessage。
// 以下返回码表示系统或资源错误，例如文件无法打开。
#define ZR_GENMASK    0x0000FF00
#define ZR_NODUPH     0x00000100     // 无法复制句柄
#define ZR_NOFILE     0x00000200     // 无法创建/打开文件
#define ZR_NOALLOC    0x00000300     // 资源分配失败。
#define ZR_WRITE      0x00000400     // 写入文件时发生错误。
#define ZR_NOTFOUND   0x00000500     // 归档中未找到指定文件。
#define ZR_MORE       0x00000600     // 仍有数据需要继续解压。
#define ZR_CORRUPT    0x00000700     // ZIP 文件损坏，或输入不是 ZIP 文件。
#define ZR_READ       0x00000800     // 读取文件时发生错误。
// 以下返回码表示调用方使用错误。
#define ZR_CALLERMASK 0x00FF0000
#define ZR_ARGS       0x00010000     // 参数错误。
#define ZR_NOTMMAP    0x00020000     // ZipGetMemory 仅适用于内存映射归档，当前归档不满足此条件。
#define ZR_MEMSIZE    0x00030000     // 内存空间不足。
#define ZR_FAILED     0x00040000     // 调用该函数之前已发生错误。
#define ZR_ENDED      0x00050000     // 归档创建过程已结束。
#define ZR_MISSIZE    0x00060000     // 实际输入大小与指定大小不一致。
#define ZR_PARTIALUNZ 0x00070000     // 该条目已经被部分解压。
#define ZR_ZMODE      0x00080000     // 混用了创建归档和打开归档的操作。
// 以下返回码表示 ZIP 库内部错误。
#define ZR_BUGMASK    0xFF000000
#define ZR_NOTINITED  0x01000000     // 内部初始化未完成。
#define ZR_SEEK       0x02000000     // 试图在不支持定位的文件或流上移动读写位置。
#define ZR_NOCHANGE   0x04000000     // 不允许在当前阶段改变存储方式。
#define ZR_FLATE      0x05000000     // 压缩或解压算法内部错误。






// 使用示例：
//
// (1) 常规用法：将已有文件加入新建的 ZIP 归档。
//     HZIP hz = CreateZip("c:\\simple1.zip",0);
//     ZipAdd(hz,"znsimple.bmp", "c:\\simple.bmp");
//     ZipAdd(hz,"znsimple.txt", "c:\\simple.txt");
//     CloseZip(hz);
//
// (2) 内存用法：使用自动分配的内存归档，添加来自不同来源的数据。
//     HZIP hz = CreateZip(0,100000, 0);
//     // 添加普通文件。
//     ZipAdd(hz,"src1.txt",  "c:\\src1.txt");
//     // 添加内存数据。
//     char buf[1000]; for (int i=0; i<1000; i++) buf[i]=(char)(i&0x7F);
//     ZipAdd(hz,"file.dat",  buf,1000);
//     // 添加管道数据。
//     HANDLE hread,hwrite; CreatePipe(&hread,&hwrite,NULL,0);
//     HANDLE hthread = CreateThread(0,0,ThreadFunc,(void*)hwrite,0,0);
//     ZipAdd(hz,"unz3.dat",  hread,1000);  // 长度参数 1000 可省略。
//     WaitForSingleObject(hthread,INFINITE);
//     CloseHandle(hthread); CloseHandle(hread);
//     // 另一个线程同时执行以下函数：DWORD WINAPI ThreadFunc(void *dat)
//                   { HANDLE hwrite = (HANDLE)dat;
//                     char buf[1000]={17};
//                     DWORD writ; WriteFile(hwrite,buf,1000,&writ,NULL);
//                     CloseHandle(hwrite);
//                     return 0;
//                   }
//     // 归档创建完成后，获取其内存数据并写入文件。
//     void *zbuf; unsigned long zlen; ZipGetMemory(hz,&zbuf,&zlen);
//     HANDLE hfz = CreateFile("test2.zip",GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
//     DWORD writ; WriteFile(hfz,zbuf,zlen,&writ,NULL);
//     CloseHandle(hfz);
//     CloseZip(hz);
//
// (3) 句柄用法：使用文件句柄或管道。
//     HANDLE hzread,hzwrite; CreatePipe(&hzread,&hzwrite,0,0);
//     HANDLE hthread = CreateThread(0,0,ZipReceiverThread,(void*)hzread,0,0);
//     HZIP hz = CreateZipHandle(hzwrite,0);
//     // 在此添加归档条目。
//     CloseZip(hz);
//     CloseHandle(hzwrite);
//     WaitForSingleObject(hthread,INFINITE);
//     CloseHandle(hthread);
//     // 另一个线程同时执行以下函数：DWORD WINAPI ZipReceiverThread(void *dat)
//                   { HANDLE hread = (HANDLE)dat;
//                     char buf[1000];
//                     while (true)
//                     { DWORD red; ReadFile(hread,buf,1000,&red,NULL);
//                       // 处理从管道收到的 ZIP 数据。
//                       if (red==0) break;
//                     }
//                     CloseHandle(hread);
//                     return 0;
//                   }



// 以下宏兼容仅包含压缩头文件，或同时包含压缩与解压头文件的用法。
// 当两个头文件都存在时，约定对应的两个实现文件也都参与编译。
// 此时根据句柄类型，在压缩和解压实现之间动态选择关闭操作。
// 若只包含一个头文件，
// 则将公共接口绑定到该头文件对应的实现。
// 这样可在不同包含组合下复用同一接口名称。
ZRESULT CloseZipZ(HZIP hz);
unsigned int FormatZipMessageZ(ZRESULT code, char *buf,unsigned int len);
bool IsZipHandleZ(HZIP hz);
#ifdef _unzip_H
#undef CloseZip
#define CloseZip(hz) (IsZipHandleZ(hz)?CloseZipZ(hz):CloseZipU(hz))
#else
#define CloseZip CloseZipZ
#define FormatZipMessage FormatZipMessageZ
#endif



#endif
