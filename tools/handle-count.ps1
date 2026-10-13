# Conta gli handle di kernel di un processo.
#
# Serve a distinguere un **costo** da una **perdita**: un'estensione della shell
# che si carica una volta fa salire il contatore e poi lo lascia fermo; una
# risorsa che non viene rilasciata lo fa salire a ogni giro. Le due cose si
# vedono uguali se si guarda solo il tempo impiegato — §7.38 di `docs/02` l'ha
# trovata cosi': dieci menu contestuali di fila e il contatore che non tornava
# mai giu'.
#
# In un ciclo:
#   for ($i=0; $i -lt 10; $i++) {
#     ... apri e chiudi il menu ...
#     powershell -File tools/handle-count.ps1 -ProcessName Bolt
#   }
param(
    [Parameter(Mandatory = $true)][string]$ProcessName
)

$process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
if ($null -eq $process) {
    Write-Host ('nessun processo di nome ' + $ProcessName)
    exit 1
}
Write-Host ('{0}: {1} handle' -f $ProcessName, $process.Handles)
