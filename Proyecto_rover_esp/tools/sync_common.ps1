<#
.SYNOPSIS
    Copia el protocolo compartido a los dos sketches.

.DESCRIPTION
    Arduino solo compila lo que hay dentro de la carpeta del sketch, asi que
    el codigo comun al rover y a la estacion base (ordenes, JSON, telemetria y
    protocolo de radio) vive en Proyecto_rover_esp/common/ y este script lo
    copia a <sketch>/src/common/ en los dos.

    La unica fuente de verdad es common/. Despues de editar algo alli, ejecuta
    este script y vuelve a subir LOS DOS sketches: si la base y el rover llevan
    versiones distintas del protocolo, se descartan los paquetes.

    Uso (desde cualquier carpeta):
        powershell -ExecutionPolicy Bypass -File Proyecto_rover_esp/tools/sync_common.ps1
#>

$ErrorActionPreference = 'Stop'

$here   = Split-Path -Parent $MyInvocation.MyCommand.Path
$common = (Resolve-Path (Join-Path $here '..\common')).Path
$targets = @(
    (Join-Path $here '..\rover_6x6\src\common'),
    (Join-Path $here '..\estacion_base\src\common')
)

$banner = "// COPIA GENERADA de Proyecto_rover_esp/common/ - NO EDITAR AQUI.`n" +
          "// Edita el original y ejecuta tools/sync_common.ps1.`n`n"

$files = Get-ChildItem -Path $common -File | Where-Object { $_.Extension -in '.h', '.cpp' }

foreach ($target in $targets) {
    $dir = [System.IO.Path]::GetFullPath($target)
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force $dir | Out-Null

    foreach ($f in $files) {
        $text = [System.IO.File]::ReadAllText($f.FullName).Replace("`r`n", "`n")
        [System.IO.File]::WriteAllText((Join-Path $dir $f.Name), $banner + $text,
                                       (New-Object System.Text.UTF8Encoding $false))
    }
    Write-Host "common/ -> $dir ($($files.Count) archivos)"
}
