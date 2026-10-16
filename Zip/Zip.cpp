#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include "zip.h"

// 修改原因：统一控制语句的换行格式，避免大括号及语句体与条件挤在同一行。
// 变更内容：仅调整大括号、语句体与缩进；保留原有表达式、接口及控制流程。

// 本文件主要基于 Info-ZIP 源码，由 Lucian Wischik 修改并重新封装。
// 原封装重写了 ZIP 文件布局的生成部分，
// 并扩展了压缩输入、输出的存储方式，
// 支持内存、句柄、管道、分页文件和磁盘文件，
// 同时增加加密与 Unicode 支持。
// 原始源码：http://www.info-zip.org。
// 以下保留原始版权和许可原文；本轮仅调整注释与 UTF-8 BOM 编码。
// 中文说明：以下为 1999 年 10 月 5 日版 Info-ZIP 版权与许可，原文予以保留。
//
//
// This is version 1999-Oct-05 of the Info-ZIP copyright and license.
// The definitive version of this document should be available at
// ftp://ftp.cdrom.com/pub/infozip/license.html indefinitely.
// 版权所有：1990—1999 Info-ZIP，保留所有权利。
// Copyright (c) 1990-1999 Info-ZIP.  All rights reserved.
// 许可中的 Info-ZIP 指下列原作者及贡献者，姓名保持原文。
// For the purposes of this copyright and license, "Info-ZIP" is defined as
// the following set of individuals:
// 作者名单：
//   Mark Adler, John Bush, Karl Davis, Harald Denker, Jean-Michel Dubois,
//   Jean-loup Gailly, Hunter Goatley, Ian Gorman, Chris Herborth, Dirk Haase,
//   Greg Hartwig, Robert Heath, Jonathan Hudson, Paul Kienitz, David Kirschbaum,
//   Johnny Lee, Onno van der Linden, Igor Mandrichenko, Steve P. Miller,
//   Sergio Monesi, Keith Owens, George Petrov, Greg Roelofs, Kai Uwe Rommel,
//   Steve Salisbury, Dave Smith, Christian Spieler, Antoine Verheijen,
//   Paul von Behren, Rich Wales, Mike White
// 免责声明译文：本软件按现状提供，无任何明示或默示保证；作者及贡献者不对使用或无法使用本软件产生的任何损害承担责任。
// This software is provided "as is," without warranty of any kind, express
// or implied.  In no event shall Info-ZIP or its contributors be held liable
// for any direct, indirect, incidental, special or consequential damages
// arising out of the use of or inability to use this software.
// 授权译文：允许任何人出于包括商业用途在内的任何目的使用、修改和自由再分发本软件，但须遵守以下条件。
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 条件 1、2 译文：源码分发须保留版权、定义、免责声明和条款；二进制分发须在随附文档或其他材料中重现这些内容。
//    1. Redistributions of source code must retain the above copyright notice,
//       definition, disclaimer, and this list of conditions.
// 条件 3 译文：修改版本须明确标注，禁止冒充原始源码或 Info-ZIP 官方发布；未经许可不得使用条款列出的名称，也不得误用其邮件地址或网址。
//    2. Redistributions in binary form must reproduce the above copyright
//       notice, definition, disclaimer, and this list of conditions in
//       documentation and/or other materials provided with the distribution.
//
//    3. Altered versions--including, but not limited to, ports to new operating
//       systems, existing ports with new graphical interfaces, and dynamic,
//       shared, or static library versions--must be plainly marked as such
//       and must not be misrepresented as being the original source.  Such
//       altered versions also must not be misrepresented as being Info-ZIP
//       releases--including, but not limited to, labeling of the altered
//       versions with the names "Info-ZIP" (or any variation thereof, including,
//       but not limited to, different capitalizations), "Pocket UnZip," "WiZ"
//       or "MacZip" without the explicit permission of Info-ZIP.  Such altered
//       versions are further prohibited from misrepresentative use of the
//       Zip-Bugs or Info-ZIP e-mail addresses or of the Info-ZIP URL(s).
// 条件 4 译文：Info-ZIP 保留在其源码和二进制发行中使用所列名称的权利。
//    4. Info-ZIP retains the right to use the names "Info-ZIP," "Zip," "UnZip,"
//       "WiZ," "Pocket UnZip," "Pocket Zip," and "MacZip" for its own source and
//       binary releases.
//

typedef unsigned char uch;  // 无符号 8 位值
typedef unsigned short ush; // 无符号 16 位值
typedef unsigned long ulg;  // 无符号 32 位值
typedef size_t extent;      // 文件大小
typedef unsigned Pos;       // 必须至少为 32 位
typedef unsigned IPos;      // Pos 为字符窗口中的索引类型；IPos 用于参数传递。

#ifndef EOF
#define EOF (-1)
#endif

// 错误返回值：0..4 和 12..18 沿用 PKZIP 的约定。
// PKZIP 将 4..10 均用于表示内存不足，
// 本实现保留 4 的含义，将 5..10 用于其他错误。
#define ZE_MISS -1   // 由 procname()、zipbare() 使用
#define ZE_OK 0      // 成功
#define ZE_EOF 2     // zip 文件意外结束
#define ZE_FORM 3    // zip 文件结构错误
#define ZE_MEM 4     // 内存不足
#define ZE_LOGIC 5   // 内部逻辑错误
#define ZE_BIG 6     // 条目太大而无法拆分
#define ZE_NOTE 7    // 归档注释格式无效。
#define ZE_TEST 8    // zip 测试 (-T) 失败或内存不足
#define ZE_ABORT 9   // 用户中断或终止
#define ZE_TEMP 10   // 使用临时文件时出错
#define ZE_READ 11   // 读取文件或移动文件位置时出错。
#define ZE_NONE 12   // 无事可做
#define ZE_NAME 13   // zip 文件丢失或为空
#define ZE_WRITE 14  // 写入文件时出错
#define ZE_CREAT 15  // 无法打开写入
#define ZE_PARMS 16  // 错误的命令行
#define ZE_OPEN 18   // 无法打开指定文件进行读取
#define ZE_MAXERR 18 // 最大错误编号。

// 内部文件属性
#define UNKNOWN (-1)
#define BINARY 0
#define ASCII 1

#define BEST -1   // 选择最佳方式：DEFLATE 压缩或直接存储。
#define STORE 0   // 直接存储，不进行压缩。
#define DEFLATE 8 // DEFLATE 压缩。

#define CRCVAL_INITIAL 0L

// MSDOS 文件或目录属性
#define MSDOS_HIDDEN_ATTR 0x02
#define MSDOS_DIR_ATTR 0x10

// 签名后的标头长度（以字节为单位）
#define LOCHEAD 26
#define CENHEAD 42
#define ENDHEAD 18

// 额外字段处理的定义：
#define EB_HEADSIZE 4           /* 额外字段块头的长度 */
#define EB_LEN 2                /* 标头中数据长度字段的偏移量 */
#define EB_UT_MINLEN 1          /* 最小 UT 字段包含标志字节 */
#define EB_UT_FLAGS 0           /* Flags 字段的字节偏移量 */
#define EB_UT_TIME1 1           /* 第一个时间值的字节偏移量 */
#define EB_UT_FL_MTIME (1 << 0) /* 包含修改时间 mtime。 */
#define EB_UT_FL_ATIME (1 << 1) /* 包含访问时间 atime。 */
#define EB_UT_FL_CTIME (1 << 2) /* 包含创建时间 ctime。 */
#define EB_UT_LEN(n) (EB_UT_MINLEN + 4 * (n))
#define EB_L_UT_SIZE (EB_HEADSIZE + EB_UT_LEN(3))
#define EB_C_UT_SIZE (EB_HEADSIZE + EB_UT_LEN(1))

// 用于将机器整数写入小端格式的宏
#define PUTSH(a, f)                         \
    {                                       \
        char _putsh_c = (char)((a) & 0xff); \
        wfunc(param, &_putsh_c, 1);         \
        _putsh_c = (char)((a) >> 8);        \
        wfunc(param, &_putsh_c, 1);         \
    }
#define PUTLG(a, f)                 \
    {                               \
        PUTSH((a) & 0xffff, (f))    \
        PUTSH((a) >> 16, (f))       \
    }

// -- ZIP 文件的结构 --
// zip 文件信息标头的签名
#define LOCSIG 0x04034b50L
#define CENSIG 0x02014b50L
#define ENDSIG 0x06054b50L
#define EXTLOCSIG 0x08074b50L

#define MIN_MATCH 3
#define MAX_MATCH 258
// 最小和最大匹配长度

#define WSIZE (0x8000)
// 最大窗口大小 = 32K。如果你实在内存不足，编译
// 使用较小的 WSIZE 但这会降低文件的压缩率
// 大小 > WSIZE。在当前实现中，WSIZE 必须是 2 的幂。
//

#define MIN_LOOKAHEAD (MAX_MATCH + MIN_MATCH + 1)
// 最少的前瞻量（输入文件末尾除外）。
// 有关 MIN_MATCH+1 的注释，请参阅 deflate.c。
//

#define MAX_DIST (WSIZE - MIN_LOOKAHEAD)
// 为了简化代码，特别是在 16 位机器上，匹配
// 距离限制为 MAX_DIST 而不是 WSIZE。
//

#define ZIP_HANDLE 1
#define ZIP_FILENAME 2
#define ZIP_MEMORY 3
#define ZIP_FOLDER 4

// ===========================================================================
// 常量定义。
//

#define MAX_BITS 15
// 所有编码的位数均不能超过 MAX_BITS。

#define MAX_BL_BITS 7
// 码长编码的位数不能超过 MAX_BL_BITS。

#define LENGTH_CODES 29
// 长度编码数量，不含 END_BLOCK。

#define LITERALS 256
// 字面值字节数量，对应 0..255。

#define END_BLOCK 256
// 数据块结束标记的字面值编码。

#define L_CODES (LITERALS + 1 + LENGTH_CODES)
// 字面值和长度编码总数，包含 END_BLOCK。

#define D_CODES 30
// 距离编码数量。

#define BL_CODES 19
// 用于传送码长的编码数量。

#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES 2
// 三种块类型

#define LIT_BUFSIZE 0x8000
#define DIST_BUFSIZE LIT_BUFSIZE
// 字面值/长度和距离的匹配缓冲区的大小。  有
// 将 LIT_BUFSIZE 限制为 64K 的 4 个原因：
// - 出现次数可以保存在16位计数器中
// - 如果第一个块的压缩不成功，则所有输入数据都将被压缩
// 仍在窗口中，因此即使输入时我们仍然可以发出存储的块
// 来自标准输入。  （这也可以对所有块进行，如果
// LIT_BUFSIZE 不大于 32K。）
// - 如果小于 64K 的文件压缩不成功，我们可以
// 甚至发出存储的文件而不是存储的块（节省 5 个字节）。
// - 不太频繁地创建新的哈夫曼树可能无法提供快速
// 适应输入数据统计的变化。 （取为
// 示例一个二进制文件，其代码可压缩性较差，后跟
// 高度可压缩的字符串表。）较小的缓冲区大小给出
// 快速适应，但当然有传输树的开销
// 更频繁。
// 原作者注：以上理由实际超过了四条。
// 当前代码是通用的，允许 DIST_BUFSIZE < LIT_BUFSIZE （以保存
// 以压缩为代价的内存）。一些优化是可能的
// 如果我们依赖 DIST_BUFSIZE == LIT_BUFSIZE。
//

#define REP_3_6 16
// 重复前一个位长度 3-6 次（2 位重复计数）

#define REPZ_3_10 17
// 重复零长度 3-10 次（3 位重复计数）

#define REPZ_11_138 18
// 重复零长度 11-138 次（7 位重复计数）

#define HEAP_SIZE (2 * L_CODES + 1)
// 最大堆大小

// ===========================================================================
// “位串”例程使用的本地数据。
//

#define Buf_size (8 * 2 * sizeof(char))
// bi_buf 中使用的位数。 （bi_buf 可以在
// 在某些系统上超过 16 位。）

// 将 16 位值输出到比特流，低（最旧）字节在前
#define PUTSHORT(state, w)                                                           \
    {                                                                                \
        if (state.bs.out_offset >= state.bs.out_size - 1)                            \
            state.flush_outbuf(state.param, state.bs.out_buf, &state.bs.out_offset); \
        state.bs.out_buf[state.bs.out_offset++] = (char)((w) & 0xff);                \
        state.bs.out_buf[state.bs.out_offset++] = (char)((ush)(w) >> 8);             \
    }

#define PUTBYTE(state, b)                                                            \
    {                                                                                \
        if (state.bs.out_offset >= state.bs.out_size)                                \
            state.flush_outbuf(state.param, state.bs.out_buf, &state.bs.out_offset); \
        state.bs.out_buf[state.bs.out_offset++] = (char)(b);                         \
    }

// DEFLATE.CPP 的头部定义。

#define HASH_BITS 15
// 为了可移植到 16 位计算机，请勿使用高于 15 的值。

#define HASH_SIZE (unsigned)(1 << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1)
#define WMASK (WSIZE - 1)
// HASH_SIZE 和 WSIZE 必须是 2 的幂

#define NIL 0
// 哈希链的尾部

#define FAST 4
#define SLOW 2
// 通用位标志的速度选项

#define TOO_FAR 4096
// 如果距离超过 TOO_FAR，长度为 3 的匹配将被丢弃

#define EQUAL 0
// 相等字符串的 memcmp 结果

// ===========================================================================
// “最长匹配”例程使用的本地数据。

#define H_SHIFT ((HASH_BITS + MIN_MATCH - 1) / MIN_MATCH)
// ins_h 和 del_h 必须每次移位的位数
// 输入步骤。必须是这样，在 MIN_MATCH 步骤之后，最旧的
// byte不再参与哈希键，即：
//   H_SHIFT * MIN_MATCH >= HASH_BITS

#define max_insert_length max_lazy_match
// 仅当长度匹配时才在哈希表中插入新字符串
// 不大于这个长度。这可以节省时间，但会降低压缩率。
// max_insert_length 仅用于压缩级别 <= 3。

const int extra_lbits[LENGTH_CODES] // 每个长度编码的附加位数。
    = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

const int extra_dbits[D_CODES] // 每个距离编码的附加位数。
    = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

const int extra_blbits[BL_CODES] // 每个码长编码的附加位数。
    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7};

const uch bl_order[BL_CODES] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
// 位长代码的长度按递减顺序发送
// 概率，以避免传输未使用的位长度代码的长度。

typedef struct config
{
    ush good_length; // 减少超过此匹配长度的惰性搜索
    ush max_lazy;    // 不要执行超过此匹配长度的惰性搜索
    ush nice_length; // 退出超过此匹配长度的搜索
    ush max_chain;
} config;

// max_lazy_match、good_match、nice_match 和 max_chain_length 的值，
// 取决于所需的包级别 (0..9)。下面给出的值有
// 经过调整以排除病理文件的最坏情况性能。
// 对于特定文件可能会找到更好的值。
//

const config configuration_table[10] = {
    // 各列依次为 good、lazy、nice、chain 配置。
    {0, 0, 0, 0},          // 等级 0：仅存储。
    {4, 4, 8, 4},          // 等级 1：优先速度，不进行惰性匹配。
    {4, 5, 16, 8},         // 2
    {4, 6, 32, 32},        // 3
    {4, 4, 16, 16},        // 等级 4：使用惰性匹配。
    {8, 16, 32, 32},       // 5
    {8, 16, 128, 128},     // 6
    {8, 32, 128, 256},     // 7
    {32, 128, 258, 1024},  // 8
    {32, 258, 258, 4096}}; // 等级 9：优先压缩率。

// 注意：deflate() 代码要求 max_lazy >= MIN_MATCH 且 max_chain >= 4
// 对于 deflate_fast() （级别 <= 3），good 会被忽略，lazy 有不同的含义。

// 描述单个值及其代码字符串的数据结构。
typedef struct ct_data
{
    union
    {
        ush freq; // 出现次数。
        ush code; // 位串。
    } fc;
    union
    {
        ush dad; // 哈夫曼树的父节点
        ush len; // 编码位数。
    } dl;
} ct_data;

typedef struct tree_desc
{
    ct_data *dyn_tree;     // 动态树。
    ct_data *static_tree;  // 对应静态树；不存在时为 NULL。
    const int *extra_bits; // 各编码附加位数表；不存在时为 NULL。
    int extra_base;        // extra_bits 的基本索引
    int elems;             // 树中的最大元素数
    int max_length;        // 代码的最大位长度
    int max_code;          // 出现次数非零的最大编码编号。
} tree_desc;

class TTreeState
{
  public:
    TTreeState();

    ct_data dyn_ltree[HEAP_SIZE];       // 字面值和长度树
    ct_data dyn_dtree[2 * D_CODES + 1]; // 距离树
    ct_data static_ltree[L_CODES + 2];  // 静态字面值树...
    // ...由于位长度是强加的，因此不需要 L_CODES
    // 堆构建期间使用的额外代码。然而代码 286 和 287
    // 需要构建规范树（请参阅下面的 ct_init）。
    ct_data static_dtree[D_CODES]; // 静态距离树...
    // ...（实际上是一个简单的树，因为所有代码都使用 5 位。）
    ct_data bl_tree[2 * BL_CODES + 1]; // 位长度的哈夫曼树

    tree_desc l_desc;
    tree_desc d_desc;
    tree_desc bl_desc;

    ush bl_count[MAX_BITS + 1]; // 最优树的每个位长度的代码数

    int heap[2 * L_CODES + 1]; // 用于构建哈夫曼树的堆
    int heap_len;              // 堆中元素的数量
    int heap_max;              // 出现次数最高的元素所在位置。
    // heap[n] 的子节点是 heap[2*n] 和 heap[2*n+1]。未使用堆[0]。
    // 使用相同的堆数组来构建所有树。

    uch depth[2 * L_CODES + 1];
    // 各子树深度；出现次数相同时以深度决定先后。

    uch length_code[MAX_MATCH - MIN_MATCH + 1];
    // 每个归一化匹配长度的长度编码，0 对应 MIN_MATCH。

    uch dist_code[512];
    // 距离代码。前 256 个值对应于距离
    // 3 .. 258，最后256个值对应于高8位
    // 15 位距离。

    int base_length[LENGTH_CODES];
    // 每个代码的第一个标准化长度 (0 = MIN_MATCH)

    int base_dist[D_CODES];
    // 每个代码的第一个归一化距离（0 = 1 的距离）

    uch far l_buf[LIT_BUFSIZE];  // 字面值/长度缓冲区
    ush far d_buf[DIST_BUFSIZE]; // 距离缓冲区

    uch flag_buf[(LIT_BUFSIZE / 8)];
    // flag_buf 是一个位数组，用于区分字面值和长度
    // l_buf，从而指示距离的存在或不存在。

    unsigned last_lit;   // l_buf 中的运行索引
    unsigned last_dist;  // d_buf 中的运行索引
    unsigned last_flags; // flag_buf 中的运行索引
    uch flags;           // 当前标志尚未保存在 flag_buf 中
    uch flag_bit;        // 标志中使用的当前位
    // 位从位 0（最低有效位）开始填充到标志中。
    // 注意：这些标志在当前代码中是多余的，因为我们不这样做
    // 利用 DIST_BUFSIZE == LIT_BUFSIZE。

    ulg opt_len;    // 具有最佳树的当前块的位长度
    ulg static_len; // 具有静态树的当前块的位长度

    ulg cmpr_bytelen;  // 压缩文件的总字节长度
    ulg cmpr_len_bits; // “cmpr_bytelen”之后的位数

    ulg input_len; // 输入文件的总字节长度
    // input_len 仅用于调试，因为我们可以通过其他方式获取它。

    ush *file_type; // 指向 UNKNOWN、BINARY 或 ASCII 文件类型标记的指针。
    //   int *file_method;     // 指向 DEFLATE 或 STORE 压缩方式的指针。
};

TTreeState::TTreeState()
{
    tree_desc a = {dyn_ltree, static_ltree, extra_lbits, LITERALS + 1, L_CODES, MAX_BITS, 0};
    l_desc = a;
    tree_desc b = {dyn_dtree, static_dtree, extra_dbits, 0, D_CODES, MAX_BITS, 0};
    d_desc = b;
    tree_desc c = {bl_tree, NULL, extra_blbits, 0, BL_CODES, MAX_BL_BITS, 0};
    bl_desc = c;
    last_lit = 0;
    last_dist = 0;
    last_flags = 0;
}

class TBitState
{
  public:
    int flush_flg;
    //
    unsigned bi_buf;
    // 输出缓冲器。位从底部开始插入（最低有效位）
    // 位）。 bi_buf 的宽度必须至少为 16 位。
    int bi_valid;
    // bi_buf 中的有效位数。  最后有效位以上的所有位
    // 总是为零。
    char *out_buf;
    // 当前输出缓冲区。
    unsigned out_offset;
    // 输出缓冲器中的当前偏移。
    // 在 16 位机器上，缓冲区限制为 64K。
    unsigned out_size;
    // 当前输出缓冲区的大小
    ulg bits_sent; // 压缩数据的位长度仅用于调试？？？
};

class TDeflateState
{
  public:
    TDeflateState()
    {
        window_size = 0;
    }

    uch window[2L * WSIZE];
    // 滑动窗。输入字节被读入窗口的后半部分，
    // 并稍后移至前半部分以保留至少 WSIZE 的字典
    // 字节的字典。采用此布局时，匹配距离最大为
    // WSIZE-MAX_MATCH 字节，但这可以确保 IO 始终
    // 以块大小的长度倍数执行。而且，它限制了
    // 窗口大小为 64K，这在 MSDOS 上非常有用。
    // 要做的：如果 SMALL_MEM，则将窗口大小限制为 WSIZE+CBSZ（代码将
    // 效率较低，因为数据必须复制 WSIZE/CBSZ 次）
    Pos prev[WSIZE];
    // 链接到具有相同哈希索引的旧字符串。限制这个的大小
    // 数组到 64K 时，仅针对最后 32K 字符串维护此链接。
    // 因此，该数组中的索引是窗口索引模 32K。
    Pos head[HASH_SIZE];
    // 哈希链的头或 NIL。如果你的编译器认为
    // HASH_SIZE是一个动态值，用-DDYN_ALLOC重新编译。

    ulg window_size;
    // 窗口大小，2*WSIZE，MMAP 或 BIG_MEM 除外，它是
    // 输入文件长度加上 MIN_LOOKAHEAD。

    long block_start;
    // 当前输出块开头的窗口位置。获取
    // 当窗口向后移动时为负。

    int sliding;
    // 当输入文件已在内存中时设置为 false

    unsigned ins_h; // 要插入的字符串的哈希索引

    unsigned int prev_length;
    // 上一步最佳匹配的长度。匹配不大于此
    // 被丢弃。这用于惰性匹配评估。

    unsigned strstart;    // 要插入的字符串的开头
    unsigned match_start; // 匹配字符串的开头
    int eofile;           // 在输入文件末尾设置标志
    unsigned lookahead;   // 窗口中前面的有效字节数

    unsigned max_chain_length;
    // 为加快 DEFLATE 压缩，搜索哈希链时不超过此长度。
    // 较高的限制可以提高压缩比，但会降低速度。

    unsigned int max_lazy_match;
    // 仅当当前匹配严格时才尝试找到更好的匹配
    // 小于该值。该机制仅用于压缩
    // 级别 >= 4。

    unsigned good_match;
    // 当上一个匹配比这个长时使用更快的搜索

    int nice_match; // 当当前匹配超过此值时停止搜索
};

typedef __int64 lutime_t; // 自己定义它，因为我们不包括 time.h

typedef struct iztimes
{
    lutime_t atime, mtime, ctime;
} iztimes; // 访问、修改、创建时间

typedef struct zlist
{
    ush vem, ver, flg, how; // 请参阅 zipfile.c 中的中央标头了解 vem..off 的含义
    ulg tim, crc, siz, len;
    extent nam, ext, cext, com; // ext 的偏移量必须 >= LOCHEAD
    ush dsk, att, lflg;         // lflg 的偏移量必须 >= LOCHEAD
    ulg atx, off;
    char name[MAX_PATH];   // zip 文件中的文件名
    char *extra;           // 额外字段（仅当 ext != 0 时设置）
    char *cextra;          // 中央目录中的附加字段，仅当 cext != 0 时设置。
    char *comment;         // 注释（仅当 com != 0 时设置）
    char iname[MAX_PATH];  // 清理后的内部文件名
    char zname[MAX_PATH];  // 内部名称的外部版本
    int mark;              // 要操作的文件的标记
    int trash;             // 要删除的文件标记
    int dosflag;           // 设置为强制 MSDOS 文件属性
    struct zlist far *nxt; // 指向列表中下一个标头的指针
} TZipFileInfo;

struct TState;
typedef unsigned (*READFUNC)(TState &state, char *buf, unsigned size);
typedef unsigned (*FLUSHFUNC)(void *param, const char *buf, unsigned *size);
typedef unsigned (*WRITEFUNC)(void *param, const char *buf, unsigned size);
struct TState
{
    void *param;
    int level;
    bool seekable;
    READFUNC readfunc;
    FLUSHFUNC flush_outbuf;
    TTreeState ts;
    TBitState bs;
    TDeflateState ds;
    const char *err;
};

void Assert(TState &state, bool cond, const char *msg)
{
    if (cond)
        return;
    state.err = msg;
}
void __cdecl Trace(const char *x, ...)
{
    va_list paramList;
    va_start(paramList, x);
    paramList;
    va_end(paramList);
}
void __cdecl Tracec(bool, const char *x, ...)
{
    va_list paramList;
    va_start(paramList, x);
    paramList;
    va_end(paramList);
}

// ===========================================================================
// 该文件中的本地（静态）例程。
//

void init_block(TState &);
void pqdownheap(TState &, ct_data *tree, int k);
void gen_bitlen(TState &, tree_desc *desc);
void gen_codes(TState &state, ct_data *tree, int max_code);
void build_tree(TState &, tree_desc *desc);
void scan_tree(TState &, ct_data *tree, int max_code);
void send_tree(TState &state, ct_data *tree, int max_code);
int build_bl_tree(TState &);
void send_all_trees(TState &state, int lcodes, int dcodes, int blcodes);
void compress_block(TState &state, ct_data *ltree, ct_data *dtree);
void set_file_type(TState &);
void send_bits(TState &state, int value, int length);
unsigned bi_reverse(unsigned code, int len);
void bi_windup(TState &state);
void copy_block(TState &state, char *buf, unsigned len, int header);

#define send_code(state, c, tree) send_bits(state, tree[c].fc.code, tree[c].dl.len)
// 输出给定树中符号 c 的编码；参数 c 和 tree 的求值不能有副作用。

// 或者...
//#define send_code(state, c, tree)
//     {
//         if (state.verbose>1)
//             fprintf(stderr,"\ncd %3d ",(c));
//         send_bits(state, tree[c].fc.code, tree[c].dl.len);
//     }

#define d_code(dist) ((dist) < 256 ? state.ts.dist_code[dist] : state.ts.dist_code[256 + ((dist) >> 7)])
// 从距离到距离代码的映射。 dist 是距离 - 1 并且
// 一定不能有副作用。 dist_code[256] 和 dist_code[257] 从未使用过。

#define Max(a, b) (a >= b ? a : b)
/* 参数不能有副作用 */

/* 分配匹配缓冲区并初始化各表，记录文件属性（ASCII/二进制）及压缩方式（DEFLATE/STORE）。
 *
 *
 *
 */
void ct_init(TState &state, ush *attr)
{
    int n;      /* 迭代树元素 */
    int bits;   /* 位计数器 */
    int length; /* 长度值。 */
    int code;   /* 编码值。 */
    int dist;   /* 距离索引。 */

    state.ts.file_type = attr;
    // state.ts.file_method = method; // 原先保存压缩方式的语句，继续保留为注释。
    state.ts.cmpr_bytelen = state.ts.cmpr_len_bits = 0L;
    state.ts.input_len = 0L;

    if (state.ts.static_dtree[0].dl.len != 0)
        return; /* ct_init 已调用 */

    /* 初始化映射长度(0..255) -> 长度代码(0..28) */
    length = 0;
    for (code = 0; code < LENGTH_CODES - 1; code++)
    {
        state.ts.base_length[code] = length;
        for (n = 0; n < (1 << extra_lbits[code]); n++)
        {
            state.ts.length_code[length++] = (uch)code;
        }
    }
    Assert(state, length == 256, "ct_init: length != 256");
    /* 长度值 255（实际匹配长度 258）可编码为 284 加 5 个附加位，或直接编码为 285；覆盖 length_code[255] 以采用更短的表示。
 *
 *
 */
    state.ts.length_code[length - 1] = (uch)code;

    /* 初始化映射 dist (0..32K) -> dist 代码 (0..29) */
    dist = 0;
    for (code = 0; code < 16; code++)
    {
        state.ts.base_dist[code] = dist;
        for (n = 0; n < (1 << extra_dbits[code]); n++)
        {
            state.ts.dist_code[dist++] = (uch)code;
        }
    }
    Assert(state, dist == 256, "ct_init: dist != 256");
    dist >>= 7; /* 从现在开始，所有距离都除以128 */
    for (; code < D_CODES; code++)
    {
        state.ts.base_dist[code] = dist << 7;
        for (n = 0; n < (1 << (extra_dbits[code] - 7)); n++)
        {
            state.ts.dist_code[256 + dist++] = (uch)code;
        }
    }
    Assert(state, dist == 256, "ct_init: 256+dist != 512");

    /* 构造静态字面值树的代码 */
    for (bits = 0; bits <= MAX_BITS; bits++)
        state.ts.bl_count[bits] = 0;
    n = 0;
    while (n <= 143)
        state.ts.static_ltree[n++].dl.len = 8, state.ts.bl_count[8]++;
    while (n <= 255)
        state.ts.static_ltree[n++].dl.len = 9, state.ts.bl_count[9]++;
    while (n <= 279)
        state.ts.static_ltree[n++].dl.len = 7, state.ts.bl_count[7]++;
    while (n <= 287)
        state.ts.static_ltree[n++].dl.len = 8, state.ts.bl_count[8]++;
    /* 符号 286 和 287 不实际使用，但构建静态树时必须包含它们，才能生成最长编码全为 1 的规范哈夫曼树。
 *
 *
 */
    gen_codes(state, (ct_data *)state.ts.static_ltree, L_CODES + 1);

    /* 静态距离树很简单： */
    for (n = 0; n < D_CODES; n++)
    {
        state.ts.static_dtree[n].dl.len = 5;
        state.ts.static_dtree[n].fc.code = (ush)bi_reverse(n, 5);
    }

    /* 初始化第一个文件的第一个块： */
    init_block(state);
}

/* ===============================================================================
 * 初始化一个新块。
 */
void init_block(TState &state)
{
    int n; /* 迭代树元素 */

    /* 初始化树。 */
    for (n = 0; n < L_CODES; n++)
        state.ts.dyn_ltree[n].fc.freq = 0;
    for (n = 0; n < D_CODES; n++)
        state.ts.dyn_dtree[n].fc.freq = 0;
    for (n = 0; n < BL_CODES; n++)
        state.ts.bl_tree[n].fc.freq = 0;

    state.ts.dyn_ltree[END_BLOCK].fc.freq = 1;
    state.ts.opt_len = state.ts.static_len = 0L;
    state.ts.last_lit = state.ts.last_dist = state.ts.last_flags = 0;
    state.ts.flags = 0;
    state.ts.flag_bit = 1;
}

#define SMALLEST 1
/* 哈夫曼树中出现次数最少的节点在堆数组中的位置。 */

/* 删除堆中最小元素，并恢复减少一个元素后的堆结构；同时更新 heap 与 heap_len。
 *
 *
 */
#define pqremove(tree, top)                                           \
    {                                                                 \
        top = state.ts.heap[SMALLEST];                                \
        state.ts.heap[SMALLEST] = state.ts.heap[state.ts.heap_len--]; \
        pqdownheap(state, tree, SMALLEST);                            \
    }

/* 比较两个子树；出现次数相同时，以树深度决定先后，减小最坏情况下的编码长度。
 *
 *
 */
#define smaller(tree, n, m)               \
    (tree[n].fc.freq < tree[m].fc.freq || \
     (tree[n].fc.freq == tree[m].fc.freq && state.ts.depth[n] <= state.ts.depth[m]))

/* 从节点 k 向下调整堆；必要时与两个子节点中较小者交换，直到所有父节点都小于其子节点。
 *
 *
 *
 *
 */
void pqdownheap(TState &state, ct_data *tree, int k)
{
    int v = state.ts.heap[k];
    int j = k << 1; /* k 的左子节点。 */
    int htemp;      /* 由于 SASC 编译器中的错误而需要 */

    while (j <= state.ts.heap_len)
    {
        /* 令 j 指向两个子节点中较小的一个。 */
        if (j < state.ts.heap_len && smaller(tree, state.ts.heap[j + 1], state.ts.heap[j]))
            j++;

        /* 若 v 小于两个子节点，则结束调整。 */
        htemp = state.ts.heap[j];
        if (smaller(tree, v, htemp))
            break;

        /* 交换 v 与较小的子节点。 */
        state.ts.heap[k] = htemp;
        k = j;

        /* 继续沿着树向下，将 j 设置为 k 的左子 */
        j <<= 1;
    }
    state.ts.heap[k] = v;
}

/* 计算树的最优码长并更新当前块总位数。输入：freq、dad 已设置，heap[heap_max] 及其后节点按出现次数递增排列。输出：len 保存最优码长，bl_count 统计各码长数量，opt_len 更新；stree 非空时也更新 static_len。
 *
 *
 *
 *
 *
 *
 *
 *
 */
void gen_bitlen(TState &state, tree_desc *desc)
{
    ct_data *tree = desc->dyn_tree;
    const int *extra = desc->extra_bits;
    int base = desc->extra_base;
    int max_code = desc->max_code;
    int max_length = desc->max_length;
    ct_data *stree = desc->static_tree;
    int h;            /* 堆索引 */
    int n, m;         /* 迭代树元素 */
    int bits;         /* 码长。 */
    int xbits;        /* 附加位数。 */
    ush f;            /* 出现次数。 */
    int overflow = 0; /* 位长太大的元素数量 */

    for (bits = 0; bits <= MAX_BITS; bits++)
        state.ts.bl_count[bits] = 0;

    /* 第一遍计算最优码长；对码长树而言，结果可能超过允许的最大码长。
 *
 */
    tree[state.ts.heap[state.ts.heap_max]].dl.len = 0; /* 堆的根 */

    for (h = state.ts.heap_max + 1; h < HEAP_SIZE; h++)
    {
        n = state.ts.heap[h];
        bits = tree[tree[n].dl.dad].dl.len + 1;
        if (bits > max_length)
            bits = max_length, overflow++;
        tree[n].dl.len = (ush)bits;
        /* 我们覆盖不再需要的tree[n].dl.dad */

        if (n > max_code)
            continue; /* 不是叶节点 */

        state.ts.bl_count[bits]++;
        xbits = 0;
        if (n >= base)
            xbits = extra[n - base];
        f = tree[n].fc.freq;
        state.ts.opt_len += (ulg)f * (bits + xbits);
        if (stree)
            state.ts.static_len += (ulg)f * (stree[n].dl.len + xbits);
    }
    if (overflow == 0)
        return;

    Trace("\nbit length overflow\n");
    /* 例如，这种情况发生在卡尔加里语料库的 obj2 和 pic 上 */

    /* 求第一个可能增加的位长度： */
    do
    {
        bits = max_length - 1;
        while (state.ts.bl_count[bits] == 0)
            bits--;
        state.ts.bl_count[bits]--;             /* 将一个叶节点向下一层移动。 */
        state.ts.bl_count[bits + 1] += (ush)2; /* 将一个溢出项移作它的兄弟节点。 */
        state.ts.bl_count[max_length]--;
        /* 溢出节点的兄弟节点也上移一层，但不会改变 bl_count[max_length]。
 *
 */
        overflow -= 2;
    }
    while (overflow > 0);

    /* 按出现次数递增顺序重新计算所有码长，此时 h 仍为 HEAP_SIZE。整体重建比只修正错误码长更简单；此方法来自 Haruhiko Okumura 编写的 ar。
 *
 *
 *
 */
    for (bits = max_length; bits != 0; bits--)
    {
        n = state.ts.bl_count[bits];
        while (n != 0)
        {
            m = state.ts.heap[--h];
            if (m > max_code)
                continue;
            if (tree[m].dl.len != (ush)bits)
            {
                Trace("code %d bits %d->%d\n", m, tree[m].dl.len, bits);
                state.ts.opt_len += ((long)bits - (long)tree[m].dl.len) * (long)tree[m].fc.freq;
                tree[m].dl.len = (ush)bits;
            }
            n--;
        }
    }
}

/* 根据给定树和码长统计生成编码，不要求树本身最优。输入：bl_count 已统计各码长数量，所有节点 len 已设置。输出：为码长非零的节点设置 code。
 *
 *
 *
 *
 *
 *
 */
void gen_codes(TState &state, ct_data *tree, int max_code)
{
    ush next_code[MAX_BITS + 1]; /* 每个位长度的下一个代码值 */
    ush code = 0;                /* 当前递推的编码值。 */
    int bits;                    /* 位索引 */
    int n;                       /* 编码索引。 */

    /* 先根据各码长数量生成尚未进行位序反转的编码值。
 *
 */
    for (bits = 1; bits <= MAX_BITS; bits++)
    {
        next_code[bits] = code = (ush)((code + state.ts.bl_count[bits - 1]) << 1);
    }
    /* 检查 bl_count 的码长统计是否一致；最后一个编码的所有位必须为 1。
 *
 */
    Assert(state, code + state.ts.bl_count[MAX_BITS] - 1 == (1 << ((ush)MAX_BITS)) - 1,
           "inconsistent bit counts");
    Trace("\ngen_codes: max_code %d ", max_code);

    for (n = 0; n <= max_code; n++)
    {
        int len = tree[n].dl.len;
        if (len == 0)
            continue;
        /* 现在反转位 */
        tree[n].fc.code = (ush)bi_reverse(next_code[len]++, len);

        //Tracec(tree != state.ts.static_ltree, "\nn %3d %c l %2d c %4x (%x) ", n, (isgraph(n) ? n : ' '), len, tree[n].fc.code, next_code[len]-1);
    }
}

/* 构建哈夫曼树并分配编码与码长，更新当前块总位数。输入：所有节点的 freq 已设置。输出：len、code 为最优码长与对应编码，更新 opt_len、max_code；stree 非空时也更新 static_len。
 *
 *
 *
 *
 *
 *
 */
void build_tree(TState &state, tree_desc *desc)
{
    ct_data *tree = desc->dyn_tree;
    ct_data *stree = desc->static_tree;
    int elems = desc->elems;
    int n, m;          /* 迭代堆元素 */
    int max_code = -1; /* 出现次数非零的最大编码编号。 */
    int node = elems;  /* 树的下一个内部节点 */

    /* 构建初始堆，出现次数最少的元素位于 heap[SMALLEST]。heap[n] 的子节点为 heap[2*n] 和 heap[2*n+1]，heap[0] 不使用。
 *
 *
 */
    state.ts.heap_len = 0, state.ts.heap_max = HEAP_SIZE;

    for (n = 0; n < elems; n++)
    {
        if (tree[n].fc.freq != 0)
        {
            state.ts.heap[++state.ts.heap_len] = max_code = n;
            state.ts.depth[n] = 0;
        }
        else
        {
            tree[n].dl.len = 0;
        }
    }

    /* PKZIP 要求至少存在一个距离编码，即使只有一种可能编码也必须输出至少 1 位。为避免后续特殊分支，强制保留至少两个出现次数非零的编码。
 *
 *
 *
 */
    while (state.ts.heap_len < 2)
    {
        int newcp = state.ts.heap[++state.ts.heap_len] = (max_code < 2 ? ++max_code : 0);
        tree[newcp].fc.freq = 1;
        state.ts.depth[newcp] = 0;
        state.ts.opt_len--;
        if (stree)
            state.ts.static_len -= stree[newcp].dl.len;
        /* new 是 0 或 1，因此它没有额外的位 */
    }
    desc->max_code = max_code;

    /* 元素 heap[heap_len/2+1 .. heap_len] 是树的叶子，
     * 建立长度递增的子堆：
 */
    for (n = state.ts.heap_len / 2; n >= 1; n--)
        pqdownheap(state, tree, n);

    /* 不断合并出现次数最少的两个节点，构建哈夫曼树。
 *
 */
    do
    {
        pqremove(tree, n);           /* n 为出现次数最少的节点。 */
        m = state.ts.heap[SMALLEST]; /* m 为出现次数次少的节点。 */

        state.ts.heap[--state.ts.heap_max] = n; /* 保持节点按出现次数排序 */
        state.ts.heap[--state.ts.heap_max] = m;

        /* 创建 n 和 m 的新节点父节点 */
        tree[node].fc.freq = (ush)(tree[n].fc.freq + tree[m].fc.freq);
        state.ts.depth[node] = (uch)(Max(state.ts.depth[n], state.ts.depth[m]) + 1);
        tree[n].dl.dad = tree[m].dl.dad = (ush)node;
        /* 并将新节点插入堆中 */
        state.ts.heap[SMALLEST] = node++;
        pqdownheap(state, tree, SMALLEST);
    }
    while (state.ts.heap_len >= 2);

    state.ts.heap[--state.ts.heap_max] = state.ts.heap[SMALLEST];

    /* 此时 freq 和 dad 已设置，可以开始生成码长。
 *
 */
    gen_bitlen(state, (tree_desc *)desc);

    /* 现在字段 len 已设置，我们可以生成位代码 */
    gen_codes(state, (ct_data *)tree, max_code);
}

/* 扫描字面值树或距离树，统计码长树中各编码的出现次数，并将重复次数的开销计入 opt_len；码长编码自身的开销在构建 bl_tree 时计入。
 *
 *
 *
 *
 */
void scan_tree(TState &state, ct_data *tree, int max_code)
{
    int n;                        /* 迭代所有树元素 */
    int prevlen = -1;             /* 上次输出的码长。 */
    int curlen;                   /* 当前编码的码长。 */
    int nextlen = tree[0].dl.len; /* 下一个编码的码长。 */
    int count = 0;                /* 当前代码的重复计数 */
    int max_count = 7;            /* 最大重复次数。 */
    int min_count = 4;            /* 最小重复次数。 */

    if (nextlen == 0)
        max_count = 138, min_count = 3;
    tree[max_code + 1].dl.len = (ush)-1; /* 守卫 */

    for (n = 0; n <= max_code; n++)
    {
        curlen = nextlen;
        nextlen = tree[n + 1].dl.len;
        if (++count < max_count && curlen == nextlen)
        {
            continue;
        }
        else if (count < min_count)
        {
            state.ts.bl_tree[curlen].fc.freq = (ush)(state.ts.bl_tree[curlen].fc.freq + count);
        }
        else if (curlen != 0)
        {
            if (curlen != prevlen)
                state.ts.bl_tree[curlen].fc.freq++;
            state.ts.bl_tree[REP_3_6].fc.freq++;
        }
        else if (count <= 10)
        {
            state.ts.bl_tree[REPZ_3_10].fc.freq++;
        }
        else
        {
            state.ts.bl_tree[REPZ_11_138].fc.freq++;
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0)
        {
            max_count = 138, min_count = 3;
        }
        else if (curlen == nextlen)
        {
            max_count = 6, min_count = 3;
        }
        else
        {
            max_count = 7, min_count = 4;
        }
    }
}

/* 使用 bl_tree 的编码，以压缩形式输出字面值树或距离树。
 *
 *
 */
void send_tree(TState &state, ct_data *tree, int max_code)
{
    int n;                        /* 迭代所有树元素 */
    int prevlen = -1;             /* 上次输出的码长。 */
    int curlen;                   /* 当前编码的码长。 */
    int nextlen = tree[0].dl.len; /* 下一个编码的码长。 */
    int count = 0;                /* 当前代码的重复计数 */
    int max_count = 7;            /* 最大重复次数。 */
    int min_count = 4;            /* 最小重复次数。 */

    /* tree[max_code+1].dl.len = -1; // 原有哨兵设置语句，保持注释状态。 */ /* 警卫已经设置 */
    if (nextlen == 0)
        max_count = 138, min_count = 3;

    for (n = 0; n <= max_code; n++)
    {
        curlen = nextlen;
        nextlen = tree[n + 1].dl.len;
        if (++count < max_count && curlen == nextlen)
        {
            continue;
        }
        else if (count < min_count)
        {
            do
            {
                send_code(state, curlen, state.ts.bl_tree);
            }
            while (--count != 0);
        }
        else if (curlen != 0)
        {
            if (curlen != prevlen)
            {
                send_code(state, curlen, state.ts.bl_tree);
                count--;
            }
            Assert(state, count >= 3 && count <= 6, " 3_6?");
            send_code(state, REP_3_6, state.ts.bl_tree);
            send_bits(state, count - 3, 2);
        }
        else if (count <= 10)
        {
            send_code(state, REPZ_3_10, state.ts.bl_tree);
            send_bits(state, count - 3, 3);
        }
        else
        {
            send_code(state, REPZ_11_138, state.ts.bl_tree);
            send_bits(state, count - 11, 7);
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0)
        {
            max_count = 138, min_count = 3;
        }
        else if (curlen == nextlen)
        {
            max_count = 6, min_count = 3;
        }
        else
        {
            max_count = 7, min_count = 4;
        }
    }
}

/* 构建码长的哈夫曼树，返回最后一个待发送码长编码在 bl_order 中的索引。
 *
 *
 */
int build_bl_tree(TState &state)
{
    int max_blindex; /* 出现次数非零的最后一个码长编码的索引。 */

    /* 确定字面值树和距离树的位长度出现次数 */
    scan_tree(state, (ct_data *)state.ts.dyn_ltree, state.ts.l_desc.max_code);
    scan_tree(state, (ct_data *)state.ts.dyn_dtree, state.ts.d_desc.max_code);

    /* 构建位长树： */
    build_tree(state, (tree_desc *)(&state.ts.bl_desc));
    /* opt_len 此时已包含树结构的表示开销，但不包含码长编码自身以及计数字段的 5+5+4 位。
 *
 */

    /* 确定需要发送的码长编码数量。PKZIP 实际要求至少发送 4 个，虽然 appnote.txt 文档写的是 3 个。
 *
 *
 */
    for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--)
    {
        if (state.ts.bl_tree[bl_order[max_blindex]].dl.len != 0)
            break;
    }
    /* 更新 opt_len 以包含位长度树和计数 */
    state.ts.opt_len += 3 * (max_blindex + 1) + 5 + 5 + 4;
    Trace("\ndyn trees: dyn %ld, stat %ld", state.ts.opt_len, state.ts.static_len);

    return max_blindex;
}

/* 输出动态哈夫曼块头：编码数量、码长编码的长度、字面值树和距离树。前提：lcodes >= 257、dcodes >= 1、blcodes >= 4。
 *
 *
 *
 */
void send_all_trees(TState &state, int lcodes, int dcodes, int blcodes)
{
    int rank; /* bl_order 中的索引 */

    Assert(state, lcodes >= 257 && dcodes >= 1 && blcodes >= 4, "not enough codes");
    Assert(state, lcodes <= L_CODES && dcodes <= D_CODES && blcodes <= BL_CODES,
           "too many codes");
    Trace("\nbl counts: ");
    send_bits(state, lcodes - 257, 5);
    /* 不是 appnote.txt 1.93a 中所述的 +255 或 2.04c 中的 -256 */
    send_bits(state, dcodes - 1, 5);
    send_bits(state, blcodes - 4, 4); /* 不是 appnote.txt 中所述的 -3 */
    for (rank = 0; rank < blcodes; rank++)
    {
        Trace("\nbl code %2d ", bl_order[rank]);
        send_bits(state, state.ts.bl_tree[bl_order[rank]].dl.len, 3);
    }
    Trace("\nbl tree: sent %ld", state.bs.bits_sent);

    send_tree(state, (ct_data *)state.ts.dyn_ltree, lcodes - 1); /* 发送字面值树 */
    Trace("\nlit tree: sent %ld", state.bs.bits_sent);

    send_tree(state, (ct_data *)state.ts.dyn_dtree, dcodes - 1); /* 发送距离树 */
    Trace("\ndist tree: sent %ld", state.bs.bits_sent);
}

/* 从动态树、静态树和直接存储中选择当前块的最佳方式，并将编码块写入 ZIP 文件；返回该文件截至当前累计的压缩字节数。
 *
 *
 *
 */
ulg flush_block(TState &state, char *buf, ulg stored_len, int eof)
{
    ulg opt_lenb, static_lenb; /* opt_len 和 static_len 以字节为单位 */
    int max_blindex;           /* 出现次数非零的最后一个码长编码的索引。 */

    state.ts.flag_buf[state.ts.last_flags] = state.ts.flags; /* 保存最后 8 项的标志 */

    /* 检查文件是ascii还是二进制 */
    if (*state.ts.file_type == (ush)UNKNOWN)
        set_file_type(state);

    /* 构建字面值树和距离树 */
    build_tree(state, (tree_desc *)(&state.ts.l_desc));
    Trace("\nlit data: dyn %ld, stat %ld", state.ts.opt_len, state.ts.static_len);

    build_tree(state, (tree_desc *)(&state.ts.d_desc));
    Trace("\ndist data: dyn %ld, stat %ld", state.ts.opt_len, state.ts.static_len);
    /* 此时 opt_len 和 static_len 表示压缩块数据的总位数，尚不包括树结构的表示开销。
 *
 */

    /* 为字面值树和距离树构建码长树，取得最后一个待发送码长编码在 bl_order 中的索引。
 *
 */
    max_blindex = build_bl_tree(state);

    /* 确定最佳编码。首先计算块长度（以字节为单位） */
    opt_lenb = (state.ts.opt_len + 3 + 7) >> 3;
    static_lenb = (state.ts.static_len + 3 + 7) >> 3;
    state.ts.input_len += stored_len; /* 仅用于调试 */

    Trace("\nopt %lu(%lu) stat %lu(%lu) stored %lu lit %u dist %u ",
          opt_lenb, state.ts.opt_len, static_lenb, state.ts.static_len, stored_len,
          state.ts.last_lit, state.ts.last_dist);

    if (static_lenb <= opt_lenb)
        opt_lenb = static_lenb;

    // 最初，zip 允许将文件从压缩文件转换为
    // 在压缩失败的情况下进入存储的文件，有
    // 只有一个区块，并且允许改变。我已经删除了这个
    // 可能性，因为如果不允许更改，代码会更干净。
    //if (stored_len <= opt_lenb && eof && state.ts.cmpr_bytelen == 0L
    //   && state.ts.cmpr_len_bits == 0L && state.seekable)
    //{   // && state.ts.file_method != NULL
    //     // LIT_BUFSIZE <= 2*WSIZE，因此输入数据仍应保留在窗口中。
    //    Assert(state,buf!=NULL,"block vanished");
    //     copy_block(state,buf, (unsigned)stored_len, 0); // 不写块头。
    //    state.ts.cmpr_bytelen = stored_len;
    //    Assert(state,false,"unimplemented *state.ts.file_method = STORE;");
    //    //*state.ts.file_method = STORE;
    //}
    // 否则
    if (stored_len + 4 <= opt_lenb && buf != (char *)NULL)
    {
        /* 长度与其反码各占一个 16 位字，共 4 字节。 */
        /* 只有 LIT_BUFSIZE > WSIZE 时才必须检查 buf != NULL；否则上次输出后处理的输入不会超过 WSIZE，仍可将当前块改为直接存储块。
 *
 *
 *
 *
 */
        send_bits(state, (STORED_BLOCK << 1) + eof, 3); /* 发送块类型 */
        state.ts.cmpr_bytelen += ((state.ts.cmpr_len_bits + 3 + 7) >> 3) + stored_len + 4;
        state.ts.cmpr_len_bits = 0L;

        copy_block(state, buf, (unsigned)stored_len, 1); /* 带标题 */
    }
    else if (static_lenb == opt_lenb)
    {
        send_bits(state, (STATIC_TREES << 1) + eof, 3);
        compress_block(state, (ct_data *)state.ts.static_ltree, (ct_data *)state.ts.static_dtree);
        state.ts.cmpr_len_bits += 3 + state.ts.static_len;
        state.ts.cmpr_bytelen += state.ts.cmpr_len_bits >> 3;
        state.ts.cmpr_len_bits &= 7L;
    }
    else
    {
        send_bits(state, (DYN_TREES << 1) + eof, 3);
        send_all_trees(state, state.ts.l_desc.max_code + 1, state.ts.d_desc.max_code + 1, max_blindex + 1);
        compress_block(state, (ct_data *)state.ts.dyn_ltree, (ct_data *)state.ts.dyn_dtree);
        state.ts.cmpr_len_bits += 3 + state.ts.opt_len;
        state.ts.cmpr_bytelen += state.ts.cmpr_len_bits >> 3;
        state.ts.cmpr_len_bits &= 7L;
    }
    Assert(state, ((state.ts.cmpr_bytelen << 3) + state.ts.cmpr_len_bits) == state.bs.bits_sent, "bad compressed size");
    init_block(state);

    if (eof)
    {
        // Assert(state,input_len == isize, "bad input size");
        bi_windup(state);
        state.ts.cmpr_len_bits += 7; /* 在字节边界上对齐 */
    }
    Trace("\n");

    return state.ts.cmpr_bytelen + (state.ts.cmpr_len_bits >> 3);
}

/* 保存匹配信息并累计符号出现次数；当前块需要输出时返回 true。
 *
 *
 */
int ct_tally(TState &state, int dist, int lc)
{
    state.ts.l_buf[state.ts.last_lit++] = (uch)lc;
    if (dist == 0)
    {
        /* 此时 lc 为未匹配的字节。 */
        state.ts.dyn_ltree[lc].fc.freq++;
    }
    else
    {
        /* 此时 lc 为匹配长度减去 MIN_MATCH。 */
        dist--; /* dist = 匹配距离 - 1 */
        Assert(state, (ush)dist < (ush)MAX_DIST && (ush)lc <= (ush)(MAX_MATCH - MIN_MATCH) && (ush)d_code(dist) < (ush)D_CODES, "ct_tally: bad match");

        state.ts.dyn_ltree[state.ts.length_code[lc] + LITERALS + 1].fc.freq++;
        state.ts.dyn_dtree[d_code(dist)].fc.freq++;

        state.ts.d_buf[state.ts.last_dist++] = (ush)dist;
        state.ts.flags |= state.ts.flag_bit;
    }
    state.ts.flag_bit <<= 1;

    /* 如果标志填满一个字节，则输出标志： */
    if ((state.ts.last_lit & 7) == 0)
    {
        state.ts.flag_buf[state.ts.last_flags++] = state.ts.flags;
        state.ts.flags = 0, state.ts.flag_bit = 1;
    }
    /* 尝试猜测在这里停止当前区块是否有利可图 */
    if (state.level > 2 && (state.ts.last_lit & 0xfff) == 0)
    {
        /* 计算压缩长度的上限 */
        ulg out_length = (ulg)state.ts.last_lit * 8L;
        ulg in_length = (ulg)state.ds.strstart - state.ds.block_start;
        int dcode;
        for (dcode = 0; dcode < D_CODES; dcode++)
        {
            out_length += (ulg)state.ts.dyn_dtree[dcode].fc.freq * (5L + extra_dbits[dcode]);
        }
        out_length >>= 3;
        Trace("\nlast_lit %u, last_dist %u, in %ld, out ~%ld(%ld%%) ",
              state.ts.last_lit, state.ts.last_dist, in_length, out_length,
              100L - out_length * 100L / in_length);
        if (state.ts.last_dist < state.ts.last_lit / 2 && out_length < in_length / 2)
            return 1;
    }
    return (state.ts.last_lit == LIT_BUFSIZE - 1 || state.ts.last_dist == DIST_BUFSIZE);
    /* 避免计数恰好达到 LIT_BUFSIZE，以防 16 位系统在 64K 处回绕；直接存储块也限制为最多 64K-1 字节。
 *
 *
 */
}

/* ===============================================================================
 * 发送使用给定的哈夫曼树压缩的块数据
 */
void compress_block(TState &state, ct_data *ltree, ct_data *dtree)
{
    unsigned dist;   /* 匹配字符串的距离 */
    int lc;          /* 匹配长度或不匹配的字符（如果 dist == 0） */
    unsigned lx = 0; /* l_buf 中的运行索引 */
    unsigned dx = 0; /* d_buf 中的运行索引 */
    unsigned fx = 0; /* flag_buf 中的运行索引 */
    uch flag = 0;    /* 当前标志 */
    unsigned code;   /* 要发送的代码 */
    int extra;       /* 要发送的额外位数 */

    if (state.ts.last_lit != 0)
        do
        {
            if ((lx & 7) == 0)
                flag = state.ts.flag_buf[fx++];
            lc = state.ts.l_buf[lx++];
            if ((flag & 1) == 0)
            {
                send_code(state, lc, ltree); /* 发送一个字面值字节 */
            }
            else
            {
                /* 这里，lc 是匹配长度 - MIN_MATCH */
                code = state.ts.length_code[lc];
                send_code(state, code + LITERALS + 1, ltree); /* 发送长度代码 */
                extra = extra_lbits[code];
                if (extra != 0)
                {
                    lc -= state.ts.base_length[code];
                    send_bits(state, lc, extra); /* 发送额外长度位 */
                }
                dist = state.ts.d_buf[dx++];
                /* 这里，dist 是匹配距离 - 1 */
                code = d_code(dist);
                Assert(state, code < D_CODES, "bad d_code");

                send_code(state, code, dtree); /* 发送距离代码 */
                extra = extra_dbits[code];
                if (extra != 0)
                {
                    dist -= state.ts.base_dist[code];
                    send_bits(state, dist, extra); /* 发送额外距离位 */
                }
            } /* 字面值或匹配对？ */
            flag >>= 1;
        }
        while (lx < state.ts.last_lit);

    send_code(state, END_BLOCK, ltree);
}

/* 粗略判断文件为 ASCII 或二进制：若超过 20% 的字节 <= 6 或 >= 128，则视为二进制。前提：dyn_ltree 的 freq 已设置，次数总和不超过 64K，以适应 16 位系统的 int。
 *
 *
 *
 *
 */
void set_file_type(TState &state)
{
    int n = 0;
    unsigned ascii_freq = 0;
    unsigned bin_freq = 0;
    while (n < 7)
        bin_freq += state.ts.dyn_ltree[n++].fc.freq;
    while (n < 128)
        ascii_freq += state.ts.dyn_ltree[n++].fc.freq;
    while (n < LITERALS)
        bin_freq += state.ts.dyn_ltree[n++].fc.freq;
    *state.ts.file_type = (ush)(bin_freq > (ascii_freq >> 2) ? BINARY : ASCII);
}

/* ===============================================================================
 * 初始化位串例程。
 */
void bi_init(TState &state, char *tgt_buf, unsigned tgt_size, int flsh_allowed)
{
    state.bs.out_buf = tgt_buf;
    state.bs.out_size = tgt_size;
    state.bs.out_offset = 0;
    state.bs.flush_flg = flsh_allowed;

    state.bs.bi_buf = 0;
    state.bs.bi_valid = 0;
    state.bs.bits_sent = 0L;
}

/* 按指定的位数输出数值。前提：length <= 16，且 value 能由 length 位表示。
 *
 *
 */
void send_bits(TState &state, int value, int length)
{
    Assert(state, length > 0 && length <= 15, "invalid length");
    state.bs.bits_sent += (ulg)length;
    /* 若 bi_buf 空间不足，则合并已有的 bi_valid 位与 value 的部分低位，输出填满的缓冲区；随后保存 value 的剩余位。
 *
 *
 *
 */
    state.bs.bi_buf |= (value << state.bs.bi_valid);
    state.bs.bi_valid += length;
    if (state.bs.bi_valid > (int)Buf_size)
    {
        PUTSHORT(state, state.bs.bi_buf);
        state.bs.bi_valid -= Buf_size;
        state.bs.bi_buf = (unsigned)value >> (length - state.bs.bi_valid);
    }
}

/* 直接反转编码最低的 len 位；使用查表可进一步加速。前提：1 <= len <= 15。
 *
 *
 *
 */
unsigned bi_reverse(unsigned code, int len)
{
    register unsigned res = 0;
    do
    {
        res |= code & 1;
        code >>= 1, res <<= 1;
    }
    while (--len > 0);
    return res >> 1;
}

/* 输出尚未形成完整字节的剩余位。
 *
 */
void bi_windup(TState &state)
{
    if (state.bs.bi_valid > 8)
    {
        PUTSHORT(state, state.bs.bi_buf);
    }
    else if (state.bs.bi_valid > 0)
    {
        PUTBYTE(state, state.bs.bi_buf);
    }
    if (state.bs.flush_flg)
    {
        state.flush_outbuf(state.param, state.bs.out_buf, &state.bs.out_offset);
    }
    state.bs.bi_buf = 0;
    state.bs.bi_valid = 0;
    state.bs.bits_sent = (state.bs.bits_sent + 7) & ~7;
}

/* 将直接存储块复制到 ZIP 文件；需要块头时，先写长度和长度的反码。
 *
 *
 */
void copy_block(TState &state, char *block, unsigned len, int header)
{
    bi_windup(state); /* 按字节边界对齐。 */

    if (header)
    {
        PUTSHORT(state, (ush)len);
        PUTSHORT(state, (ush)~len);
        state.bs.bits_sent += 2 * 16;
    }
    if (state.bs.flush_flg)
    {
        state.flush_outbuf(state.param, state.bs.out_buf, &state.bs.out_offset);
        state.bs.out_offset = len;
        state.flush_outbuf(state.param, block, &state.bs.out_offset);
    }
    else if (state.bs.out_offset + len > state.bs.out_size)
    {
        Assert(state, false, "output buffer too small for in-memory compression");
    }
    else
    {
        memcpy(state.bs.out_buf + state.bs.out_offset, block, len);
        state.bs.out_offset += len;
    }
    state.bs.bits_sent += (ulg)len << 3;
}

/* ===============================================================================
 * 函数原型。
 */

void fill_window(TState &state);
ulg deflate_fast(TState &state);

int longest_match(TState &state, IPos cur_match);

/* 用当前输入字节更新哈希值。前提：UPDATE_HASH 按连续输入字节调用，因此可从前一哈希值递推，无需完整重算。
 *
 *
 *
 *
 */
#define UPDATE_HASH(h, c) (h = (((h) << H_SHIFT) ^ (c)) & HASH_MASK)

/* 将字符串 s 插入字典，并把 match_head 设为原哈希链头，即同哈希值的最近字符串；返回原链长。前提：按连续输入字节调用，除文件最后 MIN_MATCH-1 字节外，s 的前 MIN_MATCH 字节均有效。
 *
 *
 *
 *
 *
 *
 */
#define INSERT_STRING(s, match_head)                                          \
    (UPDATE_HASH(state.ds.ins_h, state.ds.window[(s) + (MIN_MATCH - 1)]),     \
     state.ds.prev[(s) & WMASK] = match_head = state.ds.head[state.ds.ins_h], \
     state.ds.head[state.ds.ins_h] = (s))

/* 为新文件初始化最长匹配状态。若输入已读入或映射到 window[]，window_size > 0，且须容纳整个输入及额外 MIN_LOOKAHEAD 字节；否则 window_size 为 0。额外空间用于避免文件尾部匹配时越界读取。
 *
 *
 *
 *
 *
 *
 *
 */
void lm_init(TState &state, int pack_level, ush *flags)
{
    register unsigned j;

    Assert(state, pack_level >= 1 && pack_level <= 8, "bad pack level");

    /* 如果全部输入已在内存中（window_size > 0），则不滑动窗口。
 *
 */
    state.ds.sliding = 0;
    if (state.ds.window_size == 0L)
    {
        state.ds.sliding = 1;
        state.ds.window_size = (ulg)2L * WSIZE;
    }

    /* 初始化哈希表，避免 16 位系统的 64K 溢出；prev[] 在后续处理中逐步初始化。
 *
 */
    state.ds.head[HASH_SIZE - 1] = NIL;
    memset((char *)state.ds.head, NIL, (unsigned)(HASH_SIZE - 1) * sizeof(*state.ds.head));

    /* 设置默认配置参数：
 */
    state.ds.max_lazy_match = configuration_table[pack_level].max_lazy;
    state.ds.good_match = configuration_table[pack_level].good_length;
    state.ds.nice_match = configuration_table[pack_level].nice_length;
    state.ds.max_chain_length = configuration_table[pack_level].max_chain;
    if (pack_level <= 2)
    {
        *flags |= FAST;
    }
    else if (pack_level >= 8)
    {
        *flags |= SLOW;
    }
    /* 原作者待优化项：对二进制文件减小 max_chain_length。 */

    state.ds.strstart = 0;
    state.ds.block_start = 0L;

    j = WSIZE;
    j <<= 1; // 一次可以读取 64K 数据。
    state.ds.lookahead = state.readfunc(state, (char *)state.ds.window, j);

    if (state.ds.lookahead == 0 || state.ds.lookahead == (unsigned)EOF)
    {
        state.ds.eofile = 1, state.ds.lookahead = 0;
        return;
    }
    state.ds.eofile = 0;
    /* 确保有足够的前瞻数据，输入来自终端等设备时尤其需要如此。
 *
 */
    if (state.ds.lookahead < MIN_LOOKAHEAD)
        fill_window(state);

    state.ds.ins_h = 0;
    for (j = 0; j < MIN_MATCH - 1; j++)
        UPDATE_HASH(state.ds.ins_h, state.ds.window[j]);
    /* lookahead < MIN_MATCH 时 ins_h 无有效含义；此时只输出字面值字节，因此不会使用它。
 *
 */
}

/* 查找给定字符串的最长匹配，将起点存入 match_start 并返回长度。长度不超过 prev_length 的匹配被舍弃，此时返回 prev_length，match_start 无效。前提：cur_match 为当前 strstart 的哈希链头，距离 <= MAX_DIST，prev_length >= 1。
 *
 *
 *
 *
 *
 *
 */
// 对于 80x86 和 680x0 以及 ARM，优化版本位于 match.asm 或
// 匹配.S.该代码在功能上是等效的，因此您可以使用 C 版本
// 如果需要的话。这是我如此渴望的！
int longest_match(TState &state, IPos cur_match)
{
    unsigned chain_length = state.ds.max_chain_length;            /* 哈希链的最大搜索长度。 */
    register uch far *scan = state.ds.window + state.ds.strstart; /* 当前待匹配字符串。 */
    register uch far *match;                                      /* 候选匹配字符串。 */
    register int len;                                             /* 当前匹配长度。 */
    int best_len = state.ds.prev_length;                          /* 目前找到的最佳匹配长度。 */
    IPos limit = state.ds.strstart > (IPos)MAX_DIST ? state.ds.strstart - (IPos)MAX_DIST : NIL;
    /* cur_match <= limit 时停止搜索；为简化处理，不匹配窗口索引 0 处的字符串。
 *
 */

    // 该代码针对 HASH_BITS >= 8 和 MAX_MATCH-2 16 的倍数进行了优化。
    // 如有必要，很容易摆脱这种优化。
    Assert(state, HASH_BITS >= 8 && MAX_MATCH == 258, "Code too clever");

    register uch far *strend = state.ds.window + state.ds.strstart + MAX_MATCH;
    register uch scan_end1 = scan[best_len - 1];
    register uch scan_end = scan[best_len];

    /* 如果我们已经有很好的匹配，请不要浪费太多时间： */
    if (state.ds.prev_length >= state.ds.good_match)
    {
        chain_length >>= 2;
    }

    Assert(state, state.ds.strstart <= state.ds.window_size - MIN_LOOKAHEAD, "insufficient lookahead");

    do
    {
        Assert(state, cur_match < state.ds.strstart, "no future");
        match = state.ds.window + cur_match;

        /* 如果候选不能增加最佳匹配长度，或者最初两个字节不匹配，则继续检查链中的下一个候选。
 *
 */
        if (match[best_len] != scan_end ||
            match[best_len - 1] != scan_end1 ||
            *match != *scan ||
            *++match != scan[1])
            continue;

        /* best_len-1 处的比较后面会再次执行，理论上可省略，但此启发式检查并非总能加速。由于哈希值相同且 HASH_BITS >= 8，在其他字节匹配时 scan[2] 与 match[2] 也必然相同，无需额外比较。
 *
 *
 *
 *
 */
        scan += 2, match++;

        /* 每比较 8 个字节才检查一次前瞻数据边界；第 256 次比较的边界检查位于 strstart+258。
 *
 */
        do
        {
        }
        while (*++scan == *++match && *++scan == *++match &&
               *++scan == *++match && *++scan == *++match &&
               *++scan == *++match && *++scan == *++match &&
               *++scan == *++match && *++scan == *++match &&
               scan < strend);

        Assert(state, scan <= state.ds.window + (unsigned)(state.ds.window_size - 1), "wild scan");

        len = MAX_MATCH - (int)(strend - scan);
        scan = strend - MAX_MATCH;

        if (len > best_len)
        {
            state.ds.match_start = cur_match;
            best_len = len;
            if (len >= state.ds.nice_match)
                break;
            scan_end1 = scan[best_len - 1];
            scan_end = scan[best_len];
        }
    }
    while ((cur_match = state.ds.prev[cur_match & WMASK]) > limit && --chain_length != 0);

    return best_len;
}

#define check_match(state, start, match, length)
// 另一种匹配检查实现，继续保留为注释。
//void check_match(TState &state,IPos start, IPos match, int length)
// { // 检查候选内容是否确实匹配。
//    if (memcmp((char*)state.ds.window + match,
// (char*)state.ds.window + 开始, 长度) != EQUAL) {
// fprintf（标准错误，
// " 起始 %d，匹配 %d，长度 %d\n",
// 开始、匹配、长度）；
//        error("invalid match");
//    }
//    if (state.verbose > 1)
//    {
//        fprintf(stderr,"\\[%d,%d]", start-match, length);
// do
// {
//     fprintf(stdout,"%c",state.ds.window[start++]);
//     while (--长度！= 0);
//    }
//}

/* 前瞻数据不足时填充窗口，更新 strstart 和 lookahead，读到末尾时设置 eofile。输入前提：lookahead < MIN_LOOKAHEAD 且 strstart + lookahead > 0。完成后 strstart <= window_size-MIN_LOOKAHEAD；至少读入一个字节或设置 eofile。读取请求至少为两字节，以满足 translate_eol 选项。
 *
 *
 *
 *
 *
 *
 *
 */
void fill_window(TState &state)
{
    register unsigned n, m;
    unsigned more; /* 窗口末尾的剩余空间。 */

    do
    {
        more = (unsigned)(state.ds.window_size - (ulg)state.ds.lookahead - (ulg)state.ds.strstart);

        /* 窗口接近满且前瞻不足时，将后半窗口移到前半，为后续输入腾出空间。
 *
 */
        if (more == (unsigned)EOF)
        {
            /* 16 位系统上，若每次只读一个字节，strstart == 0 且 lookahead == 1 时仍可能出现此情况。
 *
 */
            more--;

            /* MMAP 或 BIG_MEM 模式下全部输入已在内存中，不能滑动窗口；但仍需调用 read_buf 以更新 CRC、lookahead，并在需要时设置 eofile。
 *
 *
 */
        }
        else if (state.ds.strstart >= WSIZE + MAX_DIST && state.ds.sliding)
        {

            /* 根据入口条件窗口非空，因此 16 位系统上不会混淆 more == 0 与 more == 64K。
 *
 */
            memcpy((char *)state.ds.window, (char *)state.ds.window + WSIZE, (unsigned)WSIZE);
            state.ds.match_start -= WSIZE;
            state.ds.strstart -= WSIZE; /* 此时 strstart 已不小于 MAX_DIST。 */

            state.ds.block_start -= (long)WSIZE;

            for (n = 0; n < HASH_SIZE; n++)
            {
                m = state.ds.head[n];
                state.ds.head[n] = (Pos)(m >= WSIZE ? m - WSIZE : NIL);
            }
            for (n = 0; n < WSIZE; n++)
            {
                m = state.ds.prev[n];
                state.ds.prev[n] = (Pos)(m >= WSIZE ? m - WSIZE : NIL);
                /* 如果 n 不属于任何哈希链，prev[n] 可以没有有效值，因为该值不会被使用。
 *
 */
            }
            more += WSIZE;
        }
        if (state.ds.eofile)
            return;

        /* 若未滑动窗口，有 strstart <= WSIZE+MAX_DIST-1、lookahead <= MIN_LOOKAHEAD-1，且 more = window_size-lookahead-strstart，故 more >= window_size-2*WSIZE+2。MMAP/BIG_MEM 时 window_size = input_size+MIN_LOOKAHEAD，可得 more >= MIN_LOOKAHEAD；普通窗口大小为 2*WSIZE，故 more >= 2。若发生滑动，则 more >= WSIZE。因此所有情况下 more >= 2。
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
        Assert(state, more >= 2, "more < 2");

        n = state.readfunc(state, (char *)state.ds.window + state.ds.strstart + state.ds.lookahead, more);

        if (n == 0 || n == (unsigned)EOF)
        {
            state.ds.eofile = 1;
        }
        else
        {
            state.ds.lookahead += n;
        }
    }
    while (state.ds.lookahead < MIN_LOOKAHEAD && !state.ds.eofile);
}

/* 按给定的输入结束标记输出当前块；前提：strstart 已指向当前匹配结束位置。
 *
 *
 */
#define FLUSH_BLOCK(state, eof) \
    flush_block(state, state.ds.block_start >= 0L ? (char *)&state.ds.window[(unsigned)state.ds.block_start] : (char *)NULL, (long)state.ds.strstart - state.ds.block_start, (eof))

/* 处理新输入并返回压缩长度。此快速压缩算法不进行惰性匹配，只对未匹配位置或较短匹配中的字符串插入字典。
 *
 *
 *
 *
 */
ulg deflate_fast(TState &state)
{
    IPos hash_head = NIL;      /* 哈希链的头部 */
    int flush;                 /* 当前数据块需要输出时置位。 */
    unsigned match_length = 0; /* 最佳匹配的长度。 */

    state.ds.prev_length = MIN_MATCH - 1;
    while (state.ds.lookahead != 0)
    {
        /* 将 window[strstart .. strstart+2] 插入字典，并将 hash_head 设为对应哈希链的头节点。
 *
 */
        if (state.ds.lookahead >= MIN_MATCH)
            INSERT_STRING(state.ds.strstart, hash_head);

        /* 搜索最长匹配，舍弃长度 <= prev_length 的候选；此时初始 match_length < MIN_MATCH。
 *
 */
        if (hash_head != NIL && state.ds.strstart - hash_head <= MAX_DIST)
        {
            /* 为简化处理，不与窗口索引 0 处的字符串匹配，尤其避免输入起点处字符串匹配自身。
 *
 *
 */
            /* 不在输入末尾之外搜索匹配，保证 DEFLATE 输出具有确定性。
 *
 */
            if ((unsigned)state.ds.nice_match > state.ds.lookahead)
                state.ds.nice_match = (int)state.ds.lookahead;
            match_length = longest_match(state, hash_head);
            /* longest_match() 设置 match_start */
            if (match_length > state.ds.lookahead)
                match_length = state.ds.lookahead;
        }
        if (match_length >= MIN_MATCH)
        {
            check_match(state, state.ds.strstart, state.ds.match_start, match_length);

            flush = ct_tally(state, state.ds.strstart - state.ds.match_start, match_length - MIN_MATCH);

            state.ds.lookahead -= match_length;

            /* 仅在匹配长度较小时将中间字符串插入哈希表，以减少耗时，但可能降低压缩率。
 *
 */
            if (match_length <= state.ds.max_insert_length && state.ds.lookahead >= MIN_MATCH)
            {
                match_length--; /* strstart 处的字符串已经位于哈希表中。 */
                do
                {
                    state.ds.strstart++;
                    INSERT_STRING(state.ds.strstart, hash_head);
                    /* strstart 不超过 WSIZE-MAX_MATCH，因此前方始终保留至少 MIN_MATCH 字节。
 *
 */
                }
                while (--match_length != 0);
                state.ds.strstart++;
            }
            else
            {
                state.ds.strstart += match_length;
                match_length = 0;
                state.ds.ins_h = state.ds.window[state.ds.strstart];
                UPDATE_HASH(state.ds.ins_h, state.ds.window[state.ds.strstart + 1]);
                Assert(state, MIN_MATCH == 3, "Call UPDATE_HASH() MIN_MATCH-3 more times");
            }
        }
        else
        {
            /* 不匹配，输出一个字面值字节 */
            flush = ct_tally(state, 0, state.ds.window[state.ds.strstart]);
            state.ds.lookahead--;
            state.ds.strstart++;
        }
        if (flush)
            FLUSH_BLOCK(state, 0), state.ds.block_start = state.ds.strstart;

        /* 除输入末尾外，始终保留足够前瞻数据：下次匹配需要 MAX_MATCH 字节，匹配后插入字典还需要 MIN_MATCH 字节。
 *
 *
 *
 */
        if (state.ds.lookahead < MIN_LOOKAHEAD)
            fill_window(state);
    }
    return FLUSH_BLOCK(state, 1); /* 输入结束。 */
}

/* 通过惰性匹配获得更好的压缩率：只有下一窗口位置不存在更优匹配时，才最终采用当前匹配。
 *
 *
 *
 */
ulg deflate(TState &state)
{
    IPos hash_head = NIL;                           /* 哈希链头 */
    IPos prev_match;                                /* 上一次的匹配位置。 */
    int flush;                                      /* 当前数据块需要输出时置位。 */
    int match_available = 0;                        /* 上一次存在待输出匹配时置位。 */
    register unsigned match_length = MIN_MATCH - 1; /* 最佳匹配的长度。 */

    if (state.level <= 3)
        return deflate_fast(state); /* 速度优化 */

    /* 处理输入块。 */
    while (state.ds.lookahead != 0)
    {
        /* 将 window[strstart .. strstart+2] 插入字典，并将 hash_head 设为对应哈希链的头节点。
 *
 */
        if (state.ds.lookahead >= MIN_MATCH)
            INSERT_STRING(state.ds.strstart, hash_head);

        /* 搜索最长匹配，舍弃长度不超过 prev_length 的候选。
 */
        state.ds.prev_length = match_length, prev_match = state.ds.match_start;
        match_length = MIN_MATCH - 1;

        if (hash_head != NIL && state.ds.prev_length < state.ds.max_lazy_match &&
            state.ds.strstart - hash_head <= MAX_DIST)
        {
            /* 不与窗口索引 0 处的字符串匹配，尤其避免在输入起点匹配字符串自身。
 *
 *
 */
            /* 不在输入末尾之外搜索匹配，保证 DEFLATE 输出具有确定性。
 *
 */
            if ((unsigned)state.ds.nice_match > state.ds.lookahead)
                state.ds.nice_match = (int)state.ds.lookahead;
            match_length = longest_match(state, hash_head);
            /* longest_match() 设置 match_start */
            if (match_length > state.ds.lookahead)
                match_length = state.ds.lookahead;

            /* 如果长度为 3 的匹配距离太远，则忽略它： */
            if (match_length == MIN_MATCH && state.ds.strstart - state.ds.match_start > TOO_FAR)
            {
                /* 如果 prev_match 同样为 MIN_MATCH，则 match_start 无效；但当前匹配无论如何都会被忽略。
 *
 */
                match_length = MIN_MATCH - 1;
            }
        }
        /* 若上一步存在匹配且当前匹配没有更优，则输出上一步的匹配。
 *
 */
        if (state.ds.prev_length >= MIN_MATCH && match_length <= state.ds.prev_length)
        {
            unsigned max_insert = state.ds.strstart + state.ds.lookahead - MIN_MATCH;
            check_match(state, state.ds.strstart - 1, prev_match, state.ds.prev_length);
            flush = ct_tally(state, state.ds.strstart - 1 - prev_match, state.ds.prev_length - MIN_MATCH);

            /* 将直到匹配末尾的各字符串插入哈希表；strstart-1 和 strstart 已经插入。
 *
 */
            state.ds.lookahead -= state.ds.prev_length - 1;
            state.ds.prev_length -= 2;
            do
            {
                if (++state.ds.strstart <= max_insert)
                {
                    INSERT_STRING(state.ds.strstart, hash_head);
                    /* strstart 不超过 WSIZE-MAX_MATCH，因此前方始终保留至少 MIN_MATCH 字节。
 *
 */
                }
            }
            while (--state.ds.prev_length != 0);
            state.ds.strstart++;
            match_available = 0;
            match_length = MIN_MATCH - 1;

            if (flush)
                FLUSH_BLOCK(state, 0), state.ds.block_start = state.ds.strstart;
        }
        else if (match_available)
        {
            /* 若前一位置没有匹配，则输出一个字面值字节；若前一位置有匹配但当前匹配更长，则将前一匹配缩短为一个字面值字节。
 *
 *
 */
            if (ct_tally(state, 0, state.ds.window[state.ds.strstart - 1]))
            {
                FLUSH_BLOCK(state, 0), state.ds.block_start = state.ds.strstart;
            }
            state.ds.strstart++;
            state.ds.lookahead--;
        }
        else
        {
            /* 尚无前一匹配可以比较，等待下一步再决定。
 *
 */
            match_available = 1;
            state.ds.strstart++;
            state.ds.lookahead--;
        }
        //        Assert(state,strstart <= isize && lookahead <= isize, "a bit too far");

        /* 除输入末尾外，保留 MAX_MATCH 字节供下次匹配，并额外保留 MIN_MATCH 字节以便插入匹配后的字符串。
 *
 *
 *
 */
        if (state.ds.lookahead < MIN_LOOKAHEAD)
            fill_window(state);
    }
    if (match_available)
        ct_tally(state, 0, state.ds.window[state.ds.strstart - 1]);

    return FLUSH_BLOCK(state, 1); /* 输入结束。 */
}

int putlocal(struct zlist far *z, WRITEFUNC wfunc, void *param)
{ // 将 *z 描述的本地头写入文件 *f。  返回 ZE_ 错误代码。
    PUTLG(LOCSIG, f);
    PUTSH(z->ver, f);
    PUTSH(z->lflg, f);
    PUTSH(z->how, f);
    PUTLG(z->tim, f);
    PUTLG(z->crc, f);
    PUTLG(z->siz, f);
    PUTLG(z->len, f);
    PUTSH(z->nam, f);
    PUTSH(z->ext, f);
    size_t res = (size_t)wfunc(param, z->iname, (unsigned int)z->nam);
    if (res != z->nam)
        return ZE_TEMP;
    if (z->ext)
    {
        res = (size_t)wfunc(param, z->extra, (unsigned int)z->ext);
        if (res != z->ext)
            return ZE_TEMP;
    }
    return ZE_OK;
}

int putextended(struct zlist far *z, WRITEFUNC wfunc, void *param)
{ // 将 *z 描述的扩展本地头写入文件 *f。返回ZE_代码
    PUTLG(EXTLOCSIG, f);
    PUTLG(z->crc, f);
    PUTLG(z->siz, f);
    PUTLG(z->len, f);
    return ZE_OK;
}

int putcentral(struct zlist far *z, WRITEFUNC wfunc, void *param)
{ // 将 *z 的中央头条目写入文件 *f。返回 ZE_ 代码。
    PUTLG(CENSIG, f);
    PUTSH(z->vem, f);
    PUTSH(z->ver, f);
    PUTSH(z->flg, f);
    PUTSH(z->how, f);
    PUTLG(z->tim, f);
    PUTLG(z->crc, f);
    PUTLG(z->siz, f);
    PUTLG(z->len, f);
    PUTSH(z->nam, f);
    PUTSH(z->cext, f);
    PUTSH(z->com, f);
    PUTSH(z->dsk, f);
    PUTSH(z->att, f);
    PUTLG(z->atx, f);
    PUTLG(z->off, f);
    if ((size_t)wfunc(param, z->iname, (unsigned int)z->nam) != z->nam ||
        (z->cext && (size_t)wfunc(param, z->cextra, (unsigned int)z->cext) != z->cext) ||
        (z->com && (size_t)wfunc(param, z->comment, (unsigned int)z->com) != z->com))
        return ZE_TEMP;
    return ZE_OK;
}

int putend(int n, ulg s, ulg c, extent m, char *z, WRITEFUNC wfunc, void *param)
{ // 将中央目录数据的末尾写入文件 *f。
    PUTLG(ENDSIG, f);
    PUTSH(0, f);
    PUTSH(0, f);
    PUTSH(n, f);
    PUTSH(n, f);
    PUTLG(s, f);
    PUTLG(c, f);
    PUTSH(m, f);
    // 若存在归档注释，则将其写入。
    if (m && wfunc(param, z, (unsigned int)m) != m)
        return ZE_TEMP;
    return ZE_OK;
}

const ulg crc_table[256] = {
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
    0x2d02ef8dL};

#define CRC32(c, b) (crc_table[((int)(c) ^ (b)) & 0xff] ^ ((c) >> 8))
#define DO1(buf) crc = CRC32(crc, *buf++)
#define DO2(buf) \
    DO1(buf);    \
    DO1(buf)
#define DO4(buf) \
    DO2(buf);    \
    DO2(buf)
#define DO8(buf) \
    DO4(buf);    \
    DO4(buf)

ulg crc32(ulg crc, const uch *buf, extent len)
{
    if (buf == NULL)
        return 0L;
    crc = crc ^ 0xffffffffL;
    while (len >= 8)
    {
        DO8(buf);
        len -= 8;
    }
    if (len)
        do
        {
            DO1(buf);
        }
        while (--len);
    return crc ^ 0xffffffffL; // （对于 64 位机器，而不是 ~c）
}

void update_keys(unsigned long *keys, char c)
{
    keys[0] = CRC32(keys[0], c);
    keys[1] += keys[0] & 0xFF;
    keys[1] = keys[1] * 134775813L + 1;
    keys[2] = CRC32(keys[2], keys[1] >> 24);
}
char decrypt_byte(unsigned long *keys)
{
    unsigned temp = ((unsigned)keys[2] & 0xffff) | 2;
    return (char)(((temp * (temp ^ 1)) >> 8) & 0xff);
}
char zencode(unsigned long *keys, char c)
{
    int t = decrypt_byte(keys);
    update_keys(keys, c);
    return (char)(t ^ c);
}

bool HasZipSuffix(const TCHAR *fn)
{
    const TCHAR *ext = fn + _tcslen(fn);
    while (ext > fn && *ext != '.')
        ext--;
    if (ext == fn && *ext != '.')
        return false;
    if (_tcsicmp(ext, _T(".Z")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".zip")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".zoo")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".arc")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".lzh")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".arj")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".gz")) == 0)
        return true;
    if (_tcsicmp(ext, _T(".tgz")) == 0)
        return true;
    return false;
}

lutime_t filetime2timet(const FILETIME ft)
{
    __int64 i = *(__int64 *)&ft;
    return (lutime_t)((i - 116444736000000000) / 10000000);
}

void filetime2dosdatetime(const FILETIME ft, WORD *dosdate, WORD *dostime)
{ // 日期：位 0..4 为日（1..31），位 5..8 为月（1..12），位 9..15 为年份减去 1980。
    // 时间：位 0..4 为秒数除以 2，位 5..10 为分（0..59），位 11..15 为时（0..23）。
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    *dosdate = (WORD)(((st.wYear - 1980) & 0x7f) << 9);
    *dosdate |= (WORD)((st.wMonth & 0xf) << 5);
    *dosdate |= (WORD)((st.wDay & 0x1f));
    *dostime = (WORD)((st.wHour & 0x1f) << 11);
    *dostime |= (WORD)((st.wMinute & 0x3f) << 5);
    *dostime |= (WORD)((st.wSecond * 2) & 0x1f);
}

ZRESULT GetFileInfo(HANDLE hf, ulg *attr, long *size, iztimes *times, ulg *timestamp)
{ // 句柄必须是文件的句柄
    // 日期与时间合并为一个 long，日期位于高位，
    // 使无符号整数比较能够反映时间先后。文件属性的
    // 高 16 位保存 Unix 属性，低 16 位保存对应的 DOS 属性。
    //struct stat s;
    //int res=stat(fn,&s);
    //if (res!=0)
    //    return false;
    // 将 Windows 文件属性转换为 zip 文件属性。
    BY_HANDLE_FILE_INFORMATION bhi;
    BOOL res = GetFileInformationByHandle(hf, &bhi);
    if (!res)
        return ZR_NOFILE;
    DWORD fa = bhi.dwFileAttributes;
    ulg a = 0;
    // 低 16 位保存由 Windows 属性映射得到的 DOS 属性。
    if (fa & FILE_ATTRIBUTE_READONLY)
        a |= 0x01;
    if (fa & FILE_ATTRIBUTE_HIDDEN)
        a |= 0x02;
    if (fa & FILE_ATTRIBUTE_SYSTEM)
        a |= 0x04;
    if (fa & FILE_ATTRIBUTE_DIRECTORY)
        a |= 0x10;
    if (fa & FILE_ATTRIBUTE_ARCHIVE)
        a |= 0x20;
    // 高 16 位保存手动构造的标准 Unix 属性。
    if (fa & FILE_ATTRIBUTE_DIRECTORY)
        a |= 0x40000000; // 目录
    else
        a |= 0x80000000; // 普通文件
    a |= 0x01000000;     // 可读的
    if (fa & FILE_ATTRIBUTE_READONLY)
    {
    }
    else
        a |= 0x00800000; // 可写的
    // 现在只需一个小的启发式方法来检查它是否是可执行文件：
    DWORD red, hsize = GetFileSize(hf, NULL);
    if (hsize > 40)
    {
        SetFilePointer(hf, 0, NULL, FILE_BEGIN);
        unsigned short magic;
        ReadFile(hf, &magic, sizeof(magic), &red, NULL);
        SetFilePointer(hf, 36, NULL, FILE_BEGIN);
        unsigned long hpos;
        ReadFile(hf, &hpos, sizeof(hpos), &red, NULL);
        if (magic == 0x54AD && hsize > hpos + 4 + 20 + 28)
        {
            SetFilePointer(hf, hpos, NULL, FILE_BEGIN);
            unsigned long signature;
            ReadFile(hf, &signature, sizeof(signature), &red, NULL);
            if (signature == IMAGE_DOS_SIGNATURE || signature == IMAGE_OS2_SIGNATURE || signature == IMAGE_OS2_SIGNATURE_LE || signature == IMAGE_NT_SIGNATURE)
            {
                a |= 0x00400000; // 可执行文件
            }
        }
    }
    //
    if (attr != NULL)
        *attr = a;
    if (size != NULL)
        *size = hsize;
    if (times != NULL)
    { // lutime_t 使用 32 位秒数，起点为 1970-01-01 00:00:00 GMT。
        // FILETIME 使用 64 位计数，单位为 100 纳秒，起点为 1601-01-01。
        times->atime = filetime2timet(bhi.ftLastAccessTime);
        times->mtime = filetime2timet(bhi.ftLastWriteTime);
        times->ctime = filetime2timet(bhi.ftCreationTime);
    }
    if (timestamp != NULL)
    {
        WORD dosdate, dostime;
        filetime2dosdatetime(bhi.ftLastWriteTime, &dosdate, &dostime);
        *timestamp = (WORD)dostime | (((DWORD)dosdate) << 16);
    }
    return ZR_OK;
}

class TZip
{
  public:
    TZip(const char *pwd)
        : hfout(0), mustclosehfout(false), hmapout(0), zfis(0), obuf(0), hfin(0), writ(0), oerr(false), hasputcen(false), ooffset(0), encwriting(false), encbuf(0), password(0), state(0)
    {
        if (pwd != 0 && *pwd != 0)
        {
            password = new char[strlen(pwd) + 1];
            strcpy(password, pwd);
        }
    }
    ~TZip()
    {
        if (state != 0)
            delete state;
        state = 0;
        if (encbuf != 0)
            delete[] encbuf;
        encbuf = 0;
        if (password != 0)
            delete[] password;
        password = 0;
    }

    // 以下成员描述正在写入的输出目标。
    // 输出支持管道、文件句柄、指定文件和内存映射。
    char *password;          // 保留密码的副本
    HANDLE hfout;            // 有效时向此文件或管道句柄写入数据。
    bool mustclosehfout;     // 如果为真，我们有责任关闭 hfout
    HANDLE hmapout;          // 使用内存映射输出时的映射句柄。
    unsigned ooffset;        // 对于 hfout，这是指针最初所在的位置
    ZRESULT oerr;            // 写入操作是否产生错误？
    unsigned writ;           // 已写入字节数；由 Add 维护，避免 write() 内部的文件定位干扰计数。
    bool ocanseek;           // 输出目标是否支持移动写入位置。
    char *obuf;              // 内存映射的已映射视图地址。
    unsigned int opos;       // mmap 中的当前位置
    unsigned int mapsize;    // 已创建内存映射的大小。
    bool hasputcen;          // 我们已经放置了中央目录了吗？
    bool encwriting;         // 为 true 时，在写入磁盘前用 keys 加密数据。
    unsigned long keys[3];   // keys 在 Add() 内初始化。
    char *encbuf;            // 如果加密，那么这是一个用于加密数据的临时工作空间
    unsigned int encbufsize; // （在 write() 内部使用和调整大小，并在析构函数中删除）
    //
    TZipFileInfo *zfis; // 每个文件都会添加到此列表中，以便在末尾写入表格
    TState *state;      // 我们每个 zip 只使用一个状态对象，因为它很大（500k）

    ZRESULT Create(void *z, unsigned int len, DWORD flags);
    static unsigned sflush(void *param, const char *buf, unsigned *size);
    static unsigned swrite(void *param, const char *buf, unsigned size);
    unsigned int write(const char *buf, unsigned int size);
    bool oseek(unsigned int pos);
    ZRESULT GetMemory(void **pbuf, unsigned long *plen);
    ZRESULT Close();

    // 以下成员描述当前读取的输入文件。
    // 这些输入状态成员直接集中存放，
    // 原作者认为引入更多对象并不能使此处设计更清晰。
    ulg attr;
    iztimes times;
    ulg timestamp; // 所有 open_* 方法都会设置这些元数据。
    bool iseekable;
    long isize, ired; // 管道输入的大小要到 close() 时才能最终确定。
    ulg crc;          // CRC 要到 close() 时才能最终确定；读取字节数持续累计。
    HANDLE hfin;
    bool selfclosehf; // 用于输入文件和管道
    const char *bufin;
    unsigned int lenin, posin; // 内存输入的缓冲区、总长度和当前位置。
    // 输入数据处理结果，即压缩输出的状态。
    ulg csize; // 压缩大小，由压缩例程设置
    // 这被一些压缩例程使用
    char buf[16384];

    ZRESULT open_file(const TCHAR *fn);
    ZRESULT open_handle(HANDLE hf, unsigned int len);
    ZRESULT open_mem(void *src, unsigned int len);
    ZRESULT open_dir();
    static unsigned sread(TState &s, char *buf, unsigned size);
    unsigned read(char *buf, unsigned size);
    ZRESULT iclose();

    ZRESULT ideflate(TZipFileInfo *zfi);
    ZRESULT istore();

    ZRESULT Add(const TCHAR *odstzn, void *src, unsigned int len, DWORD flags);
    ZRESULT AddCentral();
};

ZRESULT TZip::Create(void *z, unsigned int len, DWORD flags)
{
    if (hfout != 0 || hmapout != 0 || obuf != 0 || writ != 0 || oerr != ZR_OK || hasputcen)
        return ZR_NOTINITED;
    //
    if (flags == ZIP_HANDLE)
    {
        HANDLE hf = (HANDLE)z;
        hfout = hf;
        mustclosehfout = false;
#ifdef DuplicateHandle
        BOOL res = DuplicateHandle(GetCurrentProcess(), hf, GetCurrentProcess(), &hfout, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (res)
            mustclosehandle = true;
#endif
        // 此时已经获得 hfout；若成功复制了句柄，由本对象负责关闭副本，
        // 调用方负责关闭原句柄 h；否则使用原句柄。
        DWORD res = SetFilePointer(hfout, 0, 0, FILE_CURRENT);
        ocanseek = (res != 0xFFFFFFFF);
        if (ocanseek)
            ooffset = res;
        else
            ooffset = 0;
        return ZR_OK;
    }
    else if (flags == ZIP_FILENAME)
    {
        const TCHAR *fn = (const TCHAR *)z;
        hfout = CreateFile(fn, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hfout == INVALID_HANDLE_VALUE)
        {
            hfout = 0;
            return ZR_NOFILE;
        }
        ocanseek = true;
        ooffset = 0;
        mustclosehfout = true;
        return ZR_OK;
    }
    else if (flags == ZIP_MEMORY)
    {
        unsigned int size = len;
        if (size == 0)
            return ZR_MEMSIZE;
        if (z != 0)
            obuf = (char *)z;
        else
        {
            hmapout = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, NULL);
            if (hmapout == NULL)
                return ZR_NOALLOC;
            obuf = (char *)MapViewOfFile(hmapout, FILE_MAP_ALL_ACCESS, 0, 0, size);
            if (obuf == 0)
            {
                CloseHandle(hmapout);
                hmapout = 0;
                return ZR_NOALLOC;
            }
        }
        ocanseek = true;
        opos = 0;
        mapsize = size;
        return ZR_OK;
    }
    else
        return ZR_ARGS;
}

unsigned TZip::sflush(void *param, const char *buf, unsigned *size)
{ // 静态的
    if (*size == 0)
        return 0;
    TZip *zip = (TZip *)param;
    unsigned int writ = zip->write(buf, *size);
    if (writ != 0)
        *size = 0;
    return writ;
}
unsigned TZip::swrite(void *param, const char *buf, unsigned size)
{ // 静态的
    if (size == 0)
        return 0;
    TZip *zip = (TZip *)param;
    return zip->write(buf, size);
}
unsigned int TZip::write(const char *buf, unsigned int size)
{
    const char *srcbuf = buf;
    if (encwriting)
    {
        if (encbuf != 0 && encbufsize < size)
        {
            delete[] encbuf;
            encbuf = 0;
        }
        if (encbuf == 0)
        {
            encbuf = new char[size * 2];
            encbufsize = size;
        }
        memcpy(encbuf, buf, size);
        for (unsigned int i = 0; i < size; i++)
            encbuf[i] = zencode(keys, encbuf[i]);
        srcbuf = encbuf;
    }
    if (obuf != 0)
    {
        if (opos + size >= mapsize)
        {
            oerr = ZR_MEMSIZE;
            return 0;
        }
        memcpy(obuf + opos, srcbuf, size);
        opos += size;
        return size;
    }
    else if (hfout != 0)
    {
        DWORD writ;
        WriteFile(hfout, srcbuf, size, &writ, NULL);
        return writ;
    }
    oerr = ZR_NOTINITED;
    return 0;
}

bool TZip::oseek(unsigned int pos)
{
    if (!ocanseek)
    {
        oerr = ZR_SEEK;
        return false;
    }
    if (obuf != 0)
    {
        if (pos >= mapsize)
        {
            oerr = ZR_MEMSIZE;
            return false;
        }
        opos = pos;
        return true;
    }
    else if (hfout != 0)
    {
        SetFilePointer(hfout, pos + ooffset, NULL, FILE_BEGIN);
        return true;
    }
    oerr = ZR_NOTINITED;
    return 0;
}

ZRESULT TZip::GetMemory(void **pbuf, unsigned long *plen)
{ // 调用 GetMemory 通常意味着已经添加完所有条目。
    // 返回内存数据之前必须写入中央目录，
    // 否则返回的归档数据不完整。
    if (!hasputcen)
        AddCentral();
    hasputcen = true;
    if (pbuf != NULL)
        *pbuf = (void *)obuf;
    if (plen != NULL)
        *plen = writ;
    if (obuf == NULL)
        return ZR_NOTMMAP;
    return ZR_OK;
}

ZRESULT TZip::Close()
{ // 如果此前没有通过 GetMemory 写入中央目录，
    // 则在关闭时补写。
    ZRESULT res = ZR_OK;
    if (!hasputcen)
        res = AddCentral();
    hasputcen = true;
    if (obuf != 0 && hmapout != 0)
        UnmapViewOfFile(obuf);
    obuf = 0;
    if (hmapout != 0)
        CloseHandle(hmapout);
    hmapout = 0;
    if (hfout != 0 && mustclosehfout)
        CloseHandle(hfout);
    hfout = 0;
    mustclosehfout = false;
    return res;
}

ZRESULT TZip::open_file(const TCHAR *fn)
{
    hfin = 0;
    bufin = 0;
    selfclosehf = false;
    crc = CRCVAL_INITIAL;
    isize = 0;
    csize = 0;
    ired = 0;
    if (fn == 0)
        return ZR_ARGS;
    HANDLE hf = CreateFile(fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hf == INVALID_HANDLE_VALUE)
        return ZR_NOFILE;
    ZRESULT res = open_handle(hf, 0);
    if (res != ZR_OK)
    {
        CloseHandle(hf);
        return res;
    }
    selfclosehf = true;
    return ZR_OK;
}
ZRESULT TZip::open_handle(HANDLE hf, unsigned int len)
{
    hfin = 0;
    bufin = 0;
    selfclosehf = false;
    crc = CRCVAL_INITIAL;
    isize = 0;
    csize = 0;
    ired = 0;
    if (hf == 0 || hf == INVALID_HANDLE_VALUE)
        return ZR_ARGS;
    DWORD res = SetFilePointer(hfout, 0, 0, FILE_CURRENT);
    if (res != 0xFFFFFFFF)
    {
        ZRESULT res = GetFileInfo(hf, &attr, &isize, &times, &timestamp);
        if (res != ZR_OK)
            return res;
        SetFilePointer(hf, 0, NULL, FILE_BEGIN); // GetFileInfo 可能移动了文件位置，在此复位到起点。
        iseekable = true;
        hfin = hf;
        return ZR_OK;
    }
    else
    {
        attr = 0x80000000; // 只是一个普通文件
        isize = -1;        // 输入结束之前无法确定管道数据总大小。
        if (len != 0)
            isize = len; // 除非调用方已经明确提供长度。
        iseekable = false;
        SYSTEMTIME st;
        GetLocalTime(&st);
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        WORD dosdate, dostime;
        filetime2dosdatetime(ft, &dosdate, &dostime);
        times.atime = filetime2timet(ft);
        times.mtime = times.atime;
        times.ctime = times.atime;
        timestamp = (WORD)dostime | (((DWORD)dosdate) << 16);
        hfin = hf;
        return ZR_OK;
    }
}
ZRESULT TZip::open_mem(void *src, unsigned int len)
{
    hfin = 0;
    bufin = (const char *)src;
    selfclosehf = false;
    crc = CRCVAL_INITIAL;
    ired = 0;
    csize = 0;
    ired = 0;
    lenin = len;
    posin = 0;
    if (src == 0 || len == 0)
        return ZR_ARGS;
    attr = 0x80000000; // 只是一个普通文件
    isize = len;
    iseekable = true;
    SYSTEMTIME st;
    GetLocalTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    WORD dosdate, dostime;
    filetime2dosdatetime(ft, &dosdate, &dostime);
    times.atime = filetime2timet(ft);
    times.mtime = times.atime;
    times.ctime = times.atime;
    timestamp = (WORD)dostime | (((DWORD)dosdate) << 16);
    return ZR_OK;
}
ZRESULT TZip::open_dir()
{
    hfin = 0;
    bufin = 0;
    selfclosehf = false;
    crc = CRCVAL_INITIAL;
    isize = 0;
    csize = 0;
    ired = 0;
    attr = 0x41C00010; // 目录属性：可读、可写，并带有 Unix 与 DOS 目录标记。
    isize = 0;
    iseekable = false;
    SYSTEMTIME st;
    GetLocalTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    WORD dosdate, dostime;
    filetime2dosdatetime(ft, &dosdate, &dostime);
    times.atime = filetime2timet(ft);
    times.mtime = times.atime;
    times.ctime = times.atime;
    timestamp = (WORD)dostime | (((DWORD)dosdate) << 16);
    return ZR_OK;
}

unsigned TZip::sread(TState &s, char *buf, unsigned size)
{ // 静态的
    TZip *zip = (TZip *)s.param;
    return zip->read(buf, size);
}

unsigned TZip::read(char *buf, unsigned size)
{
    if (bufin != 0)
    {
        if (posin >= lenin)
            return 0; // 输入结束
        ulg red = lenin - posin;
        if (red > size)
            red = size;
        memcpy(buf, bufin + posin, red);
        posin += red;
        ired += red;
        crc = crc32(crc, (uch *)buf, red);
        return red;
    }
    else if (hfin != 0)
    {
        DWORD red;
        BOOL ok = ReadFile(hfin, buf, size, &red, NULL);
        if (!ok)
            return 0;
        ired += red;
        crc = crc32(crc, (uch *)buf, red);
        return red;
    }
    else
    {
        oerr = ZR_NOTINITED;
        return 0;
    }
}

ZRESULT TZip::iclose()
{
    if (selfclosehf && hfin != 0)
        CloseHandle(hfin);
    hfin = 0;
    bool mismatch = (isize != -1 && isize != ired);
    isize = ired; // 无论如何，crc 已经被更新了
    if (mismatch)
        return ZR_MISSIZE;
    else
        return ZR_OK;
}

ZRESULT TZip::ideflate(TZipFileInfo *zfi)
{
    if (state == 0)
        state = new TState();
    // 状态对象约 500 KB，因此在堆上分配，
    // 避免耗尽 PocketPC 较小的栈空间；对象随后统一释放。
    state->err = 0;
    state->readfunc = sread;
    state->flush_outbuf = sflush;
    state->param = this;
    state->level = 8;
    state->seekable = iseekable;
    state->err = NULL;
    // 以下设置通知 ct_init 执行必要初始化。
    state->ts.static_dtree[0].dl.len = 0;
    // 感谢 Alvin77 提供的这个关键修复：
    state->ds.window_size = 0;
    // 上述设置涵盖此处所需的初始化。
    //
    bi_init(*state, buf, sizeof(buf), TRUE); // 以前只是 1024 大小，而不是这里的 16384
    ct_init(*state, &zfi->att);
    lm_init(*state, state->level, &zfi->flg);
    ulg sz = deflate(*state);
    csize = sz;
    ZRESULT r = ZR_OK;
    if (state->err != NULL)
        r = ZR_FLATE;
    return r;
}

ZRESULT TZip::istore()
{
    ulg size = 0;
    for (;;)
    {
        unsigned int cin = read(buf, 16384);
        if (cin <= 0 || cin == (unsigned int)EOF)
            break;
        unsigned int cout = write(buf, cin);
        if (cout != cin)
            return ZR_MISSIZE;
        size += cin;
    }
    csize = size;
    return ZR_OK;
}

bool has_seeded = false;
ZRESULT TZip::Add(const TCHAR *odstzn, void *src, unsigned int len, DWORD flags)
{
    if (oerr)
        return ZR_FAILED;
    if (hasputcen)
        return ZR_ENDED;

    // 如果我们使用密码加密，那么每个 isize 和 csize 都会大 12 个字节
    int passex = 0;
    if (password != 0 && flags != ZIP_FOLDER)
        passex = 12;

    // ZIP 内部名称使用正斜杠分隔目录，例如 dir/file.stuff。
    TCHAR dstzn[MAX_PATH];
    _tcscpy(dstzn, odstzn);
    if (*dstzn == 0)
        return ZR_ARGS;
    TCHAR *d = dstzn;
    while (*d != 0)
    {
        if (*d == '\\')
            *d = '/';
        d++;
    }
    bool isdir = (flags == ZIP_FOLDER);
    bool needs_trailing_slash = (isdir && dstzn[_tcslen(dstzn) - 1] != '/');
    int method = DEFLATE;
    if (isdir || HasZipSuffix(dstzn))
        method = STORE;

    // 现在打开我们的输入源：
    ZRESULT openres;
    if (flags == ZIP_FILENAME)
        openres = open_file((const TCHAR *)src);
    else if (flags == ZIP_HANDLE)
        openres = open_handle((HANDLE)src, len);
    else if (flags == ZIP_MEMORY)
        openres = open_mem(src, len);
    else if (flags == ZIP_FOLDER)
        openres = open_dir();
    else
        return ZR_ARGS;
    if (openres != ZR_OK)
        return openres;

    // zip“条目”由本地标头（包括文件名）组成，
    // 然后是压缩数据，可能还有扩展的本地标头。

    // 初始化本地头
    TZipFileInfo zfi;
    zfi.nxt = NULL;
    strcpy(zfi.name, "");
#ifdef UNICODE
    WideCharToMultiByte(CP_UTF8, 0, dstzn, -1, zfi.iname, MAX_PATH, 0, 0);
#else
    strcpy(zfi.iname, dstzn);
#endif
    zfi.nam = strlen(zfi.iname);
    if (needs_trailing_slash)
    {
        strcat(zfi.iname, "/");
        zfi.nam++;
    }
    strcpy(zfi.zname, "");
    zfi.extra = NULL;
    zfi.ext = 0; // 压缩数据后的额外标头及其长度
    zfi.cextra = NULL;
    zfi.cext = 0; // 写入归档尾部中央目录的附加字段及其长度。
    zfi.comment = NULL;
    zfi.com = 0; // 条目注释及其长度。
    zfi.mark = 1;
    zfi.dosflag = 0;
    zfi.att = (ush)BINARY;
    zfi.vem = (ush)0xB17; // 0xB00 是 win32 操作系统代码。 0x17 是十进制的 23：zip 2.3
    zfi.ver = (ush)20;    // 需要 PKUNZIP 2.0 来解压
    zfi.tim = timestamp;
    // 即使我们现在写了标头，它也必须被重写，因为我们不知道压缩大小或 crc。
    zfi.crc = 0; // 稍后更新
    zfi.flg = 8; // 位值 8 表示存在数据描述符，暂按需要该描述符处理。
    if (password != 0 && !isdir)
        zfi.flg = 9;                                                     // 位值 1 表示使用密码加密。
    zfi.lflg = zfi.flg;                                                  // 稍后更新
    zfi.how = (ush)method;                                               // 稍后更新
    zfi.siz = (ulg)(method == STORE && isize >= 0 ? isize + passex : 0); // 稍后更新
    zfi.len = (ulg)(isize);                                              // 稍后更新
    zfi.dsk = 0;
    zfi.atx = attr;
    zfi.off = writ + ooffset; // 文件内该本地记录开头的偏移量
    // 将“times”结构填充到 zfi.extra 中

    // 原作者注：PocketPC CE 压缩后再在 CE 解压存在问题，曾观察到跳过下方时间字段可避免该问题。
    char xloc[EB_L_UT_SIZE];
    zfi.extra = xloc;
    zfi.ext = EB_L_UT_SIZE;
    char xcen[EB_C_UT_SIZE];
    zfi.cextra = xcen;
    zfi.cext = EB_C_UT_SIZE;
    xloc[0] = 'U';
    xloc[1] = 'T';
    xloc[2] = EB_UT_LEN(3); // e.f.数据部分的长度
    xloc[3] = 0;
    xloc[4] = EB_UT_FL_MTIME | EB_UT_FL_ATIME | EB_UT_FL_CTIME;
    xloc[5] = (char)(times.mtime);
    xloc[6] = (char)(times.mtime >> 8);
    xloc[7] = (char)(times.mtime >> 16);
    xloc[8] = (char)(times.mtime >> 24);
    xloc[9] = (char)(times.atime);
    xloc[10] = (char)(times.atime >> 8);
    xloc[11] = (char)(times.atime >> 16);
    xloc[12] = (char)(times.atime >> 24);
    xloc[13] = (char)(times.ctime);
    xloc[14] = (char)(times.ctime >> 8);
    xloc[15] = (char)(times.ctime >> 16);
    xloc[16] = (char)(times.ctime >> 24);
    memcpy(zfi.cextra, zfi.extra, EB_C_UT_SIZE);
    zfi.cextra[EB_LEN] = EB_UT_LEN(1);

    // (1) 首先写入本地头：
    int r = putlocal(&zfi, swrite, this);
    if (r != ZE_OK)
    {
        iclose();
        return ZR_WRITE;
    }
    writ += 4 + LOCHEAD + (unsigned int)zfi.nam + (unsigned int)zfi.ext;
    if (oerr != ZR_OK)
    {
        iclose();
        return oerr;
    }

    // (1.5)如果需要，写入加密头
    keys[0] = 305419896L;
    keys[1] = 591751049L;
    keys[2] = 878082192L;
    for (const char *cp = password; cp != 0 && *cp != 0; cp++)
        update_keys(keys, *cp);
    // 生成一些随机字节
    if (!has_seeded)
        srand(GetTickCount() ^ (unsigned long)GetDesktopWindow());
    char encbuf[12];
    for (int i = 0; i < 12; i++)
        encbuf[i] = (char)((rand() >> 7) & 0xff);
    encbuf[11] = (char)((zfi.tim >> 8) & 0xff);
    for (int ei = 0; ei < 12; ei++)
        encbuf[ei] = zencode(keys, encbuf[ei]);
    if (password != 0 && !isdir)
    {
        swrite(this, encbuf, 12);
        writ += 12;
    }

    // (2) 将压缩/存储的文件写入zip文件
    ZRESULT writeres = ZR_OK;
    encwriting = (password != 0 && !isdir); // 一个对象成员变量来表示我们是否加密写入磁盘
    if (!isdir && method == DEFLATE)
        writeres = ideflate(&zfi);
    else if (!isdir && method == STORE)
        writeres = istore();
    else if (isdir)
        csize = 0;
    encwriting = false;
    iclose();
    writ += csize;
    if (oerr != ZR_OK)
        return oerr;
    if (writeres != ZR_OK)
        return ZR_WRITE;

    // (3) 或者用正确的信息重写本地标头...
    bool first_header_has_size_right = (zfi.siz == csize + passex);
    zfi.crc = crc;
    zfi.siz = csize + passex;
    zfi.len = isize;
    if (ocanseek && (password == 0 || isdir))
    {
        zfi.how = (ush)method;
        if ((zfi.flg & 1) == 0)
            zfi.flg &= ~8; // 清除扩展本地标头标志
        zfi.lflg = zfi.flg;
        // 重写本地头：
        if (!oseek(zfi.off - ooffset))
            return ZR_SEEK;
        if ((r = putlocal(&zfi, swrite, this)) != ZE_OK)
            return ZR_WRITE;
        if (!oseek(writ))
            return ZR_SEEK;
    }
    else
    { // (4) ...或者在末尾放置更新的标头
        if (zfi.how != (ush)method)
            return ZR_NOCHANGE;
        if (method == STORE && !first_header_has_size_right)
            return ZR_NOCHANGE;
        if ((r = putextended(&zfi, swrite, this)) != ZE_OK)
            return ZR_WRITE;
        writ += 16L;
        zfi.flg = zfi.lflg; // 若压缩过程修改了 flg，则恢复供中央目录使用的标志。
    }
    if (oerr != ZR_OK)
        return oerr;

    // 保留 zipfileinfo 的副本，作为我们的 zip 结尾目录
    char *cextra = new char[zfi.cext];
    memcpy(cextra, zfi.cextra, zfi.cext);
    zfi.cextra = cextra;
    TZipFileInfo *pzfi = new TZipFileInfo;
    memcpy(pzfi, &zfi, sizeof(zfi));
    if (zfis == NULL)
        zfis = pzfi;
    else
    {
        TZipFileInfo *z = zfis;
        while (z->nxt != NULL)
            z = z->nxt;
        z->nxt = pzfi;
    }
    return ZR_OK;
}

ZRESULT TZip::AddCentral()
{ // 写入中心目录
    int numentries = 0;
    ulg pos_at_start_of_central = writ;
    //ulg tot_unc_size=0, tot_compressed_size=0;
    bool okay = true;
    for (TZipFileInfo *zfi = zfis; zfi != NULL;)
    {
        if (okay)
        {
            int res = putcentral(zfi, swrite, this);
            if (res != ZE_OK)
                okay = false;
        }
        writ += 4 + CENHEAD + (unsigned int)zfi->nam + (unsigned int)zfi->cext + (unsigned int)zfi->com;
        //tot_unc_size += zfi->len;
        //tot_compressed_size += zfi->siz;
        numentries++;
        //
        TZipFileInfo *zfinext = zfi->nxt;
        if (zfi->cextra != 0)
            delete[] zfi->cextra;
        delete zfi;
        zfi = zfinext;
    }
    ulg center_size = writ - pos_at_start_of_central;
    if (okay)
    {
        int res = putend(numentries, center_size, pos_at_start_of_central + ooffset, 0, NULL, swrite, this);
        if (res != ZE_OK)
            okay = false;
        writ += 4 + ENDHEAD + 0;
    }
    if (!okay)
        return ZR_WRITE;
    return ZR_OK;
}

ZRESULT lasterrorZ = ZR_OK;

unsigned int FormatZipMessageZ(ZRESULT code, char *buf, unsigned int len)
{
    if (code == ZR_RECENT)
        code = lasterrorZ;
    const char *msg = "unknown zip result code";
    switch (code)
    {
    case ZR_OK:
        msg = "Success";
        break;
    case ZR_NODUPH:
        msg = "Culdn't duplicate handle";
        break;
    case ZR_NOFILE:
        msg = "Couldn't create/open file";
        break;
    case ZR_NOALLOC:
        msg = "Failed to allocate memory";
        break;
    case ZR_WRITE:
        msg = "Error writing to file";
        break;
    case ZR_NOTFOUND:
        msg = "File not found in the zipfile";
        break;
    case ZR_MORE:
        msg = "Still more data to unzip";
        break;
    case ZR_CORRUPT:
        msg = "Zipfile is corrupt or not a zipfile";
        break;
    case ZR_READ:
        msg = "Error reading file";
        break;
    case ZR_ARGS:
        msg = "Caller: faulty arguments";
        break;
    case ZR_PARTIALUNZ:
        msg = "Caller: the file had already been partially unzipped";
        break;
    case ZR_NOTMMAP:
        msg = "Caller: can only get memory of a memory zipfile";
        break;
    case ZR_MEMSIZE:
        msg = "Caller: not enough space allocated for memory zipfile";
        break;
    case ZR_FAILED:
        msg = "Caller: there was a previous error";
        break;
    case ZR_ENDED:
        msg = "Caller: additions to the zip have already been ended";
        break;
    case ZR_ZMODE:
        msg = "Caller: mixing creation and opening of zip";
        break;
    case ZR_NOTINITED:
        msg = "Zip-bug: internal initialisation not completed";
        break;
    case ZR_SEEK:
        msg = "Zip-bug: trying to seek the unseekable";
        break;
    case ZR_MISSIZE:
        msg = "Zip-bug: the anticipated size turned out wrong";
        break;
    case ZR_NOCHANGE:
        msg = "Zip-bug: tried to change mind, but not allowed";
        break;
    case ZR_FLATE:
        msg = "Zip-bug: an internal error during flation";
        break;
    }
    unsigned int mlen = (unsigned int)strlen(msg);
    if (buf == 0 || len == 0)
        return mlen;
    unsigned int n = mlen;
    if (n + 1 > len)
        n = len - 1;
    strncpy(buf, msg, n);
    buf[n] = 0;
    return mlen;
}

typedef struct
{
    DWORD flag;
    TZip *zip;
} TZipHandleData;

HZIP CreateZipInternal(void *z, unsigned int len, DWORD flags, const char *password)
{
    TZip *zip = new TZip(password);
    lasterrorZ = zip->Create(z, len, flags);
    if (lasterrorZ != ZR_OK)
    {
        delete zip;
        return 0;
    }
    TZipHandleData *han = new TZipHandleData;
    han->flag = 2;
    han->zip = zip;
    return (HZIP)han;
}
HZIP CreateZipHandle(HANDLE h, const char *password)
{
    return CreateZipInternal(h, 0, ZIP_HANDLE, password);
}
HZIP CreateZip(const TCHAR *fn, const char *password)
{
    return CreateZipInternal((void *)fn, 0, ZIP_FILENAME, password);
}
HZIP CreateZip(void *z, unsigned int len, const char *password)
{
    return CreateZipInternal(z, len, ZIP_MEMORY, password);
}

ZRESULT ZipAddInternal(HZIP hz, const TCHAR *dstzn, void *src, unsigned int len, DWORD flags)
{
    if (hz == 0)
    {
        lasterrorZ = ZR_ARGS;
        return ZR_ARGS;
    }
    TZipHandleData *han = (TZipHandleData *)hz;
    if (han->flag != 2)
    {
        lasterrorZ = ZR_ZMODE;
        return ZR_ZMODE;
    }
    TZip *zip = han->zip;
    lasterrorZ = zip->Add(dstzn, src, len, flags);
    return lasterrorZ;
}
ZRESULT ZipAdd(HZIP hz, const TCHAR *dstzn, const TCHAR *fn)
{
    return ZipAddInternal(hz, dstzn, (void *)fn, 0, ZIP_FILENAME);
}
ZRESULT ZipAdd(HZIP hz, const TCHAR *dstzn, void *src, unsigned int len)
{
    return ZipAddInternal(hz, dstzn, src, len, ZIP_MEMORY);
}
ZRESULT ZipAddHandle(HZIP hz, const TCHAR *dstzn, HANDLE h)
{
    return ZipAddInternal(hz, dstzn, h, 0, ZIP_HANDLE);
}
ZRESULT ZipAddHandle(HZIP hz, const TCHAR *dstzn, HANDLE h, unsigned int len)
{
    return ZipAddInternal(hz, dstzn, h, len, ZIP_HANDLE);
}
ZRESULT ZipAddFolder(HZIP hz, const TCHAR *dstzn)
{
    return ZipAddInternal(hz, dstzn, 0, 0, ZIP_FOLDER);
}

ZRESULT ZipGetMemory(HZIP hz, void **buf, unsigned long *len)
{
    if (hz == 0)
    {
        if (buf != 0)
            *buf = 0;
        if (len != 0)
            *len = 0;
        lasterrorZ = ZR_ARGS;
        return ZR_ARGS;
    }
    TZipHandleData *han = (TZipHandleData *)hz;
    if (han->flag != 2)
    {
        lasterrorZ = ZR_ZMODE;
        return ZR_ZMODE;
    }
    TZip *zip = han->zip;
    lasterrorZ = zip->GetMemory(buf, len);
    return lasterrorZ;
}

ZRESULT CloseZipZ(HZIP hz)
{
    if (hz == 0)
    {
        lasterrorZ = ZR_ARGS;
        return ZR_ARGS;
    }
    TZipHandleData *han = (TZipHandleData *)hz;
    if (han->flag != 2)
    {
        lasterrorZ = ZR_ZMODE;
        return ZR_ZMODE;
    }
    TZip *zip = han->zip;
    lasterrorZ = zip->Close();
    delete zip;
    delete han;
    return lasterrorZ;
}

bool IsZipHandleZ(HZIP hz)
{
    if (hz == 0)
        return false;
    TZipHandleData *han = (TZipHandleData *)hz;
    return (han->flag == 2);
}
