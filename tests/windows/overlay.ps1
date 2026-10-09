# What the Windows media overlay shows, one line per player in it. Used by
# Get-AmpOverlay and Send-AmpOverlay in wintest.ps1, which see to it that
# this runs in Windows PowerShell 5.1: the newer PowerShell cannot reach the
# Windows Runtime.
#
# -Do presses a button (play, pause, toggle, stop, next, prev) for the
# players whose program name matches -App, and for nobody else.
param([string]$Do = "", [string]$App = "amplitude")

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
function Await($operation, [Type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
    [void]$task.Wait(5000)
    return $task.Result
}
$managerType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType = WindowsRuntime]
$propertiesType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties, Windows.Media.Control, ContentType = WindowsRuntime]
$manager = Await ($managerType::RequestAsync()) $managerType
foreach ($session in $manager.GetSessions()) {
    $info = $session.GetPlaybackInfo()
    $properties = Await ($session.TryGetMediaPropertiesAsync()) $propertiesType
    "app=[{0}] status={1} type={2} title=[{3}]" -f $session.SourceAppUserModelId, $info.PlaybackStatus, $info.PlaybackType, $properties.Title
    if ($Do -and $session.SourceAppUserModelId -match $App) {
        $operation = switch ($Do) {
            "play"   { $session.TryPlayAsync() }
            "pause"  { $session.TryPauseAsync() }
            "toggle" { $session.TryTogglePlayPauseAsync() }
            "stop"   { $session.TryStopAsync() }
            "next"   { $session.TrySkipNextAsync() }
            "prev"   { $session.TrySkipPreviousAsync() }
            default  { throw "unknown button '$Do'" }
        }
        [void](Await $operation ([bool]))
    }
}
