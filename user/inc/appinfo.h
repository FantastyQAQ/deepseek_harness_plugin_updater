#ifndef APPINFO_H
#define APPINFO_H

/*
 * 应用元信息集中定义（方便随时修改，改这里即可，无需改动业务代码）。
 * ------------------------------------------------------------------
 * 说明：
 *   工具名 / 版本号 / 作者 / 邮箱均以宏表示，被主窗口标题、"关于"对话框、
 *   Windows VERSIONINFO 资源共同引用，避免多处硬编码、改一处漏一处。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 用户可见的品牌信息 ===== */

// 工具名称（最终用户在窗口标题、exe 属性、关于对话框都看到这个名字）
#define APP_NAME            "DSH 插件更新工具"

// 原始文件名（VERSIONINFO OriginalFilename，必须与最终 exe 名字一致）
#define APP_ORIGINAL_FILENAME  "dsh_plugin_updater.exe"

// 发布者（VERSIONINFO CompanyName / LegalCopyright 用）
#define APP_AUTHOR_NAME     "lch"

// 邮箱
#define APP_AUTHOR_MAIL     "fantasylch@foxmail.com"

// 版权年份
#define APP_COPYRIGHT_YEAR  "2026"

// 显示用的简短版本号字符串（窗口标题、关于对话框都用这个）
#define APP_VERSION         "SV01.008"

// 数字版本号（exe 属性里的「文件版本 / 产品版本」用，四段：主.次.补丁.构建）
//   必须与上面的 APP_VERSION 同步：末三位一一对应（如 SV01.002 对应 1,0,0,2）
#define APP_VERSION_NUM     1,0,0,8

// 构建日期标签（形如 261110，即 26 年 11 月 10 日）
#define APP_BUILD_DATE      "261110"

/* ===== 完整标题 ===== */
#define APP_FULL_TITLE      APP_NAME " " APP_VERSION " [" APP_BUILD_DATE "]"

/* ===== exe 版本资源用的派生宏 ===== */
// 产品版本带构建日期标签（如 "SV01.000 [261109]"）
#define APP_PRODUCT_VERSION APP_VERSION " [" APP_BUILD_DATE "]"

#ifdef __cplusplus
}
#endif

#endif // APPINFO_H
