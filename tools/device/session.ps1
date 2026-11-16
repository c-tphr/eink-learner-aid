<#
.SYNOPSIS
  The one script the owner runs for the Picco device session.

.DESCRIPTION
  A thin launcher. All decisions (what may be written, where, and when) are
  made by the Python tool "piccoflash" in the folder next to this script;
  this script only prepares a private, pinned Python for it and runs it.

  First run: downloads the official CPython 3.13.17 for Windows x64 (the
  Python Software Foundation's "python" package on nuget.org), checks its
  SHA-256 against the value pinned below, unpacks it into .\runtime, and
  installs the pinned esptool and pyserial with hash checking
  (pip --require-hashes, requirements*.txt). Nothing is installed system-wide,
  nothing needs admin rights, no system setting or PATH is changed.
  Delete the .\runtime folder to remove it all.

  How to run it (no policy change needed; the bypass applies to this one run):
      powershell -ExecutionPolicy Bypass -File .\session.ps1 identify
  or, with PowerShell 7:
      pwsh -ExecutionPolicy Bypass -File .\session.ps1 identify

.EXAMPLE
  .\session.ps1 setup                 # prepare the private Python only (can be done days before)
  .\session.ps1 identify              # read-only checks
  .\session.ps1 backup                # read-only: two full reads, keep if identical
  .\session.ps1 flash -DryRun         # show exactly what would be written; writes nothing
  .\session.ps1 flash                 # the one destructive step (asks for a typed phrase)
  .\session.ps1 monitor               # record serial output until Ctrl+C
  .\session.ps1 restore "work\picco-backup-....bin"
  .\session.ps1 identify -Port COM5   # name the port if more than one device is connected
  .\session.ps1 summary              # one text file, safe to post, from the work folder

.NOTES
  Exit codes are piccoflash's: 0 OK, 1 STOP (nothing written), 3 already
  converted, 4 device not reached (nothing written), 5 flash failed part-way
  (run restore), 6 restore failed (run restore again). 10-12 are this script's
  own set-up failures (nothing touched the device).
#>
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '',
    Justification = 'Interactive console script: messages are for the person at the keyboard, not the pipeline.')]
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('setup', 'identify', 'backup', 'plan', 'flash', 'monitor', 'restore', 'ports', 'summary')]
    [string]$Command,

    [Parameter(Position = 1)]
    [string]$BackupFile,              # restore only

    [switch]$DryRun,                  # print every step; never opens the serial port
    [string]$Port,                    # e.g. COM5; default: the only Espressif USB port
    [string]$Plan,                    # default: .\kit\flash-plan.json
    [string]$WorkDir,                 # default: .\work
    [switch]$Reset,                   # monitor only: pulse reset once at the start

    # For testing the script itself (not for the session): use this Python
    # instead of the private one, and skip the set-up.
    [Parameter(DontShow)]
    [string]$PythonExe
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with the progress bar in 5.1

# ---- Pinned downloads ------------------------------------------------------
# CPython 3.13.17 x64, nuget.org package "python" (publisher: Python Software
# Foundation). SHA-256 measured on the downloaded file; its SHA-512 matched the
# packageHash in nuget.org's catalog entry for python 3.13.17.
$PinnedPythonVersion = '3.13.17'
$PythonPackageUrl    = 'https://api.nuget.org/v3-flatcontainer/python/3.13.17/python.3.13.17.nupkg'
$PythonPackageSha256 = 'dce409744b088f94c6e37ba41f8100c384c27e029e843237364c602d30d475e5'
$PinnedEsptool       = '5.6.0'

$Here    = $PSScriptRoot                                   # works from a path with spaces
$Runtime = Join-Path $Here 'runtime'
$PyHome  = Join-Path $Runtime 'py313'
$PyExe   = Join-Path (Join-Path $PyHome 'tools') 'python.exe'
$Marker  = Join-Path $PyHome 'piccoflash-runtime.txt'
$script:PythonExit = 0

function Write-Step([string]$Text) { Write-Host "[session] $Text" }

function Exit-Session([string]$Text, [int]$Code) {
    Write-Host ""
    Write-Host "[session] STOPPED: $Text" -ForegroundColor Yellow
    Write-Host "[session] Nothing on the Picco was touched."
    exit $Code
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

# What the runtime was built from; if any of it changes, the runtime is rebuilt.
function Get-RuntimeStamp {
    $parts = @("python=$PythonPackageSha256")
    foreach ($f in 'requirements-build.txt', 'requirements.txt') {
        $parts += "$f=" + (Get-Sha256 (Join-Path $Here $f))
    }
    return ($parts -join "`n")
}

# Runs Python with this folder as the working directory (so "-m piccoflash" finds
# the package beside this script). Called as a plain statement, never assigned,
# so Python talks to the console directly: its prompts show at once and it reads
# the keyboard. The exit code is left in $script:PythonExit.
function Invoke-Python([string]$Py, [string[]]$Arguments) {
    Push-Location -LiteralPath $Here
    try {
        & $Py @Arguments
        $script:PythonExit = $LASTEXITCODE
    } catch {
        $script:PythonExit = 12
        Write-Host "[session] Could not start Python: $($_.Exception.Message)"
        Write-Host "[session] If an antivirus program blocked python.exe, allow it for this folder and run again."
    } finally {
        Pop-Location
    }
}

function Test-Runtime {
    if (-not (Test-Path -LiteralPath $PyExe) -or -not (Test-Path -LiteralPath $Marker)) { return $false }
    return ((Get-Content -LiteralPath $Marker -Raw).Trim() -eq (Get-RuntimeStamp).Trim())
}

function Initialize-Runtime {
    if (Test-Runtime) { return }
    $arch = $env:PROCESSOR_ARCHITECTURE
    if ($env:PROCESSOR_ARCHITEW6432) { $arch = $env:PROCESSOR_ARCHITEW6432 }
    if ($arch -ne 'AMD64' -and $arch -ne 'ARM64') {
        Exit-Session "this PC is $arch; the pinned Python needs 64-bit Windows." 10
    }
    if ($arch -eq 'ARM64') { Write-Step "ARM64 Windows: using the x64 Python (Windows 11 runs it under emulation)." }

    Write-Step "First run: setting up a private Python $PinnedPythonVersion in $Runtime"
    New-Item -ItemType Directory -Force -Path $Runtime | Out-Null
    $pkg = Join-Path $Runtime "python.$PinnedPythonVersion.nupkg"
    if (-not (Test-Path -LiteralPath $pkg) -or (Get-Sha256 $pkg) -ne $PythonPackageSha256) {
        Write-Step "Downloading $PythonPackageUrl"
        # TLS 1.2 for Windows PowerShell 5.1; affects this process only.
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $PythonPackageUrl -OutFile $pkg
        } catch {
            Exit-Session ("could not download Python ($($_.Exception.Message)). Check the internet connection; " +
                          "a company network may block api.nuget.org or pypi.org.") 11
        }
    }
    $got = Get-Sha256 $pkg
    if ($got -ne $PythonPackageSha256) {
        Remove-Item -LiteralPath $pkg -Force
        Exit-Session "the downloaded Python package has SHA-256 $got, expected $PythonPackageSha256. It was deleted; do not use it." 11
    }
    Write-Step "Python package SHA-256 verified: $got"

    # Unpack into a temporary folder, then move it into place, so a half-done
    # unpack is never mistaken for a working runtime.
    $tmp = Join-Path $Runtime 'unpacking'
    foreach ($d in $tmp, $PyHome) { if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force } }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($pkg, $tmp)
    Move-Item -LiteralPath $tmp -Destination $PyHome
    if (-not (Test-Path -LiteralPath $PyExe)) { Exit-Session "the Python package did not contain tools\python.exe." 11 }
    if ($PSVersionTable.PSEdition -eq 'Desktop' -or $IsWindows) {
        $sig = Get-AuthenticodeSignature -LiteralPath $PyExe
        Write-Step "python.exe signature: $($sig.Status), $($sig.SignerCertificate.Subject)"
    }

    # pip comes with this Python. --isolated ignores any pip configuration on the
    # PC; --require-hashes refuses any file whose SHA-256 is not listed; --no-deps
    # installs exactly the listed packages. esptool is published only as source,
    # so it is built here with the pinned setuptools (no other downloads).
    $pip = @('-E', '-s', '-m', 'pip', '--isolated', 'install', '--no-cache-dir', '--disable-pip-version-check',
             '--no-warn-script-location', '--require-hashes', '--no-deps')
    Write-Step "Installing pinned setuptools (to build esptool)"
    Invoke-Python $PyExe ($pip + @('--only-binary', ':all:', '-r', (Join-Path $Here 'requirements-build.txt')))
    if ($script:PythonExit -ne 0) { Exit-Session "pip could not install requirements-build.txt (exit $script:PythonExit)." 12 }
    Write-Step "Installing pinned esptool $PinnedEsptool, pyserial and their dependencies"
    Invoke-Python $PyExe ($pip + @('--no-build-isolation', '--only-binary', ':all:', '--no-binary', 'esptool',
                                   '-r', (Join-Path $Here 'requirements.txt')))
    if ($script:PythonExit -ne 0) { Exit-Session "pip could not install requirements.txt (exit $script:PythonExit)." 12 }
    $v = & $PyExe -E -s -c 'import esptool, serial; print(esptool.__version__)'
    if ($LASTEXITCODE -ne 0 -or "$v".Trim() -ne $PinnedEsptool) { Exit-Session "esptool reports version '$v', expected $PinnedEsptool." 12 }

    Set-Content -LiteralPath $Marker -Value (Get-RuntimeStamp) -Encoding ASCII
    Write-Step "Private Python ready."
}

function Invoke-Session {
    param([string]$Command, [string]$BackupFile, [switch]$DryRun, [string]$Port, [string]$Plan,
          [string]$WorkDir, [switch]$Reset, [string]$PythonExe)
    if (-not $Command) {
        Get-Help $PSCommandPath -Detailed | Out-Host
        exit 2
    }
    if ($Command -eq 'restore' -and -not $BackupFile) {
        Write-Host 'restore needs the backup file, e.g.  .\session.ps1 restore "work\picco-backup-....bin"'
        exit 2
    }
    if ($Command -eq 'restore' -and -not (Test-Path -LiteralPath $BackupFile -PathType Leaf)) {
        Write-Host "restore: there is no file at '$BackupFile'."
        exit 2
    }

    if ($PythonExe) { $py = $PythonExe } else { Initialize-Runtime; $py = $PyExe }
    if ($Command -eq 'setup') {
        Invoke-Python $py @('-E', '-s', '-m', 'piccoflash', '--version')
        exit $script:PythonExit
    }

    # Paths are passed without a trailing backslash: "C:\x y\" would end the
    # quoted argument early under Windows' argument rules.
    if (-not $Plan)    { $Plan = Join-Path (Join-Path $Here 'kit') 'flash-plan.json' }
    if (-not $WorkDir) { $WorkDir = Join-Path $Here 'work' }
    $Plan = $Plan.TrimEnd('\', '/')
    $WorkDir = $WorkDir.TrimEnd('\', '/')

    $argv = @('-E', '-s', '-m', 'piccoflash', $Command, '--work-dir', $WorkDir, '--launcher', '.\session.ps1')
    if ($Command -eq 'plan' -or $Command -eq 'flash') { $argv += @('--plan', $Plan) }
    if ($Port)   { $argv += @('--port', $Port) }
    if ($DryRun) { $argv += '--dry-run' }
    if ($Reset -and $Command -eq 'monitor') { $argv += '--reset' }
    if ($Command -eq 'restore') { $argv += (Resolve-Path -LiteralPath $BackupFile).Path }

    Invoke-Python $py $argv
    $code = $script:PythonExit
    if ($code -eq 4) {
        Write-Host ""
        Write-Host "[session] Serial ports Windows sees right now:"
        Invoke-Python $py @('-E', '-s', '-m', 'piccoflash', 'ports')
    }
    exit $code
}

# Dot-sourcing (". .\session.ps1") only loads the functions, for testing.
if ($MyInvocation.InvocationName -ne '.') {
    Invoke-Session -Command $Command -BackupFile $BackupFile -DryRun:$DryRun -Port $Port -Plan $Plan `
        -WorkDir $WorkDir -Reset:$Reset -PythonExe $PythonExe
}
