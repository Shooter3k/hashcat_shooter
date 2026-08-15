[CmdletBinding()]
param(
  [string] $Root = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PackageRoot = [IO.Path]::GetFullPath($Root)
$ManifestPath = Join-Path $PackageRoot 'SHA256SUMS'

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
  throw "Package manifest not found: $ManifestPath"
}

$Verified = 0

foreach ($Line in Get-Content -LiteralPath $ManifestPath) {
  if ([string]::IsNullOrWhiteSpace($Line)) {
    continue
  }

  if ($Line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
    throw "Malformed SHA256SUMS entry: $Line"
  }

  $Expected = $Matches[1].ToLowerInvariant()
  $RelativePath = $Matches[2].Replace('/', [IO.Path]::DirectorySeparatorChar)
  $FilePath = [IO.Path]::GetFullPath((Join-Path $PackageRoot $RelativePath))

  if (-not $FilePath.StartsWith($PackageRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Manifest entry leaves the package root: $RelativePath"
  }

  if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
    throw "Packaged file is missing: $RelativePath"
  }

  $Actual = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()

  if ($Actual -ne $Expected) {
    throw "Checksum mismatch for $RelativePath. Expected $Expected but received $Actual."
  }

  $Verified++
}

Write-Host "Verified $Verified packaged files against SHA256SUMS."
