# Salva un riquadro dello schermo, senza toccare finestre.
#
# `capture-window.ps1` cattura la finestra di Bolt intera e per farlo la porta in
# primo piano: durante un trascinamento non si puo' fare, perche' cambiare l'ordine
# delle finestre annulla il gesto. Qui si legge lo schermo com'e', e si salva SOLO
# il riquadro chiesto — quello che sta attorno non si salva (AGENTS.md §0).
param(
    [Parameter(Mandatory = $true)][int]$X,
    [Parameter(Mandatory = $true)][int]$Y,
    [Parameter(Mandatory = $true)][int]$W,
    [Parameter(Mandatory = $true)][int]$H,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

Add-Type -AssemblyName System.Drawing

$image = New-Object System.Drawing.Bitmap($W, $H)
$graphics = [System.Drawing.Graphics]::FromImage($image)
$graphics.CopyFromScreen($X, $Y, 0, 0, (New-Object System.Drawing.Size($W, $H)))
$graphics.Dispose()
$image.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$image.Dispose()

Write-Output "cattura: $OutputPath ($X,$Y ${W}x${H})"
