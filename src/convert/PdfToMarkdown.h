#pragma once

#include <QString>

#include <functional>

namespace Reader {

// PDF -> Markdown 转换结果（仅统计信息）。
struct MarkdownExportResult
{
    int pageCount = 0;
    int imageCount = 0;
    // 导出的图片目录（绝对路径）。没有图片时为空。
    QString assetsDirectory;
};

// 进度回调：done / total 为页数（内部会分两遍扫描，因此 total 是页数的两倍）。
// 返回 false 表示用户取消。
using MarkdownProgressFn = std::function<bool(int done, int total)>;

// 把 pdfPath 转换为 markdownPath（UTF-8 文本，原子写入）。
// 图片（如有）写入与 Markdown 同级的 "<文件名>_assets" 目录。
// password 用于加密 PDF，可留空。
// 返回 false 时 error 中给出可读的中文错误信息。
bool exportPdfToMarkdown(const QString& pdfPath, const QString& markdownPath,
                         const QString& password, const MarkdownProgressFn& progress,
                         MarkdownExportResult* result, QString* error);

} // namespace Reader
