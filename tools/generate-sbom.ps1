[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $Root,

  [Parameter(Mandatory = $true)]
  [string] $OutputPath,

  [Parameter(Mandatory = $true)]
  [string] $Version,

  [Parameter(Mandatory = $true)]
  [string] $Commit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ResolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$ResolvedOutput = [IO.Path]::GetFullPath($OutputPath)
$Files = New-Object System.Collections.Generic.List[object]
$Relationships = New-Object System.Collections.Generic.List[object]
$FileIndex = 0

$RuntimeFiles = @(Get-ChildItem -LiteralPath $ResolvedRoot -File -Recurse |
  Where-Object {
    $_.Name -eq 'hashcat.exe' -or
    $_.Extension -eq '.dll'
  } |
  Sort-Object FullName)

foreach ($File in $RuntimeFiles) {
  $FileIndex++
  $Id = 'SPDXRef-File-{0:D5}' -f $FileIndex
  $Relative = $File.FullName.Substring($ResolvedRoot.Length).TrimStart('\', '/').Replace('\', '/')
  $Hash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()

  $Files.Add([ordered]@{
      SPDXID          = $Id
      fileName        = "./$Relative"
      checksums       = @([ordered]@{ algorithm = 'SHA256'; checksumValue = $Hash })
      licenseConcluded = 'NOASSERTION'
      copyrightText   = 'NOASSERTION'
    })
  $Relationships.Add([ordered]@{
      spdxElementId      = 'SPDXRef-Package-ShooterHashcat'
      relationshipType  = 'CONTAINS'
      relatedSpdxElement = $Id
    })
}

$Packages = @(
  [ordered]@{
    name             = 'shooter_hashcat'
    SPDXID           = 'SPDXRef-Package-ShooterHashcat'
    versionInfo      = $Version
    downloadLocation = 'https://github.com/Shooter3k/shooter_hashcat'
    # This focused runtime SBOM inventories the shipped executables and DLLs;
    # it does not claim that every source and documentation file was analyzed.
    filesAnalyzed    = $false
    licenseConcluded = 'MIT'
    licenseDeclared  = 'MIT'
    copyrightText    = 'NOASSERTION'
  },
  [ordered]@{
    name             = 'hashcat'
    SPDXID           = 'SPDXRef-Package-UpstreamHashcat'
    versionInfo      = '7.1.2'
    downloadLocation = 'https://github.com/hashcat/hashcat'
    filesAnalyzed    = $false
    licenseConcluded = 'MIT'
    licenseDeclared  = 'MIT'
    copyrightText    = 'NOASSERTION'
  },
  [ordered]@{
    name             = 'zlib-motley'
    SPDXID           = 'SPDXRef-Package-Zlib'
    versionInfo      = '1.3.1.1-motley'
    downloadLocation = 'https://github.com/hashcat/zlib'
    filesAnalyzed    = $false
    licenseConcluded = 'Zlib'
    licenseDeclared  = 'Zlib'
    copyrightText    = 'NOASSERTION'
  },
  [ordered]@{
    name             = 'MinGW-w64 runtime'
    SPDXID           = 'SPDXRef-Package-MingwRuntime'
    versionInfo      = 'build-toolchain supplied'
    downloadLocation = 'https://www.mingw-w64.org/'
    filesAnalyzed    = $false
    licenseConcluded = 'NOASSERTION'
    licenseDeclared  = 'NOASSERTION'
    copyrightText    = 'NOASSERTION'
  },
  [ordered]@{
    name             = 'Rust standard library'
    SPDXID           = 'SPDXRef-Package-RustStd'
    versionInfo      = 'stable build-toolchain supplied'
    downloadLocation = 'https://github.com/rust-lang/rust'
    filesAnalyzed    = $false
    licenseConcluded = 'Apache-2.0 OR MIT'
    licenseDeclared  = 'Apache-2.0 OR MIT'
    copyrightText    = 'NOASSERTION'
  }
)

foreach ($Dependency in @(
    'SPDXRef-Package-UpstreamHashcat',
    'SPDXRef-Package-Zlib',
    'SPDXRef-Package-MingwRuntime',
    'SPDXRef-Package-RustStd'
  )) {
  $Relationships.Add([ordered]@{
      spdxElementId       = 'SPDXRef-Package-ShooterHashcat'
      relationshipType   = 'DEPENDS_ON'
      relatedSpdxElement = $Dependency
    })
}

$Document = [ordered]@{
  spdxVersion       = 'SPDX-2.3'
  dataLicense       = 'CC0-1.0'
  SPDXID            = 'SPDXRef-DOCUMENT'
  name              = "shooter_hashcat-$Version-windows-x64-complete"
  documentNamespace = "https://github.com/Shooter3k/shooter_hashcat/spdx/$Commit"
  creationInfo      = [ordered]@{
    created  = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    creators = @('Tool: shooter_hashcat tools/generate-sbom.ps1')
  }
  documentDescribes = @('SPDXRef-Package-ShooterHashcat')
  packages          = $Packages
  files             = $Files
  relationships     = $Relationships
}

$OutputDirectory = Split-Path -Parent $ResolvedOutput
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($ResolvedOutput, ($Document | ConvertTo-Json -Depth 8), $Utf8NoBom)

Write-Host "SPDX SBOM: $ResolvedOutput ($($RuntimeFiles.Count) runtime files)"
