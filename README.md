# Zip

这是一个面向 Windows/C++ 的 ZIP 压缩与解压源码库。支持文件、内存和 Windows 句柄等输入输出方式，并提供 Unicode 路径和归档密码接口。接口使用 C++ 重载，需要将对应 `.cpp` 实现一起编译，并非只包含头文件即可使用。

仓库地址：[DogIsMe/Zip](https://github.com/DogIsMe/Zip)。

项目源代码基于 Info-ZIP 与 zlib 的相关实现重新封装。原始版权和许可说明保留在 [Zip/Zip.cpp](Zip/Zip.cpp) 与 [Zip/unzip.cpp](Zip/unzip.cpp) 的文件头中；使用、修改或再发布时请同时保留这些声明。

## 目录结构

```text
.
├── Zip.sln
└── Zip
    ├── Zip.vcxproj
    ├── Zip.cpp       # ZIP 压缩实现
    ├── zip.h         # ZIP 压缩接口
    ├── unzip.cpp     # ZIP 解压实现
    └── unzip.h       # ZIP 解压接口
```

## 环境要求

- Windows
- Visual Studio 2022，使用项目文件声明的 `v143` 工具集
- Win32 或 x64；建议使用 `Release|x64`
- 需要在包含 Windows SDK 的 Visual Studio 开发者命令提示符中编译
- 项目未显式指定 C++ 标准版本，使用工具集默认值；下方示例与工程保持一致，启用 `UNICODE` 和 `_UNICODE`

## 编译

在 Visual Studio 中打开 `Zip.sln`，选择 `Release` 和 `x64`。当前工程类型为控制台应用，但尚未提供入口函数，因此完整“生成”会在链接阶段报告缺少 `main`；可先使用下面的命令单独检查源码编译。

也可以在 Visual Studio 开发者命令提示符中执行：

```powershell
msbuild Zip\Zip.vcxproj /t:ClCompile /p:Configuration=Release /p:Platform=x64
```

仓库中的工程只包含压缩库实现，没有示例 `main` 函数。将 `Zip.cpp`、`unzip.cpp` 以及对应头文件加入自己的 Windows 应用后，由应用程序提供入口并完成最终链接。

在现有项目中集成时，将 `Zip` 目录加入包含目录，把所需 `.cpp` 加入编译，保持头文件与实现的字符集配置一致。如果同时包含 `zip.h` 和 `unzip.h`，应同时编译两个实现文件，以满足关闭句柄宏的依赖。包含两个头文件时可按压缩/解压方向显式调用 `FormatZipMessageZ` / `FormatZipMessageU`。

## 快速使用

压缩和解压接口都使用 `HZIP` 句柄。每个成功创建或打开的句柄都必须调用一次 `CloseZip`；返回类型为 `ZRESULT` 的函数以 `ZR_OK` 表示成功，`CreateZip` / `OpenZip` 则以非空句柄表示成功。下方函数示例返回 `bool`，可放入应用的 `.cpp` 后调用。

### 将文件压缩为 ZIP

```cpp
#include "zip.h"

/**
 * 功能：将一个本地文件添加到新建 ZIP 中。
 * 参数：archivePath 为输出归档路径；sourcePath 为源文件路径。
 * 返回值：添加文件及关闭归档均成功时返回 true，否则返回 false。
 */
bool CreateFileArchive(const TCHAR* archivePath, const TCHAR* sourcePath)
{
    HZIP zipHandle = CreateZip(archivePath, nullptr);
    if (zipHandle == nullptr)
    {
        return false;
    }

    const ZRESULT addResult = ZipAdd(zipHandle, L"docs/readme.txt", sourcePath);
    const ZRESULT closeResult = CloseZip(zipHandle);
    return addResult == ZR_OK && closeResult == ZR_OK;
}
```

调用示例：`CreateFileArchive(L"example.zip", L"readme.txt");`。输入文件必须存在，归档输出路径的父目录需事先准备好；同名归档会被覆盖。`ZipAddFolder` 只添加目录条目，不会递归添加目录中的文件。

`CreateZip` 的第二个参数是归档密码，类型为 `const char*`，密码使用 ASCII 编码。`ZipAdd` 的第一个路径是 ZIP 内部条目名，第二个路径是本地文件路径；也可以使用 `ZipAdd` 的内存重载或 `ZipAddHandle` 添加内存和句柄数据。

### 从 ZIP 解压到目录

```cpp
#include "unzip.h"

/**
 * 功能：将归档条目逐项解压到指定目录。
 * 参数：archivePath 为归档路径；outputDir 为解压基目录。
 * 返回值：所有条目和关闭操作均成功时返回 true，否则返回 false。
 */
bool ExtractArchive(const TCHAR* archivePath, const TCHAR* outputDir)
{
    HZIP zipHandle = OpenZip(archivePath, nullptr);
    if (zipHandle == nullptr)
    {
        return false;
    }

    ZRESULT result = SetUnzipBaseDir(zipHandle, outputDir);
    ZIPENTRY entry{};
    if (result == ZR_OK)
    {
        result = GetZipItem(zipHandle, -1, &entry);
    }
    if (result == ZR_OK)
    {
        const int itemCount = entry.index;
        for (int index = 0; index < itemCount; ++index)
        {
            result = GetZipItem(zipHandle, index, &entry);
            if (result != ZR_OK)
            {
                break;
            }
            result = UnzipItem(zipHandle, index, entry.name);
            if (result != ZR_OK)
            {
                break;
            }
        }
    }
    const ZRESULT closeResult = CloseZip(zipHandle);
    return result == ZR_OK && closeResult == ZR_OK;
}
```

调用示例：`ExtractArchive(L"example.zip", L"output");`。`SetUnzipBaseDir` 设置相对文件名的解压基目录，默认值是当前工作目录；传入非空目录路径，调用前创建好基目录。目录条目通过文件名方式解压时会创建所需子目录。

### 使用内存归档

创建一个由库管理的内存归档时，将 `nullptr` 传给 `CreateZip` 的第一个参数，并指定最大容量。下面将归档复制到调用方容器中，保证关闭句柄后仍可使用：

```cpp
#include "zip.h"
#include <vector>

/**
 * 功能：在内存中创建包含一个数据条目的 ZIP，并复制归档结果。
 * 参数：archive 接收归档字节，仅在成功获取归档后赋值。
 * 返回值：添加、获取内存及关闭操作均成功时返回 true。
 */
bool CreateMemoryArchive(std::vector<unsigned char>& archive)
{
    HZIP zipHandle = CreateZip(static_cast<void*>(nullptr), 1024 * 1024, nullptr);
    if (zipHandle == nullptr)
    {
        return false;
    }

    char data[] = "hello zip";
    ZRESULT result = ZipAdd(zipHandle, L"data.bin", data,
                           static_cast<unsigned int>(sizeof(data) - 1));
    void* archiveBuffer = nullptr;
    unsigned long archiveLength = 0;
    if (result == ZR_OK)
    {
        result = ZipGetMemory(zipHandle, &archiveBuffer, &archiveLength);
    }
    if (result == ZR_OK)
    {
        // 库管理的地址在 CloseZip 后失效，因此先复制到调用方容器。
        const auto* bytes = static_cast<const unsigned char*>(archiveBuffer);
        archive.assign(bytes, bytes + archiveLength);
    }
    const ZRESULT closeResult = CloseZip(zipHandle);
    return result == ZR_OK && closeResult == ZR_OK;
}
```

调用 `ZipGetMemory` 后不能继续添加条目。上例归档内存由库管理，调用方不要对 `archiveBuffer` 执行 `delete` 或 `free`。如果使用已有缓冲区，则需要保证缓冲区在整个压缩过程结束前保持有效。

打开内存归档可调用 `OpenZip(buffer, length, password)`，输入缓冲区必须保持有效直到 `CloseZip`。

解压到内存可使用 `UnzipItem(zipHandle, index, buffer, bufferLength)`。当缓冲区不足时返回 `ZR_MORE`，调用方应继续使用同一条目和后续缓冲区读取剩余数据。

## 句柄和管道

- `CreateZipHandle`、`ZipAddHandle` 和 `UnzipItemHandle` 接受 Windows `HANDLE`，适用于文件句柄或管道。
- 桌面 Windows 下库会复制传入句柄，调用方仍应负责关闭自己持有的原始句柄。
- 管道不支持随机定位，压缩条目的大小可能要等数据写入完成后才能确定；从管道添加条目时，建议通过 `ZipAddHandle` 的长度参数提供明确的输入长度。
- 当前解压实现的源码说明标注了管道输入尚未实现，因此不要依赖 `OpenZipHandle` 读取管道归档；如需流式处理，应先确认目标版本的实现行为。

## 错误处理

常用返回码包括：

| 返回码 | 含义 |
| --- | --- |
| `ZR_OK` | 操作成功 |
| `ZR_NOFILE` | 无法创建或打开文件 |
| `ZR_NOALLOC` | 资源分配失败 |
| `ZR_READ` / `ZR_WRITE` | 读取或写入失败 |
| `ZR_NOTFOUND` | 未找到指定条目 |
| `ZR_CORRUPT` | ZIP 文件损坏或格式无效 |
| `ZR_PASSWORD` | 解压密码不正确 |
| `ZR_MORE` | 仍有数据需要继续解压 |
| `ZR_ARGS` | 参数错误 |
| `ZR_MEMSIZE` | 提供的内存空间不足 |

可以将 `ZR_RECENT` 传给错误说明函数查询最近一次错误。应在 `CloseZip` 等后续调用之前获取说明，以免最近错误被覆盖。压缩侧的 `FormatZipMessageZ` 接受 `char*` 缓冲区，解压侧的 `FormatZipMessageU` 接受 `TCHAR*` 缓冲区，例如：

```cpp
char zipMessage[256]{};
FormatZipMessageZ(ZR_RECENT, zipMessage, 256);

TCHAR unzipMessage[256]{};
FormatZipMessageU(ZR_RECENT, unzipMessage, 256);
```

上述两个调用需分别包含对应头文件；函数名末尾的 `Z` / `U` 区分压缩与解压。发生错误时仍要调用 `CloseZip` 释放已经创建的非空句柄，并检查关闭结果。

## 许可

本项目包含 Info-ZIP 和 zlib 的衍生代码。具体版权、免责声明和再发布条件以 `Zip.cpp`、`unzip.cpp` 文件头中的原始许可文本为准。发布包含本项目的二进制程序时，也应在随附文档中保留相应版权和许可信息。
