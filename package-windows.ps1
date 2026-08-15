[CmdletBinding()]
param(
  [ValidateSet('Build', 'Rebuild', 'Skip')]
  [string] $BuildAction = 'Rebuild',

  [ValidateRange(1, 256)]
  [int] $Jobs = [Environment]::ProcessorCount,

  [string] $ToolchainDirectory = '',

  [string] $OutputDirectory = 'dist',

  [string] $SevenZipPath = '',

  [string] $ExpectedVersion = '',

  [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

function Resolve-SevenZip {
  param(
    [Parameter(Mandatory = $true)]
    [string] $RepoRoot,

    [string] $RequestedPath,

    [string] $RequestedToolchainDirectory
  )

  $Candidates = New-Object System.Collections.Generic.List[string]

  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    $Candidates.Add([IO.Path]::GetFullPath($RequestedPath))
  }

  if (-not [string]::IsNullOrWhiteSpace($RequestedToolchainDirectory)) {
    $ToolRoot = if ([IO.Path]::IsPathRooted($RequestedToolchainDirectory)) {
      [IO.Path]::GetFullPath($RequestedToolchainDirectory)
    }
    else {
      [IO.Path]::GetFullPath((Join-Path $RepoRoot $RequestedToolchainDirectory))
    }

    $Candidates.Add((Join-Path $ToolRoot 'msys64\mingw64\bin\7z.exe'))
    $Candidates.Add((Join-Path $ToolRoot 'mingw64\bin\7z.exe'))
  }

  $Candidates.Add((Join-Path $RepoRoot '.build-tools\msys64\mingw64\bin\7z.exe'))

  $Command = Get-Command 7z.exe -ErrorAction SilentlyContinue

  if ($null -ne $Command) {
    $Candidates.Add($Command.Source)
  }

  foreach ($Candidate in $Candidates) {
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
      return [IO.Path]::GetFullPath($Candidate)
    }
  }

  throw '7z.exe was not found. Run .\build-windows.ps1 once, install 7-Zip, or pass -SevenZipPath.'
}

function Copy-BuiltFiles {
  param(
    [Parameter(Mandatory = $true)]
    [string] $SourceRoot,

    [Parameter(Mandatory = $true)]
    [string] $DestinationRoot,

    [Parameter(Mandatory = $true)]
    [string] $RelativeDirectory
  )

  $SourceDirectory = Join-Path $SourceRoot $RelativeDirectory

  foreach ($File in Get-ChildItem -LiteralPath $SourceDirectory -Filter '*.dll' -File -Recurse) {
    $RelativeFile = $File.FullName.Substring($SourceRoot.Length).TrimStart('\', '/')
    $DestinationFile = Join-Path $DestinationRoot $RelativeFile
    $DestinationDirectory = Split-Path -Parent $DestinationFile

    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $File.FullName -Destination $DestinationFile -Force
  }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
  throw 'package-windows.ps1 must be run on Windows.'
}

if (-not [Environment]::Is64BitOperatingSystem) {
  throw 'The Windows package requires a 64-bit operating system.'
}

$RepoRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$GitPath = (Get-Command git.exe -ErrorAction Stop).Source

Push-Location -LiteralPath $RepoRoot

try {
  $InsideWorkTree = (& $GitPath rev-parse --is-inside-work-tree).Trim()

  if (($LASTEXITCODE -ne 0) -or ($InsideWorkTree -ne 'true')) {
    throw "$RepoRoot is not a Git worktree."
  }

  $TrackedChanges = @(& $GitPath status --porcelain --untracked-files=no)

  if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the Git worktree.'
  }

  if ($TrackedChanges.Count -gt 0) {
    throw 'Tracked files are modified. Commit them before creating a source/binary release package.'
  }

  if ($BuildAction -ne 'Skip') {
    $BuildParameters = @{
      Action = $BuildAction
      Jobs   = $Jobs
    }

    if (-not [string]::IsNullOrWhiteSpace($ToolchainDirectory)) {
      $BuildParameters.ToolchainDirectory = $ToolchainDirectory
    }

    & (Join-Path $RepoRoot 'build-windows.ps1') @BuildParameters
  }

  $HashcatPath = Join-Path $RepoRoot 'hashcat.exe'

  if (-not (Test-Path -LiteralPath $HashcatPath -PathType Leaf)) {
    throw 'hashcat.exe is missing. Run the build or omit -BuildAction Skip.'
  }

  $Version = (& $HashcatPath --version).Trim()

  if (($LASTEXITCODE -ne 0) -or ($Version -notmatch '^v[0-9].+')) {
    throw "The built executable returned an invalid version: $Version"
  }

  if ((-not [string]::IsNullOrWhiteSpace($ExpectedVersion)) -and ($Version -cne $ExpectedVersion)) {
    throw "The built executable version '$Version' does not match the expected release version '$ExpectedVersion'."
  }

  $Commit = (& $GitPath rev-parse HEAD).Trim()

  if (($LASTEXITCODE -ne 0) -or ($Commit -notmatch '^[0-9a-f]{40}$')) {
    throw 'Unable to resolve the source commit.'
  }

  $SevenZip = Resolve-SevenZip -RepoRoot $RepoRoot -RequestedPath $SevenZipPath -RequestedToolchainDirectory $ToolchainDirectory

  $OutputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
  }
  else {
    [IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputDirectory))
  }

  New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

  $PackageName = "shooter_hashcat-$Version-windows-x64-complete"
  $ArchivePath = Join-Path $OutputRoot "$PackageName.7z"

  if (Test-Path -LiteralPath $ArchivePath) {
    if (-not $Force) {
      throw "$ArchivePath already exists. Pass -Force to replace it."
    }

    Remove-Item -LiteralPath $ArchivePath -Force
  }

  $TempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  $StagingRoot = Join-Path $TempBase ("shooter-hashcat-package-" + [Guid]::NewGuid().ToString('N'))
  $PackageRoot = Join-Path $StagingRoot $PackageName
  $SourceTar = Join-Path $StagingRoot 'source.tar'

  New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null

  try {
    Invoke-Checked $GitPath '-C' $RepoRoot 'archive' '--format=tar' '--output' $SourceTar 'HEAD'
    Invoke-Checked 'tar.exe' '-xf' $SourceTar '-C' $PackageRoot

    $RequiredRuntimeFiles = @(
      'hashcat.exe'
      'libiconv-2.dll'
      'libgcc_s_seh-1.dll'
      'libstdc++-6.dll'
      'libwinpthread-1.dll'
    )

    foreach ($RelativeFile in $RequiredRuntimeFiles) {
      $SourceFile = Join-Path $RepoRoot $RelativeFile

      if (-not (Test-Path -LiteralPath $SourceFile -PathType Leaf)) {
        throw "Required Windows runtime file is missing: $RelativeFile"
      }

      Copy-Item -LiteralPath $SourceFile -Destination (Join-Path $PackageRoot $RelativeFile) -Force
    }

    Copy-BuiltFiles -SourceRoot $RepoRoot -DestinationRoot $PackageRoot -RelativeDirectory 'modules'
    Copy-BuiltFiles -SourceRoot $RepoRoot -DestinationRoot $PackageRoot -RelativeDirectory 'bridges'
    Copy-BuiltFiles -SourceRoot $RepoRoot -DestinationRoot $PackageRoot -RelativeDirectory 'feeds'

    $SourceModuleCount = @(Get-ChildItem -LiteralPath (Join-Path $PackageRoot 'src\modules') -Filter 'module_*.c' -File).Count
    $BuiltModuleCount = @(Get-ChildItem -LiteralPath (Join-Path $PackageRoot 'modules') -Filter 'module_*.dll' -File).Count
    $BuiltBridgeCount = @(Get-ChildItem -LiteralPath (Join-Path $PackageRoot 'bridges') -Filter '*.dll' -File -Recurse).Count
    $BuiltFeedCount = @(Get-ChildItem -LiteralPath (Join-Path $PackageRoot 'feeds') -Filter '*.dll' -File -Recurse).Count

    if ($BuiltModuleCount -ne $SourceModuleCount) {
      throw "Incomplete module set: found $BuiltModuleCount DLLs for $SourceModuleCount module sources."
    }

    if (($BuiltBridgeCount -eq 0) -or ($BuiltFeedCount -eq 0)) {
      throw 'The built bridge or feed DLL set is empty.'
    }

    $BuildInfo = @(
      "Package: $PackageName"
      "Version: $Version"
      "Source commit: $Commit"
      "Packaged UTC: $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))"
      "Architecture: Windows x64 portable baseline"
      "Prebuilt executable: hashcat.exe"
      "Module DLLs: $BuiltModuleCount"
      "Bridge DLLs: $BuiltBridgeCount"
      "Feed DLLs: $BuiltFeedCount"
      ''
      'Run the prebuilt program from this directory:'
      '  .\hashcat.exe --version'
      ''
      'Rebuild all Windows binaries from the included source:'
      '  .\build-windows.ps1 -Action Rebuild'
      ''
      'The first rebuild bootstraps a repository-local MSYS2/MinGW64 toolchain.'
      'Internet access and at least 5 GB of free disk space are required for that first bootstrap.'
      'GPU vendor drivers are runtime prerequisites and are not redistributed.'
      ''
      'Verify every packaged file:'
      '  .\verify-windows-package.ps1'
    )

    $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllLines((Join-Path $PackageRoot 'BUILD-INFO.txt'), $BuildInfo, $Utf8NoBom)

    $ManifestPath = Join-Path $PackageRoot 'SHA256SUMS'
    $ManifestLines = New-Object System.Collections.Generic.List[string]

    $PackageFiles = Get-ChildItem -LiteralPath $PackageRoot -File -Recurse |
      Where-Object { $_.FullName -ne $ManifestPath } |
      Sort-Object FullName

    foreach ($File in $PackageFiles) {
      $RelativeFile = $File.FullName.Substring($PackageRoot.Length).TrimStart('\', '/').Replace('\', '/')
      $ManifestLines.Add("$(Get-Sha256 -Path $File.FullName)  $RelativeFile")
    }

    [IO.File]::WriteAllLines($ManifestPath, $ManifestLines, $Utf8NoBom)

    & (Join-Path $PackageRoot 'verify-windows-package.ps1') -Root $PackageRoot

    Push-Location -LiteralPath $StagingRoot

    try {
      Invoke-Checked $SevenZip 'a' '-t7z' '-m0=lzma2:d=64m' '-mx=9' '-mmt=on' '-ms=on' $ArchivePath $PackageName
    }
    finally {
      Pop-Location
    }

    Invoke-Checked $SevenZip 't' $ArchivePath

    $ArchiveHash = Get-Sha256 -Path $ArchivePath

    Write-Host ''
    Write-Host "Package succeeded: $ArchivePath"
    Write-Host "Version: $Version"
    Write-Host "Source commit: $Commit"
    Write-Host "SHA-256: $ArchiveHash"
  }
  finally {
    $ResolvedStagingRoot = [IO.Path]::GetFullPath($StagingRoot)

    if ($ResolvedStagingRoot.StartsWith($TempBase, [StringComparison]::OrdinalIgnoreCase)) {
      Remove-Item -LiteralPath $ResolvedStagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
}
finally {
  Pop-Location
}
