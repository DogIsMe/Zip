#ifndef _unzip_H
#define _unzip_H

#include <Windows.h>
// 修改原因：统一声明及示例中的大括号和控制语句排版。
// 变更内容：仅调整换行、空白和缩进，不修改接口定义。
// ZIP 解压接口。本轮修改原因：统一中文说明与 UTF-8 BOM 编码；变更内容仅限注释和编码。
// 本文件由 Lucian Wischik 对 zlib 源码片段重新封装。
// 原始源码位于 www.gzip.org/zlib，作者为 Jean-Loup Gailly 和 Mark Adler。
// 完整版权声明保留在 unzip.cpp 中。
// 封装简化并扩展了 Windows/C++ 下的使用方式，
// 同时增加了加密与 Unicode 文件名支持。

#ifndef _zip_H
DECLARE_HANDLE(HZIP);
#endif
// HZIP 标识已经打开的 ZIP 文件。

typedef DWORD ZRESULT;
// ZRESULT 为 ZIP 接口返回码类型，具体定义见下文。

typedef struct
{
    int index;                    // 条目在 ZIP 文件中的索引。
    TCHAR name[MAX_PATH];         // 条目在 ZIP 文件内部的名称。
    DWORD attr;                   // 文件属性，含义与 GetFileAttributes 的返回值一致。
    FILETIME atime, ctime, mtime; // 访问时间、创建时间和修改时间。
    long comp_size;               // 分别为条目的压缩大小和未压缩大小；
    long unc_size;                // 尚未确定时可为 -1，例如正在以流方式读取输入。
} ZIPENTRY;

HZIP OpenZip(const TCHAR *fn, const char *password);
HZIP OpenZip(void *z, unsigned int len, const char *password);
HZIP OpenZipHandle(HANDLE h, const char *password);
// OpenZip：打开 ZIP 文件，返回用于查询内容和解压的句柄。
// 支持以下输入来源：
// 管道：OpenZipHandle(hpipe_read,0);
// 文件句柄：OpenZipHandle(hfile,0);
// 指定文件：OpenZip("c:\\test.zip","password");
// 内存块：OpenZip(bufstart, buflen,0);
// 按照原接口约定，从管道读取时只能按索引递增顺序访问条目，
// 每个条目只能解压一次；但可在解压前后
// 调用 GetZipItem 查询该条目信息。
// 其他输入方式允许随机访问。
// 注意：当前版本尚未实现管道输入。
// 注意：ZIP 密码使用 ASCII 编码，而非 Unicode。
// 注意：Windows CE 下必须在 CloseZip 后才能关闭传入句柄。
// 桌面 Windows 下库会复制该句柄，
// 调用方可以自行关闭原句柄。

ZRESULT GetZipItem(HZIP hz, int index, ZIPENTRY *ze);
// GetZipItem：获取 ZIP 归档中指定条目的信息。
// 如果 index 为 -1，且归档不是通过管道打开，
// 则查询整个归档的信息，
// 其中 ze.index 返回归档条目总数。
// 注意：条目可能是目录，可用 ze.attr & FILE_ATTRIBUTE_DIRECTORY 判断。
// 目录条目的解压行为见 UnzipItem 的说明。
// 按原接口约定，从管道打开归档时不能随机访问，
// GetZipItem(-1) 也会失败，因此不能直接查询条目总数。
// 只能从索引 0 开始，依次调用 GetZipItem，
// 直到查询失败为止。
// 此外，如果读取来源和归档创建目标都为管道，
// comp_size 以及部分情况下的 unc_size
// 可能要在条目解压完成后才能确定。

ZRESULT FindZipItem(HZIP hz, const TCHAR *name, bool ic, int *index, ZIPENTRY *ze);
// FindZipItem：按名称查找条目，ic 为 true 时忽略大小写。
// 通过 index 返回索引，通过 ze 返回条目信息。
// 若未找到条目，则将 index 设为 -1，
// 并返回相应错误码。

ZRESULT UnzipItem(HZIP hz, int index, const TCHAR *fn);
ZRESULT UnzipItem(HZIP hz, int index, void *z, unsigned int len);
ZRESULT UnzipItemHandle(HZIP hz, int index, HANDLE h);
// UnzipItem：按索引解压条目，支持以下输出目标：
// 管道：UnzipItemHandle(hz,i, hpipe_write);
// 文件句柄：UnzipItemHandle(hz,i, hfile);
// 指定文件：UnzipItem(hz,i, ze.name);
// 内存块：UnzipItem(hz,i, buf,buflen);
// 写入内存时，如果缓冲区不足以容纳全部数据，
// 则返回码指示仍有数据需要继续读取。
// 若缓冲区足够，可通过 GetZipItem 获取条目的准确大小。
// 注意：ZIP 条目通常采用相对路径。
// 以 ZIP_FILENAME 方式解压到相对路径时，
// 将相对于当前解压基目录创建文件，并先创建所需子目录。
// 条目本身也可能是目录。
// 目录条目以 ZIP_FILENAME 方式解压时会创建对应目录；
// 若解压到句柄或内存块，则不创建目录，
// 也不输出任何数据，即输出长度为 0 字节。
ZRESULT SetUnzipBaseDir(HZIP hz, const TCHAR *dir);
// SetUnzipBaseDir：设置按相对文件名解压时使用的基目录。
// 默认使用当前工作目录。

ZRESULT CloseZip(HZIP hz);
// CloseZip：必须通过该函数关闭 ZIP 句柄。

unsigned int FormatZipMessage(ZRESULT code, TCHAR *buf, unsigned int len);
// FormatZipMessage：将返回码格式化为错误说明字符串。
// 返回错误说明的长度；如果 buf 和 len 指定了有效缓冲区，
// 还会在容量允许的范围内将说明写入该缓冲区。

// 以下为接口返回码：
#define ZR_OK 0x00000000     // 操作成功。ZR_RECENT 为查询最近错误的伪返回码，接口不会直接返回它。
#define ZR_RECENT 0x00000001 // 可将该值传入 FormatZipMessage 查询最近一次错误说明。
// 以下返回码表示系统或资源错误，例如文件无法打开。
#define ZR_GENMASK 0x0000FF00
#define ZR_NODUPH 0x00000100   // 无法复制句柄。
#define ZR_NOFILE 0x00000200   // 无法创建或打开文件。
#define ZR_NOALLOC 0x00000300  // 资源分配失败。
#define ZR_WRITE 0x00000400    // 写入文件时发生错误。
#define ZR_NOTFOUND 0x00000500 // 归档中未找到指定文件。
#define ZR_MORE 0x00000600     // 仍有数据需要继续解压。
#define ZR_CORRUPT 0x00000700  // ZIP 文件损坏，或输入不是 ZIP 文件。
#define ZR_READ 0x00000800     // 读取文件时发生错误。
#define ZR_PASSWORD 0x00001000 // 解压密码不正确。
// 以下返回码表示调用方使用错误。
#define ZR_CALLERMASK 0x00FF0000
#define ZR_ARGS 0x00010000       // 参数错误。
#define ZR_NOTMMAP 0x00020000    // ZipGetMemory 仅适用于内存映射归档，当前归档不满足此条件。
#define ZR_MEMSIZE 0x00030000    // 内存空间不足。
#define ZR_FAILED 0x00040000     // 调用该函数之前已发生错误。
#define ZR_ENDED 0x00050000      // 归档创建过程已结束。
#define ZR_MISSIZE 0x00060000    // 实际输入大小与指定大小不一致。
#define ZR_PARTIALUNZ 0x00070000 // 该条目已经被部分解压。
#define ZR_ZMODE 0x00080000      // 混用了创建归档和打开归档的操作。
// 以下返回码表示 ZIP 库内部错误。
#define ZR_BUGMASK 0xFF000000
#define ZR_NOTINITED 0x01000000 // 内部初始化未完成。
#define ZR_SEEK 0x02000000      // 试图在不支持定位的文件或流上移动读写位置。
#define ZR_NOCHANGE 0x04000000  // 不允许在当前阶段改变存储方式。
#define ZR_FLATE 0x05000000     // 压缩或解压算法内部错误。

// 使用示例：
//
// SetCurrentDirectory("c:\\docs\\stuff");
// HZIP hz = OpenZip("c:\\stuff.zip",0);
// ZIPENTRY ze; GetZipItem(hz,-1,&ze); int numitems=ze.index;
// for (int i=0; i<numitems; i++)
// {
//   GetZipItem(hz,i,&ze);
//   UnzipItem(hz,i,ze.name);
// }
// CloseZip(hz);
//
//
// HRSRC hrsrc = FindResource(hInstance,MAKEINTRESOURCE(1),RT_RCDATA);
// HANDLE hglob = LoadResource(hInstance,hrsrc);
// void *zipbuf=LockResource(hglob);
// unsigned int ziplen=SizeofResource(hInstance,hrsrc);
// HZIP hz = OpenZip(zipbuf, ziplen, 0);
// 解压到按条目大小分配的内存缓冲区。
// ZIPENTRY ze; int i; FindZipItem(hz,"file.dat",true,&i,&ze);
// char *ibuf = new char[ze.unc_size];
// UnzipItem(hz,i, ibuf, ze.unc_size);
// delete[] ibuf;
// 分多次解压到固定大小的内存缓冲区。
// ZIPENTRY ze; int i; FindZipItem(hz,"file.dat",true,&i,&ze);
// char ibuf[1024]; ZRESULT zr=ZR_MORE; unsigned long totsize=0;
// while (zr==ZR_MORE)
// {
//   zr = UnzipItem(hz,i, ibuf,1024);
//   unsigned long bufsize=1024;
//   if (zr==ZR_OK)
//   {
//     bufsize=ze.unc_size-totsize;
//   }
//   totsize+=bufsize;
// }
// 解压到管道。
// HANDLE hwrite; HANDLE hthread=CreateWavReaderThread(&hwrite);
// int i; ZIPENTRY ze; FindZipItem(hz,"sound.wav",true,&i,&ze);
// UnzipItemHandle(hz,i, hwrite);
// CloseHandle(hwrite);
// WaitForSingleObject(hthread,INFINITE);
// CloseHandle(hwrite); CloseHandle(hthread);
// 解压完成。
// CloseZip(hz);
// // 注意：通过 FindResource、LoadResource、LockResource 获取的资源无需在此释放。
//
//
// SetCurrentDirectory("c:\\docs\\pipedzipstuff");
// HANDLE hread,hwrite; CreatePipe(&hread,&hwrite,0,0);
// CreateZipWriterThread(hwrite);
// HZIP hz = OpenZipHandle(hread,0);
// for (int i=0; ; i++)
// {
//   ZIPENTRY ze;
//   ZRESULT zr=GetZipItem(hz,i,&ze);
//   if (zr!=ZR_OK)
//   {
//     break; // 没有更多条目。
//   }
//   UnzipItem(hz,i, ze.name);
// }
// CloseZip(hz);
//
//

// 以下宏兼容仅包含压缩头文件，或同时包含压缩与解压头文件的用法。
// 当两个头文件都存在时，约定对应的两个实现文件也都参与编译。
// 此时根据句柄类型，在压缩和解压实现之间动态选择关闭操作。
// 若只包含一个头文件，
// 则将公共接口绑定到该头文件对应的实现。
// 这样可在不同包含组合下复用同一接口名称。
ZRESULT CloseZipU(HZIP hz);
unsigned int FormatZipMessageU(ZRESULT code, TCHAR *buf, unsigned int len);
bool IsZipHandleU(HZIP hz);
#ifdef _zip_H
#undef CloseZip
#define CloseZip(hz) (IsZipHandleU(hz) ? CloseZipU(hz) : CloseZipZ(hz))
#else
#define CloseZip CloseZipU
#define FormatZipMessage FormatZipMessageU
#endif

#endif // _unzip_H 头文件保护结束。
