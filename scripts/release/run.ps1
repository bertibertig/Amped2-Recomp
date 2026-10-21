# Amped2-Recomp launcher (Windows PowerShell 5.1 or PowerShell 7): starts amped2_recomp.exe from this folder
# with the default settings - the PowerShell counterpart of run.sh (Linux) and run.bat.
#   Right-click run.ps1 -> "Run with PowerShell", or in a PowerShell console in this folder:
#     powershell -ExecutionPolicy Bypass -File .\run.ps1
# The game's console output goes to run.log next to this file (attach it to bug reports); on an error the last
# lines are shown here too. Arguments are passed on to the game.
# Options: set them before starting, e.g.
#     $env:AMPED2_SKIP_INTRO = '1'; .\run.ps1
# Defaults (only set when not set already) - the same as run.bat:
#   RECOMP_WIN_PLACEHOLDER=1  reserve the whole 4 GB guest window up front: without it Wine/Proton cannot map the
#                             GPU's tiled memory window (0xF0000000) and the game crashes at the first movie frame
#   RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=1 RECOMP_AC97_READY=1 RECOMP_HEAP_RECLAIM=1 RECOMP_FLIP_VSYNC=1
#   RECOMP_PB_EXEC=1 RECOMP_FB_WINDOW=1 RECOMP_USB=1 RECOMP_KEYBOARD=1 AMPED2_SKIP_LOGOS=1
#   (what they do: run.sh in the source repository, docs/notes.md)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here 'amped2_recomp.exe'
$log = Join-Path $here 'run.log'
$gameFiles = Join-Path $here 'game_files'
$interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected

function Stop-WithPause([int]$code) {
  if ($interactive) { Read-Host 'Press Enter to close' | Out-Null }
  exit $code
}

if (-not (Test-Path -LiteralPath (Join-Path $gameFiles 'default.xbe'))) {
  Write-Host 'game_files\default.xbe missing: put the extracted contents of your Amped 2 Xbox disc (PAL) into'
  Write-Host "$gameFiles\  - see README.txt, section 1."
  Stop-WithPause 1
}
if (-not (Test-Path -LiteralPath $exe)) {
  Write-Host "amped2_recomp.exe missing next to run.ps1 ($here) - unpack the whole package."
  Stop-WithPause 1
}

$defaults = [ordered]@{
  RECOMP_WIN_PLACEHOLDER = '1'
  RECOMP_TITLE_KEVENTS   = '1'
  RECOMP_VBLANK          = '1'
  RECOMP_AC97_READY      = '1'
  RECOMP_HEAP_RECLAIM    = '1'
  RECOMP_FLIP_VSYNC      = '1'
  RECOMP_PB_EXEC         = '1'
  RECOMP_FB_WINDOW       = '1'
  RECOMP_USB             = '1'
  RECOMP_KEYBOARD        = '1'
  AMPED2_SKIP_LOGOS      = '1'
}
foreach ($name in $defaults.Keys) {
  if (-not [Environment]::GetEnvironmentVariable($name)) {
    [Environment]::SetEnvironmentVariable($name, $defaults[$name])
  }
}

Set-Location -LiteralPath $here
Write-Host 'Starting Amped 2 (output: run.log) ...'
# stdout and stderr into run.log as UTF-8 (Tee-Object/Out-File would write UTF-16 in PowerShell 5.1). Native
# stderr lines arrive as error records under 2>&1, so do not let them stop the script.
$ErrorActionPreference = 'Continue'
$writer = New-Object System.IO.StreamWriter($log, $false, (New-Object System.Text.UTF8Encoding($false)))
$writer.AutoFlush = $true
try {
  & $exe @args 2>&1 | ForEach-Object { $writer.WriteLine("$_") }
  $rc = $LASTEXITCODE
} finally {
  $writer.Dispose()
}

if ($rc -ne 0) {
  Write-Host "amped2_recomp.exe ended with exit code $rc - last lines of run.log:"
  Get-Content -LiteralPath $log -Tail 15 | ForEach-Object { Write-Host "  $_" }
  Stop-WithPause $rc
}
exit 0
