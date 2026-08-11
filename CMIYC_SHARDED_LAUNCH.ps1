[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
  [string] $HashFile,

  [Parameter(Mandatory = $true)]
  [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
  [string] $Wordlist,

  [string] $OutputDirectory,

  [string] $CombinedOutput,

  [ValidateRange(1, 999999)]
  [int] $Mode = 29960,

  [ValidateNotNullOrEmpty()]
  [string[]] $Devices = (2..12),

  [switch] $DryRun,

  [switch] $Wait
)

$ErrorActionPreference = 'Stop'

if (-not [string]::IsNullOrWhiteSpace($CombinedOutput)) {
  $CombinedOutput = [IO.Path]::GetFullPath($CombinedOutput)
  $Wait = $true
}

$hashcat = Join-Path $PSScriptRoot 'hashcat.exe'

if (-not (Test-Path -LiteralPath $hashcat -PathType Leaf)) {
  throw "hashcat.exe was not found beside this script: $hashcat"
}

$hashes = @(
  Get-Content -LiteralPath $HashFile |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_.Length -gt 0 }
)

if ($hashes.Count -eq 0) {
  throw "No hashes were found in $HashFile"
}

$normalizedDevices = foreach ($deviceSpec in $Devices) {
  foreach ($part in ($deviceSpec -split ',')) {
    $device = 0
    $value = $part.Trim()

    if (-not [int]::TryParse($value, [ref] $device) -or $device -lt 1) {
      throw "Invalid device ID: '$value'"
    }

    $device
  }
}

$deviceIds = @($normalizedDevices | Select-Object -Unique)

if ($deviceIds.Count -gt $hashes.Count) {
  $deviceIds = @($deviceIds[0..($hashes.Count - 1)])
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $parent = Split-Path -Parent ([IO.Path]::GetFullPath($HashFile))
  $OutputDirectory = Join-Path $parent "cmiyc_29960_sharded_$stamp"
}

$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$shardDirectory = Join-Path $OutputDirectory 'shards'
$crackedDirectory = Join-Path $OutputDirectory 'outfiles'
$potDirectory = Join-Path $OutputDirectory 'potfiles'
$logDirectory = Join-Path $OutputDirectory 'logs'

foreach ($directory in @($OutputDirectory, $shardDirectory, $crackedDirectory, $potDirectory, $logDirectory)) {
  $null = New-Item -ItemType Directory -Path $directory -Force
}

$shards = @()

for ($i = 0; $i -lt $deviceIds.Count; $i++) {
  $shards += ,([Collections.Generic.List[string]]::new())
}

for ($i = 0; $i -lt $hashes.Count; $i++) {
  $shards[$i % $deviceIds.Count].Add($hashes[$i])
}

$utf8NoBom = [Text.UTF8Encoding]::new($false)
$processes = @()
$outfilePaths = @()

for ($i = 0; $i -lt $deviceIds.Count; $i++) {
  $device = $deviceIds[$i]
  $label = '{0:D2}' -f $device
  $shardPath = Join-Path $shardDirectory "device_$label.hashes"
  $outfile = Join-Path $crackedDirectory "device_$label.cracked.txt"
  $potfile = Join-Path $potDirectory "device_$label.potfile"
  $stdout = Join-Path $logDirectory "device_$label.stdout.log"
  $stderr = Join-Path $logDirectory "device_$label.stderr.log"
  $session = "cmiyc29960_d${label}_$stamp"

  $outfilePaths += $outfile

  [IO.File]::WriteAllLines($shardPath, $shards[$i], $utf8NoBom)

  if ($DryRun) {
    [pscustomobject]@{
      Device = $device
      Hashes = $shards[$i].Count
      PID = '-'
      Session = $session
      Log = $stdout
    } | Format-Table -AutoSize

    continue
  }

  $arguments = @(
    '-m', $Mode,
    '-w', '4',
    '-a', '0',
    $shardPath,
    [IO.Path]::GetFullPath($Wordlist),
    '-d', $device,
    '-o', $outfile,
    '--potfile-path', $potfile,
    '--session', $session,
    '--outfile-check-dir', $crackedDirectory,
    '--outfile-check-timer', '30',
    '--status',
    '--status-timer', '60',
    '--backend-ignore-opencl'
  )

  $process = Start-Process `
    -FilePath $hashcat `
    -ArgumentList $arguments `
    -WorkingDirectory $PSScriptRoot `
    -WindowStyle Hidden `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr `
    -PassThru

  $processes += $process

  [pscustomobject]@{
    Device = $device
    Hashes = $shards[$i].Count
    PID = $process.Id
    Session = $session
    Log = $stdout
  } | Format-Table -AutoSize
}

if ($DryRun) {
  Write-Host "Dry run complete; prepared $($deviceIds.Count) balanced shards without starting Hashcat."
} else {
  Write-Host "Started $($processes.Count) isolated Hashcat processes."
}
Write-Host "Results: $crackedDirectory"
Write-Host "Logs:    $logDirectory"
Write-Host "Potfiles:$potDirectory"

if ($Wait -and -not $DryRun) {
  if (-not [string]::IsNullOrWhiteSpace($CombinedOutput)) {
    $combinedParent = Split-Path -Parent $CombinedOutput

    if (-not [string]::IsNullOrWhiteSpace($combinedParent)) {
      $null = New-Item -ItemType Directory -Path $combinedParent -Force
    }

    $seenLines = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)

    if (Test-Path -LiteralPath $CombinedOutput -PathType Leaf) {
      foreach ($line in [IO.File]::ReadAllLines($CombinedOutput)) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
          $null = $seenLines.Add($line)
        }
      }
    }

    function Merge-CmiycOutput {
      $newLines = [Collections.Generic.List[string]]::new()

      foreach ($path in $outfilePaths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
          continue
        }

        $stream = [IO.FileStream]::new(
          $path,
          [IO.FileMode]::Open,
          [IO.FileAccess]::Read,
          [IO.FileShare]::ReadWrite
        )

        try {
          if ($stream.Length -eq 0) {
            continue
          }

          $bytes = [byte[]]::new($stream.Length)
          $read = $stream.Read($bytes, 0, $bytes.Length)
          $lastNewline = [Array]::LastIndexOf($bytes, [byte] 10, $read - 1)

          if ($lastNewline -lt 0) {
            continue
          }

          $text = $utf8NoBom.GetString($bytes, 0, $lastNewline + 1)

          foreach ($line in ($text -split "`r?`n")) {
            if ($line.StartsWith('$cmiyc$2026$') -and $seenLines.Add($line)) {
              $newLines.Add($line)
            }
          }
        } finally {
          $stream.Dispose()
        }
      }

      if ($newLines.Count -gt 0) {
        [IO.File]::AppendAllLines($CombinedOutput, $newLines, $utf8NoBom)
        Write-Host "Merged $($newLines.Count) new result(s) into $CombinedOutput"
      }
    }

    Write-Host "Live combined output: $CombinedOutput"

    do {
      Merge-CmiycOutput
      $stillRunning = @($processes | Where-Object { -not $_.HasExited })

      if ($stillRunning.Count -gt 0) {
        Start-Sleep -Seconds 2
      }
    } while ($stillRunning.Count -gt 0)

    Merge-CmiycOutput
  } else {
    $processes | Wait-Process
  }

  Write-Host 'All shard processes have exited.'
}
