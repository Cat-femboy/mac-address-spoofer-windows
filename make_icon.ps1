# Generates icon.ico.\r\n$ErrorActionPreference = 'Stop'
$w = 32; $h = 32
$xor = New-Object byte[] ($w * $h * 4)

function Set-Px([int]$x, [int]$y, [byte]$r, [byte]$g, [byte]$b) {
    if ($x -lt 0 -or $x -ge 32 -or $y -lt 0 -or $y -ge 32) { return }
    $row = 31 - $y
    $i = ($row * 32 + $x) * 4
    $xor[$i]     = $b
    $xor[$i + 1] = $g
    $xor[$i + 2] = $r
    $xor[$i + 3] = 255
}

for ($y = 0; $y -lt 32; $y++) { for ($x = 0; $x -lt 32; $x++) { Set-Px $x $y 14 17 23 } }
for ($i = 0; $i -lt 32; $i++) {
    Set-Px $i 0 0 216 160; Set-Px $i 31 0 216 160
    Set-Px 0 $i 0 216 160; Set-Px 31 $i 0 216 160
}

$pat = @('#...#', '##.##', '#.#.#', '#...#', '#...#')
for ($py = 0; $py -lt 5; $py++) {
    for ($px = 0; $px -lt 5; $px++) {
        if ($pat[$py][$px] -eq '#') {
            for ($dy = 0; $dy -lt 4; $dy++) {
                for ($dx = 0; $dx -lt 4; $dx++) {
                    Set-Px (6 + $px*4 + $dx) (6 + $py*4 + $dy) 0 216 160
                }
            }
        }
    }
}

$bih = New-Object byte[] 40
[Array]::Copy([BitConverter]::GetBytes([uint32]40),  0, $bih, 0, 4)
[Array]::Copy([BitConverter]::GetBytes([int]32),     0, $bih, 4, 4)
[Array]::Copy([BitConverter]::GetBytes([int]64),     0, $bih, 8, 4)
[Array]::Copy([BitConverter]::GetBytes([uint16]1),   0, $bih, 12, 2)
[Array]::Copy([BitConverter]::GetBytes([uint16]32),  0, $bih, 14, 2)
[Array]::Copy([BitConverter]::GetBytes([uint32]4096),0, $bih, 20, 4)

$entry = New-Object byte[] 16
$entry[0] = 32; $entry[1] = 32
[Array]::Copy([BitConverter]::GetBytes([uint16]1),    0, $entry, 4, 2)
[Array]::Copy([BitConverter]::GetBytes([uint16]32),   0, $entry, 6, 2)
[Array]::Copy([BitConverter]::GetBytes([uint32]4264), 0, $entry, 8, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]22),   0, $entry, 12, 4)

$hdr = [byte[]]@(0, 0, 1, 0, 1, 0)
$mask = New-Object byte[] 128
$all = New-Object byte[] ($hdr.Length + $entry.Length + $bih.Length + $xor.Length + $mask.Length)
$off = 0
foreach ($chunk in @($hdr, $entry, $bih, $xor, $mask)) {
    [Array]::Copy($chunk, 0, $all, $off, $chunk.Length)
    $off += $chunk.Length
}
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'icon.ico'), $all)
"icon.ico written: $($all.Length) bytes"
