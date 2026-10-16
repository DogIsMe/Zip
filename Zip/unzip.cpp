#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include "unzip.h"

// 本文件主要基于 Jean-loup Gailly、Mark Adler 的 zlib 解压代码，由 Lucian Wischik 修改。
// 原封装吸收了 1.1.4 的问题修复，
// 扩展了句柄、管道、文件、内存输入输出，
// 增加加密与 Unicode 支持，提供 Windows 风格接口，
// 并把实现集中到一个 cpp 文件。
// 原始源码：http://www.gzip.org/zlib/。
// 以下保留原始版权许可原文；本轮修改原因是统一中文注释和编码，变更内容仅限注释与 UTF-8 BOM。
// 中文说明：以下为 zlib 1.1.3（1998-07-09）的原始版权与许可，英文原文保留。
//
//
// zlib.h -- interface of the 'zlib' general purpose compression library
//  version 1.1.3, July 9th, 1998
// 版权所有：1995—1998 Jean-loup Gailly 和 Mark Adler。
//  Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler
// 免责声明译文：软件按现状提供，无任何明示或默示保证；作者不承担因使用本软件产生的损害。
//  This software is provided 'as-is', without any express or implied
//  warranty.  In no event will the authors be held liable for any damages
//  arising from the use of this software.
// 授权译文：允许任何人出于任何目的（包括商业用途）使用、修改和自由再分发软件，须遵守下列限制。
//  Permission is granted to anyone to use this software for any purpose,
//  including commercial applications, and to alter it and redistribute it
//  freely, subject to the following restrictions:
// 条款 1 译文：不得歪曲来源或声称编写了原始软件；在产品文档中致谢受到欢迎，但并非强制。
//  1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software
//     in a product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//     misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
// 条款 2、3 译文：修改版必须明确标注，不得冒充原版；任何源码分发不得删除或修改本声明。
//  Jean-loup Gailly        Mark Adler
//  jloup@gzip.org          madler@alumni.caltech.edu
// 作者与邮箱保持原文。
//
// zlib 的格式规范见 RFC 1950、1951、1952，
// 地址：ftp://ds.internic.net/rfc/rfc1950.txt，
// 分别对应 zlib、DEFLATE、gzip 格式；后两者文件名为 rfc1951.txt、rfc1952.txt。
//
//
// zlib 库提供内存压缩与解压，并校验未压缩数据的完整性。该版本
// 仅支持 DEFLATE；原接口设计允许以后增加使用相同流接口的其
// 他算法。
//
//
//
// 缓冲区足够大时可一次完成压缩，例如输入文件已映射到内存。也可反复
// 调用压缩函数；此时应用必须在每次调用前提供更多输入，或消费现有输
// 出以腾出输出空间。
//
//
//
// 库还支持以类似 stdio 的接口读取和写入 gzip（.gz）
// 文件。
//
// 库不安装信号处理程序。原说明要求解码器检查数据一致性，使损坏的输
// 入也不致导致崩溃；此处描述原设计目标。
//
//
// ZIP 格式参考：ftp://ftp.cdrom.com/pub/infozip/doc/appnote-970311-iz.zip。
// PKWare 规范参考：ftp://ftp.pkware.com/probdesc.zip。

#define ZIP_HANDLE   1
#define ZIP_FILENAME 2
#define ZIP_MEMORY   3


#define zmalloc(len) malloc(len)

#define zfree(p) free(p)

/*
void *zmalloc(unsigned int len)
{ char *buf = new char[len+32];
  for (int i=0; i<16; i++)
  { buf[i]=i;
    buf[len+31-i]=i;
  }
  *((unsigned int*)buf) = len;
  char c[1000]; wsprintf(c,"malloc 0x%lx  - %lu",buf+16,len);
  OutputDebugString(c);
  return buf+16;
}

void zfree(void *buf)
{ char c[1000]; wsprintf(c,"free   0x%lx",buf);
  OutputDebugString(c);
  char *p = ((char*)buf)-16;
  unsigned int len = *((unsigned int*)p);
  bool blown=false;
  for (int i=0; i<16; i++)
  { char lo = p[i];
    char hi = p[len+31-i];
    if (hi!=i || (lo!=i && i>4)) blown=true;
  }
  if (blown)
  { OutputDebugString("BLOWN!!!");
  }
  delete[] p;
}
*/


typedef struct tm_unz_s
{ unsigned int tm_sec;            // 秒数，范围 [0,59]。
  unsigned int tm_min;            // 分钟数，范围 [0,59]。
  unsigned int tm_hour;           // 小时数，范围 [0,23]。
  unsigned int tm_mday;           // 当月日期，范围 [1,31]。
  unsigned int tm_mon;            // 从一月起算的月份编号，范围 [0,11]。
  unsigned int tm_year;           // 年份，范围 [1980,2044]。
} tm_unz;


// unz_global_info 结构包含有关 ZIP 文件的全局数据
typedef struct unz_global_info_s
{ unsigned long number_entry;         // 该磁盘上中央目录中的条目总数
  unsigned long size_comment;         // zip 文件全局注释的大小
} unz_global_info;

// unz_file_info 包含有关 zip 文件中文件的信息
typedef struct unz_file_info_s
{ unsigned long version;              // 创建该归档时的版本信息，2 字节。
  unsigned long version_needed;       // 解压所需的最低版本，2 字节。
  unsigned long flag;                 // 通用位标志 2 字节
  unsigned long compression_method;   // 压缩方式 2字节
  unsigned long dosDate;              // DOS 格式的最后修改时间，4 字节。
  unsigned long crc;                  // crc-32 4 字节
  unsigned long compressed_size;      // 压缩大小 4 字节
  unsigned long uncompressed_size;    // 未压缩大小 4 字节
  unsigned long size_filename;        // 文件名长度2字节
  unsigned long size_file_extra;      // 额外字段长度 2 个字节
  unsigned long size_file_comment;    // 文件注释长度2字节
  unsigned long disk_num_start;       // 条目起始磁盘编号，2 字节。
  unsigned long internal_fa;          // 内部文件属性 2 字节
  unsigned long external_fa;          // 外部文件属性 4 字节
  tm_unz tmu_date;
} unz_file_info;


#define UNZ_OK                  (0)
#define UNZ_END_OF_LIST_OF_FILE (-100)
#define UNZ_ERRNO               (Z_ERRNO)
#define UNZ_EOF                 (0)
#define UNZ_PARAMERROR          (-102)
#define UNZ_BADZIPFILE          (-103)
#define UNZ_INTERNALERROR       (-104)
#define UNZ_CRCERROR            (-105)
#define UNZ_PASSWORD            (-106)







#define ZLIB_VERSION "1.1.3"


// 允许的冲洗值；详细信息请参见 deflate()
#define Z_NO_FLUSH      0
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4


// 压缩级别
#define Z_NO_COMPRESSION         0
#define Z_BEST_SPEED             1
#define Z_BEST_COMPRESSION       9
#define Z_DEFAULT_COMPRESSION  (-1)

// 压缩策略；详细信息请参见 deflateInit2()
#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_DEFAULT_STRATEGY    0

// data_type 字段的可能值
#define Z_BINARY   0
#define Z_ASCII    1
#define Z_UNKNOWN  2

// DEFLATE 压缩方式，本版本唯一支持的压缩方式。
#define Z_DEFLATED   8

// 用于初始化 zalloc、zfree、opaque
#define Z_NULL  0

// 搜索文件名时区分大小写
#define CASE_SENSITIVE 1
#define CASE_INSENSITIVE 2


// 压缩/解压缩函数的返回代码。阴性
// 值是错误的，正值用于特殊但正常的事件。
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)



// 基本数据类型
typedef unsigned char  Byte;  // 8位
typedef unsigned int   uInt;  // 16 位或更多
typedef unsigned long  uLong; // 32 位或更多
typedef void *voidpf;
typedef void     *voidp;
typedef long z_off_t;












typedef voidpf (*alloc_func) (voidpf opaque, uInt items, uInt size);
typedef void   (*free_func)  (voidpf opaque, voidpf address);

struct internal_state;

typedef struct z_stream_s {
    Byte    *next_in;  // 下一个输入字节
    uInt     avail_in;  // next_in 可用的字节数
    uLong    total_in;  // 到目前为止读取的输入字节总数 nb

    Byte    *next_out; // 下一个输出字节应该放在那里
    uInt     avail_out; // next_out 处的剩余可用空间
    uLong    total_out; // 到目前为止输出的总字节数

    char     *msg;      // 最后的错误消息，如果没有错误则为 NULL
    struct internal_state *state; // 应用程序不可见

    alloc_func zalloc;  // 用于分配内部状态
    free_func  zfree;   // 用于释放内部状态
    voidpf     opaque;  // 传递给 zalloc 和 zfree 的私有数据对象

    int     data_type;  // 关于数据类型的最佳猜测：ascii 或二进制
    uLong   adler;      // 未压缩数据的 adler32 值
    uLong   reserved;   // 保留供将来使用。
} z_stream;

typedef z_stream *z_streamp;


// avail_in 降到 0 时，应用必须更新 next_in 与
// avail_in；avail_out 降到 0 时更新
// next_out 与 avail_out。调用初始化函数前须设置
// zalloc、zfree 和 opaque；其余字段由库设置，应用
// 不得改写。
//
// 应用提供的 opaque 将作为 zalloc 和 zfree
// 的第一个参数，便于自定义内存管理；压缩库本身不解释该值。
//
//
//
// 内存不足时 zalloc 必须返回 Z_NULL；在多线程应用中
// 使用 zlib 时，zalloc 和 zfree 必须是线程安全
// 的。
//
// total_in 和 total_out 可用于统计或进度报告。
// 压缩结束后 total_in 为未压缩数据总大小，可保存给解压器
// ，尤其适合需要一次性解压全部数据的场景。
//
//
//


// 基础函数。

const char *zlibVersion ();
// 应用可比较 zlibVersion 与 ZLIB_VERSION
// 。首字符不同表明实际库与应用使用的 zlib.h 不兼容；
// inflateInit 会自动执行此检查。
//






int inflate (z_streamp strm, int flush);
//
// inflate 尽量解压数据，输入耗尽或输出缓冲区满时停止。除非
// 强制刷新，否则可能存在输出延迟，即已经读取输入但暂未生成输出。
//
//
//
// inflate 每次执行以下一个或两个动作：
//
//
// 从 next_in 继续解压并更新 next_in、
// avail_in。若输出空间不足而无法处理全部输入，则记录更新后
// 的输入位置，下次调用从该位置继续。
//
//
// 从 next_out 开始生成输出并更新 next_out、
// avail_out，直到输入耗尽或输出空间不足。flush 参数
// 会影响此行为，详见后文。
//
//
// 调用 inflate 前，应用必须通过提供输入或消费输出，使至少
// 一个动作可以执行，并更新 next_* 和 avail_*。应用
// 可在缓冲区满时或每次调用后处理输出；若返回 Z_OK 且
// avail_out 为 0，应腾出空间后继续调用，因为仍可能有待
// 输出数据。
//
//
//
//
// flush 为 Z_SYNC_FLUSH 时，inflate 尽
// 量将数据刷新到输出缓冲区。接口未规定 Z_SYNC_FLUSH、
// Z_FINISH 之外取值的刷新行为，不过当前实现仍尽量输出全部
// 可用数据。
//
//
// 通常应持续调用 inflate，直到 Z_STREAM_END
// 或错误。若要求一次调用完成全部解压，设置 Z_FINISH，并为
//  avail_out 提供足以容纳全部未压缩数据的空间。随后必须
// 调用 inflateEnd 释放状态。Z_FINISH 并非必需
// ，但可提示使用适合单次解压的更快处理过程。
//
//
//
//
//
//
// 需要预置字典时，inflate 将 strm->adler 设为
// 压缩方使用的字典校验值并返回 Z_NEED_DICT。否则该字段
// 保存已输出数据的 Adler-32，返回 Z_OK、
// Z_STREAM_END 或错误码。流结束时会比较计算值与归档值
// ，仅校验一致才返回 Z_STREAM_END。
//
//
//
//
// 返回值含义：Z_OK 表示消耗输入或生成输出；
// Z_STREAM_END 表示压缩流结束且输出完成；
// Z_NEED_DICT 表示需要字典；Z_DATA_ERROR
// 表示格式损坏或 Adler-32 不匹配；
// Z_STREAM_ERROR 表示流结构无效，例如
// next_in 或 next_out 为空；
// Z_MEM_ERROR 表示内存不足；Z_BUF_ERROR 表
// 示无法继续，或 Z_FINISH 下输出空间不足。发生
// Z_DATA_ERROR 时，可调用 inflateSync 寻
// 找下一个有效压缩块。
//
//


int inflateEnd (z_streamp strm);
//
// 释放此流动态分配的全部数据结构，丢弃未处理输入，不刷新待输出内容
// 。
//
//
// inflateEnd 成功返回 Z_OK，流状态不一致时返回
// Z_STREAM_ERROR。出错时 msg 可能指向静态字符串
// ，不得释放该字符串。

                        // 高级函数。

// 以下函数仅用于部分特殊应用。





int inflateSetDictionary (z_streamp strm,
                                             const Byte *dictionary,
                                             uInt  dictLength);
//
// 用给定的未压缩字节序列初始化解压字典。inflate 返回
// Z_NEED_DICT 后必须立即调用此函数；可根据
// inflate 返回的 Adler32 值识别所需字典。压缩和解
// 压必须使用完全相同的字典。
//
//
//
// inflateSetDictionary 成功返回 Z_OK；参
// 数无效（如字典为空）或流状态不一致时返回
// Z_STREAM_ERROR；字典 Adler32 不匹配时返回
//  Z_DATA_ERROR。此函数不执行解压，解压由后续
// inflate 调用完成。
//


int inflateSync (z_streamp strm);
//
// 跳过无效压缩输入，直到找到完整刷新点或全部可用输入均已跳过；不产
// 生输出。
//
// inflateSync 找到完整刷新点时返回 Z_OK，无输入时
// 返回 Z_BUF_ERROR，未找到刷新点时返回
// Z_DATA_ERROR，流结构无效时返回
// Z_STREAM_ERROR。成功后可保存 total_in 以
// 记录有效数据位置；失败后可反复提供更多输入并重试，直到成功或输入
// 结束。
//


int inflateReset (z_streamp strm);
// 此函数相当于先 inflateEnd 再 inflateInit
// ，但不释放并重新分配内部解压状态，且保留
// inflateInit2 所设置的属性。
//
// inflateReset 成功返回 Z_OK；源流状态不一致时（如 zalloc
// 或 state 为空）返回 Z_STREAM_ERROR。
//



// 校验和函数。
// 这些函数虽不直接执行压缩，仍作为接口导出，方便压缩库使用方进行数
// 据校验。
//

uLong adler32 (uLong adler, const Byte *buf, uInt len);
// 用 buf[0..len-1] 增量更新 Adler-32 并返
// 回新值；buf 为空时返回校验和的初始值。
//
// Adler-32 的可靠性接近 CRC32，计算速度更快。以下为
// 使用示例：
//
//     uLong adler = adler32(0L, Z_NULL, 0);
//
//     while (read_buffer(buffer, length) != EOF) {
//       adler = adler32(adler, buffer, length);
//     }
//     if (adler != original_adler) error();

uLong ucrc32   (uLong crc, const Byte *buf, uInt len);
// 使用 buf[0..len-1] 增量更新 CRC，并返回更新后的值。
// buf 为 NULL 时返回 CRC 所需的初始值。
// 函数内部完成前置与后置处理（按位取反），
// 应用程序不应重复执行这些处理。
// 使用示例：
//
//     uLong crc = crc32(0L, Z_NULL, 0);
//
//     while (read_buffer(buffer, length) != EOF) {
//       crc = crc32(crc, buffer, length);
//     }
//     if (crc != original_crc) error();




const char   *zError           (int err);
int           inflateSyncPoint (z_streamp z);
const uLong *get_crc_table    (void);



typedef unsigned char  uch;
typedef uch uchf;
typedef unsigned short ush;
typedef ush ushf;
typedef unsigned long  ulg;



const char * const z_errmsg[10] = { // 由 2-zlib_error 索引
"need dictionary",     // Z_NEED_DICT = 2：需要预置字典。
"stream end",          // Z_STREAM_END = 1：数据流结束。
"",                    // Z_OK = 0：成功。
"file error",          // Z_ERRNO = -1：系统错误。
"stream error",        // Z_STREAM_ERROR = -2：流状态错误。
"data error",          // Z_DATA_ERROR = -3：数据错误。
"insufficient memory", // Z_MEM_ERROR = -4：内存不足。
"buffer error",        // Z_BUF_ERROR = -5：缓冲区不足或无法继续。
"incompatible version",// Z_VERSION_ERROR = -6：版本不兼容。
""};


#define ERR_MSG(err) z_errmsg[Z_NEED_DICT-(err)]

#define ERR_RETURN(strm,err) \
  return (strm->msg = (char*)ERR_MSG(err), (err))
// 仅在已知状态有效时使用

        // 公共常量。


#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES    2
// 三种块类型

#define MIN_MATCH  3
#define MAX_MATCH  258
// 最小和最大匹配长度

#define PRESET_DICT 0x20 // zlib 标头中预设字典标志

        // 目标依赖关系

#define OS_CODE  0x0b  // Windows 95 和 Windows NT



         // 函数声明。

#define zmemzero(dest, len) memset(dest, 0, len)

// 诊断函数。
#define LuAssert(cond,msg)
#define LuTrace(x)
#define LuTracev(x)
#define LuTracevv(x)
#define LuTracec(c,x)
#define LuTracecv(c,x)


typedef uLong (*check_func) (uLong check, const Byte *buf, uInt len);
voidpf zcalloc (voidpf opaque, unsigned items, unsigned size);
void   zcfree  (voidpf opaque, voidpf ptr);

#define ZALLOC(strm, items, size) \
           (*((strm)->zalloc))((strm)->opaque, (items), (size))
#define ZFREE(strm, addr)  (*((strm)->zfree))((strm)->opaque, (voidpf)(addr))

//void ZFREE(z_streamp strm,voidpf addr)
//{ *((strm)->zfree))((strm)->opaque, addr);
//}

#define TRY_FREE(s, p) {if (p) ZFREE(s, p);}




// 哈夫曼查找表项；在使用 16 位指针的系统上占 4 字节，例如采
// 用小型或中型内存模型的 PC。


typedef struct inflate_huft_s inflate_huft;

struct inflate_huft_s {
  union {
    struct {
      Byte Exop;        // 附加位数或操作码。
      Byte Bits;        // 当前编码或子编码的位数。
    } what;
    uInt pad;           // 填充结构体，使其大小为 2 的幂；16 位 int 系统上为 4 字
  } word;               // 节，32 位 int 系统上为 8 字节。
  uInt base;            // 字面值、长度基值、距离基值或子表偏移。
};

// 动态树的最大存储量。长时间非穷尽搜索发现最多需要 1004 个
// huft 节点：长度/字面值用 850 个，距离用 154 个，
// 其中距离部分为穷尽结果。真实总上限未知，下述取值留有余量。
//
//
#define MANY 1440

int inflate_trees_bits (
    uInt *,                    // 19 个码长。
    uInt *,                    // 码长树期望的查表位数与实际位数。
    inflate_huft * *,       // 生成的码长树。
    inflate_huft *,             // 树节点存储空间。
    z_streamp);                // 用于返回错误消息。

int inflate_trees_dynamic (
    uInt,                       // 字面值/长度编码数量。
    uInt,                       // 距离编码数量。
    uInt *,                    // 需要处理的码长总数。
    uInt *,                    // 字面值树期望的查表位数与实际位数。
    uInt *,                    // 距离树期望的查表位数与实际位数。
    inflate_huft * *,       // 生成的字面值/长度树。
    inflate_huft * *,       // 生成的距离树。
    inflate_huft *,             // 树节点存储空间。
    z_streamp);                // 用于消息

int inflate_trees_fixed (
    uInt *,                    // 字面值树期望的查表位数与实际位数。
    uInt *,                    // 距离树期望的查表位数与实际位数。
    const inflate_huft * *,       // 生成的字面值/长度树。
    const inflate_huft * *,       // 生成的距离树。
    z_streamp);                // 用于内存分配





struct inflate_blocks_state;
typedef struct inflate_blocks_state inflate_blocks_statef;

inflate_blocks_statef * inflate_blocks_new (
    z_streamp z,
    check_func c,               // 检查功能
    uInt w);                   // 窗口大小

int inflate_blocks (
    inflate_blocks_statef *,
    z_streamp ,
    int);                      // 初始返回码

void inflate_blocks_reset (
    inflate_blocks_statef *,
    z_streamp ,
    uLong *);                  // 检查输出值

int inflate_blocks_free (
    inflate_blocks_statef *,
    z_streamp);

void inflate_set_dictionary (
    inflate_blocks_statef *s,
    const Byte *d,  // 字典
    uInt  n);       // 字典长度

int inflate_blocks_sync_point (
    inflate_blocks_statef *s);




struct inflate_codes_state;
typedef struct inflate_codes_state inflate_codes_statef;

inflate_codes_statef *inflate_codes_new (
    uInt, uInt,
    const inflate_huft *, const inflate_huft *,
    z_streamp );

int inflate_codes (
    inflate_blocks_statef *,
    z_streamp ,
    int);

void inflate_codes_free (
    inflate_codes_statef *,
    z_streamp );




typedef enum {
      IBM_TYPE,     // 获取类型位（3，包括结束位）
      IBM_LENS,     // 获取存储的长度
      IBM_STORED,   // 处理存储块
      IBM_TABLE,    // 获取表长度
      IBM_BTREE,    // 读取动态块的码长树。
      IBM_DTREE,    // 获取动态块的长度、距离树
      IBM_CODES,    // 处理固定或动态块
      IBM_DRY,      // 输出剩余窗口字节
      IBM_DONE,     // 完成最后一个区块，完成
      IBM_BAD}      // 已发生数据错误，停止在此状态。
inflate_block_mode;

// inflate 区块半私有状态
struct inflate_blocks_state {

  // 模式
  inflate_block_mode  mode;     // 当前的 inflate_block 模式

  // 模式相关信息
  union {
    uInt left;          // STORED 状态下还需要复制的字节数。
    struct {
      uInt table;               // 表长度（14 位）
      uInt index;               // 在 blens 或 border 中的索引。
      uInt *blens;             // 代码的位长度
      uInt bb;                  // 位长树深度
      inflate_huft *tb;         // 位长解码树
    } trees;            // DTREE 状态下树的解码信息。
    struct {
      inflate_codes_statef 
         *codes;
    } decode;           // CODES 状态下当前编码的处理状态。
  } sub;                // 子模式
  uInt last;            // 如果该块是最后一个块则为 true

  // 模式独立信息
  uInt bitk;            // 位缓冲区中的位
  uLong bitb;           // 位缓冲器
  inflate_huft *hufts;  // 用于树节点的一次性内存分配。
  Byte *window;        // 滑动窗
  Byte *end;           // 滑动窗口末尾之后的位置。
  Byte *read;          // 窗口读指针
  Byte *write;         // 窗口写指针
  check_func checkfn;   // 检查功能
  uLong check;          // 检查输出

};


// inflate 输入输出相关宏定义。
// 更新指针并返回
#define UPDBITS {s->bitb=b;s->bitk=k;}
#define UPDIN {z->avail_in=n;z->total_in+=(uLong)(p-z->next_in);z->next_in=p;}
#define UPDOUT {s->write=q;}
#define UPDATE {UPDBITS UPDIN UPDOUT}
#define LEAVE {UPDATE return inflate_flush(s,z,r);}
// 获取字节和位
#define LOADIN {p=z->next_in;n=z->avail_in;b=s->bitb;k=s->bitk;}
#define NEEDBYTE {if(n)r=Z_OK;else LEAVE}
#define NEXTBYTE (n--,*p++)
#define NEEDBITS(j) {while(k<(j)){NEEDBYTE;b|=((uLong)NEXTBYTE)<<k;k+=8;}}
#define DUMPBITS(j) {b>>=(j);k-=(j);}
// 输出字节
#define WAVAIL (uInt)(q<s->read?s->read-q-1:s->end-q)
#define LOADOUT {q=s->write;m=(uInt)WAVAIL;m;}
#define WRAP {if(q==s->end&&s->read!=s->window){q=s->window;m=(uInt)WAVAIL;}}
#define FLUSH {UPDOUT r=inflate_flush(s,z,r); LOADOUT}
#define NEEDOUT {if(m==0){WRAP if(m==0){FLUSH WRAP if(m==0) LEAVE}}r=Z_OK;}
#define OUTBYTE(a) {*q++=(Byte)(a);m--;}
// 加载本地指针
#define LOAD {LOADIN LOADOUT}

// 低位掩码；显式指定数组长度以避免 Visual C++ 警告。
// 与 mask[n] 按位与运算可取出最低 n 位。
const uInt inflate_mask[17] = {
    0x0000,
    0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

// 尽可能多地从滑动窗口复制到输出区域
int inflate_flush (inflate_blocks_statef *, z_streamp, int);

int inflate_fast (uInt, uInt, const inflate_huft *, const inflate_huft *, inflate_blocks_statef *, z_streamp );



const uInt fixed_bl = 9;
const uInt fixed_bd = 5;
const inflate_huft fixed_tl[] = {
    {{{96,7}},256}, {{{0,8}},80}, {{{0,8}},16}, {{{84,8}},115},
    {{{82,7}},31}, {{{0,8}},112}, {{{0,8}},48}, {{{0,9}},192},
    {{{80,7}},10}, {{{0,8}},96}, {{{0,8}},32}, {{{0,9}},160},
    {{{0,8}},0}, {{{0,8}},128}, {{{0,8}},64}, {{{0,9}},224},
    {{{80,7}},6}, {{{0,8}},88}, {{{0,8}},24}, {{{0,9}},144},
    {{{83,7}},59}, {{{0,8}},120}, {{{0,8}},56}, {{{0,9}},208},
    {{{81,7}},17}, {{{0,8}},104}, {{{0,8}},40}, {{{0,9}},176},
    {{{0,8}},8}, {{{0,8}},136}, {{{0,8}},72}, {{{0,9}},240},
    {{{80,7}},4}, {{{0,8}},84}, {{{0,8}},20}, {{{85,8}},227},
    {{{83,7}},43}, {{{0,8}},116}, {{{0,8}},52}, {{{0,9}},200},
    {{{81,7}},13}, {{{0,8}},100}, {{{0,8}},36}, {{{0,9}},168},
    {{{0,8}},4}, {{{0,8}},132}, {{{0,8}},68}, {{{0,9}},232},
    {{{80,7}},8}, {{{0,8}},92}, {{{0,8}},28}, {{{0,9}},152},
    {{{84,7}},83}, {{{0,8}},124}, {{{0,8}},60}, {{{0,9}},216},
    {{{82,7}},23}, {{{0,8}},108}, {{{0,8}},44}, {{{0,9}},184},
    {{{0,8}},12}, {{{0,8}},140}, {{{0,8}},76}, {{{0,9}},248},
    {{{80,7}},3}, {{{0,8}},82}, {{{0,8}},18}, {{{85,8}},163},
    {{{83,7}},35}, {{{0,8}},114}, {{{0,8}},50}, {{{0,9}},196},
    {{{81,7}},11}, {{{0,8}},98}, {{{0,8}},34}, {{{0,9}},164},
    {{{0,8}},2}, {{{0,8}},130}, {{{0,8}},66}, {{{0,9}},228},
    {{{80,7}},7}, {{{0,8}},90}, {{{0,8}},26}, {{{0,9}},148},
    {{{84,7}},67}, {{{0,8}},122}, {{{0,8}},58}, {{{0,9}},212},
    {{{82,7}},19}, {{{0,8}},106}, {{{0,8}},42}, {{{0,9}},180},
    {{{0,8}},10}, {{{0,8}},138}, {{{0,8}},74}, {{{0,9}},244},
    {{{80,7}},5}, {{{0,8}},86}, {{{0,8}},22}, {{{192,8}},0},
    {{{83,7}},51}, {{{0,8}},118}, {{{0,8}},54}, {{{0,9}},204},
    {{{81,7}},15}, {{{0,8}},102}, {{{0,8}},38}, {{{0,9}},172},
    {{{0,8}},6}, {{{0,8}},134}, {{{0,8}},70}, {{{0,9}},236},
    {{{80,7}},9}, {{{0,8}},94}, {{{0,8}},30}, {{{0,9}},156},
    {{{84,7}},99}, {{{0,8}},126}, {{{0,8}},62}, {{{0,9}},220},
    {{{82,7}},27}, {{{0,8}},110}, {{{0,8}},46}, {{{0,9}},188},
    {{{0,8}},14}, {{{0,8}},142}, {{{0,8}},78}, {{{0,9}},252},
    {{{96,7}},256}, {{{0,8}},81}, {{{0,8}},17}, {{{85,8}},131},
    {{{82,7}},31}, {{{0,8}},113}, {{{0,8}},49}, {{{0,9}},194},
    {{{80,7}},10}, {{{0,8}},97}, {{{0,8}},33}, {{{0,9}},162},
    {{{0,8}},1}, {{{0,8}},129}, {{{0,8}},65}, {{{0,9}},226},
    {{{80,7}},6}, {{{0,8}},89}, {{{0,8}},25}, {{{0,9}},146},
    {{{83,7}},59}, {{{0,8}},121}, {{{0,8}},57}, {{{0,9}},210},
    {{{81,7}},17}, {{{0,8}},105}, {{{0,8}},41}, {{{0,9}},178},
    {{{0,8}},9}, {{{0,8}},137}, {{{0,8}},73}, {{{0,9}},242},
    {{{80,7}},4}, {{{0,8}},85}, {{{0,8}},21}, {{{80,8}},258},
    {{{83,7}},43}, {{{0,8}},117}, {{{0,8}},53}, {{{0,9}},202},
    {{{81,7}},13}, {{{0,8}},101}, {{{0,8}},37}, {{{0,9}},170},
    {{{0,8}},5}, {{{0,8}},133}, {{{0,8}},69}, {{{0,9}},234},
    {{{80,7}},8}, {{{0,8}},93}, {{{0,8}},29}, {{{0,9}},154},
    {{{84,7}},83}, {{{0,8}},125}, {{{0,8}},61}, {{{0,9}},218},
    {{{82,7}},23}, {{{0,8}},109}, {{{0,8}},45}, {{{0,9}},186},
    {{{0,8}},13}, {{{0,8}},141}, {{{0,8}},77}, {{{0,9}},250},
    {{{80,7}},3}, {{{0,8}},83}, {{{0,8}},19}, {{{85,8}},195},
    {{{83,7}},35}, {{{0,8}},115}, {{{0,8}},51}, {{{0,9}},198},
    {{{81,7}},11}, {{{0,8}},99}, {{{0,8}},35}, {{{0,9}},166},
    {{{0,8}},3}, {{{0,8}},131}, {{{0,8}},67}, {{{0,9}},230},
    {{{80,7}},7}, {{{0,8}},91}, {{{0,8}},27}, {{{0,9}},150},
    {{{84,7}},67}, {{{0,8}},123}, {{{0,8}},59}, {{{0,9}},214},
    {{{82,7}},19}, {{{0,8}},107}, {{{0,8}},43}, {{{0,9}},182},
    {{{0,8}},11}, {{{0,8}},139}, {{{0,8}},75}, {{{0,9}},246},
    {{{80,7}},5}, {{{0,8}},87}, {{{0,8}},23}, {{{192,8}},0},
    {{{83,7}},51}, {{{0,8}},119}, {{{0,8}},55}, {{{0,9}},206},
    {{{81,7}},15}, {{{0,8}},103}, {{{0,8}},39}, {{{0,9}},174},
    {{{0,8}},7}, {{{0,8}},135}, {{{0,8}},71}, {{{0,9}},238},
    {{{80,7}},9}, {{{0,8}},95}, {{{0,8}},31}, {{{0,9}},158},
    {{{84,7}},99}, {{{0,8}},127}, {{{0,8}},63}, {{{0,9}},222},
    {{{82,7}},27}, {{{0,8}},111}, {{{0,8}},47}, {{{0,9}},190},
    {{{0,8}},15}, {{{0,8}},143}, {{{0,8}},79}, {{{0,9}},254},
    {{{96,7}},256}, {{{0,8}},80}, {{{0,8}},16}, {{{84,8}},115},
    {{{82,7}},31}, {{{0,8}},112}, {{{0,8}},48}, {{{0,9}},193},
    {{{80,7}},10}, {{{0,8}},96}, {{{0,8}},32}, {{{0,9}},161},
    {{{0,8}},0}, {{{0,8}},128}, {{{0,8}},64}, {{{0,9}},225},
    {{{80,7}},6}, {{{0,8}},88}, {{{0,8}},24}, {{{0,9}},145},
    {{{83,7}},59}, {{{0,8}},120}, {{{0,8}},56}, {{{0,9}},209},
    {{{81,7}},17}, {{{0,8}},104}, {{{0,8}},40}, {{{0,9}},177},
    {{{0,8}},8}, {{{0,8}},136}, {{{0,8}},72}, {{{0,9}},241},
    {{{80,7}},4}, {{{0,8}},84}, {{{0,8}},20}, {{{85,8}},227},
    {{{83,7}},43}, {{{0,8}},116}, {{{0,8}},52}, {{{0,9}},201},
    {{{81,7}},13}, {{{0,8}},100}, {{{0,8}},36}, {{{0,9}},169},
    {{{0,8}},4}, {{{0,8}},132}, {{{0,8}},68}, {{{0,9}},233},
    {{{80,7}},8}, {{{0,8}},92}, {{{0,8}},28}, {{{0,9}},153},
    {{{84,7}},83}, {{{0,8}},124}, {{{0,8}},60}, {{{0,9}},217},
    {{{82,7}},23}, {{{0,8}},108}, {{{0,8}},44}, {{{0,9}},185},
    {{{0,8}},12}, {{{0,8}},140}, {{{0,8}},76}, {{{0,9}},249},
    {{{80,7}},3}, {{{0,8}},82}, {{{0,8}},18}, {{{85,8}},163},
    {{{83,7}},35}, {{{0,8}},114}, {{{0,8}},50}, {{{0,9}},197},
    {{{81,7}},11}, {{{0,8}},98}, {{{0,8}},34}, {{{0,9}},165},
    {{{0,8}},2}, {{{0,8}},130}, {{{0,8}},66}, {{{0,9}},229},
    {{{80,7}},7}, {{{0,8}},90}, {{{0,8}},26}, {{{0,9}},149},
    {{{84,7}},67}, {{{0,8}},122}, {{{0,8}},58}, {{{0,9}},213},
    {{{82,7}},19}, {{{0,8}},106}, {{{0,8}},42}, {{{0,9}},181},
    {{{0,8}},10}, {{{0,8}},138}, {{{0,8}},74}, {{{0,9}},245},
    {{{80,7}},5}, {{{0,8}},86}, {{{0,8}},22}, {{{192,8}},0},
    {{{83,7}},51}, {{{0,8}},118}, {{{0,8}},54}, {{{0,9}},205},
    {{{81,7}},15}, {{{0,8}},102}, {{{0,8}},38}, {{{0,9}},173},
    {{{0,8}},6}, {{{0,8}},134}, {{{0,8}},70}, {{{0,9}},237},
    {{{80,7}},9}, {{{0,8}},94}, {{{0,8}},30}, {{{0,9}},157},
    {{{84,7}},99}, {{{0,8}},126}, {{{0,8}},62}, {{{0,9}},221},
    {{{82,7}},27}, {{{0,8}},110}, {{{0,8}},46}, {{{0,9}},189},
    {{{0,8}},14}, {{{0,8}},142}, {{{0,8}},78}, {{{0,9}},253},
    {{{96,7}},256}, {{{0,8}},81}, {{{0,8}},17}, {{{85,8}},131},
    {{{82,7}},31}, {{{0,8}},113}, {{{0,8}},49}, {{{0,9}},195},
    {{{80,7}},10}, {{{0,8}},97}, {{{0,8}},33}, {{{0,9}},163},
    {{{0,8}},1}, {{{0,8}},129}, {{{0,8}},65}, {{{0,9}},227},
    {{{80,7}},6}, {{{0,8}},89}, {{{0,8}},25}, {{{0,9}},147},
    {{{83,7}},59}, {{{0,8}},121}, {{{0,8}},57}, {{{0,9}},211},
    {{{81,7}},17}, {{{0,8}},105}, {{{0,8}},41}, {{{0,9}},179},
    {{{0,8}},9}, {{{0,8}},137}, {{{0,8}},73}, {{{0,9}},243},
    {{{80,7}},4}, {{{0,8}},85}, {{{0,8}},21}, {{{80,8}},258},
    {{{83,7}},43}, {{{0,8}},117}, {{{0,8}},53}, {{{0,9}},203},
    {{{81,7}},13}, {{{0,8}},101}, {{{0,8}},37}, {{{0,9}},171},
    {{{0,8}},5}, {{{0,8}},133}, {{{0,8}},69}, {{{0,9}},235},
    {{{80,7}},8}, {{{0,8}},93}, {{{0,8}},29}, {{{0,9}},155},
    {{{84,7}},83}, {{{0,8}},125}, {{{0,8}},61}, {{{0,9}},219},
    {{{82,7}},23}, {{{0,8}},109}, {{{0,8}},45}, {{{0,9}},187},
    {{{0,8}},13}, {{{0,8}},141}, {{{0,8}},77}, {{{0,9}},251},
    {{{80,7}},3}, {{{0,8}},83}, {{{0,8}},19}, {{{85,8}},195},
    {{{83,7}},35}, {{{0,8}},115}, {{{0,8}},51}, {{{0,9}},199},
    {{{81,7}},11}, {{{0,8}},99}, {{{0,8}},35}, {{{0,9}},167},
    {{{0,8}},3}, {{{0,8}},131}, {{{0,8}},67}, {{{0,9}},231},
    {{{80,7}},7}, {{{0,8}},91}, {{{0,8}},27}, {{{0,9}},151},
    {{{84,7}},67}, {{{0,8}},123}, {{{0,8}},59}, {{{0,9}},215},
    {{{82,7}},19}, {{{0,8}},107}, {{{0,8}},43}, {{{0,9}},183},
    {{{0,8}},11}, {{{0,8}},139}, {{{0,8}},75}, {{{0,9}},247},
    {{{80,7}},5}, {{{0,8}},87}, {{{0,8}},23}, {{{192,8}},0},
    {{{83,7}},51}, {{{0,8}},119}, {{{0,8}},55}, {{{0,9}},207},
    {{{81,7}},15}, {{{0,8}},103}, {{{0,8}},39}, {{{0,9}},175},
    {{{0,8}},7}, {{{0,8}},135}, {{{0,8}},71}, {{{0,9}},239},
    {{{80,7}},9}, {{{0,8}},95}, {{{0,8}},31}, {{{0,9}},159},
    {{{84,7}},99}, {{{0,8}},127}, {{{0,8}},63}, {{{0,9}},223},
    {{{82,7}},27}, {{{0,8}},111}, {{{0,8}},47}, {{{0,9}},191},
    {{{0,8}},15}, {{{0,8}},143}, {{{0,8}},79}, {{{0,9}},255}
  };
const inflate_huft fixed_td[] = {
    {{{80,5}},1}, {{{87,5}},257}, {{{83,5}},17}, {{{91,5}},4097},
    {{{81,5}},5}, {{{89,5}},1025}, {{{85,5}},65}, {{{93,5}},16385},
    {{{80,5}},3}, {{{88,5}},513}, {{{84,5}},33}, {{{92,5}},8193},
    {{{82,5}},9}, {{{90,5}},2049}, {{{86,5}},129}, {{{192,5}},24577},
    {{{80,5}},2}, {{{87,5}},385}, {{{83,5}},25}, {{{91,5}},6145},
    {{{81,5}},7}, {{{89,5}},1537}, {{{85,5}},97}, {{{93,5}},24577},
    {{{80,5}},4}, {{{88,5}},769}, {{{84,5}},49}, {{{92,5}},12289},
    {{{82,5}},13}, {{{90,5}},3073}, {{{86,5}},193}, {{{192,5}},24577}
  };







// 尽可能多地从滑动窗口复制到输出区域
int inflate_flush(inflate_blocks_statef *s,z_streamp z,int r)
{
  uInt n;
  Byte *p;
  Byte *q;

  // 源指针和目标指针的本地副本
  p = z->next_out;
  q = s->read;

  // 计算要复制到窗口末尾的字节数
  n = (uInt)((q <= s->write ? s->write : s->end) - q);
  if (n > z->avail_out) n = z->avail_out;
  if (n && r == Z_BUF_ERROR) r = Z_OK;

  // 更新计数器
  z->avail_out -= n;
  z->total_out += n;

  // 更新检查信息
  if (s->checkfn != Z_NULL)
    z->adler = s->check = (*s->checkfn)(s->check, q, n);

  // 复制到窗口末尾
  if (n!=0)          // 检查 n!=0 以避免唤醒 CodeGuard
  { memcpy(p, q, n);
    p += n;
    q += n;
  }

  // 查看是否有更多内容要复制到窗口开头
  if (q == s->end)
  {
    // 将循环窗口指针绕回起点。
    q = s->window;
    if (s->write == s->end)
      s->write = s->window;

    // 计算要复制的字节
    n = (uInt)(s->write - q);
    if (n > z->avail_out) n = z->avail_out;
    if (n && r == Z_BUF_ERROR) r = Z_OK;

    // 更新计数器
    z->avail_out -= n;
    z->total_out += n;

    // 更新检查信息
    if (s->checkfn != Z_NULL)
      z->adler = s->check = (*s->checkfn)(s->check, q, n);

    // 复制
    if (n!=0) {memcpy(p,q,n); p+=n; q+=n;}
  }

  // 更新指针
  z->next_out = p;
  s->read = q;

  // 完成
  return r;
}






// 通过一些定义简化 inflate_huft 类型的使用
#define exop word.what.Exop
#define bits word.what.Bits

typedef enum {        // 等待“i:”=输入，“o:”=输出，“x:”=无
      START,    // x：为 LEN 设置
      LEN,      // i：接下来获取长度/字面值/eob
      LENEXT,   // i：已取得长度基值，继续读取长度附加位。
      DIST,     // i：下一步获取距离
      DISTEXT,  // i：继续读取距离附加位。
      COPY,     // o：在窗口中复制字节，等待空间
      LIT,      // o：已获取字面值，正在等待输出空间
      WASH,     // o：得到 eob，可能仍在输出等待
      END,      // x：获得 eob 并刷新所有数据
      BADCODE}  // x：出现错误
inflate_codes_mode;

// inflate 编码处理的私有状态。
struct inflate_codes_state {

  // 模式
  inflate_codes_mode mode;      // 当前的 inflate_codes 模式

  // 模式相关信息
  uInt len;
  union {
    struct {
      const inflate_huft *tree;       // 指向树的指针
      uInt need;                // 需要位
    } code;             // 如果是 LEN 或 DIST，则在树中的哪个位置
    uInt lit;           // 如果 LIT，则为字面值
    struct {
      uInt get;                 // 需要读取的附加位数。
      uInt dist;                // 回溯复制的距离。
    } copy;             // EXT 或 COPY 状态下要复制的位置和长度。
  } sub;                // 子模式

  // 模式独立信息
  Byte lbits;           // 字面值树每次分支解码的位数。
  Byte dbits;           // 距离树每次分支解码的位数。
  const inflate_huft *ltree;          // 字面值/长度/eob 树
  const inflate_huft *dtree;          // 距离树

};


inflate_codes_statef *inflate_codes_new(
uInt bl, uInt bd,
const inflate_huft *tl,
const inflate_huft *td, // Borland C++ 需要单独声明
z_streamp z)
{
  inflate_codes_statef *c;

  if ((c = (inflate_codes_statef *)
       ZALLOC(z,1,sizeof(struct inflate_codes_state))) != Z_NULL)
  {
    c->mode = START;
    c->lbits = (Byte)bl;
    c->dbits = (Byte)bd;
    c->ltree = tl;
    c->dtree = td;
    LuTracev((stderr, "inflate:       codes new\n"));
  }
  return c;
}


int inflate_codes(inflate_blocks_statef *s, z_streamp z, int r)
{
  uInt j;               // 临时储存
  const inflate_huft *t;      // 临时指针
  uInt e;               // 额外的位或操作
  uLong b;              // 位缓冲器
  uInt k;               // 位缓冲区中的位
  Byte *p;             // 输入数据指针
  uInt n;               // 那里可用的字节数
  Byte *q;             // 输出窗口写指针
  uInt m;               // 到窗口末尾或读指针的字节
  Byte *f;             // 从中复制字符串的指针
  inflate_codes_statef *c = s->sub.decode.codes;  // 编码处理状态。

  // 将输入输出信息载入局部变量；UPDATE 宏负责写回。
  LOAD

  // 根据当前状态处理输入和输出
  for(;;) switch (c->mode)
  {             // 等待“i:”=输入，“o:”=输出，“x:”=无
    case START:         // x：为 LEN 设置
#ifndef SLOW
      if (m >= 258 && n >= 10)
      {
        UPDATE
        r = inflate_fast(c->lbits, c->dbits, c->ltree, c->dtree, s, z);
        LOAD
        if (r != Z_OK)
        {
          c->mode = r == Z_STREAM_END ? WASH : BADCODE;
          break;
        }
      }
#endif // 非 SLOW 编译分支。
      c->sub.code.need = c->lbits;
      c->sub.code.tree = c->ltree;
      c->mode = LEN;
    case LEN:           // i：接下来获取长度/字面值/eob
      j = c->sub.code.need;
      NEEDBITS(j)
      t = c->sub.code.tree + ((uInt)b & inflate_mask[j]);
      DUMPBITS(t->bits)
      e = (uInt)(t->exop);
      if (e == 0)               // 字面值字节。
      {
        c->sub.lit = t->base;
        LuTracevv((stderr, t->base >= 0x20 && t->base < 0x7f ?
                 "inflate:         literal '%c'\n" :
                 "inflate:         literal 0x%02x\n", t->base));
        c->mode = LIT;
        break;
      }
      if (e & 16)               // 长度
      {
        c->sub.copy.get = e & 15;
        c->len = t->base;
        c->mode = LENEXT;
        break;
      }
      if ((e & 64) == 0)        // 进入下一级查找表。
      {
        c->sub.code.need = e;
        c->sub.code.tree = t + t->base;
        break;
      }
      if (e & 32)               // 块结束
      {
        LuTracevv((stderr, "inflate:         end of block\n"));
        c->mode = WASH;
        break;
      }
      c->mode = BADCODE;        // 无效代码
      z->msg = (char*)"invalid literal/length code";
      r = Z_DATA_ERROR;
      LEAVE
    case LENEXT:        // i：已取得长度基值，继续读取长度附加位。
      j = c->sub.copy.get;
      NEEDBITS(j)
      c->len += (uInt)b & inflate_mask[j];
      DUMPBITS(j)
      c->sub.code.need = c->dbits;
      c->sub.code.tree = c->dtree;
      LuTracevv((stderr, "inflate:         length %u\n", c->len));
      c->mode = DIST;
    case DIST:          // i：下一步获取距离
      j = c->sub.code.need;
      NEEDBITS(j)
      t = c->sub.code.tree + ((uInt)b & inflate_mask[j]);
      DUMPBITS(t->bits)
      e = (uInt)(t->exop);
      if (e & 16)               // 距离
      {
        c->sub.copy.get = e & 15;
        c->sub.copy.dist = t->base;
        c->mode = DISTEXT;
        break;
      }
      if ((e & 64) == 0)        // 进入下一级查找表。
      {
        c->sub.code.need = e;
        c->sub.code.tree = t + t->base;
        break;
      }
      c->mode = BADCODE;        // 无效代码
      z->msg = (char*)"invalid distance code";
      r = Z_DATA_ERROR;
      LEAVE
    case DISTEXT:       // i：读取距离附加位。
      j = c->sub.copy.get;
      NEEDBITS(j)
      c->sub.copy.dist += (uInt)b & inflate_mask[j];
      DUMPBITS(j)
      LuTracevv((stderr, "inflate:         distance %u\n", c->sub.copy.dist));
      c->mode = COPY;
    case COPY:          // o：在窗口中复制字节，等待空间
      f = q - c->sub.copy.dist;
      while (f < s->window)             // 按窗口大小循环回绕；使用 while 而非 if，
        f += s->end - s->window;        // 以处理异常距离需要多次回绕的情况。
      while (c->len)
      {
        NEEDOUT
        OUTBYTE(*f++)
        if (f == s->end)
          f = s->window;
        c->len--;
      }
      c->mode = START;
      break;
    case LIT:           // o：已获取字面值，正在等待输出空间
      NEEDOUT
      OUTBYTE(c->sub.lit)
      c->mode = START;
      break;
    case WASH:          // o：得到 eob，可能有更多输出
      if (k > 7)        // 若有多读而未使用的字节，则退回输入。
      {
        // Assert(k < 16, "inflate_codes 抓取了太多字节")
        k -= 8;
        n++;
        p--;            // 此处总能退回一个字节。
      }
      FLUSH
      if (s->read != s->write)
        LEAVE
      c->mode = END;
    case END:
      r = Z_STREAM_END;
      LEAVE
    case BADCODE:       // x：出现错误
      r = Z_DATA_ERROR;
      LEAVE
    default:
      r = Z_STREAM_ERROR;
      LEAVE
  }
}


void inflate_codes_free(inflate_codes_statef *c,z_streamp z)
{ ZFREE(z, c);
  LuTracev((stderr, "inflate:       codes free\n"));
}



// infblock.c——解释和处理块类型到最后一个块
// 版权所有（C）1995—1998 Mark Adler。
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明

// struct inflate_codes_state {int dummy;}; // 对于有缺陷的编译器



// 来自 PKZIP 的 appnote.txt 的 deflate 表。
const uInt border[] = { // 位长代码长度的顺序
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

//
// 1.93a appnote.txt 之外的注释：
//
// 1. 距离指针永远不会指向输出流开头之前。
// 2.距离指针可以跨区块指向回，最远可达32k。
// 码长表的最大码长为 7 位，实际数据编码的最大码长为 15 位。
//
// 若只有一个编码，也用 1 位表示，避免零位编码带来的歧义；若有两
// 个编码，则分别用单个位 0、1 表示。
//
// 1.93a 规则不允许零个距离编码，没有实际距离编码时必须发送占
// 位项。PKZIP 2.0 之前曾把没有距离编码的块直接存储，该限
// 制过严。2.04c 则允许零个距离编码，采用一个码长为 0 的编
// 码表示。
//
//
// 字面值/长度最多 286 个编码，256 为块结束。静态树包含
// 288 个编码以补全哈夫曼编码，但 286、287 未定义基值或
// 附加位，不能出现在数据中。距离最多 30 个编码，静态树补齐为
// 32 个 5 位编码，最后两个同样不得实际使用。
//
//
//
// 解压器可以检查动态哈夫曼块的编码集合是否完整；只有一个编码时属于
// 例外，见第 4 条。
// 块类型之后的 5 位表示字面值/长度编码数量减去 257。
//
// 码长序列 8,16,16 可表示 13 个长度为 8 的编码（1
// +6+6）。因此重复三次同一码长需要三个编码（1+1+1），重复
// 四次反而只需两个（1+3）。
//
// 10、树重建算法中，Code = Code + Increment
// 仅当 BitLength(i) 不为零时。  （非常明显。）
// 11. 修正：4 位：位长代码的数量 - 4 (4 - 19)
// 长度编码 284 可表示 227..258，而 285 专用于
// 258。高度重复的数据经常使用最大匹配长度，因此单独为 258
// 分配短编码；258 减去最小长度 3 等于 255。
//
// 字面值/长度与距离的码长作为一个连续序列读取，重复编码 16、1
// 7、18 可以跨越两组码长之间的边界，这样通常更节省空间。
//
//


void inflate_blocks_reset(inflate_blocks_statef *s, z_streamp z, uLong *c)
{
  if (c != Z_NULL)
    *c = s->check;
  if (s->mode == IBM_BTREE || s->mode == IBM_DTREE)
    ZFREE(z, s->sub.trees.blens);
  if (s->mode == IBM_CODES)
    inflate_codes_free(s->sub.decode.codes, z);
  s->mode = IBM_TYPE;
  s->bitk = 0;
  s->bitb = 0;
  s->read = s->write = s->window;
  if (s->checkfn != Z_NULL)
    z->adler = s->check = (*s->checkfn)(0L, (const Byte *)Z_NULL, 0);
  LuTracev((stderr, "inflate:   blocks reset\n"));
}


inflate_blocks_statef *inflate_blocks_new(z_streamp z, check_func c, uInt w)
{
  inflate_blocks_statef *s;

  if ((s = (inflate_blocks_statef *)ZALLOC
       (z,1,sizeof(struct inflate_blocks_state))) == Z_NULL)
    return s;
  if ((s->hufts =
       (inflate_huft *)ZALLOC(z, sizeof(inflate_huft), MANY)) == Z_NULL)
  {
    ZFREE(z, s);
    return Z_NULL;
  }
  if ((s->window = (Byte *)ZALLOC(z, 1, w)) == Z_NULL)
  {
    ZFREE(z, s->hufts);
    ZFREE(z, s);
    return Z_NULL;
  }
  s->end = s->window + w;
  s->checkfn = c;
  s->mode = IBM_TYPE;
  LuTracev((stderr, "inflate:   blocks allocated\n"));
  inflate_blocks_reset(s, z, Z_NULL);
  return s;
}


int inflate_blocks(inflate_blocks_statef *s, z_streamp z, int r)
{
  uInt t;               // 临时储存
  uLong b;              // 位缓冲器
  uInt k;               // 位缓冲区中的位
  Byte *p;             // 输入数据指针
  uInt n;               // 那里可用的字节数
  Byte *q;             // 输出窗口写指针
  uInt m;               // 到窗口末尾或读指针的字节

  // 将输入/输出信息复制到本地（更新宏恢复）
  LOAD

  // 根据当前状态处理输入
  for(;;) switch (s->mode)
  {
    case IBM_TYPE:
      NEEDBITS(3)
      t = (uInt)b & 7;
      s->last = t & 1;
      switch (t >> 1)
      {
        case 0:                         // 已存储
          LuTracev((stderr, "inflate:     stored block%s\n",
                 s->last ? " (last)" : ""));
          DUMPBITS(3)
          t = k & 7;                    // 转到字节边界
          DUMPBITS(t)
          s->mode = IBM_LENS;               // 获取存储块的长度
          break;
        case 1:                         // 固定的
          LuTracev((stderr, "inflate:     fixed codes block%s\n",
                 s->last ? " (last)" : ""));
          {
            uInt bl, bd;
            const inflate_huft *tl, *td;

            inflate_trees_fixed(&bl, &bd, &tl, &td, z);
            s->sub.decode.codes = inflate_codes_new(bl, bd, tl, td, z);
            if (s->sub.decode.codes == Z_NULL)
            {
              r = Z_MEM_ERROR;
              LEAVE
            }
          }
          DUMPBITS(3)
          s->mode = IBM_CODES;
          break;
        case 2:                         // 动态
          LuTracev((stderr, "inflate:     dynamic codes block%s\n",
                 s->last ? " (last)" : ""));
          DUMPBITS(3)
          s->mode = IBM_TABLE;
          break;
        case 3:                         // 非法的
          DUMPBITS(3)
          s->mode = IBM_BAD;
          z->msg = (char*)"invalid block type";
          r = Z_DATA_ERROR;
          LEAVE
      }
      break;
    case IBM_LENS:
      NEEDBITS(32)
      if ((((~b) >> 16) & 0xffff) != (b & 0xffff))
      {
        s->mode = IBM_BAD;
        z->msg = (char*)"invalid stored block lengths";
        r = Z_DATA_ERROR;
        LEAVE
      }
      s->sub.left = (uInt)b & 0xffff;
      b = k = 0;                      // 转储位
      LuTracev((stderr, "inflate:       stored length %u\n", s->sub.left));
      s->mode = s->sub.left ? IBM_STORED : (s->last ? IBM_DRY : IBM_TYPE);
      break;
    case IBM_STORED:
      if (n == 0)
        LEAVE
      NEEDOUT
      t = s->sub.left;
      if (t > n) t = n;
      if (t > m) t = m;
      memcpy(q, p, t);
      p += t;  n -= t;
      q += t;  m -= t;
      if ((s->sub.left -= t) != 0)
        break;
      LuTracev((stderr, "inflate:       stored end, %lu total out\n",
              z->total_out + (q >= s->read ? q - s->read :
              (s->end - s->read) + (q - s->window))));
      s->mode = s->last ? IBM_DRY : IBM_TYPE;
      break;
    case IBM_TABLE:
      NEEDBITS(14)
      s->sub.trees.table = t = (uInt)b & 0x3fff;
      // 删除此部分以解决 pkzip 中的错误
      if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29)
      {
        s->mode = IBM_BAD;
        z->msg = (char*)"too many length or distance symbols";
        r = Z_DATA_ERROR;
        LEAVE
      }
      // 结束删除
      t = 258 + (t & 0x1f) + ((t >> 5) & 0x1f);
      if ((s->sub.trees.blens = (uInt*)ZALLOC(z, t, sizeof(uInt))) == Z_NULL)
      {
        r = Z_MEM_ERROR;
        LEAVE
      }
      DUMPBITS(14)
      s->sub.trees.index = 0;
      LuTracev((stderr, "inflate:       table sizes ok\n"));
      s->mode = IBM_BTREE;
    case IBM_BTREE:
      while (s->sub.trees.index < 4 + (s->sub.trees.table >> 10))
      {
        NEEDBITS(3)
        s->sub.trees.blens[border[s->sub.trees.index++]] = (uInt)b & 7;
        DUMPBITS(3)
      }
      while (s->sub.trees.index < 19)
        s->sub.trees.blens[border[s->sub.trees.index++]] = 0;
      s->sub.trees.bb = 7;
      t = inflate_trees_bits(s->sub.trees.blens, &s->sub.trees.bb,
                             &s->sub.trees.tb, s->hufts, z);
      if (t != Z_OK)
      {
        r = t;
        if (r == Z_DATA_ERROR)
        {
          ZFREE(z, s->sub.trees.blens);
          s->mode = IBM_BAD;
        }
        LEAVE
      }
      s->sub.trees.index = 0;
      LuTracev((stderr, "inflate:       bits tree ok\n"));
      s->mode = IBM_DTREE;
    case IBM_DTREE:
      while (t = s->sub.trees.table,
             s->sub.trees.index < 258 + (t & 0x1f) + ((t >> 5) & 0x1f))
      {
        inflate_huft *h;
        uInt i, j, c;

        t = s->sub.trees.bb;
        NEEDBITS(t)
        h = s->sub.trees.tb + ((uInt)b & inflate_mask[t]);
        t = h->bits;
        c = h->base;
        if (c < 16)
        {
          DUMPBITS(t)
          s->sub.trees.blens[s->sub.trees.index++] = c;
        }
        else // c 为重复码长编码，取值 16..18。
        {
          i = c == 18 ? 7 : c - 14;
          j = c == 18 ? 11 : 3;
          NEEDBITS(t + i)
          DUMPBITS(t)
          j += (uInt)b & inflate_mask[i];
          DUMPBITS(i)
          i = s->sub.trees.index;
          t = s->sub.trees.table;
          if (i + j > 258 + (t & 0x1f) + ((t >> 5) & 0x1f) ||
              (c == 16 && i < 1))
          {
            ZFREE(z, s->sub.trees.blens);
            s->mode = IBM_BAD;
            z->msg = (char*)"invalid bit length repeat";
            r = Z_DATA_ERROR;
            LEAVE
          }
          c = c == 16 ? s->sub.trees.blens[i - 1] : 0;
          do {
            s->sub.trees.blens[i++] = c;
          } while (--j);
          s->sub.trees.index = i;
        }
      }
      s->sub.trees.tb = Z_NULL;
      {
        uInt bl, bd;
        inflate_huft *tl, *td;
        inflate_codes_statef *c;

        bl = 9;         // 对于前瞻假设，必须 <= 9
        bd = 6;         // 对于前瞻假设，必须 <= 9
        t = s->sub.trees.table;
        t = inflate_trees_dynamic(257 + (t & 0x1f), 1 + ((t >> 5) & 0x1f),
                                  s->sub.trees.blens, &bl, &bd, &tl, &td,
                                  s->hufts, z);
        if (t != Z_OK)
        {
          if (t == (uInt)Z_DATA_ERROR)
          {
            ZFREE(z, s->sub.trees.blens);
            s->mode = IBM_BAD;
          }
          r = t;
          LEAVE
        }
        LuTracev((stderr, "inflate:       trees ok\n"));
        if ((c = inflate_codes_new(bl, bd, tl, td, z)) == Z_NULL)
        {
          r = Z_MEM_ERROR;
          LEAVE
        }
        s->sub.decode.codes = c;
      }
      ZFREE(z, s->sub.trees.blens);
      s->mode = IBM_CODES;
    case IBM_CODES:
      UPDATE
      if ((r = inflate_codes(s, z, r)) != Z_STREAM_END)
        return inflate_flush(s, z, r);
      r = Z_OK;
      inflate_codes_free(s->sub.decode.codes, z);
      LOAD
      LuTracev((stderr, "inflate:       codes end, %lu total out\n",
              z->total_out + (q >= s->read ? q - s->read :
              (s->end - s->read) + (q - s->window))));
      if (!s->last)
      {
        s->mode = IBM_TYPE;
        break;
      }
      s->mode = IBM_DRY;
    case IBM_DRY:
      FLUSH
      if (s->read != s->write)
        LEAVE
      s->mode = IBM_DONE;
    case IBM_DONE:
      r = Z_STREAM_END;
      LEAVE
    case IBM_BAD:
      r = Z_DATA_ERROR;
      LEAVE
    default:
      r = Z_STREAM_ERROR;
      LEAVE
  }
}


int inflate_blocks_free(inflate_blocks_statef *s, z_streamp z)
{
  inflate_blocks_reset(s, z, Z_NULL);
  ZFREE(z, s->window);
  ZFREE(z, s->hufts);
  ZFREE(z, s);
  LuTracev((stderr, "inflate:   blocks freed\n"));
  return Z_OK;
}



// inftrees.c——生成哈夫曼树以进行高效解码
// 版权所有（C）1995—1998 Mark Adler。
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明
//



extern const char inflate_copyright[] =
   " inflate 1.1.3 Copyright 1995-1998 Mark Adler ";
// 作者欢迎在产品文档中注明使用 zlib；若无法致谢，则希望保留产
// 品可执行文件中的版权字符串。
//
//



int huft_build (
    uInt *,            // 各编码的位长。
    uInt,               // 编码数量。
    uInt,               // 简单编码数量。
    const uInt *,      // 非简单代码的基值列表
    const uInt *,      // 非简单代码的额外位列表
    inflate_huft **,// 返回的首级查找表。
    uInt *,            // 最大查表位数；返回时更新为实际值。
    inflate_huft *,     // 树节点存储空间。
    uInt *,             // 存储空间中已经使用的 huft 节点数。
    uInt * );         // 用于存放符号值的工作区。

// 来自 PKZIP 的 appnote.txt 的 deflate 表。
const uInt cplens[31] = { // 编码 257..285 对应的复制长度。
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
        // 请参阅上面的注释 #13 约 258
const uInt cplext[31] = { // 编码 257..285 对应的附加位数。
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 112, 112}; // 112==无效
const uInt cpdist[30] = { // 距离编码 0..29 对应的复制偏移。
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};
const uInt cpdext[30] = { // 距离代码的额外位
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
        12, 12, 13, 13};

//
// 哈夫曼解码采用多级查表。按最长码长创建单级表虽能最快解码，但短输
// 入的建表耗时可能过高。短编码出现频繁，适合用较小首级表直接处理，
// 再通过子表解码长编码，从而平衡解码速度和建表开销。
//
//
//
//
//
//
//
//
// lbits 和 dbits 分别表示字面值/长度树、距离树的首级
// 表一次可解码的位数；后续子表不超过相同大小。如果所有编码更短，则
// 改用最长码长；如果最短编码仍长于期望查表位数，则改用最短码长。
//
//
//
//
//
//
//
// 两类表采用不同参数，因为字面值/长度有 286 种可能值，距离有
//  30 种，近似等长编码分别需要稍多于 8 位、稍少于 5 位。
// 较快的查表参数通常额外增加 1 位，即 lbits=9、
// dbits=6；最优值仍取决于机器和编译器。
//
//
//
//
//


// 如果BMAX需要大于16，那么h和x[]应该是uLong。
#define BMAX 15         // 任一编码允许的最大码长。

int huft_build(
uInt *b,               // 各编码位长，假定均 <= BMAX。
uInt n,                 // 编码数量，假定 <= 288。
uInt s,                 // 取值为 0..s-1 的简单编码数量。
const uInt *d,         // 非简单代码的基值列表
const uInt *e,         // 非简单代码的额外位列表
inflate_huft * *t,  // 返回的首级查找表。
uInt *m,               // 最大查表位数，返回实际位数。
inflate_huft *hp,       // 树节点存储空间。
uInt *hn,               // 存储空间中已经使用的 huft 节点数。
uInt *v)               // 工作区：按码长顺序排列的符号值。
// 根据码长列表和最大表大小构建解码表。成功返回 Z_OK；编码集合
// 不完整时仍构建表，但返回 Z_BUF_ERROR；输入无效时返回
//  Z_DATA_ERROR。
//
{

  uInt a;                       // 长度为 k 的代码计数器
  uInt c[BMAX+1];               // 位长计数表
  uInt f;                       // i 在表中每 f 个条目重复一次
  int g;                        // 最大码长。
  int h;                        // 表层
  register uInt i;              // 计数器，当前代码
  register uInt j;              // 计数器。
  register int k;               // 当前编码的位数。
  int l;                        // 每个表的位数（以 m 为单位返回）
  uInt mask;                    // (1 << w) - 1，以避免 HP 上的 cc -O 错误
  register uInt *p;            // 指向 c[]、b[] 或 v[] 的指针
  inflate_huft *q;              // 指向当前表
  struct inflate_huft_s r;      // 用于结构体赋值的查找表项。
  inflate_huft *u[BMAX];        // 表栈
  register int w;               // 进入当前表之前已经解码的位数，等于 l*h。
  uInt x[BMAX+1];               // 位偏移量，然后是代码堆栈
  uInt *xp;                    // 指向 x 的指针
  int y;                        // 为了补全树而添加的占位编码数量。
  uInt z;                       // 当前表中的条目数


  // 生成每个位长度的计数
  p = c;
#define C0 *p++ = 0;
#define C2 C0 C0 C0 C0
#define C4 C2 C2 C2 C2
  C4; p;                          // 清除c[]--假设BMAX+1为16
  p = b;  i = n;
  do {
    c[*p++]++;                  // 假设所有条目 <= BMAX
  } while (--i);
  if (c[0] == n)                // 空输入——所有零长度代码
  {
    *t = (inflate_huft *)Z_NULL;
    *m = 0;
    return Z_OK;
  }


  // 求最短和最长码长，并将 *m 限制在该范围内。
  l = *m;
  for (j = 1; j <= BMAX; j++)
    if (c[j])
      break;
  k = j;                        // 最小码长。
  if ((uInt)l < j)
    l = j;
  for (i = BMAX; i; i--)
    if (c[i])
      break;
  g = i;                        // 最大码长。
  if ((uInt)l > i)
    l = i;
  *m = l;


  // 必要时增加最长码长的数量，以补全编码空间。
  for (y = 1 << j; j < i; j++, y <<= 1)
    if ((y -= c[j]) < 0)
      return Z_DATA_ERROR;
  if ((y -= c[i]) < 0)
    return Z_DATA_ERROR;
  c[i] += y;


  // 计算每种码长在符号值表中的起始偏移。
  x[1] = j = 0;
  p = c + 1;  xp = x + 2;
  while (--i) {                 // 请注意，上面的 i == g
    *xp++ = (j += *p++);
  }


  // 按码长顺序构建符号值表。
  p = b;  i = 0;
  do {
    if ((j = *p++) != 0)
      v[x[j]++] = i;
  } while (++i < n);
  n = x[g];                     // 将 n 设置为 v 的长度


  // 生成哈夫曼代码并为每个代码创建表条目
  x[0] = i = 0;                 // 第一个哈夫曼码为零
  p = v;                        // 按码长顺序获取符号值。
  h = -1;                       // 还没有表--级别-1
  w = -l;                       // 解码的位数 == (l * h)
  u[0] = (inflate_huft *)Z_NULL;        // 初始化以避免编译器产生未初始化警告。
  q = (inflate_huft *)Z_NULL;   // 同上
  z = 0;                        // 同上

  // 检查位长度（k 已经是最短代码中的位）
  for (; k <= g; k++)
  {
    a = c[k];
    while (a--)
    {
      // 这里 i 是值 *p 的长度为 k 位的哈夫曼代码
      // 逐级构建所需的查找表。
      while (k > w + l)
      {
        h++;
        w += l;                 // 前一个表总是 l 位

        // 计算不超过 l 位的最小查找表。
        z = g - w;
        z = z > (uInt)l ? l : z;        // 表大小上限
        if ((f = 1 << (j = k - w)) > a + 1)     // 尝试 k-w 位表
        {                       // k-w 位表代码太少
          f -= a + 1;           // 从剩余位组合数量中扣除当前编码数。
          xp = c + k;
          if (j < z)
            while (++j < z)     // 尝试较小的表（最多 z 位）
            {
              if ((f <<= 1) <= *++xp)
                break;          // 编码数已足够占满 j 位的表。
              f -= *xp;         // 否则从剩余位组合中扣除编码数。
            }
        }
        z = 1 << j;             // j 位查找表的表项数量。

        // 分配新表
        if (*hn + z > MANY)     // （注：与固定无关）
          return Z_DATA_ERROR;  // 超过 MANY 指定的节点数量上限。
        u[h] = q = hp + *hn;
        *hn += z;

        // 连接到最后一个表（如果有）
        if (h)
        {
          x[h] = i;             // 保存当前编码模式，以便回溯。
          r.bits = (Byte)l;     // 进入本表前已消耗的位数。
          r.exop = (Byte)j;     // 该表中的位
          j = i >> (w - l);
          r.base = (uInt)(q - u[h-1] - j);   // 到该表的偏移量
          u[h-1][j] = r;        // 连接到最后一个表
        }
        else
          *t = q;               // 第一个表是返回结果
      }

      // 在r中设置表条目
      r.bits = (Byte)(k - w);
      if (p >= v + n)
        r.exop = 128 + 64;      // 超出值--无效代码
      else if (*p < s)
      {
        r.exop = (Byte)(*p < 256 ? 0 : 32 + 64);     // 256 是块结束符
        r.base = *p++;          // 简单编码直接使用符号值。
      }
      else
      {
        r.exop = (Byte)(e[*p - s] + 16 + 64);// 非简单——在列表中查找
        r.base = d[*p++ - s];
      }

      // 用 r 填充匹配当前编码模式的表项。
      f = 1 << (k - w);
      for (j = i >> w; j < z; j += f)
        q[j] = r;

      // 按反向位序递增 k 位编码 i。
      for (j = 1 << (k - 1); i & j; j >>= 1)
        i ^= j;
      i ^= j;

      // 处理完子表后回溯到上级表。
      mask = (1 << w) - 1;      // HP 需要，cc -O bug
      while ((i & mask) != x[h])
      {
        h--;                    // 不需要更新q
        w -= l;
        mask = (1 << w) - 1;
      }
    }
  }


  // 如果给我们一个不完整的表，则返回 Z_BUF_ERROR
  return y != 0 && g != 1 ? Z_BUF_ERROR : Z_OK;
}


int inflate_trees_bits(
uInt *c,               // 19 个码长。
uInt *bb,              // 码长树期望的查表位数与实际位数。
inflate_huft * *tb, // 生成的码长树。
inflate_huft *hp,       // 树节点存储空间。
z_streamp z)            // 用于消息
{
  int r;
  uInt hn = 0;          // 已使用的 huft 节点数。
  uInt *v;             // huft_build 的工作区

  if ((v = (uInt*)ZALLOC(z, 19, sizeof(uInt))) == Z_NULL)
    return Z_MEM_ERROR;
  r = huft_build(c, 19, 19, (uInt*)Z_NULL, (uInt*)Z_NULL,
                 tb, bb, hp, &hn, v);
  if (r == Z_DATA_ERROR)
    z->msg = (char*)"oversubscribed dynamic bit lengths tree";
  else if (r == Z_BUF_ERROR || *bb == 0)
  {
    z->msg = (char*)"incomplete dynamic bit lengths tree";
    r = Z_DATA_ERROR;
  }
  ZFREE(z, v);
  return r;
}


int inflate_trees_dynamic(
uInt nl,                // 字面值/长度编码数量。
uInt nd,                // 距离编码数量。
uInt *c,               // 需要处理的码长总数。
uInt *bl,              // 字面值树期望的查表位数与实际位数。
uInt *bd,              // 距离树期望的查表位数与实际位数。
inflate_huft * *tl, // 生成的字面值/长度树。
inflate_huft * *td, // 生成的距离树。
inflate_huft *hp,       // 树节点存储空间。
z_streamp z)            // 用于消息
{
  int r;
  uInt hn = 0;          // 已使用的 huft 节点数。
  uInt *v;             // huft_build 的工作区

  // 分配工作区域
  if ((v = (uInt*)ZALLOC(z, 288, sizeof(uInt))) == Z_NULL)
    return Z_MEM_ERROR;

  // 构建字面值/长度树
  r = huft_build(c, nl, 257, cplens, cplext, tl, bl, hp, &hn, v);
  if (r != Z_OK || *bl == 0)
  {
    if (r == Z_DATA_ERROR)
      z->msg = (char*)"oversubscribed literal/length tree";
    else if (r != Z_MEM_ERROR)
    {
      z->msg = (char*)"incomplete literal/length tree";
      r = Z_DATA_ERROR;
    }
    ZFREE(z, v);
    return r;
  }

  // 构建距离树
  r = huft_build(c + nl, nd, 0, cpdist, cpdext, td, bd, hp, &hn, v);
  if (r != Z_OK || (*bd == 0 && nl > 257))
  {
    if (r == Z_DATA_ERROR)
      z->msg = (char*)"oversubscribed distance tree";
    else if (r == Z_BUF_ERROR) {
      z->msg = (char*)"incomplete distance tree";
      r = Z_DATA_ERROR;
    }
    else if (r != Z_MEM_ERROR)
    {
      z->msg = (char*)"empty distance tree with lengths";
      r = Z_DATA_ERROR;
    }
    ZFREE(z, v);
    return r;
  }

  // 完成
  ZFREE(z, v);
  return Z_OK;
}





int inflate_trees_fixed(
uInt *bl,               // 字面值树期望的查表位数与实际位数。
uInt *bd,               // 距离树期望的查表位数与实际位数。
const inflate_huft * * tl,     // 生成的字面值/长度树。
const inflate_huft * *td,     // 生成的距离树。
z_streamp )             // 用于内存分配
{
  *bl = fixed_bl;
  *bd = fixed_bd;
  *tl = fixed_tl;
  *td = fixed_td;
  return Z_OK;
}


// inffast.c——快速处理字面值和长度/距离对
// 版权所有 (C) 1995-1998 马克·阿德勒
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明
//


// struct inflate_codes_state {int dummy;}; // 对于有缺陷的编译器


// 用于不检查位输入和返回未使用字节的宏
#define GRABBITS(j) {while(k<(j)){b|=((uLong)NEXTBYTE)<<k;k+=8;}}
#define UNGRAB {c=z->avail_in-n;c=(k>>3)<c?k>>3:c;n+=c;p-=c;k-=c<<3;}

// 调用快速处理时，输出窗口至少应有 258 字节（最大匹配长度）的
// 空间，可用输入至少为 10 字节：最长长度/距离对需要 6 字节
// ，另预留 4 字节供位缓冲区提前读取。
//

int inflate_fast(
uInt bl, uInt bd,
const inflate_huft *tl,
const inflate_huft *td, // Borland C++ 需要单独声明
inflate_blocks_statef *s,
z_streamp z)
{
  const inflate_huft *t;      // 临时指针
  uInt e;               // 额外的位或操作
  uLong b;              // 位缓冲器
  uInt k;               // 位缓冲区中的位
  Byte *p;             // 输入数据指针
  uInt n;               // 那里可用的字节数
  Byte *q;             // 输出窗口写指针
  uInt m;               // 到窗口末尾或读指针的字节
  uInt ml;              // 字面值/长度树的掩码
  uInt md;              // 距离树的掩码
  uInt c;               // 要复制的字节
  uInt d;               // 复制距离
  Byte *r;             // 复制源指针

  // 加载输入、输出、位值
  LOAD

  // 初始化掩码
  ml = inflate_mask[bl];
  md = inflate_mask[bd];

  // 直到没有足够的输入或输出空间用于快速循环
  do {                          // 假设调用 m >= 258 && n >= 10
    // 获取字面值/长度代码
    GRABBITS(20)                // 字面值/长度代码的最大位数
    if ((e = (t = tl + ((uInt)b & ml))->exop) == 0)
    {
      DUMPBITS(t->bits)
      LuTracevv((stderr, t->base >= 0x20 && t->base < 0x7f ?
                "inflate:         * literal '%c'\n" :
                "inflate:         * literal 0x%02x\n", t->base));
      *q++ = (Byte)t->base;
      m--;
      continue;
    }
    for (;;) {
      DUMPBITS(t->bits)
      if (e & 16)
      {
        // 读取长度附加位。
        e &= 15;
        c = t->base + ((uInt)b & inflate_mask[e]);
        DUMPBITS(e)
        LuTracevv((stderr, "inflate:         * length %u\n", c));

        // 解码待复制数据的距离基值。
        GRABBITS(15);           // 距离代码的最大位数
        e = (t = td + ((uInt)b & md))->exop;
        for (;;) {
          DUMPBITS(t->bits)
          if (e & 16)
          {
            // 读取距离附加位并与距离基值相加。
            e &= 15;
            GRABBITS(e)         // 获得额外的位（最多 13 位）
            d = t->base + ((uInt)b & inflate_mask[e]);
            DUMPBITS(e)
            LuTracevv((stderr, "inflate:         * distance %u\n", d));

            // 复制匹配数据。
            m -= c;
            r = q - d;
            if (r < s->window)                  // 必要时回绕窗口。
            {
              do {
                r += s->end - s->window;        // 将指针回绕到窗口范围内。
              } while (r < s->window);          // 处理超出窗口范围的距离。
              e = (uInt) (s->end - r);
              if (c > e)
              {
                c -= e;                         // 跨窗口边界复制。
                do {
                    *q++ = *r++;
                } while (--e);
                r = s->window;
                do {
                    *q++ = *r++;
                } while (--c);
              }
              else                              // 在窗口内直接复制。
              {
                *q++ = *r++;  c--;
                *q++ = *r++;  c--;
                do {
                    *q++ = *r++;
                } while (--c);
              }
            }
            else                                /* 在窗口内直接复制。 */
            {
              *q++ = *r++;  c--;
              *q++ = *r++;  c--;
              do {
                *q++ = *r++;
              } while (--c);
            }
            break;
          }
          else if ((e & 64) == 0)
          {
            t += t->base;
            e = (t += ((uInt)b & inflate_mask[e]))->exop;
          }
          else
          {
            z->msg = (char*)"invalid distance code";
            UNGRAB
            UPDATE
            return Z_DATA_ERROR;
          }
        };
        break;
      }
      if ((e & 64) == 0)
      {
        t += t->base;
        if ((e = (t += ((uInt)b & inflate_mask[e]))->exop) == 0)
        {
          DUMPBITS(t->bits)
          LuTracevv((stderr, t->base >= 0x20 && t->base < 0x7f ?
                    "inflate:         * literal '%c'\n" :
                    "inflate:         * literal 0x%02x\n", t->base));
          *q++ = (Byte)t->base;
          m--;
          break;
        }
      }
      else if (e & 32)
      {
        LuTracevv((stderr, "inflate:         * end of block\n"));
        UNGRAB
        UPDATE
        return Z_STREAM_END;
      }
      else
      {
        z->msg = (char*)"invalid literal/length code";
        UNGRAB
        UPDATE
        return Z_DATA_ERROR;
      }
    };
  } while (m >= 258 && n >= 10);

  // 输入或输出不足——恢复指针并返回
  UNGRAB
  UPDATE
  return Z_OK;
}






// crc32.c -- 计算数据流的 CRC-32
// 版权所有 (C) 1995-1998 马克·阿德勒
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明

// 原始源码版本标记：@(#) $Id$。






// 所有单字节值的 CRC-32 表（由 make_crc_table 制作）
const uLong crc_table[256] = {
  0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
  0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
  0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
  0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
  0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
  0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
  0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
  0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
  0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
  0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
  0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
  0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
  0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
  0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
  0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
  0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
  0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
  0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
  0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
  0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
  0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
  0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
  0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
  0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
  0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
  0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
  0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
  0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
  0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
  0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
  0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
  0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
  0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
  0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
  0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
  0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
  0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
  0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
  0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
  0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
  0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
  0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
  0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
  0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
  0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
  0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
  0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
  0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
  0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
  0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
  0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
  0x2d02ef8dL
};

const uLong * get_crc_table()
{ return (const uLong *)crc_table;
}

#define CRC_DO1(buf) crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define CRC_DO2(buf)  CRC_DO1(buf); CRC_DO1(buf);
#define CRC_DO4(buf)  CRC_DO2(buf); CRC_DO2(buf);
#define CRC_DO8(buf)  CRC_DO4(buf); CRC_DO4(buf);

uLong ucrc32(uLong crc, const Byte *buf, uInt len)
{ if (buf == Z_NULL) return 0L;
  crc = crc ^ 0xffffffffL;
  while (len >= 8)  {CRC_DO8(buf); len -= 8;}
  if (len) do {CRC_DO1(buf);} while (--len);
  return crc ^ 0xffffffffL;
}



// =============================================================
// 一些解密例程
#define CRC32(c, b) (crc_table[((int)(c)^(b))&0xff]^((c)>>8))
void Uupdate_keys(unsigned long *keys, char c)
{ keys[0] = CRC32(keys[0],c);
  keys[1] += keys[0] & 0xFF;
  keys[1] = keys[1]*134775813L +1;
  keys[2] = CRC32(keys[2], keys[1] >> 24);
}
char Udecrypt_byte(unsigned long *keys)
{ unsigned temp = ((unsigned)keys[2] & 0xffff) | 2;
  return (char)(((temp * (temp ^ 1)) >> 8) & 0xff);
}
char zdecode(unsigned long *keys, char c)
{ c^=Udecrypt_byte(keys);
  Uupdate_keys(keys,c);
  return c;
}



// adler32.c -- 计算数据流的 Adler-32 校验和
// 版权所有 (C) 1995-1998 马克·阿德勒
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明

// 原始源码版本标记：@(#) $Id$。


#define BASE 65521L // 小于 65536 的最大素数
#define NMAX 5552
// NMAX 是满足 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 的最大 n

#define AD_DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define AD_DO2(buf,i)  AD_DO1(buf,i); AD_DO1(buf,i+1);
#define AD_DO4(buf,i)  AD_DO2(buf,i); AD_DO2(buf,i+2);
#define AD_DO8(buf,i)  AD_DO4(buf,i); AD_DO4(buf,i+4);
#define AD_DO16(buf)   AD_DO8(buf,0); AD_DO8(buf,8);

// =========================================================================
uLong adler32(uLong adler, const Byte *buf, uInt len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = (adler >> 16) & 0xffff;
    int k;

    if (buf == Z_NULL) return 1L;

    while (len > 0) {
        k = len < NMAX ? len : NMAX;
        len -= k;
        while (k >= 16) {
            AD_DO16(buf);
	    buf += 16;
            k -= 16;
        }
        if (k != 0) do {
            s1 += *buf++;
	    s2 += s1;
        } while (--k);
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}



// zutil.c——压缩库的目标相关实用函数
// 版权所有 (C) 1995-1998 Jean-loup Gailly。
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明
// 原始源码版本标记：@(#) $Id$。






const char * zlibVersion()
{
    return ZLIB_VERSION;
}

// 导出错误码到字符串的转换函数，供 compress() 和
// uncompress() 使用。
const char * zError(int err)
{ return ERR_MSG(err);
}




voidpf zcalloc (voidpf opaque, unsigned items, unsigned size)
{
    if (opaque) items += size - size; // 避免编译器产生未使用参数警告。
    return (voidpf)calloc(items, size);
}

void  zcfree (voidpf opaque, voidpf ptr)
{
    zfree(ptr);
    if (opaque) return; // 避免编译器产生未使用参数警告。
}



// inflate.c——inflate 模块的 zlib 接口
// 版权所有 (C) 1995-1998 马克·阿德勒
// 有关分发和使用条件，请参阅 zlib.h 中的版权声明

// struct inflate_blocks_state {int dummy;}; // 对于有缺陷的编译器

typedef enum {
      IM_METHOD,   // 等待方法字节
      IM_FLAG,     // 等待标志字节
      IM_DICT4,    // 四个字典检查字节
      IM_DICT3,    // 三个字典检查字节
      IM_DICT2,    // 两个字典检查字节
      IM_DICT1,    // 一个字典检查字节
      IM_DICT0,    // 等待 inflateSetDictionary
      IM_BLOCKS,   // 解压块
      IM_CHECK4,   // 还剩四个检查字节
      IM_CHECK3,   // 还剩三个检查字节
      IM_CHECK2,   // 还需要两个检查字节
      IM_CHECK1,   // 还剩一个检查字节
      IM_DONE,     // 检查完毕，完成
      IM_BAD}      // 出现错误--留在这里
inflate_mode;

// inflate 解压的私有状态。
struct internal_state {

  // 模式
  inflate_mode  mode;   // 当前 inflate 解压状态。

  // 模式相关信息
  union {
    uInt method;        // 如果是 IM_FLAGS，则为方法字节
    struct {
      uLong was;                // 计算的检查值
      uLong need;               // 流校验值
    } check;            // 如果检查，检查要比较的值
    uInt marker;        // 如果 IM_BAD，inflateSync 的标记字节数
  } sub;        // 子模式

  // 模式独立信息
  int  nowrap;          // 没有包装器的标志
  uInt wbits;           // log2（窗口大小）（8..15，默认为15）
  inflate_blocks_statef
    *blocks;            // 当前 inflate_blocks 状态

};

int inflateReset(z_streamp z)
{
  if (z == Z_NULL || z->state == Z_NULL)
    return Z_STREAM_ERROR;
  z->total_in = z->total_out = 0;
  z->msg = Z_NULL;
  z->state->mode = z->state->nowrap ? IM_BLOCKS : IM_METHOD;
  inflate_blocks_reset(z->state->blocks, z, Z_NULL);
  LuTracev((stderr, "inflate: reset\n"));
  return Z_OK;
}

int inflateEnd(z_streamp z)
{
  if (z == Z_NULL || z->state == Z_NULL || z->zfree == Z_NULL)
    return Z_STREAM_ERROR;
  if (z->state->blocks != Z_NULL)
    inflate_blocks_free(z->state->blocks, z);
  ZFREE(z, z->state);
  z->state = Z_NULL;
  LuTracev((stderr, "inflate: end\n"));
  return Z_OK;
}


int inflateInit2(z_streamp z)
{ const char *version = ZLIB_VERSION; int stream_size = sizeof(z_stream);
  if (version == Z_NULL || version[0] != ZLIB_VERSION[0] || stream_size != sizeof(z_stream)) return Z_VERSION_ERROR;

  int w = -15; // MAX_WBITS：32K LZ77 窗口。
  // 警告：减少 MAX_WBITS 会使 minigzip 无法提取 gzip 创建的 .gz 文件。
  // deflate 的内存需求为（以字节为单位）：
  //            (1 << (windowBits+2)) +  (1 << (memLevel+9))
  // 即： windowBits=15 时为 128K + memLevel = 8 时为 128K（默认值）
  // 小对象另需数 KB。若要把默认内存需求从 256K 降至 128
  // K，可用 make CFLAGS="-O -DMAX_WBITS
  // =14 -DMAX_MEM_LEVEL=7" 编译；代价是通常会
  // 降低压缩率。
  //
  // inflate 所需内存约为 1 << windowBits 字
  // 节；默认 windowBits=15 时为 32K，此外小对象还
  // 需数 KB。

  // 初始化状态
  if (z == Z_NULL) return Z_STREAM_ERROR;
  z->msg = Z_NULL;
  if (z->zalloc == Z_NULL)
  {
    z->zalloc = zcalloc;
    z->opaque = (voidpf)0;
  }
  if (z->zfree == Z_NULL) z->zfree = zcfree;
  if ((z->state = (struct internal_state *)
       ZALLOC(z,1,sizeof(struct internal_state))) == Z_NULL)
    return Z_MEM_ERROR;
  z->state->blocks = Z_NULL;

  // 处理未记录的 nowrap 选项（无 zlib 标头或检查）
  z->state->nowrap = 0;
  if (w < 0)
  {
    w = - w;
    z->state->nowrap = 1;
  }

  // 设置窗口大小
  if (w < 8 || w > 15)
  {
    inflateEnd(z);
    return Z_STREAM_ERROR;
  }
  z->state->wbits = (uInt)w;

  // 创建 inflate_blocks 状态
  if ((z->state->blocks =
      inflate_blocks_new(z, z->state->nowrap ? Z_NULL : adler32, (uInt)1 << w))
      == Z_NULL)
  {
    inflateEnd(z);
    return Z_MEM_ERROR;
  }
  LuTracev((stderr, "inflate: allocated\n"));

  // 重置状态
  inflateReset(z);
  return Z_OK;
}



#define IM_NEEDBYTE {if(z->avail_in==0)return r;r=f;}
#define IM_NEXTBYTE (z->avail_in--,z->total_in++,*z->next_in++)

int inflate(z_streamp z, int f)
{
  int r;
  uInt b;

  if (z == Z_NULL || z->state == Z_NULL || z->next_in == Z_NULL)
    return Z_STREAM_ERROR;
  f = f == Z_FINISH ? Z_BUF_ERROR : Z_OK;
  r = Z_BUF_ERROR;
  for (;;) switch (z->state->mode)
  {
    case IM_METHOD:
      IM_NEEDBYTE
      if (((z->state->sub.method = IM_NEXTBYTE) & 0xf) != Z_DEFLATED)
      {
        z->state->mode = IM_BAD;
        z->msg = (char*)"unknown compression method";
        z->state->sub.marker = 5;       // 无法尝试 inflateSync
        break;
      }
      if ((z->state->sub.method >> 4) + 8 > z->state->wbits)
      {
        z->state->mode = IM_BAD;
        z->msg = (char*)"invalid window size";
        z->state->sub.marker = 5;       // 无法尝试 inflateSync
        break;
      }
      z->state->mode = IM_FLAG;
    case IM_FLAG:
      IM_NEEDBYTE
      b = IM_NEXTBYTE;
      if (((z->state->sub.method << 8) + b) % 31)
      {
        z->state->mode = IM_BAD;
        z->msg = (char*)"incorrect header check";
        z->state->sub.marker = 5;       // 无法尝试 inflateSync
        break;
      }
      LuTracev((stderr, "inflate: zlib header ok\n"));
      if (!(b & PRESET_DICT))
      {
        z->state->mode = IM_BLOCKS;
        break;
      }
      z->state->mode = IM_DICT4;
    case IM_DICT4:
      IM_NEEDBYTE
      z->state->sub.check.need = (uLong)IM_NEXTBYTE << 24;
      z->state->mode = IM_DICT3;
    case IM_DICT3:
      IM_NEEDBYTE
      z->state->sub.check.need += (uLong)IM_NEXTBYTE << 16;
      z->state->mode = IM_DICT2;
    case IM_DICT2:
      IM_NEEDBYTE
      z->state->sub.check.need += (uLong)IM_NEXTBYTE << 8;
      z->state->mode = IM_DICT1;
    case IM_DICT1:
      IM_NEEDBYTE; r;
      z->state->sub.check.need += (uLong)IM_NEXTBYTE;
      z->adler = z->state->sub.check.need;
      z->state->mode = IM_DICT0;
      return Z_NEED_DICT;
    case IM_DICT0:
      z->state->mode = IM_BAD;
      z->msg = (char*)"need dictionary";
      z->state->sub.marker = 0;       // 可以尝试inflateSync
      return Z_STREAM_ERROR;
    case IM_BLOCKS:
      r = inflate_blocks(z->state->blocks, z, r);
      if (r == Z_DATA_ERROR)
      {
        z->state->mode = IM_BAD;
        z->state->sub.marker = 0;       // 可以尝试inflateSync
        break;
      }
      if (r == Z_OK)
        r = f;
      if (r != Z_STREAM_END)
        return r;
      r = f;
      inflate_blocks_reset(z->state->blocks, z, &z->state->sub.check.was);
      if (z->state->nowrap)
      {
        z->state->mode = IM_DONE;
        break;
      }
      z->state->mode = IM_CHECK4;
    case IM_CHECK4:
      IM_NEEDBYTE
      z->state->sub.check.need = (uLong)IM_NEXTBYTE << 24;
      z->state->mode = IM_CHECK3;
    case IM_CHECK3:
      IM_NEEDBYTE
      z->state->sub.check.need += (uLong)IM_NEXTBYTE << 16;
      z->state->mode = IM_CHECK2;
    case IM_CHECK2:
      IM_NEEDBYTE
      z->state->sub.check.need += (uLong)IM_NEXTBYTE << 8;
      z->state->mode = IM_CHECK1;
    case IM_CHECK1:
      IM_NEEDBYTE
      z->state->sub.check.need += (uLong)IM_NEXTBYTE;

      if (z->state->sub.check.was != z->state->sub.check.need)
      {
        z->state->mode = IM_BAD;
        z->msg = (char*)"incorrect data check";
        z->state->sub.marker = 5;       // 无法尝试 inflateSync
        break;
      }
      LuTracev((stderr, "inflate: zlib check ok\n"));
      z->state->mode = IM_DONE;
    case IM_DONE:
      return Z_STREAM_END;
    case IM_BAD:
      return Z_DATA_ERROR;
    default:
      return Z_STREAM_ERROR;
  }
}





// unzip.c——使用 zlib 对 .zip 文件进行 IO
// 版本 0.15 beta，1998 年 3 月 19 日，
// 阅读 unzip.h 了解更多信息




#define UNZ_BUFSIZE (16384)
#define UNZ_MAXFILENAMEINZIP (256)
#define SIZECENTRALDIRITEM (0x2e)
#define SIZEZIPLOCALHEADER (0x1e)




const char unz_copyright[] = " unzip 0.15 Copyright 1998 Gilles Vollant ";

// unz_file_info_interntal 包含有关 zipfile 中文件的内部信息
typedef struct unz_file_info_internal_s
{
    uLong offset_curfile;// 本地头的相对偏移量 4 字节
} unz_file_info_internal;


typedef struct
{ bool is_handle; // 句柄或内存
  bool canseek;
  // 对于句柄：
  HANDLE h; bool herr; unsigned long initial_offset; bool mustclosehandle;
  // 用于内存输入。
  void *buf; unsigned int len,pos; // 如果它是一个内存块
} LUFILE;


LUFILE *lufopen(void *z,unsigned int len,DWORD flags,ZRESULT *err)
{ if (flags!=ZIP_HANDLE && flags!=ZIP_FILENAME && flags!=ZIP_MEMORY) {*err=ZR_ARGS; return NULL;}
  //
  HANDLE h=0; bool canseek=false; *err=ZR_OK;
  bool mustclosehandle=false;
  if (flags==ZIP_HANDLE||flags==ZIP_FILENAME)
  { if (flags==ZIP_HANDLE)
    { HANDLE hf = z;
      h=hf; mustclosehandle=false;
#ifdef DuplicateHandle
      BOOL res = DuplicateHandle(GetCurrentProcess(),hf,GetCurrentProcess(),&h,0,FALSE,DUPLICATE_SAME_ACCESS);
      if (!res) mustclosehandle=true;
#endif
    }
    else
    { h=CreateFile((const TCHAR*)z,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
      if (h==INVALID_HANDLE_VALUE) {*err=ZR_NOFILE; return NULL;}
      mustclosehandle=true;
    }
    // 检查句柄是否支持定位；Windows CE 没有相应接口，因此不使用 GetFileType(h)==FILE_TYPE_DISK 判断。
    DWORD res = SetFilePointer(h,0,0,FILE_CURRENT);
    canseek = (res!=0xFFFFFFFF);
  }
  LUFILE *lf = new LUFILE;
  if (flags==ZIP_HANDLE||flags==ZIP_FILENAME)
  { lf->is_handle=true; lf->mustclosehandle=mustclosehandle;
    lf->canseek=canseek;
    lf->h=h; lf->herr=false;
    lf->initial_offset=0;
    if (canseek) lf->initial_offset = SetFilePointer(h,0,NULL,FILE_CURRENT);
  }
  else
  { lf->is_handle=false;
    lf->canseek=true;
    lf->mustclosehandle=false;
    lf->buf=z; lf->len=len; lf->pos=0; lf->initial_offset=0;
  }
  *err=ZR_OK;
  return lf;
}


int lufclose(LUFILE *stream)
{ if (stream==NULL) return EOF;
  if (stream->mustclosehandle) CloseHandle(stream->h);
  delete stream;
  return 0;
}

int luferror(LUFILE *stream)
{ if (stream->is_handle && stream->herr) return 1;
  else return 0;
}

long int luftell(LUFILE *stream)
{ if (stream->is_handle && stream->canseek) return SetFilePointer(stream->h,0,NULL,FILE_CURRENT)-stream->initial_offset;
  else if (stream->is_handle) return 0;
  else return stream->pos;
}

int lufseek(LUFILE *stream, long offset, int whence)
{ if (stream->is_handle && stream->canseek)
  { if (whence==SEEK_SET) SetFilePointer(stream->h,stream->initial_offset+offset,0,FILE_BEGIN);
    else if (whence==SEEK_CUR) SetFilePointer(stream->h,offset,NULL,FILE_CURRENT);
    else if (whence==SEEK_END) SetFilePointer(stream->h,offset,NULL,FILE_END);
    else return 19; // EINVAL：参数无效。
    return 0;
  }
  else if (stream->is_handle) return 29; // ESPIPE：流不支持定位。
  else
  { if (whence==SEEK_SET) stream->pos=offset;
    else if (whence==SEEK_CUR) stream->pos+=offset;
    else if (whence==SEEK_END) stream->pos=stream->len+offset;
    return 0;
  }
}


size_t lufread(void *ptr,size_t size,size_t n,LUFILE *stream)
{ unsigned int toread = (unsigned int)(size*n);
  if (stream->is_handle)
  { DWORD red; BOOL res = ReadFile(stream->h,ptr,toread,&red,NULL);
    if (!res) stream->herr=true;
    return red/size;
  }
  if (stream->pos+toread > stream->len) toread = stream->len-stream->pos;
  memcpy(ptr, (char*)stream->buf + stream->pos, toread); DWORD red = toread;
  stream->pos += red;
  return red/size;
}




// file_in_zip_read_info_s 包含有关 zipfile 中文件的内部信息，
// 读取并解压时
typedef struct
{
	char  *read_buffer;         // 压缩数据的内部缓冲区
	z_stream stream;            // 用于 inflate 的 zLib 流结构

	uLong pos_in_zipfile;       // zip 文件上的字节位置，用于 fseek
	uLong stream_initialised;   // 如果流结构已初始化则设置标志

	uLong offset_local_extrafield;// 本地额外字段的偏移量
	uInt  size_local_extrafield;// 本地额外字段的大小
	uLong pos_local_extrafield;   // read 中本地额外字段的位置

	uLong crc32;                // 所有未压缩数据的 crc32
	uLong crc32_wait;           // 全部解压后必须得到crc32
	uLong rest_read_compressed; // 要解压缩的字节数
	uLong rest_read_uncompressed;// 解压后还需输出的字节数。
	LUFILE* file;                 // zip 文件的 io 结构体
	uLong compression_method;   // 压缩方法（0==存储）
	uLong byte_before_the_zipfile;// zip 文件之前的字节，（对于 sfx，>0）
  bool encrypted;               // 它是加密的吗？
  unsigned long keys[3];        // 解密密钥，由 unzOpenCurrentFile 初始化
  int encheadleft;              // 对 unzReadCurrentFile 的第一次调用将首先读取这么多加密头字节
  char crcenctest;              // 如果加密，我们将根据此检查加密缓冲区
} file_in_zip_read_info_s;


// unz_s 包含有关 zip 文件的内部信息
typedef struct
{
	LUFILE* file;               // zip 文件的 io 结构体
	unz_global_info gi;         // 公开的归档全局信息。
	uLong byte_before_the_zipfile;// zip 文件之前的字节，（对于 sfx，>0）
	uLong num_file;             // zip 文件中当前文件的编号
	uLong pos_in_central_dir;   // 中央目录中当前文件的位置
	uLong current_file_ok;      // 关于当前文件可用性的标志
	uLong central_pos;          // 中央目录开始的位置

	uLong size_central_dir;     // 中央目录的大小
	uLong offset_central_dir;   // 中央目录开头相对于起始磁盘号的偏移量

	unz_file_info cur_file_info; // 有关 zip 中当前文件的公共信息
	unz_file_info_internal cur_file_info_internal; // 当前条目的内部信息。
    file_in_zip_read_info_s* pfile_in_zip_read; // 如果我们正在解压缩当前文件的结构
} unz_s, *unzFile;


int unzStringFileNameCompare (const char* fileName1,const char* fileName2,int iCaseSensitivity);
// 比较两个文件名（文件名1，文件名2）。

z_off_t unztell (unzFile file);
// 给出未压缩数据中的当前位置

int unzeof (unzFile file);
// 如果到达文件末尾则返回 1，其他位置返回 0

int unzGetLocalExtrafield (unzFile file, voidp buf, unsigned len);
// 从当前文件中读取额外字段（由 unzOpenCurrentFile 打开）
// 读取本地文件头的附加字段；其内容有时比中央目录中的附加字段更丰富
// 。
//
// 如果 buf==NULL，则返回本地额外字段的大小
//
// 如果 buf!=NULL，len 是缓冲区的大小，则额外的标头将被复制到
// 缓冲区。
// 返回值是 buf 中复制的字节数，或者（如果 <0）
// 错误代码



// ===========================================================================
// 从 gz_stream 读取一个字节，并更新 next_in 和
// avail_in；输入结束时返回 EOF。
// IN 断言：流 s 已成功打开以供读取。

int unzlocal_getByte(LUFILE *fin,int *pi)
{ unsigned char c;
  int err = (int)lufread(&c, 1, 1, fin);
  if (err==1)
  { *pi = (int)c;
    return UNZ_OK;
  }
  else
  { if (luferror(fin)) return UNZ_ERRNO;
    else return UNZ_EOF;
  }
}


// ===========================================================================
// 从给定的 gz_stream 按小端字节顺序读取一个 long。
int unzlocal_getShort (LUFILE *fin,uLong *pX)
{
    uLong x ;
    int i;
    int err;

    err = unzlocal_getByte(fin,&i);
    x = (uLong)i;

    if (err==UNZ_OK)
        err = unzlocal_getByte(fin,&i);
    x += ((uLong)i)<<8;

    if (err==UNZ_OK)
        *pX = x;
    else
        *pX = 0;
    return err;
}

int unzlocal_getLong (LUFILE *fin,uLong *pX)
{
    uLong x ;
    int i;
    int err;

    err = unzlocal_getByte(fin,&i);
    x = (uLong)i;
    
    if (err==UNZ_OK)
        err = unzlocal_getByte(fin,&i);
    x += ((uLong)i)<<8;

    if (err==UNZ_OK)
        err = unzlocal_getByte(fin,&i);
    x += ((uLong)i)<<16;

    if (err==UNZ_OK)
        err = unzlocal_getByte(fin,&i);
    x += ((uLong)i)<<24;
   
    if (err==UNZ_OK)
        *pX = x;
    else
        *pX = 0;
    return err;
}


// 自定义的忽略大小写比较函数，对应 strcmpi / strcasecmp。
int strcmpcasenosensitive_internal (const char* fileName1,const char *fileName2)
{
	for (;;)
	{
		char c1=*(fileName1++);
		char c2=*(fileName2++);
		if ((c1>='a') && (c1<='z'))
			c1 -= (char)0x20;
		if ((c2>='a') && (c2<='z'))
			c2 -= (char)0x20;
		if (c1=='\0')
			return ((c2=='\0') ? 0 : -1);
		if (c2=='\0')
			return 1;
		if (c1<c2)
			return -1;
		if (c1>c2)
			return 1;
	}
}




//
// 比较两个文件名（文件名1，文件名2）。
// 如果 iCaseSenisivity = 1，则比较区分大小写（如 strcmp）
// 如果 iCaseSenisivity = 2，则比较不区分大小写（如 strcmpi 或 strcasecmp）
//
int unzStringFileNameCompare (const char*fileName1,const char*fileName2,int iCaseSensitivity)
{ if (iCaseSensitivity==1) return strcmp(fileName1,fileName2);
  else return strcmpcasenosensitive_internal(fileName1,fileName2);
} 

#define BUFREADCOMMENT (0x400)


// 定位 ZIP 尾部、全局注释之前的中央目录。Lucian 于 2005
// -07-26 修正：未找到时返回 0xFFFFFFFF；0 是空归档的
// 有效中央目录位置，不能表示失败。
uLong unzlocal_SearchCentralDir(LUFILE *fin)
{ if (lufseek(fin,0,SEEK_END) != 0) return 0xFFFFFFFF;
  uLong uSizeFile = luftell(fin);

  uLong uMaxBack=0xffff; // 全局归档注释的最大大小
  if (uMaxBack>uSizeFile) uMaxBack = uSizeFile;

  unsigned char *buf = (unsigned char*)zmalloc(BUFREADCOMMENT+4);
  if (buf==NULL) return 0xFFFFFFFF;
  uLong uPosFound=0xFFFFFFFF;

  uLong uBackRead = 4;
  while (uBackRead<uMaxBack)
  { uLong uReadSize,uReadPos ;
    int i;
    if (uBackRead+BUFREADCOMMENT>uMaxBack) uBackRead = uMaxBack;
    else uBackRead+=BUFREADCOMMENT;
    uReadPos = uSizeFile-uBackRead ;
    uReadSize = ((BUFREADCOMMENT+4) < (uSizeFile-uReadPos)) ? (BUFREADCOMMENT+4) : (uSizeFile-uReadPos);
    if (lufseek(fin,uReadPos,SEEK_SET)!=0) break;
    if (lufread(buf,(uInt)uReadSize,1,fin)!=1) break;
    for (i=(int)uReadSize-3; (i--)>=0;)
    { if (((*(buf+i))==0x50) && ((*(buf+i+1))==0x4b) &&	((*(buf+i+2))==0x05) && ((*(buf+i+3))==0x06))
      { uPosFound = uReadPos+i;	break;
      }
    }
    if (uPosFound!=0) break;
  }
  if (buf) zfree(buf);
  return uPosFound;
}


int unzGoToFirstFile (unzFile file);
int unzCloseCurrentFile (unzFile file);

// 打开 Zip 文件。
// 如果无法打开 zip 文件（文件不存在或无效），则返回 NULL。
// 否则，返回值是 unzFile 句柄，可与其他解压缩函数一起使用
unzFile unzOpenInternal(LUFILE *fin)
{ if (fin==NULL) return NULL;
  if (unz_copyright[0]!=' ') {lufclose(fin); return NULL;}

  int err=UNZ_OK;
  unz_s us;
  uLong central_pos,uL;
  central_pos = unzlocal_SearchCentralDir(fin);
  if (central_pos==0xFFFFFFFF) err=UNZ_ERRNO;
  if (lufseek(fin,central_pos,SEEK_SET)!=0) err=UNZ_ERRNO;
  // 签名，已经检查过
  if (unzlocal_getLong(fin,&uL)!=UNZ_OK) err=UNZ_ERRNO;
  // 该磁盘的编号
  uLong number_disk;          // 当前磁盘号；用于分卷 ZIP，当前不支持，因此总为 0。
  if (unzlocal_getShort(fin,&number_disk)!=UNZ_OK) err=UNZ_ERRNO;
  // 中央目录起始位置所在的磁盘编号。
  uLong number_disk_with_CD;  // 中央目录所在磁盘号；用于分卷 ZIP，当前不支持，因此总为 0。
  if (unzlocal_getShort(fin,&number_disk_with_CD)!=UNZ_OK) err=UNZ_ERRNO;
  // 该磁盘上中央目录中的条目总数
  if (unzlocal_getShort(fin,&us.gi.number_entry)!=UNZ_OK) err=UNZ_ERRNO;
  // 中央目录中的条目总数
  uLong number_entry_CD;      // 中央目录条目总数；非分卷归档中与 number_entry 相同。
  if (unzlocal_getShort(fin,&number_entry_CD)!=UNZ_OK) err=UNZ_ERRNO;
  if ((number_entry_CD!=us.gi.number_entry) || (number_disk_with_CD!=0) || (number_disk!=0)) err=UNZ_BADZIPFILE;
  // 中央目录的大小
  if (unzlocal_getLong(fin,&us.size_central_dir)!=UNZ_OK) err=UNZ_ERRNO;
  // 中央目录开头相对于起始磁盘号的偏移量
  if (unzlocal_getLong(fin,&us.offset_central_dir)!=UNZ_OK) err=UNZ_ERRNO;
  // zip文件注释长度
  if (unzlocal_getShort(fin,&us.gi.size_comment)!=UNZ_OK) err=UNZ_ERRNO;
  if ((central_pos+fin->initial_offset<us.offset_central_dir+us.size_central_dir) && (err==UNZ_OK)) err=UNZ_BADZIPFILE;
  if (err!=UNZ_OK) {lufclose(fin);return NULL;}

  us.file=fin;
  us.byte_before_the_zipfile = central_pos+fin->initial_offset - (us.offset_central_dir+us.size_central_dir);
  us.central_pos = central_pos;
  us.pfile_in_zip_read = NULL;
  fin->initial_offset = 0; // 因为 zip 文件本身应该能够处理这个问题

  unz_s *s = (unz_s*)zmalloc(sizeof(unz_s));
  *s=us;
  unzGoToFirstFile((unzFile)s);
  return (unzFile)s;
}



// 关闭用 unzipOpen 打开的 ZipFile。
// 如果 .Zip 中有使用 unzipOpenCurrentFile 打开的文件（见下文），
// 在调用 unzipClose 之前，必须使用 unzipCloseCurrentFile 关闭这些文件。
// 如果没有问题则返回UNZ_OK。
int unzClose (unzFile file)
{
	unz_s* s;
	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;

    if (s->pfile_in_zip_read!=NULL)
        unzCloseCurrentFile(file);

	lufclose(s->file);
	if (s) zfree(s); // 未使用的原语句：s=0;
	return UNZ_OK;
}


// 在 *pglobal_info 结构中写入有关 ZipFile 的信息。
// 无需准备结构
// 如果没有问题则返回UNZ_OK。
int unzGetGlobalInfo (unzFile file,unz_global_info *pglobal_info)
{
	unz_s* s;
	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
	*pglobal_info=s->gi;
	return UNZ_OK;
}


// 将日期/时间从 Dos 格式转换为 tm_unz（更容易阅读）
void unzlocal_DosDateToTmuDate (uLong ulDosDate, tm_unz* ptm)
{
    uLong uDate;
    uDate = (uLong)(ulDosDate>>16);
    ptm->tm_mday = (uInt)(uDate&0x1f) ;
    ptm->tm_mon =  (uInt)((((uDate)&0x1E0)/0x20)-1) ;
    ptm->tm_year = (uInt)(((uDate&0x0FE00)/0x0200)+1980) ;

    ptm->tm_hour = (uInt) ((ulDosDate &0xF800)/0x800);
    ptm->tm_min =  (uInt) ((ulDosDate&0x7E0)/0x20) ;
    ptm->tm_sec =  (uInt) (2*(ulDosDate&0x1f)) ;
}

// 获取有关 zip 文件中当前文件的信息，仅包含内部信息
int unzlocal_GetCurrentFileInfoInternal (unzFile file,
                                                  unz_file_info *pfile_info,
                                                  unz_file_info_internal
                                                  *pfile_info_internal,
                                                  char *szFileName,
												  uLong fileNameBufferSize,
                                                  void *extraField,
												  uLong extraFieldBufferSize,
                                                  char *szComment,
												  uLong commentBufferSize);

int unzlocal_GetCurrentFileInfoInternal (unzFile file, unz_file_info *pfile_info,
   unz_file_info_internal *pfile_info_internal, char *szFileName,
   uLong fileNameBufferSize, void *extraField, uLong extraFieldBufferSize,
   char *szComment, uLong commentBufferSize)
{
	unz_s* s;
	unz_file_info file_info;
	unz_file_info_internal file_info_internal;
	int err=UNZ_OK;
	uLong uMagic;
	long lSeek=0;

	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
	if (lufseek(s->file,s->pos_in_central_dir+s->byte_before_the_zipfile,SEEK_SET)!=0)
		err=UNZ_ERRNO;


	// 检查文件头的魔数签名。
	if (err==UNZ_OK)
		if (unzlocal_getLong(s->file,&uMagic) != UNZ_OK)
			err=UNZ_ERRNO;
		else if (uMagic!=0x02014b50)
			err=UNZ_BADZIPFILE;

	if (unzlocal_getShort(s->file,&file_info.version) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.version_needed) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.flag) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.compression_method) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getLong(s->file,&file_info.dosDate) != UNZ_OK)
		err=UNZ_ERRNO;

    unzlocal_DosDateToTmuDate(file_info.dosDate,&file_info.tmu_date);

	if (unzlocal_getLong(s->file,&file_info.crc) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getLong(s->file,&file_info.compressed_size) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getLong(s->file,&file_info.uncompressed_size) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.size_filename) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.size_file_extra) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.size_file_comment) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.disk_num_start) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&file_info.internal_fa) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getLong(s->file,&file_info.external_fa) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getLong(s->file,&file_info_internal.offset_curfile) != UNZ_OK)
		err=UNZ_ERRNO;

	lSeek+=file_info.size_filename;
	if ((err==UNZ_OK) && (szFileName!=NULL))
	{
		uLong uSizeRead ;
		if (file_info.size_filename<fileNameBufferSize)
		{
			*(szFileName+file_info.size_filename)='\0';
			uSizeRead = file_info.size_filename;
		}
		else
			uSizeRead = fileNameBufferSize;

		if ((file_info.size_filename>0) && (fileNameBufferSize>0))
			if (lufread(szFileName,(uInt)uSizeRead,1,s->file)!=1)
				err=UNZ_ERRNO;
		lSeek -= uSizeRead;
	}


	if ((err==UNZ_OK) && (extraField!=NULL))
	{
		uLong uSizeRead ;
		if (file_info.size_file_extra<extraFieldBufferSize)
			uSizeRead = file_info.size_file_extra;
		else
			uSizeRead = extraFieldBufferSize;

		if (lSeek!=0)
			if (lufseek(s->file,lSeek,SEEK_CUR)==0)
				lSeek=0;
			else
				err=UNZ_ERRNO;
		if ((file_info.size_file_extra>0) && (extraFieldBufferSize>0))
			if (lufread(extraField,(uInt)uSizeRead,1,s->file)!=1)
				err=UNZ_ERRNO;
		lSeek += file_info.size_file_extra - uSizeRead;
	}
	else
		lSeek+=file_info.size_file_extra;


	if ((err==UNZ_OK) && (szComment!=NULL))
	{
		uLong uSizeRead ;
		if (file_info.size_file_comment<commentBufferSize)
		{
			*(szComment+file_info.size_file_comment)='\0';
			uSizeRead = file_info.size_file_comment;
		}
		else
			uSizeRead = commentBufferSize;

		if (lSeek!=0)
			if (lufseek(s->file,lSeek,SEEK_CUR)==0)
				{} // 未使用的原语句：lSeek=0;
			else
				err=UNZ_ERRNO;
		if ((file_info.size_file_comment>0) && (commentBufferSize>0))
			if (lufread(szComment,(uInt)uSizeRead,1,s->file)!=1)
				err=UNZ_ERRNO;
		// 未使用的原语句：lSeek+=file_info.size_file_comment - uSizeRead;
	}
	else {} // 未使用的原语句：lSeek+=file_info.size_file_comment;

	if ((err==UNZ_OK) && (pfile_info!=NULL))
		*pfile_info=file_info;

	if ((err==UNZ_OK) && (pfile_info_internal!=NULL))
		*pfile_info_internal=file_info_internal;

	return err;
}



// 在 *pglobal_info 结构中写入有关 ZipFile 的信息。
// 无需准备结构
// 如果没有问题则返回UNZ_OK。
int unzGetCurrentFileInfo (unzFile file, unz_file_info *pfile_info,
  char *szFileName, uLong fileNameBufferSize, void *extraField, uLong extraFieldBufferSize,
  char *szComment, uLong commentBufferSize)
{ return unzlocal_GetCurrentFileInfoInternal(file,pfile_info,NULL,szFileName,fileNameBufferSize,
      extraField,extraFieldBufferSize, szComment,commentBufferSize);
}


// 将 zip 文件的当前文件设置为第一个文件。
// 没有问题则返回UNZ_OK
int unzGoToFirstFile (unzFile file)
{
	int err;
	unz_s* s;
	if (file==NULL) return UNZ_PARAMERROR;
	s=(unz_s*)file;
	s->pos_in_central_dir=s->offset_central_dir;
	s->num_file=0;
	err=unzlocal_GetCurrentFileInfoInternal(file,&s->cur_file_info,
											 &s->cur_file_info_internal,
											 NULL,0,NULL,0,NULL,0);
	s->current_file_ok = (err == UNZ_OK);
	return err;
}


// 将 zip 文件的当前文件设置为下一个文件。
// 没有问题则返回UNZ_OK
// 如果当前文件已经是最后一项，则返回 UNZ_END_OF_LIST_OF_FILE。
int unzGoToNextFile (unzFile file)
{
	unz_s* s;
	int err;

	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
	if (!s->current_file_ok)
		return UNZ_END_OF_LIST_OF_FILE;
	if (s->num_file+1==s->gi.number_entry)
		return UNZ_END_OF_LIST_OF_FILE;

	s->pos_in_central_dir += SIZECENTRALDIRITEM + s->cur_file_info.size_filename +
			s->cur_file_info.size_file_extra + s->cur_file_info.size_file_comment ;
	s->num_file++;
	err = unzlocal_GetCurrentFileInfoInternal(file,&s->cur_file_info,
											   &s->cur_file_info_internal,
											   NULL,0,NULL,0,NULL,0);
	s->current_file_ok = (err == UNZ_OK);
	return err;
}


// 尝试在 zip 文件中找到文件 szFileName。
// 有关 iCaseSensitivity 含义，请参阅 unzStringFileNameCompare
// 返回值：
// 如果找到文件，则 UNZ_OK。它成为当前文件。
// UNZ_END_OF_LIST_OF_FILE（如果未找到文件）
int unzLocateFile (unzFile file, const char *szFileName, int iCaseSensitivity)
{
	unz_s* s;
	int err;


	uLong num_fileSaved;
	uLong pos_in_central_dirSaved;


	if (file==NULL)
		return UNZ_PARAMERROR;

    if (strlen(szFileName)>=UNZ_MAXFILENAMEINZIP)
        return UNZ_PARAMERROR;

	s=(unz_s*)file;
	if (!s->current_file_ok)
		return UNZ_END_OF_LIST_OF_FILE;

	num_fileSaved = s->num_file;
	pos_in_central_dirSaved = s->pos_in_central_dir;

	err = unzGoToFirstFile(file);

	while (err == UNZ_OK)
	{
		char szCurrentFileName[UNZ_MAXFILENAMEINZIP+1];
		unzGetCurrentFileInfo(file,NULL,
								szCurrentFileName,sizeof(szCurrentFileName)-1,
								NULL,0,NULL,0);
		if (unzStringFileNameCompare(szCurrentFileName,szFileName,iCaseSensitivity)==0)
			return UNZ_OK;
		err = unzGoToNextFile(file);
	}

	s->num_file = num_fileSaved ;
	s->pos_in_central_dir = pos_in_central_dirSaved ;
	return err;
}


// 读取当前zip文件的本地头
// 核对本地文件头与中央目录中当前条目信息是否一致；通过 *
// piSizeVar 返回本地文件头可变部分的大小，即文件名和附加
// 字段的总长度。
//
int unzlocal_CheckCurrentFileCoherencyHeader (unz_s *s,uInt *piSizeVar,
  uLong *poffset_local_extrafield, uInt  *psize_local_extrafield)
{
	uLong uMagic,uData,uFlags;
	uLong size_filename;
	uLong size_extra_field;
	int err=UNZ_OK;

	*piSizeVar = 0;
	*poffset_local_extrafield = 0;
	*psize_local_extrafield = 0;

	if (lufseek(s->file,s->cur_file_info_internal.offset_curfile + s->byte_before_the_zipfile,SEEK_SET)!=0)
		return UNZ_ERRNO;


	if (err==UNZ_OK)
		if (unzlocal_getLong(s->file,&uMagic) != UNZ_OK)
			err=UNZ_ERRNO;
		else if (uMagic!=0x04034b50)
			err=UNZ_BADZIPFILE;

	if (unzlocal_getShort(s->file,&uData) != UNZ_OK)
		err=UNZ_ERRNO;
//	else if ((err==UNZ_OK) && (uData!=s->cur_file_info.wVersion))
//		err=UNZ_BADZIPFILE;
	if (unzlocal_getShort(s->file,&uFlags) != UNZ_OK)
		err=UNZ_ERRNO;

	if (unzlocal_getShort(s->file,&uData) != UNZ_OK)
		err=UNZ_ERRNO;
	else if ((err==UNZ_OK) && (uData!=s->cur_file_info.compression_method))
		err=UNZ_BADZIPFILE;

    if ((err==UNZ_OK) && (s->cur_file_info.compression_method!=0) &&
                         (s->cur_file_info.compression_method!=Z_DEFLATED))
        err=UNZ_BADZIPFILE;

	if (unzlocal_getLong(s->file,&uData) != UNZ_OK) // 日期/时间
		err=UNZ_ERRNO;

	if (unzlocal_getLong(s->file,&uData) != UNZ_OK) // 循环冗余校验（CRC）
		err=UNZ_ERRNO;
	else if ((err==UNZ_OK) && (uData!=s->cur_file_info.crc) &&
		                      ((uFlags & 8)==0))
		err=UNZ_BADZIPFILE;

	if (unzlocal_getLong(s->file,&uData) != UNZ_OK) // 压缩后大小。
		err=UNZ_ERRNO;
	else if ((err==UNZ_OK) && (uData!=s->cur_file_info.compressed_size) &&
							  ((uFlags & 8)==0))
		err=UNZ_BADZIPFILE;

	if (unzlocal_getLong(s->file,&uData) != UNZ_OK) // 未压缩大小。
		err=UNZ_ERRNO;
	else if ((err==UNZ_OK) && (uData!=s->cur_file_info.uncompressed_size) &&
							  ((uFlags & 8)==0))
		err=UNZ_BADZIPFILE;


	if (unzlocal_getShort(s->file,&size_filename) != UNZ_OK)
		err=UNZ_ERRNO;
	else if ((err==UNZ_OK) && (size_filename!=s->cur_file_info.size_filename))
		err=UNZ_BADZIPFILE;

	*piSizeVar += (uInt)size_filename;

	if (unzlocal_getShort(s->file,&size_extra_field) != UNZ_OK)
		err=UNZ_ERRNO;
	*poffset_local_extrafield= s->cur_file_info_internal.offset_curfile +
									SIZEZIPLOCALHEADER + size_filename;
	*psize_local_extrafield = (uInt)size_extra_field;

	*piSizeVar += (uInt)size_extra_field;

	return err;
}





// 打开 zip 文件中的当前文件以读取数据。
// 如果没有错误并且文件被打开，则返回值为UNZ_OK。
int unzOpenCurrentFile (unzFile file, const char *password)
{
	int err;
	int Store;
	uInt iSizeVar;
	unz_s* s;
	file_in_zip_read_info_s* pfile_in_zip_read_info;
	uLong offset_local_extrafield;  // 本地额外字段的偏移量
	uInt  size_local_extrafield;    // 本地额外字段的大小

	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
	if (!s->current_file_ok)
		return UNZ_PARAMERROR;

    if (s->pfile_in_zip_read != NULL)
        unzCloseCurrentFile(file);

	if (unzlocal_CheckCurrentFileCoherencyHeader(s,&iSizeVar,
				&offset_local_extrafield,&size_local_extrafield)!=UNZ_OK)
		return UNZ_BADZIPFILE;

	pfile_in_zip_read_info = (file_in_zip_read_info_s*)zmalloc(sizeof(file_in_zip_read_info_s));
	if (pfile_in_zip_read_info==NULL)
		return UNZ_INTERNALERROR;

	pfile_in_zip_read_info->read_buffer=(char*)zmalloc(UNZ_BUFSIZE);
	pfile_in_zip_read_info->offset_local_extrafield = offset_local_extrafield;
	pfile_in_zip_read_info->size_local_extrafield = size_local_extrafield;
	pfile_in_zip_read_info->pos_local_extrafield=0;

	if (pfile_in_zip_read_info->read_buffer==NULL)
	{
		if (pfile_in_zip_read_info!=0) zfree(pfile_in_zip_read_info); // 未使用的原语句：pfile_in_zip_read_info=0;
		return UNZ_INTERNALERROR;
	}

	pfile_in_zip_read_info->stream_initialised=0;

	if ((s->cur_file_info.compression_method!=0) && (s->cur_file_info.compression_method!=Z_DEFLATED))
        { // 未使用的原语句：err=UNZ_BADZIPFILE;
        }
	Store = s->cur_file_info.compression_method==0;

	pfile_in_zip_read_info->crc32_wait=s->cur_file_info.crc;
	pfile_in_zip_read_info->crc32=0;
	pfile_in_zip_read_info->compression_method = s->cur_file_info.compression_method;
	pfile_in_zip_read_info->file=s->file;
	pfile_in_zip_read_info->byte_before_the_zipfile=s->byte_before_the_zipfile;

    pfile_in_zip_read_info->stream.total_out = 0;

	if (!Store)
	{
	  pfile_in_zip_read_info->stream.zalloc = (alloc_func)0;
	  pfile_in_zip_read_info->stream.zfree = (free_func)0;
	  pfile_in_zip_read_info->stream.opaque = (voidpf)0;

          err=inflateInit2(&pfile_in_zip_read_info->stream);
	  if (err == Z_OK)
	    pfile_in_zip_read_info->stream_initialised=1;
        // 传递 windowBits < 0 表示没有 zlib 标头。
        // 此模式下 inflate 通常需要在压缩流之后再有一个占位字节，
        // 才能完成解压并返回 Z_STREAM_END。本 ZIP 实现已
        // 知压缩和未压缩大小，因此不会只依赖 Z_STREAM_END 判
        // 断结束。
        //
	}
	pfile_in_zip_read_info->rest_read_compressed = s->cur_file_info.compressed_size ;
	pfile_in_zip_read_info->rest_read_uncompressed = s->cur_file_info.uncompressed_size ;
  pfile_in_zip_read_info->encrypted = (s->cur_file_info.flag&1)!=0;
  bool extlochead = (s->cur_file_info.flag&8)!=0;
  if (extlochead) pfile_in_zip_read_info->crcenctest = (char)((s->cur_file_info.dosDate>>8)&0xff);
  else pfile_in_zip_read_info->crcenctest = (char)(s->cur_file_info.crc >> 24);
  pfile_in_zip_read_info->encheadleft = (pfile_in_zip_read_info->encrypted?12:0);
  pfile_in_zip_read_info->keys[0] = 305419896L;
  pfile_in_zip_read_info->keys[1] = 591751049L;
  pfile_in_zip_read_info->keys[2] = 878082192L;
  for (const char *cp=password; cp!=0 && *cp!=0; cp++) Uupdate_keys(pfile_in_zip_read_info->keys,*cp);

	pfile_in_zip_read_info->pos_in_zipfile =
            s->cur_file_info_internal.offset_curfile + SIZEZIPLOCALHEADER +
			  iSizeVar;

	pfile_in_zip_read_info->stream.avail_in = (uInt)0;

	s->pfile_in_zip_read = pfile_in_zip_read_info;

  return UNZ_OK;
}


// 从当前文件中读取字节。
// buf 包含必须复制数据的缓冲区
// len 是 buf 的大小。
// 成功复制部分数据时返回已复制字节数，并设置 *reached_eof。
// 到达文件末尾时返回 0，并设置 *reached_eof。
// 发生错误时返回负错误码；此时 *reached_eof 不具有有效含义。
// （UNZ_ERRNO 表示 IO 错误，或 zLib 错误表示解压缩错误）
int unzReadCurrentFile  (unzFile file, voidp buf, unsigned len, bool *reached_eof)
{ int err=UNZ_OK;
  uInt iRead = 0;
  if (reached_eof!=0) *reached_eof=false;

  unz_s *s = (unz_s*)file;
  if (s==NULL) return UNZ_PARAMERROR;

  file_in_zip_read_info_s* pfile_in_zip_read_info = s->pfile_in_zip_read;
  if (pfile_in_zip_read_info==NULL) return UNZ_PARAMERROR;
  if ((pfile_in_zip_read_info->read_buffer == NULL)) return UNZ_END_OF_LIST_OF_FILE;
  if (len==0) return 0;

  pfile_in_zip_read_info->stream.next_out = (Byte*)buf;
  pfile_in_zip_read_info->stream.avail_out = (uInt)len;

  if (len>pfile_in_zip_read_info->rest_read_uncompressed)
  { pfile_in_zip_read_info->stream.avail_out = (uInt)pfile_in_zip_read_info->rest_read_uncompressed;
  }

  while (pfile_in_zip_read_info->stream.avail_out>0)
  { if ((pfile_in_zip_read_info->stream.avail_in==0) && (pfile_in_zip_read_info->rest_read_compressed>0))
    { uInt uReadThis = UNZ_BUFSIZE;
      if (pfile_in_zip_read_info->rest_read_compressed<uReadThis) uReadThis = (uInt)pfile_in_zip_read_info->rest_read_compressed;
      if (uReadThis == 0) {if (reached_eof!=0) *reached_eof=true; return UNZ_EOF;}
      if (lufseek(pfile_in_zip_read_info->file, pfile_in_zip_read_info->pos_in_zipfile + pfile_in_zip_read_info->byte_before_the_zipfile,SEEK_SET)!=0) return UNZ_ERRNO;
      if (lufread(pfile_in_zip_read_info->read_buffer,uReadThis,1,pfile_in_zip_read_info->file)!=1) return UNZ_ERRNO;
      pfile_in_zip_read_info->pos_in_zipfile += uReadThis;
      pfile_in_zip_read_info->rest_read_compressed-=uReadThis;
      pfile_in_zip_read_info->stream.next_in = (Byte*)pfile_in_zip_read_info->read_buffer;
      pfile_in_zip_read_info->stream.avail_in = (uInt)uReadThis;
      //
      if (pfile_in_zip_read_info->encrypted)
      { char *buf = (char*)pfile_in_zip_read_info->stream.next_in;
        for (unsigned int i=0; i<uReadThis; i++) buf[i]=zdecode(pfile_in_zip_read_info->keys,buf[i]);
      }
    }

    unsigned int uDoEncHead = pfile_in_zip_read_info->encheadleft;
    if (uDoEncHead>pfile_in_zip_read_info->stream.avail_in) uDoEncHead=pfile_in_zip_read_info->stream.avail_in;
    if (uDoEncHead>0)
    { char bufcrc=pfile_in_zip_read_info->stream.next_in[uDoEncHead-1];
      pfile_in_zip_read_info->rest_read_uncompressed-=uDoEncHead;
      pfile_in_zip_read_info->stream.avail_in -= uDoEncHead;
      pfile_in_zip_read_info->stream.next_in += uDoEncHead;
      pfile_in_zip_read_info->encheadleft -= uDoEncHead;
      if (pfile_in_zip_read_info->encheadleft==0)
      { if (bufcrc!=pfile_in_zip_read_info->crcenctest) return UNZ_PASSWORD;
      }
    }

    if (pfile_in_zip_read_info->compression_method==0)
    { uInt uDoCopy,i ;
      if (pfile_in_zip_read_info->stream.avail_out < pfile_in_zip_read_info->stream.avail_in)
      { uDoCopy = pfile_in_zip_read_info->stream.avail_out ;
      }
      else
      { uDoCopy = pfile_in_zip_read_info->stream.avail_in ;
      }
      for (i=0;i<uDoCopy;i++) *(pfile_in_zip_read_info->stream.next_out+i) = *(pfile_in_zip_read_info->stream.next_in+i);
      pfile_in_zip_read_info->crc32 = ucrc32(pfile_in_zip_read_info->crc32,pfile_in_zip_read_info->stream.next_out,uDoCopy);
      pfile_in_zip_read_info->rest_read_uncompressed-=uDoCopy;
      pfile_in_zip_read_info->stream.avail_in -= uDoCopy;
      pfile_in_zip_read_info->stream.avail_out -= uDoCopy;
      pfile_in_zip_read_info->stream.next_out += uDoCopy;
      pfile_in_zip_read_info->stream.next_in += uDoCopy;
      pfile_in_zip_read_info->stream.total_out += uDoCopy;
      iRead += uDoCopy;
      if (pfile_in_zip_read_info->rest_read_uncompressed==0) {if (reached_eof!=0) *reached_eof=true;}
    }
    else
    { uLong uTotalOutBefore,uTotalOutAfter;
      const Byte *bufBefore;
      uLong uOutThis;
      int flush=Z_SYNC_FLUSH;
      uTotalOutBefore = pfile_in_zip_read_info->stream.total_out;
      bufBefore = pfile_in_zip_read_info->stream.next_out;
      //
      err=inflate(&pfile_in_zip_read_info->stream,flush);
      //
      uTotalOutAfter = pfile_in_zip_read_info->stream.total_out;
      uOutThis = uTotalOutAfter-uTotalOutBefore;
      pfile_in_zip_read_info->crc32 = ucrc32(pfile_in_zip_read_info->crc32,bufBefore,(uInt)(uOutThis));
      pfile_in_zip_read_info->rest_read_uncompressed -= uOutThis;
      iRead += (uInt)(uTotalOutAfter - uTotalOutBefore);
      if (err==Z_STREAM_END || pfile_in_zip_read_info->rest_read_uncompressed==0)
      { if (reached_eof!=0) *reached_eof=true;
        return iRead;
      }
      if (err!=Z_OK) break;
    }
  }

  if (err==Z_OK) return iRead;
  return err;
}


// 给出未压缩数据中的当前位置
z_off_t unztell (unzFile file)
{
	unz_s* s;
	file_in_zip_read_info_s* pfile_in_zip_read_info;
	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
    pfile_in_zip_read_info=s->pfile_in_zip_read;

	if (pfile_in_zip_read_info==NULL)
		return UNZ_PARAMERROR;

	return (z_off_t)pfile_in_zip_read_info->stream.total_out;
}


// 如果到达文件末尾则返回 1，其他位置返回 0
int unzeof (unzFile file)
{
	unz_s* s;
	file_in_zip_read_info_s* pfile_in_zip_read_info;
	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
    pfile_in_zip_read_info=s->pfile_in_zip_read;

	if (pfile_in_zip_read_info==NULL)
		return UNZ_PARAMERROR;

	if (pfile_in_zip_read_info->rest_read_uncompressed == 0)
		return 1;
	else
		return 0;
}



// 从当前文件中读取额外字段（由 unzOpenCurrentFile 打开）
// 这里读取本地文件头的附加字段；其中可能比中央目录的附加字段包含更
// 多信息。
// 如果buf==NULL，则返回可读取的本地额外字段的大小
// 如果 buf!=NULL，len 是缓冲区的大小，则额外的标头将复制到 buf 中。
// 返回值是 buf 中复制的字节数，或者（如果 <0）错误代码
int unzGetLocalExtrafield (unzFile file,voidp buf,unsigned len)
{
	unz_s* s;
	file_in_zip_read_info_s* pfile_in_zip_read_info;
	uInt read_now;
	uLong size_to_read;

	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
    pfile_in_zip_read_info=s->pfile_in_zip_read;

	if (pfile_in_zip_read_info==NULL)
		return UNZ_PARAMERROR;

	size_to_read = (pfile_in_zip_read_info->size_local_extrafield -
				pfile_in_zip_read_info->pos_local_extrafield);

	if (buf==NULL)
		return (int)size_to_read;

	if (len>size_to_read)
		read_now = (uInt)size_to_read;
	else
		read_now = (uInt)len ;

	if (read_now==0)
		return 0;

	if (lufseek(pfile_in_zip_read_info->file, pfile_in_zip_read_info->offset_local_extrafield +  pfile_in_zip_read_info->pos_local_extrafield,SEEK_SET)!=0)
		return UNZ_ERRNO;

	if (lufread(buf,(uInt)size_to_read,1,pfile_in_zip_read_info->file)!=1)
		return UNZ_ERRNO;

	return (int)read_now;
}

// 关闭使用 unzipOpenCurrentFile 打开的 zip 文件
// 若已读完当前条目但 CRC 不匹配，则返回 UNZ_CRCERROR。
int unzCloseCurrentFile (unzFile file)
{
	int err=UNZ_OK;

	unz_s* s;
	file_in_zip_read_info_s* pfile_in_zip_read_info;
	if (file==NULL)
		return UNZ_PARAMERROR;
	s=(unz_s*)file;
    pfile_in_zip_read_info=s->pfile_in_zip_read;

	if (pfile_in_zip_read_info==NULL)
		return UNZ_PARAMERROR;


	if (pfile_in_zip_read_info->rest_read_uncompressed == 0)
	{
		if (pfile_in_zip_read_info->crc32 != pfile_in_zip_read_info->crc32_wait)
			err=UNZ_CRCERROR;
	}


	if (pfile_in_zip_read_info->read_buffer!=0)
        { void *buf = pfile_in_zip_read_info->read_buffer;
          zfree(buf);
          pfile_in_zip_read_info->read_buffer=0;
        }
	pfile_in_zip_read_info->read_buffer = NULL;
	if (pfile_in_zip_read_info->stream_initialised)
		inflateEnd(&pfile_in_zip_read_info->stream);

	pfile_in_zip_read_info->stream_initialised = 0;
        if (pfile_in_zip_read_info!=0) zfree(pfile_in_zip_read_info); // 未使用的原语句：pfile_in_zip_read_info=0;

    s->pfile_in_zip_read=NULL;

	return err;
}


// 获取 szComment 缓冲区中 ZipFile 的全局注释字符串。
// uSizeBuf 是 szComment 缓冲区的大小。
// 返回复制的字节数或错误代码<0
int unzGetGlobalComment (unzFile file, char *szComment, uLong uSizeBuf)
{ //int err=UNZ_OK;
  unz_s* s;
  uLong uReadThis ;
  if (file==NULL) return UNZ_PARAMERROR;
  s=(unz_s*)file;
  uReadThis = uSizeBuf;
  if (uReadThis>s->gi.size_comment) uReadThis = s->gi.size_comment;
  if (lufseek(s->file,s->central_pos+22,SEEK_SET)!=0) return UNZ_ERRNO;
  if (uReadThis>0)
  { *szComment='\0';
    if (lufread(szComment,(uInt)uReadThis,1,s->file)!=1) return UNZ_ERRNO;
  }
  if ((szComment != NULL) && (uSizeBuf > s->gi.size_comment)) *(szComment+s->gi.size_comment)='\0';
  return (int)uReadThis;
}





int unzOpenCurrentFile (unzFile file, const char *password);
int unzReadCurrentFile (unzFile file, void *buf, unsigned len);
int unzCloseCurrentFile (unzFile file);


typedef unsigned __int32 lutime_t;       // 自己定义它，因为我们不包括 time.h

FILETIME timet2filetime(const lutime_t t)
{ LONGLONG i = Int32x32To64(t,10000000) + 116444736000000000;
  FILETIME ft;
  ft.dwLowDateTime = (DWORD) i;
  ft.dwHighDateTime = (DWORD)(i >>32);
  return ft;
}

FILETIME dosdatetime2filetime(WORD dosdate,WORD dostime)
{ // 日期位域：0..4 为日，5..8 为月，9..15 为年份减去 1980。
  // 时间位域：0..4 为秒数除以 2，5..10 为分钟，11..15 为小时。
  SYSTEMTIME st;
  st.wYear = (WORD)(((dosdate>>9)&0x7f) + 1980);
  st.wMonth = (WORD)((dosdate>>5)&0xf);
  st.wDay = (WORD)(dosdate&0x1f);
  st.wHour = (WORD)((dostime>>11)&0x1f);
  st.wMinute = (WORD)((dostime>>5)&0x3f);
  st.wSecond = (WORD)((dostime&0x1f)*2);
  st.wMilliseconds = 0;
  FILETIME ft; SystemTimeToFileTime(&st,&ft);
  return ft;
}



class TUnzip
{ public:
  TUnzip(const char *pwd) : uf(0), unzbuf(0), currentfile(-1), czei(-1), password(0) {if (pwd!=0) {password=new char[strlen(pwd)+1]; strcpy(password,pwd);}}
  ~TUnzip() {if (password!=0) delete[] password; password=0; if (unzbuf!=0) delete[] unzbuf; unzbuf=0;}

  unzFile uf; int currentfile; ZIPENTRY cze; int czei;
  char *password;
  char *unzbuf;            // 延迟创建和销毁，由 Unzip 使用
  TCHAR rootdir[MAX_PATH]; // 包括尾部斜杠

  ZRESULT Open(void *z,unsigned int len,DWORD flags);
  ZRESULT Get(int index,ZIPENTRY *ze);
  ZRESULT Find(const TCHAR *name,bool ic,int *index,ZIPENTRY *ze);
  ZRESULT Unzip(int index,void *dst,unsigned int len,DWORD flags);
  ZRESULT SetUnzipBaseDir(const TCHAR *dir);
  ZRESULT Close();
};


ZRESULT TUnzip::Open(void *z,unsigned int len,DWORD flags)
{ if (uf!=0 || currentfile!=-1) return ZR_NOTINITED;
  //
#ifdef GetCurrentDirectory
  GetCurrentDirectory(MAX_PATH,rootdir);
#else
  _tcscpy(rootdir,_T("\\"));
#endif
  TCHAR lastchar = rootdir[_tcslen(rootdir)-1];
  if (lastchar!='\\' && lastchar!='/') _tcscat(rootdir,_T("\\"));
  //
  if (flags==ZIP_HANDLE)
  { // 检查句柄是否支持定位；Windows CE 没有相应接口，因此不使用 GetFileType(h)==FILE_TYPE_DISK 判断。
    DWORD res = SetFilePointer(z,0,0,FILE_CURRENT);
    bool canseek = (res!=0xFFFFFFFF);
    if (!canseek) return ZR_SEEK;
  }
  ZRESULT e; LUFILE *f = lufopen(z,len,flags,&e);
  if (f==NULL) return e;
  uf = unzOpenInternal(f);
  if (uf==0) return ZR_NOFILE;
  return ZR_OK;
}

ZRESULT TUnzip::SetUnzipBaseDir(const TCHAR *dir)
{ _tcscpy(rootdir,dir);
  TCHAR lastchar = rootdir[_tcslen(rootdir)-1];
  if (lastchar!='\\' && lastchar!='/') _tcscat(rootdir,_T("\\"));
  return ZR_OK;
}

ZRESULT TUnzip::Get(int index,ZIPENTRY *ze)
{ if (index<-1 || index>=(int)uf->gi.number_entry) return ZR_ARGS;
  if (currentfile!=-1) unzCloseCurrentFile(uf); currentfile=-1;
  if (index==czei && index!=-1) {memcpy(ze,&cze,sizeof(ZIPENTRY)); return ZR_OK;}
  if (index==-1)
  { ze->index = uf->gi.number_entry;
    ze->name[0]=0;
    ze->attr=0;
    ze->atime.dwLowDateTime=0; ze->atime.dwHighDateTime=0;
    ze->ctime.dwLowDateTime=0; ze->ctime.dwHighDateTime=0;
    ze->mtime.dwLowDateTime=0; ze->mtime.dwHighDateTime=0;
    ze->comp_size=0;
    ze->unc_size=0;
    return ZR_OK;
  }
  if (index<(int)uf->num_file) unzGoToFirstFile(uf);
  while ((int)uf->num_file<index) unzGoToNextFile(uf);
  unz_file_info ufi; char fn[MAX_PATH];
  unzGetCurrentFileInfo(uf,&ufi,fn,MAX_PATH,NULL,0,NULL,0);
  // 现在获取额外的标头。我们自己做这件事，而不是
  // 调用 unzOpenCurrentFile &c.，以避免分配不必要的资源。
  unsigned int extralen,iSizeVar; unsigned long offset;
  int res = unzlocal_CheckCurrentFileCoherencyHeader(uf,&iSizeVar,&offset,&extralen);
  if (res!=UNZ_OK) return ZR_CORRUPT;
  if (lufseek(uf->file,offset,SEEK_SET)!=0) return ZR_READ;
  unsigned char *extra = new unsigned char[extralen];
  if (lufread(extra,1,(uInt)extralen,uf->file)!=extralen) {delete[] extra; return ZR_READ;}
  //
  ze->index=uf->num_file;
  TCHAR tfn[MAX_PATH];
#ifdef UNICODE
  MultiByteToWideChar(CP_UTF8,0,fn,-1,tfn,MAX_PATH);
#else
  strcpy(tfn,fn);
#endif
  // 原有安全处理：如果 ZIP 条目名包含可疑的路径片段，
  // 例如 c:\windows\file.txt、\windows\file.txt 或 fred\..\..\..\windows\file.txt，
  // 则先移除这些片段，供 UnzipItem(hz,i,ze.name) 使用。
  // 原作者注明：若调用方确实需要完整的原始路径信息，
  // 可自行调整此安全处理；此处仅翻译说明，保留原实现。
  // 具体会截掉 c:\、\、/、[目录]\..、[目录]/.. 等前缀。
  const TCHAR *sfn=tfn;
  for (;;)
  { if (sfn[0]!=0 && sfn[1]==':') {sfn+=2; continue;}
    if (sfn[0]=='\\') {sfn++; continue;}
    if (sfn[0]=='/') {sfn++; continue;}
    const TCHAR *c;
    c=_tcsstr(sfn,_T("\\..\\")); if (c!=0) {sfn=c+4; continue;}
    c=_tcsstr(sfn,_T("\\../")); if (c!=0) {sfn=c+4; continue;}
    c=_tcsstr(sfn,_T("/../")); if (c!=0) {sfn=c+4; continue;}
    c=_tcsstr(sfn,_T("/..\\")); if (c!=0) {sfn=c+4; continue;}
    break;
  }
  _tcscpy(ze->name, sfn);


  // ZIP 属性为 32 位数值，低 16 位表示 Windows/DOS 属性，
  // 高 16 位表示标准 Unix stat.st_mode 属性；先尝试
  // 按 Unix 属性解释。
  unsigned long a = ufi.external_fa;
  bool isdir  =   (a&0x40000000)!=0;
  bool readonly=  (a&0x00800000)==0;
  // bool readable=  (a&0x01000000)!=0; // 未使用，保留原语句。
  // bool executable=(a&0x00400000)!=0; // 未使用，保留原语句。
  bool hidden=false, system=false, archive=true;
  // 但对常见的来源系统类型，将使用低 16 位属性覆盖上述判断。
  int host = ufi.version>>8;
  if (host==0 || host==7 || host==11 || host==14)
  { readonly=  (a&0x00000001)!=0;
    hidden=    (a&0x00000002)!=0;
    system=    (a&0x00000004)!=0;
    isdir=     (a&0x00000010)!=0;
    archive=   (a&0x00000020)!=0;
  }
  ze->attr=0;
  if (isdir) ze->attr |= FILE_ATTRIBUTE_DIRECTORY;
  if (archive) ze->attr|=FILE_ATTRIBUTE_ARCHIVE;
  if (hidden) ze->attr|=FILE_ATTRIBUTE_HIDDEN;
  if (readonly) ze->attr|=FILE_ATTRIBUTE_READONLY;
  if (system) ze->attr|=FILE_ATTRIBUTE_SYSTEM;
  ze->comp_size = ufi.compressed_size;
  ze->unc_size = ufi.uncompressed_size;
  //
  WORD dostime = (WORD)(ufi.dosDate&0xFFFF);
  WORD dosdate = (WORD)((ufi.dosDate>>16)&0xFFFF);
  FILETIME ftd = dosdatetime2filetime(dosdate,dostime);
  FILETIME ft; LocalFileTimeToFileTime(&ftd,&ft);
  ze->atime=ft; ze->ctime=ft; ze->mtime=ft;
  // ZIP 条目至少包含 DOS 时间；如果还存在扩展时间字段，
  // 则优先使用其中的时间信息。
  unsigned int epos=0;
  while (epos+4<extralen)
  { char etype[3]; etype[0]=extra[epos+0]; etype[1]=extra[epos+1]; etype[2]=0;
    int size = extra[epos+2];
    if (strcmp(etype,"UT")!=0) {epos += 4+size; continue;}
    int flags = extra[epos+4];
    bool hasmtime = (flags&1)!=0;
    bool hasatime = (flags&2)!=0;
    bool hasctime = (flags&4)!=0;
    epos+=5;
    if (hasmtime)
    { lutime_t mtime = ((extra[epos+0])<<0) | ((extra[epos+1])<<8) |((extra[epos+2])<<16) | ((extra[epos+3])<<24);
	  epos+=4;
      ze->mtime = timet2filetime(mtime);
    }
    if (hasatime)
    { lutime_t atime = ((extra[epos+0])<<0) | ((extra[epos+1])<<8) |((extra[epos+2])<<16) | ((extra[epos+3])<<24);
      epos+=4;
      ze->atime = timet2filetime(atime);
    }
    if (hasctime)
    { lutime_t ctime = ((extra[epos+0])<<0) | ((extra[epos+1])<<8) |((extra[epos+2])<<16) | ((extra[epos+3])<<24);
      epos+=4;
      ze->ctime = timet2filetime(ctime);
    }
    break;
  }
  //
  if (extra!=0) delete[] extra;
  memcpy(&cze,ze,sizeof(ZIPENTRY)); czei=index;
  return ZR_OK;
}

ZRESULT TUnzip::Find(const TCHAR *tname,bool ic,int *index,ZIPENTRY *ze)
{ char name[MAX_PATH];
#ifdef UNICODE
  WideCharToMultiByte(CP_UTF8,0,tname,-1,name,MAX_PATH,0,0);
#else
  strcpy(name,tname);
#endif
  int res = unzLocateFile(uf,name,ic?CASE_INSENSITIVE:CASE_SENSITIVE);
  if (res!=UNZ_OK)
  { if (index!=0) *index=-1;
    if (ze!=NULL) {ZeroMemory(ze,sizeof(ZIPENTRY)); ze->index=-1;}
    return ZR_NOTFOUND;
  }
  if (currentfile!=-1) unzCloseCurrentFile(uf); currentfile=-1;
  int i = (int)uf->num_file;
  if (index!=NULL) *index=i;
  if (ze!=NULL)
  { ZRESULT zres = Get(i,ze);
    if (zres!=ZR_OK) return zres;
  }
  return ZR_OK;
}

void EnsureDirectory(const TCHAR *rootdir, const TCHAR *dir)
{ if (rootdir!=0 && GetFileAttributes(rootdir)==0xFFFFFFFF) CreateDirectory(rootdir,0);
  if (*dir==0) return;
  const TCHAR *lastslash=dir, *c=lastslash;
  while (*c!=0) {if (*c=='/' || *c=='\\') lastslash=c; c++;}
  const TCHAR *name=lastslash;
  if (lastslash!=dir)
  { TCHAR tmp[MAX_PATH]; memcpy(tmp,dir,sizeof(TCHAR)*(lastslash-dir));
    tmp[lastslash-dir]=0;
    EnsureDirectory(rootdir,tmp);
    name++;
  }
  TCHAR cd[MAX_PATH]; *cd=0; if (rootdir!=0) _tcscpy(cd,rootdir); _tcscat(cd,dir);
  if (GetFileAttributes(cd)==0xFFFFFFFF) CreateDirectory(cd,NULL);
}



ZRESULT TUnzip::Unzip(int index,void *dst,unsigned int len,DWORD flags)
{ if (flags!=ZIP_MEMORY && flags!=ZIP_FILENAME && flags!=ZIP_HANDLE) return ZR_ARGS;
  if (flags==ZIP_MEMORY)
  { if (index!=currentfile)
    { if (currentfile!=-1) unzCloseCurrentFile(uf); currentfile=-1;
      if (index>=(int)uf->gi.number_entry) return ZR_ARGS;
      if (index<(int)uf->num_file) unzGoToFirstFile(uf);
      while ((int)uf->num_file<index) unzGoToNextFile(uf);
      unzOpenCurrentFile(uf,password); currentfile=index;
    }
    bool reached_eof;
    int res = unzReadCurrentFile(uf,dst,len,&reached_eof);
    if (res<=0) {unzCloseCurrentFile(uf); currentfile=-1;}
    if (reached_eof) return ZR_OK;
    if (res>0) return ZR_MORE;
    if (res==UNZ_PASSWORD) return ZR_PASSWORD;
    return ZR_FLATE;
  }
  // 否则我们将写入句柄或文件
  if (currentfile!=-1) unzCloseCurrentFile(uf); currentfile=-1;
  if (index>=(int)uf->gi.number_entry) return ZR_ARGS;
  if (index<(int)uf->num_file) unzGoToFirstFile(uf);
  while ((int)uf->num_file<index) unzGoToNextFile(uf);
  ZIPENTRY ze; Get(index,&ze);
  // 目录条目采用专门处理。
  if ((ze.attr&FILE_ATTRIBUTE_DIRECTORY)!=0)
  { if (flags==ZIP_HANDLE) return ZR_OK; // 向句柄或内存解压目录时，不执行数据写入。
    const TCHAR *dir = (const TCHAR*)dst;
    bool isabsolute = (dir[0]=='/' || dir[0]=='\\' || (dir[0]!=0 && dir[1]==':'));
    if (isabsolute) EnsureDirectory(0,dir); else EnsureDirectory(rootdir,dir);
    return ZR_OK;
  }
  // 普通文件条目则写入文件或句柄。
  HANDLE h;
  if (flags==ZIP_HANDLE) h=dst;
  else
  { const TCHAR *ufn = (const TCHAR*)dst;
    // 相对路径拼接到解压基目录；绝对路径按原样使用。
    // ufn="zipfile.txt"  dir=""  name="zipfile.txt"  fn="c:\\currentdir\\zipfile.txt"
    // ufn="dir1/dir2/subfile.txt"  dir="dir1/dir2/"  name="subfile.txt"  fn="c:\\currentdir\\dir1/dir2/subfiles.txt"
    // ufn="\z\file.txt"  dir="\z\"  name="file.txt"  fn="\z\file.txt"
    // 若直接使用 ZIP 条目名作为 ufn，可能存在路径安全风险：
    // 恶意归档可能写入 c:\windows。因此，通过 GetZipItem
    // 获取条目名称时，原有处理不会返回绝对路径。
    const TCHAR *name=ufn; const TCHAR *c=name; while (*c!=0) {if (*c=='/' || *c=='\\') name=c+1; c++;}
    TCHAR dir[MAX_PATH]; _tcscpy(dir,ufn); if (name==ufn) *dir=0; else dir[name-ufn]=0;
    TCHAR fn[MAX_PATH]; 
    bool isabsolute = (dir[0]=='/' || dir[0]=='\\' || (dir[0]!=0 && dir[1]==':'));
    if (isabsolute) {wsprintf(fn,_T("%s%s"),dir,name); EnsureDirectory(0,dir);}
    else {wsprintf(fn,_T("%s%s%s"),rootdir,dir,name); EnsureDirectory(rootdir,dir);}
    //
    h = CreateFile(fn,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,ze.attr,NULL);
  }
  if (h==INVALID_HANDLE_VALUE) return ZR_NOFILE;
  unzOpenCurrentFile(uf,password);
  if (unzbuf==0) unzbuf=new char[16384]; DWORD haderr=0;
  //

  for (; haderr==0;)
  { bool reached_eof;
    int res = unzReadCurrentFile(uf,unzbuf,16384,&reached_eof);
    if (res==UNZ_PASSWORD) {haderr=ZR_PASSWORD; break;}
    if (res<0) {haderr=ZR_FLATE; break;}
    if (res>0) {DWORD writ; BOOL bres=WriteFile(h,unzbuf,res,&writ,NULL); if (!bres) {haderr=ZR_WRITE; break;}}
    if (reached_eof) break;
    if (res==0) {haderr=ZR_FLATE; break;}
  }
  if (!haderr) SetFileTime(h,&ze.ctime,&ze.atime,&ze.mtime); // 如果是管道可能会失败
  if (flags!=ZIP_HANDLE) CloseHandle(h);
  unzCloseCurrentFile(uf);
  if (haderr!=0) return haderr;
  return ZR_OK;
}

ZRESULT TUnzip::Close()
{ if (currentfile!=-1) unzCloseCurrentFile(uf); currentfile=-1;
  if (uf!=0) unzClose(uf); uf=0;
  return ZR_OK;
}





ZRESULT lasterrorU=ZR_OK;

unsigned int FormatZipMessageU(ZRESULT code, TCHAR *buf,unsigned int len)
{ if (code==ZR_RECENT) code=lasterrorU;
  const TCHAR *msg=_T("unknown zip result code");
  switch (code)
  { case ZR_OK: msg=_T("Success"); break;
    case ZR_NODUPH: msg=_T("Culdn't duplicate handle"); break;
    case ZR_NOFILE: msg=_T("Couldn't create/open file"); break;
    case ZR_NOALLOC: msg=_T("Failed to allocate memory"); break;
    case ZR_WRITE: msg=_T("Error writing to file"); break;
    case ZR_NOTFOUND: msg=_T("File not found in the zipfile"); break;
    case ZR_MORE: msg=_T("Still more data to unzip"); break;
    case ZR_CORRUPT: msg=_T("Zipfile is corrupt or not a zipfile"); break;
    case ZR_READ: msg=_T("Error reading file"); break;
    case ZR_PASSWORD: msg=_T("Correct password required"); break;
    case ZR_ARGS: msg=_T("Caller: faulty arguments"); break;
    case ZR_PARTIALUNZ: msg=_T("Caller: the file had already been partially unzipped"); break;
    case ZR_NOTMMAP: msg=_T("Caller: can only get memory of a memory zipfile"); break;
    case ZR_MEMSIZE: msg=_T("Caller: not enough space allocated for memory zipfile"); break;
    case ZR_FAILED: msg=_T("Caller: there was a previous error"); break;
    case ZR_ENDED: msg=_T("Caller: additions to the zip have already been ended"); break;
    case ZR_ZMODE: msg=_T("Caller: mixing creation and opening of zip"); break;
    case ZR_NOTINITED: msg=_T("Zip-bug: internal initialisation not completed"); break;
    case ZR_SEEK: msg=_T("Zip-bug: trying to seek the unseekable"); break;
    case ZR_MISSIZE: msg=_T("Zip-bug: the anticipated size turned out wrong"); break;
    case ZR_NOCHANGE: msg=_T("Zip-bug: tried to change mind, but not allowed"); break;
    case ZR_FLATE: msg=_T("Zip-bug: an internal error during flation"); break;
  }
  unsigned int mlen=(unsigned int)_tcslen(msg);
  if (buf==0 || len==0) return mlen;
  unsigned int n=mlen; if (n+1>len) n=len-1;
  _tcsncpy(buf,msg,n); buf[n]=0;
  return mlen;
}


typedef struct
{ DWORD flag;
  TUnzip *unz;
} TUnzipHandleData;

HZIP OpenZipInternal(void *z,unsigned int len,DWORD flags, const char *password)
{ TUnzip *unz = new TUnzip(password);
  lasterrorU = unz->Open(z,len,flags);
  if (lasterrorU!=ZR_OK) {delete unz; return 0;}
  TUnzipHandleData *han = new TUnzipHandleData;
  han->flag=1; han->unz=unz; return (HZIP)han;
}
HZIP OpenZipHandle(HANDLE h, const char *password) {return OpenZipInternal((void*)h,0,ZIP_HANDLE,password);}
HZIP OpenZip(const TCHAR *fn, const char *password) {return OpenZipInternal((void*)fn,0,ZIP_FILENAME,password);}
HZIP OpenZip(void *z,unsigned int len, const char *password) {return OpenZipInternal(z,len,ZIP_MEMORY,password);}


ZRESULT GetZipItem(HZIP hz, int index, ZIPENTRY *ze)
{ ze->index=0; *ze->name=0; ze->unc_size=0;
  if (hz==0) {lasterrorU=ZR_ARGS;return ZR_ARGS;}
  TUnzipHandleData *han = (TUnzipHandleData*)hz;
  if (han->flag!=1) {lasterrorU=ZR_ZMODE;return ZR_ZMODE;}
  TUnzip *unz = han->unz;
  lasterrorU = unz->Get(index,ze);
  return lasterrorU;
}

ZRESULT FindZipItem(HZIP hz, const TCHAR *name, bool ic, int *index, ZIPENTRY *ze)
{ if (hz==0) {lasterrorU=ZR_ARGS;return ZR_ARGS;}
  TUnzipHandleData *han = (TUnzipHandleData*)hz;
  if (han->flag!=1) {lasterrorU=ZR_ZMODE;return ZR_ZMODE;}
  TUnzip *unz = han->unz;
  lasterrorU = unz->Find(name,ic,index,ze);
  return lasterrorU;
}

ZRESULT UnzipItemInternal(HZIP hz, int index, void *dst, unsigned int len, DWORD flags)
{ if (hz==0) {lasterrorU=ZR_ARGS;return ZR_ARGS;}
  TUnzipHandleData *han = (TUnzipHandleData*)hz;
  if (han->flag!=1) {lasterrorU=ZR_ZMODE;return ZR_ZMODE;}
  TUnzip *unz = han->unz;
  lasterrorU = unz->Unzip(index,dst,len,flags);
  return lasterrorU;
}
ZRESULT UnzipItemHandle(HZIP hz, int index, HANDLE h) {return UnzipItemInternal(hz,index,(void*)h,0,ZIP_HANDLE);}
ZRESULT UnzipItem(HZIP hz, int index, const TCHAR *fn) {return UnzipItemInternal(hz,index,(void*)fn,0,ZIP_FILENAME);}
ZRESULT UnzipItem(HZIP hz, int index, void *z,unsigned int len) {return UnzipItemInternal(hz,index,z,len,ZIP_MEMORY);}

ZRESULT SetUnzipBaseDir(HZIP hz, const TCHAR *dir)
{ if (hz==0) {lasterrorU=ZR_ARGS;return ZR_ARGS;}
  TUnzipHandleData *han = (TUnzipHandleData*)hz;
  if (han->flag!=1) {lasterrorU=ZR_ZMODE;return ZR_ZMODE;}
  TUnzip *unz = han->unz;
  lasterrorU = unz->SetUnzipBaseDir(dir);
  return lasterrorU;
}


ZRESULT CloseZipU(HZIP hz)
{ if (hz==0) {lasterrorU=ZR_ARGS;return ZR_ARGS;}
  TUnzipHandleData *han = (TUnzipHandleData*)hz;
  if (han->flag!=1) {lasterrorU=ZR_ZMODE;return ZR_ZMODE;}
  TUnzip *unz = han->unz;
  lasterrorU = unz->Close();
  delete unz;
  delete han;
  return lasterrorU;
}

bool IsZipHandleU(HZIP hz)
{ if (hz==0) return false;
  TUnzipHandleData *han = (TUnzipHandleData*)hz;
  return (han->flag==1);
}


