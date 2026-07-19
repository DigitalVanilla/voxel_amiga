param(
    [string]$MapDirectory = "maps",
    [int[]]$Indexes = @(0)
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Export-IndexedPixels {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [string]$OutputPath
    )

    if ($Bitmap.PixelFormat -ne [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed) {
        throw "Expected an indexed 8-bit image for $OutputPath"
    }

    $rectangle = [System.Drawing.Rectangle]::new(0, 0, $Bitmap.Width, $Bitmap.Height)
    $data = $Bitmap.LockBits(
        $rectangle,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed
    )

    try {
        $row = [byte[]]::new($Bitmap.Width)
        $stream = [System.IO.File]::Create($OutputPath)
        try {
            for ($y = 0; $y -lt $Bitmap.Height; $y++) {
                if ($data.Stride -ge 0) {
                    $source = [IntPtr]::Add($data.Scan0, $y * $data.Stride)
                } else {
                    $source = [IntPtr]::Add(
                        $data.Scan0,
                        ($Bitmap.Height - 1 - $y) * (-$data.Stride)
                    )
                }
                [Runtime.InteropServices.Marshal]::Copy(
                    $source, $row, 0, $Bitmap.Width
                )
                $stream.Write($row, 0, $row.Length)
            }
        } finally {
            $stream.Dispose()
        }
    } finally {
        $Bitmap.UnlockBits($data)
    }
}

function Export-Palette {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [string]$OutputPath
    )

    $palette = [byte[]]::new(256 * 3)
    for ($index = 0; $index -lt 256; $index++) {
        $color = $Bitmap.Palette.Entries[$index]
        $palette[$index * 3] = $color.R
        $palette[$index * 3 + 1] = $color.G
        $palette[$index * 3 + 2] = $color.B
    }
    [System.IO.File]::WriteAllBytes($OutputPath, $palette)
}

$resolvedDirectory = (Resolve-Path $MapDirectory).Path
foreach ($index in $Indexes) {
    $heightGif = Join-Path $resolvedDirectory "map$index.height.gif"
    $colorGif = Join-Path $resolvedDirectory "map$index.color.gif"
    $heightRaw = Join-Path $resolvedDirectory "map$index.height.raw"
    $colorRaw = Join-Path $resolvedDirectory "map$index.color.raw"
    $paletteRaw = Join-Path $resolvedDirectory "map$index.palette.raw"

    $heightBitmap = [System.Drawing.Bitmap]::new($heightGif)
    try {
        Export-IndexedPixels $heightBitmap $heightRaw
    } finally {
        $heightBitmap.Dispose()
    }

    $colorBitmap = [System.Drawing.Bitmap]::new($colorGif)
    try {
        Export-IndexedPixels $colorBitmap $colorRaw
        Export-Palette $colorBitmap $paletteRaw
    } finally {
        $colorBitmap.Dispose()
    }

    Write-Output "Converted map $index"
}
