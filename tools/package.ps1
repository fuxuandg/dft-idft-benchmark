param([string]$OutputZip)
$ErrorActionPreference = 'Stop'
$packageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputZip) { $OutputZip = Join-Path (Split-Path $packageRoot -Parent) 'idft_benchmark_release_20261014.zip' }
$OutputZip = [IO.Path]::GetFullPath($OutputZip)
if (Test-Path -LiteralPath $OutputZip) { throw "Output already exists; choose a new zip name: $OutputZip" }
$sourceFiles = Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Where-Object {
    $rel = [IO.Path]::GetRelativePath($packageRoot, $_.FullName).Replace('\','/')
    $rel -ne 'SHA256SUMS' -and $rel -notmatch '(^|/)\.git(/|$)' -and -not $rel.StartsWith('build/') -and -not $rel.StartsWith('data/current/')
} | Sort-Object FullName
$checksumLines = foreach ($item in $sourceFiles) {
    $rel = [IO.Path]::GetRelativePath($packageRoot, $item.FullName).Replace('\','/')
    $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $rel"
}
# Generated manifest: UTF-8 without BOM, LF separators for sha256sum on Linux.
[IO.File]::WriteAllText((Join-Path $packageRoot 'SHA256SUMS'), ($checksumLines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
Add-Type -AssemblyName System.IO.Compression
$zipStream = [IO.File]::Open($OutputZip, [IO.FileMode]::CreateNew)
try {
    $zip = [IO.Compression.ZipArchive]::new($zipStream,[IO.Compression.ZipArchiveMode]::Create,$true,[Text.Encoding]::UTF8)
    try {
        foreach ($item in Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName) {
            $rel = [IO.Path]::GetRelativePath($packageRoot,$item.FullName).Replace('\','/')
            if ($rel.StartsWith('build/') -or $rel -match '(^|/)\.git(/|$)') { continue }
            $entry = $zip.CreateEntry($rel,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $item.LastWriteTimeUtc
            $inputStream = [IO.File]::OpenRead($item.FullName)
            $entryStream = $entry.Open()
            try { $inputStream.CopyTo($entryStream) } finally { $inputStream.Dispose(); $entryStream.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $zipStream.Dispose() }
Write-Output "Archive: $OutputZip"
Write-Output "Static checksum files: $($sourceFiles.Count)"
Get-FileHash -LiteralPath $OutputZip -Algorithm SHA256
