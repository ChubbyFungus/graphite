param(
    [string]$BuildDir = "build\engine-slice",
    [string]$PenDiagnosticsCsv = "",
    [string]$MaterialCaptureManifestCsv = "",
    [string]$MaterialCalibrationCsv = "",
    [string]$ExpectedCudaToolkitMajorMinor = "",
    [switch]$StrictPen,
    [switch]$RequireCompleteEvidence
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$appPath = Join-Path $repoRoot "$BuildDir\native\Graphite.EngineSlice\Graphite.NativeApp.exe"

function Invoke-VcCommand {
    param([string]$Command)
    cmd /c "call ""$vcvars"" && $Command"
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Command"
    }
}

function Get-NvccToolkitVersion {
    param([string]$NvccPath = "nvcc")
    $nvccOutput = & $NvccPath --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "$NvccPath --version failed. CUDA toolkit compiler is not available."
    }
    $joined = ($nvccOutput | Out-String)
    if ($joined -match "release\s+([0-9]+\.[0-9]+)") {
        return $Matches[1]
    }
    throw "Could not parse nvcc toolkit version from: $joined"
}

function Get-CMakeCudaCompiler {
    param([string]$CachePath)
    if (-not (Test-Path $CachePath)) {
        return ""
    }
    $line = Get-Content $CachePath | Where-Object { $_ -like "CMAKE_CUDA_COMPILER:STRING=*" } | Select-Object -First 1
    if (-not $line) {
        return ""
    }
    return ($line -replace "^CMAKE_CUDA_COMPILER:STRING=", "")
}

Push-Location $repoRoot
try {
    Write-Host "== Check CUDA toolkit compiler =="
    $cmakeCudaCompiler = Get-CMakeCudaCompiler (Join-Path $repoRoot "$BuildDir\CMakeCache.txt")
    $nvccPath = if ($cmakeCudaCompiler) { $cmakeCudaCompiler } else { "nvcc" }
    $nvccVersion = Get-NvccToolkitVersion $nvccPath
    Write-Host "nvcc toolkit=$nvccVersion compiler=$nvccPath"
    if ($ExpectedCudaToolkitMajorMinor -and $nvccVersion -ne $ExpectedCudaToolkitMajorMinor) {
        throw "CUDA toolkit mismatch: expected nvcc $ExpectedCudaToolkitMajorMinor but found $nvccVersion. nvidia-smi reports driver capability, not the compiler toolkit."
    }

    Write-Host "== Build native slice =="
    Invoke-VcCommand "cmake --build ""$BuildDir"" --config Release"

    Write-Host "== Run native tests =="
    Invoke-VcCommand "ctest --test-dir ""$BuildDir"" --output-on-failure"

    Write-Host "== Smoke launch native app =="
    Get-Process Graphite.NativeApp -ErrorAction SilentlyContinue | Stop-Process -Force
    $process = Start-Process -FilePath $appPath -PassThru
    Start-Sleep -Seconds 3
    $started = -not $process.HasExited
    if ($started) {
        $null = $process.CloseMainWindow()
        Start-Sleep -Milliseconds 500
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
    Write-Host "started=$started exitCode=$($process.ExitCode)"
    if (-not $started) {
        throw "Native app did not remain alive for smoke window."
    }

    if ($PenDiagnosticsCsv) {
        Write-Host "== Analyze pen diagnostics =="
        $args = @("tools\analyze_pen_input_diagnostics.py", $PenDiagnosticsCsv)
        if ($StrictPen) {
            $args += @(
                "--require-pressure-variation",
                "--require-tilt",
                "--require-rotation",
                "--require-eraser",
                "--require-barrel"
            )
        }
        python @args
        if ($LASTEXITCODE -ne 0) {
            throw "Pen diagnostics analysis failed."
        }
    }

    if ($MaterialCaptureManifestCsv) {
        Write-Host "== Check material capture manifest =="
        python tools\check_material_calibration_manifest.py $MaterialCaptureManifestCsv --require-ready --require-files
        if ($LASTEXITCODE -ne 0) {
            throw "Material capture manifest check failed."
        }
    }

    if ($MaterialCalibrationCsv) {
        Write-Host "== Analyze material calibration =="
        python tools\analyze_material_calibration.py $MaterialCalibrationCsv --require-one-session
        if ($LASTEXITCODE -ne 0) {
            throw "Material calibration analysis failed."
        }
    }

    Write-Host "== Check evidence manifest =="
    $manifestArgs = @("tools\check_validation_evidence_manifest.py", "docs\native_validation_evidence_manifest.md")
    if (-not $RequireCompleteEvidence) {
        $manifestArgs += "--allow-missing"
    }
    python @manifestArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Validation evidence manifest is incomplete."
    }

    Write-Host "VERIFY_NATIVE_SLICE: PASS"
}
finally {
    Pop-Location
}
