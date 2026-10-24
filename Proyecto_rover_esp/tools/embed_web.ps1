<#
.SYNOPSIS
    Incrusta el panel web en el firmware.

.DESCRIPTION
    Arduino no tiene un equivalente a EMBED_FILES de ESP-IDF, asi que este
    script convierte cada archivo de Proyecto_rover_panel/rover_web_gcs en un
    arreglo de bytes dentro de rover_6x6/src/transport/WebAssets.h.

    La carpeta del panel sigue siendo la unica fuente de verdad: despues de
    editar cualquier archivo del panel, vuelve a ejecutar este script y luego
    compila y sube el sketch.

    Uso (desde cualquier carpeta):
        powershell -ExecutionPolicy Bypass -File Proyecto_rover_esp/tools/embed_web.ps1
#>

$ErrorActionPreference = 'Stop'

$here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$webRoot = (Resolve-Path (Join-Path $here '..\..\Proyecto_rover_panel\rover_web_gcs')).Path
$outFile = Join-Path $here '..\rover_6x6\src\transport\WebAssets.h'

$mimeTypes = @{
    '.html' = 'text/html; charset=utf-8'
    '.js'   = 'text/javascript; charset=utf-8'
    '.css'  = 'text/css; charset=utf-8'
    '.json' = 'application/json'
    '.svg'  = 'image/svg+xml'
    '.png'  = 'image/png'
    '.ico'  = 'image/x-icon'
}

$files = Get-ChildItem -Path $webRoot -Recurse -File |
    Where-Object { $mimeTypes.ContainsKey($_.Extension.ToLower()) } |
    Sort-Object FullName

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('/*')
[void]$sb.AppendLine(' * GENERADO por tools/embed_web.ps1 - NO EDITAR A MANO.')
[void]$sb.AppendLine(' * Fuente: Proyecto_rover_panel/rover_web_gcs. Edita alli y vuelve a generar.')
[void]$sb.AppendLine(' */')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#include <stddef.h>')
[void]$sb.AppendLine('#include <stdint.h>')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('struct WebAsset {')
[void]$sb.AppendLine('    const char    *uri;')
[void]$sb.AppendLine('    const char    *mime;')
[void]$sb.AppendLine('    const uint8_t *data;')
[void]$sb.AppendLine('    size_t         len;')
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')

$entries = @()
$i = 0
foreach ($f in $files) {
    $rel   = $f.FullName.Substring($webRoot.Length).Replace('\', '/')
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    $name  = "kWebAsset$i"

    [void]$sb.AppendLine("// $rel ($($bytes.Length) bytes)")
    [void]$sb.Append("static const uint8_t $name[] = {")
    for ($b = 0; $b -lt $bytes.Length; $b++) {
        if ($b % 20 -eq 0) { [void]$sb.Append("`n    ") }
        [void]$sb.Append(('0x{0:x2},' -f $bytes[$b]))
    }
    [void]$sb.AppendLine("`n};")
    [void]$sb.AppendLine('')

    $mime = $mimeTypes[$f.Extension.ToLower()]
    $entries += "    { `"$rel`", `"$mime`", $name, sizeof($name) },"
    $i++
}

[void]$sb.AppendLine('static const WebAsset kWebAssets[] = {')
foreach ($e in $entries) { [void]$sb.AppendLine($e) }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const size_t kWebAssetCount = sizeof(kWebAssets) / sizeof(kWebAssets[0]);')

# UTF-8 sin BOM y con saltos de linea LF, igual en Windows que en Linux/macOS.
$text = $sb.ToString().Replace("`r`n", "`n")
[System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($outFile), $text, (New-Object System.Text.UTF8Encoding $false))

Write-Host "WebAssets.h generado con $($files.Count) archivos del panel:"
$files | ForEach-Object { Write-Host ('  ' + $_.FullName.Substring($webRoot.Length).Replace('\', '/')) }
