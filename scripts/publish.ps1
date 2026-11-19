# 发布脚本：对打包后的 exe 计算 sha256/size，生成并签名 latest.json
#
# 用法（在工程根目录执行）：
#   powershell -ExecutionPolicy Bypass -File scripts/publish.ps1 `
#       -Exe pack/dsh_plugin_updater_SV01.011.exe `
#       -Version SV01.011 `
#       -Notes "新增在线更新功能；修复若干界面问题"
#
# 私钥默认使用 update_keys\update_sign_private.pem（相对工程根目录），
# 需要时可用 -PrivateKey 覆盖。
#
# 产物输出到 publish_info/ 目录：latest.json、latest.json.sig（这两个要上传到服务器）
# 该目录整目录被 .gitignore 排除，属于本机发布产物，不入版本管理。
#
# 清单里的 url 字段自动取 -Exe 的文件名（如 releases/SV01.011/dsh_plugin_updater_SV01.011.exe），
# 因此上传时不需要改名，服务器上的文件名与本地打包产物保持一致。

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Notes = "",
    [string]$PrivateKey = "",
    [string]$AppId = "dsh_plugin_updater",
    [string]$OutDir = "publish_info"
)

$ErrorActionPreference = "Stop"

# 切换到工程根目录（脚本所在目录的上一级），让所有相对路径稳定生效
# 这样无论从哪个目录调用，update_keys/ 与 user/inc/ 都能正确解析。
# 注意：Set-Location 只改 PowerShell 自己的位置，[Environment]::CurrentDirectory
# 是 .NET API（如 [IO.File]::WriteAllText）使用的当前目录，两者必须一起同步，
# 否则从其它目录调用时清单会被写到调用者所在目录。
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
[Environment]::CurrentDirectory = $projectRoot

# 私钥默认走工程内的相对路径
if ([string]::IsNullOrWhiteSpace($PrivateKey)) {
    $PrivateKey = "update_keys/update_sign_private.pem"
}

if (-not (Test-Path $Exe)) { throw "找不到待发布的 exe：$Exe（当前目录：$projectRoot）" }
if (-not (Test-Path $PrivateKey)) {
    throw "找不到私钥：$PrivateKey（相对工程根目录 $projectRoot）"
}

# 版本号必须与 app_info.h 里的 APP_VERSION 一致，避免清单指向错误的目录
$appInfo = Get-Content "user/inc/app_info.h" -Raw
if ($appInfo -notmatch ('#define\s+APP_VERSION\s+"' + [regex]::Escape($Version) + '"')) {
    throw "版本号不匹配：app_info.h 里的 APP_VERSION 不是 $Version，请先升版本号"
}

# 1) 计算大小与 SHA256（对打包后的 exe 计算，不是 release/ 下的裸 exe）
$file = Get-Item $Exe
$sha = (Get-FileHash -Path $Exe -Algorithm SHA256).Hash.ToLower()

# 统一换行写法：允许调用方传字面量 \n（cmd 里常见），也允许传真实换行，
# 一律归一成真实换行；写 JSON 时会自动转义为 \n，客户端按换行显示
$Notes = $Notes -replace "`r`n", "`n"
$Notes = $Notes -replace '\\n', "`n"

# 2) 生成清单。注意：签名后不能再改内容，否则验签必然失败
# 清单里的 url 直接使用待发布 exe 的文件名（如 dsh_plugin_updater_SV01.011.exe），
# 这样上传时无需改名，服务器上的文件名与本地打包产物完全一致，避免
# 「清单写 dsh_plugin_updater.exe、服务器上却是 dsh_plugin_updater_SV01.011.exe」的 404。
$exeName = Split-Path $Exe -Leaf
$manifest = [ordered]@{
    schema      = 1
    app         = $AppId
    version     = $Version
    size        = $file.Length
    sha256      = $sha
    url         = "releases/$Version/$exeName"
    publishedAt = (Get-Date).ToString("yyyy-MM-ddTHH:mm:sszzz")
    notes       = $Notes
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$manifestPath = Join-Path $OutDir "latest.json"
# 不带 BOM、UTF-8 无换行转换问题：用 .NET 直接写字节，保证签名与内容一一对应
$json = ($manifest | ConvertTo-Json -Depth 4) -replace "`r`n", "`n"
[IO.File]::WriteAllText($manifestPath, $json, (New-Object Text.UTF8Encoding($false)))

# 3) 签名（PKCS#1 v1.5 + SHA-256，与客户端 BCrypt 验签对应）
$sigPath = Join-Path $OutDir "latest.json.sig"
& openssl dgst -sha256 -sign $PrivateKey -out $sigPath $manifestPath
if ($LASTEXITCODE -ne 0) { throw "openssl 签名失败" }

# 4) 本地自检：用配对公钥验证一次，避免把坏签名传上服务器
$pubPath = Join-Path (Split-Path $PrivateKey) "update_sign_public.pem"
if (Test-Path $pubPath) {
    $verify = & openssl dgst -sha256 -verify $pubPath -signature $sigPath $manifestPath
    if ($LASTEXITCODE -ne 0) { throw "签名自检失败：$verify" }
    Write-Host "签名自检：$verify"
} else {
    # 没有公钥就不做自检，但要提醒（客户端验签失败时排查成本更高）
    Write-Host "警告：未找到配对公钥 $pubPath，跳过签名自检" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "发布清单已生成：$manifestPath"
Write-Host "  版本：$Version"
Write-Host "  大小：$($file.Length) 字节"
Write-Host "  SHA256：$sha"
Write-Host "  私钥：$(Resolve-Path $PrivateKey)"
Write-Host ""
Write-Host "上传顺序（先 exe、后清单，目标文件名与本地一致、无需改名）："
Write-Host "  scp `"$Exe`" root@<公网IP>:/www/wwwroot/qttools_updates/$AppId/releases/$Version/$exeName"
Write-Host "  scp `"$manifestPath`" `"$sigPath`" root@<公网IP>:/www/wwwroot/qttools_updates/$AppId/"
Write-Host "  ssh root@<公网IP> `"chown -R www:www /www/wwwroot/qttools_updates`""
Write-Host ""
Write-Host "注意：目录 /www/wwwroot/qttools_updates 是宝塔站点的根目录；"
Write-Host "     站点根下必须存在 $AppId 子目录，清单 url 指向 releases/$Version/$exeName。"
Write-Host "     若站点根目录配置不同，请以实际配置为准。"
