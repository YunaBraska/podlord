#requires -Version 7.0
<#
Package the existing Release/x64 executable. This is not a build or signing command.
Microsoft permits app-local deployment from VC/Redist, subject to the VS license:
https://learn.microsoft.com/cpp/windows/determining-which-dlls-to-redistribute
Windows 10 includes D3DCompiler; validate the actual OS copy before omitting it:
https://learn.microsoft.com/gaming/gdk/docs/features/common/packaging/packaging-framework-packages
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$QtDirectory,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$ArchivePath,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [string]$SourceDirectory = (Split-Path -Parent $PSScriptRoot),
    [string]$VisualStudioDirectory = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (!$IsWindows -or ![Environment]::Is64BitProcess -or [Environment]::OSVersion.Version.Major -lt 10) {
    throw 'Requires 64-bit PowerShell on Windows 10 or newer. No package was created.'
}
foreach ($name in @('BuildDirectory', 'QtDirectory', 'PackageDirectory', 'ArchivePath', 'EvidenceDirectory', 'SourceDirectory')) {
    $value = Get-Variable -Name $name -ValueOnly
    if (![IO.Path]::IsPathFullyQualified($value)) { throw "$name must be absolute." }
    Set-Variable -Name $name -Value ([IO.Path]::GetFullPath($value).TrimEnd('\'))
}
function Test-ContainedPath([string]$Path, [string]$Root) {
    return [IO.Path]::GetFullPath($Path).StartsWith(
        [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}
$outputs = @($PackageDirectory, $ArchivePath, $EvidenceDirectory)
for ($i = 0; $i -lt $outputs.Count; ++$i) {
    if (Test-Path -LiteralPath $outputs[$i]) { throw "Output already exists: $($outputs[$i])" }
    for ($j = $i + 1; $j -lt $outputs.Count; ++$j) {
        if ($outputs[$i] -eq $outputs[$j] -or (Test-ContainedPath $outputs[$i] $outputs[$j]) -or (Test-ContainedPath $outputs[$j] $outputs[$i])) {
            throw 'Output paths must not overlap.'
        }
    }
}
foreach ($output in @($PackageDirectory, $ArchivePath, $EvidenceDirectory)) {
    foreach ($inputRoot in @($SourceDirectory, $BuildDirectory, $QtDirectory)) {
        if ((Test-ContainedPath $output $inputRoot) -or $output -eq $inputRoot) { throw 'Outputs must not overwrite source or SDK/build inputs.' }
    }
}
$app = Join-Path $BuildDirectory 'Release\podlord-native.exe'
$deploy = Join-Path $QtDirectory 'bin\windeployqt.exe'
$scanner = Join-Path $QtDirectory 'bin\qmlimportscanner.exe'
foreach ($file in @($app, $deploy, $scanner, (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required build input missing: $file" }
}
if (!$VisualStudioDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Official Visual Studio installation locator missing.' }
    $VisualStudioDirectory = (& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or !$VisualStudioDirectory) { throw 'Could not locate the licensed Visual Studio C++ installation.' }
}
$VisualStudioDirectory = [IO.Path]::GetFullPath($VisualStudioDirectory)
# Never obtain distributable DLLs from VC/Tools/MSVC/*/bin or System32.
$crtCandidates = @(Get-Item -Path (Join-Path $VisualStudioDirectory 'VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT') -ErrorAction SilentlyContinue |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'vcruntime140.dll') } |
    Sort-Object { (Get-Item -LiteralPath (Join-Path $_.FullName 'vcruntime140.dll')).VersionInfo.FileVersionRaw } -Descending)
if (!$crtCandidates.Count) { throw 'Official x64 Microsoft.VC143.CRT app-local redistributables missing under VC\Redist.' }
$crtRoot = $crtCandidates[0].FullName
$dumpbinCandidates = @(Get-Item -Path (Join-Path $VisualStudioDirectory 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe') |
    Sort-Object { $_.VersionInfo.FileVersionRaw } -Descending)
if (!$dumpbinCandidates.Count) { throw 'dumpbin is required to inspect every deployed PE dependency.' }
$dumpbin = $dumpbinCandidates[0].FullName
$sslEntry = @(Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^OPENSSL_INCLUDE_DIR:[^=]+=(.+)$')
if ($sslEntry.Count -ne 1) { throw 'Expected one actual OPENSSL_INCLUDE_DIR in the configured build cache.' }
$sslRoot = Split-Path -Parent $sslEntry[0].Matches[0].Groups[1].Value
$sslLicense = @('LICENSE.txt', 'LICENSE', 'share\openssl\copyright') |
    ForEach-Object { Join-Path $sslRoot $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (!$sslLicense) { throw "Actual OpenSSL supplier license missing under $sslRoot." }
$system32 = Join-Path $env:SystemRoot 'System32'
$d3dCompiler = Join-Path $system32 'D3DCompiler_47.dll'
if (!(Test-Path -LiteralPath $d3dCompiler) -or (Get-AuthenticodeSignature -LiteralPath $d3dCompiler).Status -ne 'Valid') {
    throw 'The signed OS D3DCompiler_47.dll is required before omitting the bundled duplicate.'
}
New-Item -ItemType Directory -Path $EvidenceDirectory | Out-Null
$work = Join-Path $EvidenceDirectory 'work'
New-Item -ItemType Directory -Path $work, $PackageDirectory | Out-Null
$result = [ordered]@{ passed = $false; architecture = 'x64'; package = $PackageDirectory; archive = $ArchivePath
    crtSource = $crtRoot; opensslSource = $sslRoot; osD3DCompiler = $d3dCompiler; probes = @(); errors = @() }
$archiveCreated = $false

function Invoke-PackageProbe([string]$Style, [string]$Renderer) {
    $name = "$Renderer-$Style"
    $profile = Join-Path $work $name
    New-Item -ItemType Directory -Path $profile | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $extracted 'podlord-native.exe'
    $start.WorkingDirectory = $extracted
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.Environment.Clear()
    foreach ($key in @('SystemRoot', 'WINDIR', 'SystemDrive', 'ProgramData')) {
        $value = [Environment]::GetEnvironmentVariable($key)
        if ($value) { $start.Environment[$key] = $value }
    }
    $start.Environment['PATH'] = "$system32;$env:SystemRoot;$system32\Wbem"
    foreach ($key in @('USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'TEMP', 'TMP')) { $start.Environment[$key] = $profile }
    $start.Environment['KUBECONFIG'] = Join-Path $profile 'absent-kubeconfig'
    $start.Environment['PODLORD_DISABLE_AUDIO'] = '1'
    $start.Environment['PODLORD_DISABLE_UPDATE_CHECK'] = '1'
    $start.Environment['QT_QPA_PLATFORM'] = 'windows'
    $start.Environment['QT_QUICK_CONTROLS_STYLE'] = $Style
    $start.Environment['QT_QUICK_CONTROLS_FALLBACK_STYLE'] = 'Basic'
    $start.Environment['QT_PLUGIN_PATH'] = $extracted
    $start.Environment['QML_IMPORT_PATH'] = Join-Path $extracted 'qml'
    $start.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
    $start.Environment['QT_MESSAGE_PATTERN'] = '%{category}: %{message}'
    $start.Environment['QSG_INFO'] = '1'
    $start.Environment['QSG_RENDER_TIMING'] = '1'
    $start.Environment['QT_LOGGING_RULES'] = 'qt.rhi.general=true;qt.scenegraph.general=true;qt.scenegraph.time.*=true'
    if ($Renderer -eq 'software') { $start.Environment['QT_QUICK_BACKEND'] = 'software' }
    else {
        $start.Environment['QSG_RHI_BACKEND'] = 'd3d11'
        $start.Environment['QSG_RHI_PREFER_SOFTWARE_RENDERER'] = $(if ($Renderer -eq 'warp') { '1' } else { '0' })
    }
    if ($Renderer -eq 'help') { $start.ArgumentList.Add('--help') }
    else { $start.ArgumentList.Add('--profile'); $start.ArgumentList.Add($profile) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $start
    $stdout = $null; $stderr = $null; $modules = @(); $failure = $null; $started = $false
    try {
        if (!$process.Start()) { throw 'Could not start the extracted public executable.' }
        $started = $true
        $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
        if ($Renderer -eq 'help') {
            if (!$process.WaitForExit(3000) -or $process.ExitCode -ne 0) { throw 'Packaged command-line startup failed.' }
        } else {
            # Preserve the existing three-second Windows startup smoke interval.
            Start-Sleep -Seconds 3
            $process.Refresh()
            if ($process.HasExited -or $process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'No live public application window after startup.' }
            $modules = @($process.Modules | ForEach-Object { [ordered]@{ name = $_.ModuleName; path = $_.FileName } })
            foreach ($module in $modules) {
                $owned = Test-ContainedPath $module.path $extracted
                if ($module.name -match '^(Qt6|libssl|libcrypto|msvcp\d|vcruntime\d|concrt\d|vccorlib\d)' -and !$owned) {
                    throw "Host Qt/OpenSSL/VC runtime resolved outside the extracted package: $($module.path)"
                }
                if (!$owned -and !(Test-ContainedPath $module.path $env:SystemRoot)) { throw "Unexpected external module: $($module.path)" }
            }
            foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Quick.dll', 'msvcp140.dll', 'vcruntime140.dll')) {
                if (!@($modules | Where-Object { $_.name -ieq $required -and (Test-ContainedPath $_.path $extracted) }).Count) {
                    throw "Required app-local runtime was not observed: $required"
                }
            }
        }
    } catch { $failure = $_.Exception.Message }
    finally {
        if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        if ($stdout) { $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$name.stdout.log") -Encoding utf8 }
        if ($stderr) { $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$name.stderr.log") -Encoding utf8 }
        $modules | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$name.modules.json") -Encoding utf8
        $process.Dispose()
    }
    if ($failure) { throw $failure }
    $out = Get-Content -LiteralPath (Join-Path $EvidenceDirectory "$name.stdout.log") -Raw
    $log = Get-Content -LiteralPath (Join-Path $EvidenceDirectory "$name.stderr.log") -Raw
    if ($log -match '(?i)Binding loop|ReferenceError|TypeError|failed to load|module .+ is not installed|could not find or load|failed to create|cannot create the native|cannot mix incompatible') {
        throw "Runtime load/render error in $name.stderr.log"
    }
    if ($Renderer -eq 'help') {
        if ($out -notmatch '--kubeconfig' -or $out -notmatch '--profile') { throw 'Expected public CLI help missing.' }
        return [ordered]@{ style = $Style; renderer = $Renderer; actualAdapterKind = 'not-applicable'; passed = $true }
    }
    $actualAdapterKind = 'qtquick-software'
    if ($Renderer -eq 'software') {
        if ($log -notmatch "Frame rendered with 'software' renderloop") { throw 'No completed Qt Quick software frame.' }
    } else {
        if ($log -notmatch 'Created QRhi' -or $log -notmatch 'syncAndRender: frame rendered') { throw 'No completed D3D render frame.' }
        $adapters = [regex]::Matches($log, "(?m)^.*Adapter \d+: '[^']*' \(vendor 0x[0-9A-Fa-f]+ device 0x[0-9A-Fa-f]+ flags 0x(?<flags>[0-9A-Fa-f]+)\)\r?\n[^\r\n]*using this adapter")
        if (!$adapters.Count) { throw 'Selected D3D adapter identity unavailable. Hardware is not inferred from startup.' }
        $selected = $adapters[$adapters.Count - 1]
        $softwareAdapter = ([Convert]::ToUInt32($selected.Groups['flags'].Value, 16) -band 2) -ne 0
        $actualAdapterKind = $(if ($softwareAdapter) { 'warp' } else { 'hardware' })
        if ($Renderer -eq 'warp' -and !$softwareAdapter) { throw 'Explicit WARP probe did not select a software adapter.' }
    }
    return [ordered]@{ style = $Style; renderer = $(if ($Renderer -eq 'd3d11') { 'd3d11/default' } else { $Renderer })
        actualAdapterKind = $actualAdapterKind; passed = $true; modules = $modules.Count }
}

try {
    Copy-Item -LiteralPath $app -Destination (Join-Path $PackageDirectory 'podlord-native.exe')
    & $deploy --release --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --qmldir (Join-Path $SourceDirectory 'native\ui') --dir $PackageDirectory (Join-Path $PackageDirectory 'podlord-native.exe') *> (Join-Path $EvidenceDirectory 'windeployqt.log')
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }
    $crtFiles = @(Get-ChildItem -LiteralPath $crtRoot -Filter '*.dll' -File)
    if (!$crtFiles.Count) { throw 'The official CRT directory is empty.' }
    foreach ($dll in $crtFiles) {
        $signature = Get-AuthenticodeSignature -LiteralPath $dll.FullName
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') { throw "Unverified Microsoft redist: $($dll.Name)" }
        Copy-Item -LiteralPath $dll.FullName -Destination $PackageDirectory
        if ((Get-FileHash -LiteralPath (Join-Path $PackageDirectory $dll.Name)).Hash -ne (Get-FileHash -LiteralPath $dll.FullName).Hash) { throw 'App-local CRT bytes changed.' }
    }
    foreach ($pattern in @('libcrypto-3*.dll', 'libssl-3*.dll')) {
        $runtime = @(Get-ChildItem -LiteralPath (Join-Path $sslRoot 'bin') -Filter $pattern -File)
        if ($runtime.Count -ne 1) { throw "Expected exactly one $pattern under the actual OpenSSL root." }
        Copy-Item -LiteralPath $runtime[0].FullName -Destination $PackageDirectory
    }
    Copy-Item -LiteralPath (Join-Path $SourceDirectory 'native\licenses') -Destination (Join-Path $PackageDirectory 'licenses') -Recurse
    Copy-Item -LiteralPath (Join-Path $SourceDirectory 'LICENSE') -Destination (Join-Path $PackageDirectory 'licenses\Podlord-LICENSE.txt')
    Copy-Item -LiteralPath $sslLicense -Destination (Join-Path $PackageDirectory 'licenses\OpenSSL-supplier-LICENSE.txt')
    $metadataDirectory = Join-Path $PackageDirectory 'licenses\qt-spdx'
    New-Item -ItemType Directory -Path $metadataDirectory | Out-Null
    foreach ($module in @('qtbase', 'qtdeclarative', 'qtmultimedia', 'qtsvg', 'qtwebsockets')) {
        $metadata = @(Get-ChildItem -Path (Join-Path $QtDirectory "sbom\$module-*.spdx.json"), (Join-Path $QtDirectory "share\qt\sbom\$module-*.spdx") -File -ErrorAction SilentlyContinue)
        if (!$metadata.Count) { throw "Actual Qt supplier metadata missing: $module" }
        $metadata | Copy-Item -Destination $metadataDirectory
    }
    # Check real imports before applying the same Basic/Fusion-only style policy as macOS.
    $unused = @('Imagine', 'Material', 'Universal', 'FluentWinUI3', 'iOS', 'macOS', 'Windows')
    $explicitStyles = Get-ChildItem -LiteralPath (Join-Path $SourceDirectory 'native\ui') -Filter '*.qml' -File |
        Select-String -Pattern 'import\s+QtQuick\.Controls\.(Imagine|Material|Universal|FluentWinUI3|iOS|macOS|Windows)\b'
    if ($explicitStyles) { throw 'A product QML import requires a style scheduled for pruning.' }
    & $scanner -rootPath (Join-Path $SourceDirectory 'native\ui') -importPath (Join-Path $QtDirectory 'qml') > (Join-Path $EvidenceDirectory 'qml-imports.json') 2> (Join-Path $EvidenceDirectory 'qml-imports.stderr.log')
    if ($LASTEXITCODE -ne 0) { throw 'Actual QML import scan failed.' }
    $imports = @(Get-Content -LiteralPath (Join-Path $EvidenceDirectory 'qml-imports.json') -Raw | ConvertFrom-Json)
    if (!@($imports | Where-Object { $_.name -eq 'QtQuick.Controls' }).Count) { throw 'The product Qt Quick Controls import was not scanned.' }
    foreach ($style in $unused) {
        $directory = Join-Path $PackageDirectory "qml\QtQuick\Controls\$style"
        if (Test-Path -LiteralPath $directory) { Remove-Item -LiteralPath $directory -Recurse -Force }
        foreach ($name in @("Qt6QuickControls2$style.dll", "Qt6QuickControls2${style}StyleImpl.dll")) {
            $file = Join-Path $PackageDirectory $name
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
        }
    }
    foreach ($style in @('Basic', 'Fusion')) {
        if (!(Test-Path -LiteralPath (Join-Path $PackageDirectory "qml\QtQuick\Controls\$style\qmldir"))) { throw "Required style missing: $style" }
    }
    foreach ($required in @('Qt6Core.dll', 'platforms\qwindows.dll', 'tls\qopensslbackend.dll', 'msvcp140.dll', 'vcruntime140.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $PackageDirectory $required))) { throw "Required packaged runtime missing: $required" }
    }
    if (Test-Path -LiteralPath (Join-Path $PackageDirectory 'opengl32sw.dll')) { throw 'Software OpenGL was not omitted as requested.' }
    if (Test-Path -LiteralPath (Join-Path $PackageDirectory 'D3DCompiler_47.dll')) { throw 'Bundled duplicate D3DCompiler_47.dll remains.' }
    "[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`nTranslations=translations`n" | Set-Content -LiteralPath (Join-Path $PackageDirectory 'qt.conf') -Encoding ascii
    $manifest = @()
    foreach ($file in Get-ChildItem -LiteralPath $PackageDirectory -Recurse -Force) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Package reparse point rejected: $($file.FullName)" }
        if (!$file.PSIsContainer) {
            if ($file.Extension -in @('.pdb', '.lib', '.exp')) { throw 'Debug/linker files do not belong in the runtime package.' }
            $manifest += [ordered]@{ path = [IO.Path]::GetRelativePath($PackageDirectory, $file.FullName); bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
        }
    }
    $result.installedBytes = [long](($manifest | Measure-Object -Property bytes -Sum).Sum)
    if ($result.installedBytes -le 0 -or $result.installedBytes -gt 100000000) { throw "Installed runtime exceeds the unchanged 100000000-byte limit: $($result.installedBytes)" }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'files.json') -Encoding utf8
    [IO.Compression.ZipFile]::CreateFromDirectory($PackageDirectory, $ArchivePath, [IO.Compression.CompressionLevel]::Optimal, $false)
    $archiveCreated = $true
    $result.zipBytes = (Get-Item -LiteralPath $ArchivePath).Length
    if ($result.zipBytes -le 0 -or $result.zipBytes -gt 50000000) { throw "Archive exceeds the unchanged 50000000-byte limit: $($result.zipBytes)" }
    $extracted = Join-Path $work 'extracted'
    [IO.Compression.ZipFile]::ExtractToDirectory($ArchivePath, $extracted)
    if (@(Get-ChildItem -LiteralPath $extracted -Recurse -File -Force).Count -ne $manifest.Count) { throw 'Archive extraction changed the payload count.' }
    foreach ($entry in $manifest) {
        $file = Join-Path $extracted $entry.path
        if ((Get-Item -LiteralPath $file).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) { throw 'Archive extraction changed payload bytes.' }
    }
    # Inspect the extracted PE graph, including app-local CRT and dynamic-plugin imports.
    $dependencies = @()
    foreach ($file in Get-ChildItem -LiteralPath $extracted -Recurse -File | Where-Object { $_.Extension -in @('.dll', '.exe') }) {
        $dump = & $dumpbin /HEADERS /DEPENDENTS $file.FullName 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $dump -notmatch '(?i)8664 machine') { throw "Invalid/non-x64 PE: $($file.FullName)" }
        foreach ($match in [regex]::Matches($dump, '(?mi)^\s+([A-Za-z0-9_.-]+\.dll)\s*$')) {
            $name = $match.Groups[1].Value
            $local = Join-Path $extracted $name
            $system = Join-Path $system32 $name
            if ($name -match '^(?i:api-ms-win-|ext-ms-)') { $resolved = 'Windows API set' }
            elseif (Test-Path -LiteralPath $local -PathType Leaf) { $resolved = $local }
            elseif ($name -match '^(?i:Qt6|libssl|libcrypto|msvcp\d|vcruntime\d|concrt\d|vccorlib\d)') { throw "App-local dependency missing: $name" }
            elseif (Test-Path -LiteralPath $system -PathType Leaf) { $resolved = $system }
            else { throw "Unresolved extracted-package import: $name ($($file.Name))" }
            $dependencies += [ordered]@{ file = [IO.Path]::GetRelativePath($extracted, $file.FullName); dependency = $name; resolved = $resolved }
        }
    }
    $dependencies | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'dependencies.json') -Encoding utf8
    $result.probes += Invoke-PackageProbe 'Fusion' 'help'
    foreach ($style in @('Basic', 'Fusion')) {
        foreach ($renderer in @('d3d11', 'warp', 'software')) {
            try { $result.probes += Invoke-PackageProbe $style $renderer }
            catch {
                $result.probes += [ordered]@{ style = $style; renderer = $(if ($renderer -eq 'd3d11') { 'd3d11/default' } else { $renderer })
                    actualAdapterKind = 'unverified'; passed = $false; error = $_.Exception.Message }
                $result.errors += "$renderer/$style`: $($_.Exception.Message)"
            }
        }
    }
    if ($result.errors.Count) { throw 'One or more actual packaged renderer/runtime probes failed. See package-result.json and per-probe logs.' }
    $result.sha256 = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash
    $result.passed = $true
} catch {
    $result.errors += $_.Exception.Message
    if ($archiveCreated -and (Test-Path -LiteralPath $ArchivePath)) { Remove-Item -LiteralPath $ArchivePath -Force }
    throw
} finally {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'package-result.json') -Encoding utf8
}
Write-Output "Native Windows package passed: ZIP=$($result.zipBytes), installed=$($result.installedBytes), SHA256=$($result.sha256)"
