[CmdletBinding()]
param(
  [ValidateSet('Build', 'Rebuild', 'Clean')]
  [string] $Action = 'Build',

  [ValidateRange(1, 256)]
  [int] $Jobs = [Environment]::ProcessorCount,

  [string] $ToolchainDirectory = '',

  [switch] $UpdateToolchain
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Msys2Release = '2026-06-11'
$Msys2Archive = 'msys2-base-x86_64-20260611.sfx.exe'
$Msys2ArchiveSha256 = 'c105946e64e08f099ac0e4647461ce762b95333ad211777666476a9a41451d65'
$Msys2ArchiveUrl = "https://github.com/msys2/msys2-installer/releases/download/$Msys2Release/$Msys2Archive"
$ToolchainStampVersion = 'hashcat-shooter-windows-toolchain-v4'
$RequiredPackages = @(
  'git'
  'mingw-w64-x86_64-clang'
  'mingw-w64-x86_64-gcc'
  'mingw-w64-x86_64-lld'
  'mingw-w64-x86_64-llvm'
  'mingw-w64-x86_64-make'
  'mingw-w64-x86_64-openssl'
  'mingw-w64-x86_64-libiconv'
  'mingw-w64-x86_64-rustup'
)

function Invoke-Checked {
  param(
    [Parameter(Mandatory = $true)]
    [string] $FilePath,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Arguments
  )

  & $FilePath @Arguments

  if ($LASTEXITCODE -ne 0) {
    throw ("Command failed with exit code {0}: {1} {2}" -f $LASTEXITCODE, $FilePath, ($Arguments -join ' '))
  }
}

function Get-Sha256 {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path
  )

  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-Msys {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Command
  )

  Invoke-Checked $script:BashPath '-lc' $Command
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
  throw 'build-windows.ps1 must be run on Windows.'
}

if (-not [Environment]::Is64BitOperatingSystem) {
  throw 'The Windows build requires a 64-bit operating system.'
}

$RepoRoot = [IO.Path]::GetFullPath($PSScriptRoot)

if ([string]::IsNullOrWhiteSpace($ToolchainDirectory)) {
  $ToolsRoot = Join-Path $RepoRoot '.build-tools'
}
elseif ([IO.Path]::IsPathRooted($ToolchainDirectory)) {
  $ToolsRoot = [IO.Path]::GetFullPath($ToolchainDirectory)
}
else {
  $ToolsRoot = [IO.Path]::GetFullPath((Join-Path $RepoRoot $ToolchainDirectory))
}

$DownloadsRoot = Join-Path $ToolsRoot 'downloads'
$MsysRoot = Join-Path $ToolsRoot 'msys64'
$script:BashPath = Join-Path $MsysRoot 'usr\bin\bash.exe'
$MingwBin = Join-Path $MsysRoot 'mingw64\bin'
$MakePath = Join-Path $MingwBin 'mingw32-make.exe'
$GccPath = Join-Path $MingwBin 'gcc.exe'
$ClangPath = Join-Path $MingwBin 'clang.exe'
$LibClangLibrary = Join-Path $MingwBin 'libclang.dll'
$LldPath = Join-Path $MingwBin 'ld.lld.exe'
$RustupPath = Join-Path $MingwBin 'rustup.exe'
$RustToolchainBin = Join-Path $MsysRoot 'var\lib\hashcat-shooter\rustup\toolchains\stable-x86_64-pc-windows-gnu\bin'
$CargoPath = Join-Path $RustToolchainBin 'cargo.exe'
$RustcPath = Join-Path $RustToolchainBin 'rustc.exe'
$OpenSslHeader = Join-Path $MsysRoot 'mingw64\include\openssl\opensslv.h'
$IconvLibrary = Join-Path $MsysRoot 'mingw64\lib\libiconv.a'
$StampPath = Join-Path $ToolsRoot 'toolchain.stamp'
$PackageVersionsPath = Join-Path $ToolsRoot 'toolchain-packages.txt'

New-Item -ItemType Directory -Force -Path $DownloadsRoot | Out-Null

if (-not (Test-Path -LiteralPath $script:BashPath -PathType Leaf)) {
  $ArchivePath = Join-Path $DownloadsRoot $Msys2Archive
  $ArchiveIsValid = (Test-Path -LiteralPath $ArchivePath -PathType Leaf) -and
    ((Get-Sha256 -Path $ArchivePath) -eq $Msys2ArchiveSha256)

  if (-not $ArchiveIsValid) {
    $PartialPath = "$ArchivePath.partial"
    Remove-Item -LiteralPath $PartialPath -Force -ErrorAction SilentlyContinue

    Write-Host "Downloading the local MSYS2 base ($Msys2Release)..."

    if (-not ([Net.ServicePointManager]::SecurityProtocol -band [Net.SecurityProtocolType]::Tls12)) {
      [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    }

    Invoke-WebRequest -Uri $Msys2ArchiveUrl -OutFile $PartialPath -UseBasicParsing

    $DownloadedSha256 = Get-Sha256 -Path $PartialPath

    if ($DownloadedSha256 -ne $Msys2ArchiveSha256) {
      Remove-Item -LiteralPath $PartialPath -Force
      throw "MSYS2 archive checksum mismatch. Expected $Msys2ArchiveSha256 but received $DownloadedSha256."
    }

    Move-Item -LiteralPath $PartialPath -Destination $ArchivePath -Force
  }

  Write-Host "Extracting MSYS2 under $ToolsRoot..."
  Invoke-Checked $ArchivePath '-y' "-o$ToolsRoot"

  if (-not (Test-Path -LiteralPath $script:BashPath -PathType Leaf)) {
    throw "MSYS2 extraction did not create $script:BashPath."
  }
}

$RequiredFiles = @(
  $MakePath
  $GccPath
  $ClangPath
  $LibClangLibrary
  $LldPath
  $RustupPath
  $CargoPath
  $RustcPath
  $OpenSslHeader
  $IconvLibrary
)
$MissingRequiredFiles = @($RequiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
$StampMatches = (Test-Path -LiteralPath $StampPath -PathType Leaf) -and
  ((Get-Content -LiteralPath $StampPath -Raw).Trim() -eq $ToolchainStampVersion)

if ($UpdateToolchain -or -not $StampMatches -or $MissingRequiredFiles.Count -gt 0) {
  Write-Host 'Preparing the repo-local compiler and build dependencies...'

  # MSYS2 is rolling-release and does not support partial upgrades. Run the
  # full upgrade twice because an msys2-runtime update can end the first shell;
  # the second invocation then completes in a fresh bash process.
  Invoke-Msys 'pacman -Syu --noconfirm --disable-download-timeout'
  Invoke-Msys 'pacman -Syu --noconfirm --disable-download-timeout'
  Invoke-Msys ("pacman -S --needed --noconfirm --disable-download-timeout " + ($RequiredPackages -join ' '))

  # Keep the Rust toolchain in the repo-local MSYS2 tree and select the GNU
  # host explicitly. A plain `rustup default stable` selects MSVC on Windows,
  # which cannot link the MinGW bridge DLLs built by this project.
  Invoke-Msys 'export PATH=/mingw64/bin:/usr/bin:$PATH; export RUSTUP_HOME=/var/lib/hashcat-shooter/rustup; export CARGO_HOME=/var/lib/hashcat-shooter/cargo; rustup toolchain install stable-x86_64-pc-windows-gnu --profile minimal; rustup default stable-x86_64-pc-windows-gnu; rustup target add x86_64-pc-windows-gnu --toolchain stable-x86_64-pc-windows-gnu'

  $MissingRequiredFiles = @($RequiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })

  if ($MissingRequiredFiles.Count -gt 0) {
    throw "The local toolchain is incomplete. Missing: $($MissingRequiredFiles -join ', ')"
  }

  $PackageQuery = 'pacman -Q ' + ($RequiredPackages -join ' ')
  $PackageVersions = & $script:BashPath '-lc' $PackageQuery

  if ($LASTEXITCODE -eq 0) {
    Set-Content -LiteralPath $PackageVersionsPath -Value $PackageVersions -Encoding UTF8
  }

  Set-Content -LiteralPath $StampPath -Value $ToolchainStampVersion -Encoding ASCII
}

$MakeCommand = "/mingw64/bin/mingw32-make.exe PRODUCTION=1 ENABLE_LTO=0 RUST_CARGO=/var/lib/hashcat-shooter/rustup/toolchains/stable-x86_64-pc-windows-gnu/bin/cargo.exe RUST_RUSTUP=/mingw64/bin/rustup.exe -j$Jobs"
$PathPrefix = 'export PATH=/mingw64/bin:/var/lib/hashcat-shooter/rustup/toolchains/stable-x86_64-pc-windows-gnu/bin:/usr/bin:$PATH; export LIBCLANG_PATH=/mingw64/bin; export RUSTUP_HOME=/var/lib/hashcat-shooter/rustup; export CARGO_HOME=/var/lib/hashcat-shooter/cargo; '

switch ($Action) {
  'Clean' {
    $BuildCommand = $PathPrefix + '/mingw64/bin/mingw32-make.exe clean'
  }
  'Rebuild' {
    $BuildCommand = $PathPrefix + '/mingw64/bin/mingw32-make.exe clean && ' + $MakeCommand
  }
  default {
    $BuildCommand = $PathPrefix + $MakeCommand
  }
}

Write-Host "Running the $Action action with $Jobs parallel jobs..."

Push-Location -LiteralPath $RepoRoot

try {
  Invoke-Checked $script:BashPath '-c' $BuildCommand
}
finally {
  Pop-Location
}

if ($Action -eq 'Clean') {
  Write-Host 'Clean completed.'
  exit 0
}

$HashcatPath = Join-Path $RepoRoot 'hashcat.exe'

if (-not (Test-Path -LiteralPath $HashcatPath -PathType Leaf)) {
  throw "The build completed without producing $HashcatPath."
}

$RuntimeDlls = @(
  'libiconv-2.dll'
  'libgcc_s_seh-1.dll'
  'libstdc++-6.dll'
  'libwinpthread-1.dll'
)

foreach ($RuntimeDll in $RuntimeDlls) {
  $SourceDll = Join-Path $MingwBin $RuntimeDll
  $DestinationDll = Join-Path $RepoRoot $RuntimeDll

  if (Test-Path -LiteralPath $SourceDll -PathType Leaf) {
    $DestinationMatches = (Test-Path -LiteralPath $DestinationDll -PathType Leaf) -and
      ((Get-Sha256 -Path $DestinationDll) -eq (Get-Sha256 -Path $SourceDll))

    if (-not $DestinationMatches) {
      Copy-Item -LiteralPath $SourceDll -Destination $DestinationDll -Force
    }
  }
}

Push-Location $RepoRoot

try {
  $Version = (& $HashcatPath --version).Trim()

  if ($LASTEXITCODE -ne 0) {
    throw "hashcat.exe --version failed with exit code $LASTEXITCODE."
  }
}
finally {
  Pop-Location
}

Write-Host ''
Write-Host "Build succeeded: $HashcatPath"
Write-Host "Version: $Version"
Write-Host "Local toolchain: $MsysRoot"
Write-Host 'The .build-tools cache and generated binaries are ignored by Git.'
