param([switch]$Run, [switch]$Test, [switch]$Preview)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
python (Join-Path $PSScriptRoot 'compile_project.py')
if ($LASTEXITCODE -ne 0) { throw 'Project compilation failed' }
python (Join-Path $PSScriptRoot 'rasterize_assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Asset preparation failed (requires PyMuPDF)' }
$sha = [System.Security.Cryptography.SHA256]::Create()
$suffix = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($repoRoot)))).Replace('-', '').Substring(0, 12)
$buildRoot = Join-Path ([IO.Path]::GetTempPath()) "battle-bound-dx-$suffix"
cmake -S $repoRoot -B $buildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $buildRoot --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'C++26 build failed' }
python (Join-Path $PSScriptRoot 'package_exe.py') (Join-Path $buildRoot 'battle_bound_dx.exe')
if ($LASTEXITCODE -ne 0) { throw 'Single-file packaging failed' }
python (Join-Path $PSScriptRoot 'check_portable.py') (Join-Path $buildRoot 'battle_bound_dx.exe')
if ($LASTEXITCODE -ne 0) { throw 'Executable has non-system DLL dependencies' }
if ($Test) {
    ctest --test-dir $buildRoot --verbose
    if ($LASTEXITCODE -ne 0) { throw 'Native checks failed' }
    $isolatedRoot = Join-Path $buildRoot 'isolated'
    New-Item -ItemType Directory -Force $isolatedRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildRoot 'battle_bound_dx.exe') -Destination (Join-Path $isolatedRoot 'battle_bound_dx.exe') -Force
    $savedPath = $env:PATH
    Push-Location $isolatedRoot
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $portableCheck = Start-Process -FilePath (Join-Path $isolatedRoot 'battle_bound_dx.exe') -ArgumentList '--headless', '--exercise', '--ticks', '300' -Wait -PassThru -WindowStyle Hidden
        if ($portableCheck.ExitCode -ne 0) { throw "Isolated single-file test failed: $($portableCheck.ExitCode)" }
        Write-Host 'Isolated EXE passed: empty working folder, no assets or toolchain on PATH.'
    } finally {
        $env:PATH = $savedPath
        Pop-Location
    }
}
if ($Preview) {
    python (Join-Path $PSScriptRoot 'workspace_budget.py') --reserve 5000000
    if ($LASTEXITCODE -ne 0) { throw 'Preview exceeds workspace budget' }
    $previewCheck = Start-Process -FilePath (Join-Path $buildRoot 'battle_bound_dx.exe') -ArgumentList '--root', ('"{0}"' -f $repoRoot), '--game', '--render-smoke', '--ticks', '90' -Wait -PassThru -WindowStyle Hidden
    if ($previewCheck.ExitCode -ne 0) { throw 'Native render check failed' }
    python (Join-Path $PSScriptRoot 'finalize_preview.py')
    if ($LASTEXITCODE -ne 0) { throw 'Framebuffer preview conversion failed' }
}
python (Join-Path $PSScriptRoot 'workspace_budget.py')
if ($LASTEXITCODE -ne 0) { throw 'Workspace size check failed' }
python (Join-Path $PSScriptRoot 'workspace_budget.py') --reserve 100000000
if ($LASTEXITCODE -ne 0) { throw 'Binary exceeds workspace budget' }
$binaryRoot = Join-Path $repoRoot 'bin'
New-Item -ItemType Directory -Force $binaryRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'battle_bound_dx.exe') -Destination (Join-Path $binaryRoot 'battle_bound_dx.exe') -Force
python (Join-Path $PSScriptRoot 'workspace_budget.py')
if ($LASTEXITCODE -ne 0) { throw 'Workspace size check failed' }
Write-Host "Standalone Windows executable: $binaryRoot/battle_bound_dx.exe"
if ($Run) { & (Join-Path $buildRoot 'battle_bound_dx.exe') --root $repoRoot }
