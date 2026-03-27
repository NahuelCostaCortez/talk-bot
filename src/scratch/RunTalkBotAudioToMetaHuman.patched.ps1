param(
    [string]$WavPath = "",
    [switch]$Latest,
    [string]$MetaHumanPath = "/Game/MetaHumans/Hana/BP_Hana.BP_Hana",
    [string]$StorageRoot = "/Game/TalkBot",
    [ValidateSet("Auto", "Neutral", "Happy", "Sad", "Disgust", "Anger", "Surprise", "Fear")]
    [string]$Mood = "Auto",
    [ValidateSet("FullFace", "MouthOnly")]
    [string]$ProcessMask = "FullFace",
    [ValidateSet("ControlRig", "TransformTrack", "Disabled")]
    [string]$HeadMovementMode = "ControlRig"
)

$projectRoot = $PSScriptRoot
$uprojectPath = Join-Path $projectRoot "MyProject.uproject"
$pythonScriptPath = Join-Path $projectRoot "Scripts\talkbot_audio_to_metahuman.py"
$unrealExe = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

function Convert-ToUnrealPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path).Replace('\', '/')
}

function Escape-ForExecutePythonScript([string]$Value) {
    $normalized = $Value.Replace('\', '/')
    $escaped = $normalized.Replace('"', '\\"')
    return '\\"' + $escaped + '\\"'
}

if (-not (Test-Path $unrealExe)) {
    throw "UnrealEditor-Cmd.exe not found at $unrealExe"
}

if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before running this script. UnrealEditor.exe is currently open."
}

if (-not $Latest -and [string]::IsNullOrWhiteSpace($WavPath)) {
    $Latest = $true
}

$scriptPathForUnreal = Convert-ToUnrealPath $pythonScriptPath
$scriptAndArgsParts = @($scriptPathForUnreal)

if ($Latest) {
    $scriptAndArgsParts += "--latest"
}

if (-not [string]::IsNullOrWhiteSpace($WavPath)) {
    $wavPathForUnreal = Convert-ToUnrealPath $WavPath
    $scriptAndArgsParts += "--wav-path"
    $scriptAndArgsParts += (Escape-ForExecutePythonScript $wavPathForUnreal)
}

$scriptAndArgsParts += "--meta-human-path"
$scriptAndArgsParts += (Escape-ForExecutePythonScript $MetaHumanPath)
$scriptAndArgsParts += "--storage-root"
$scriptAndArgsParts += (Escape-ForExecutePythonScript $StorageRoot)
$scriptAndArgsParts += "--mood"
$scriptAndArgsParts += $Mood
$scriptAndArgsParts += "--process-mask"
$scriptAndArgsParts += $ProcessMask
$scriptAndArgsParts += "--head-movement-mode"
$scriptAndArgsParts += $HeadMovementMode

$scriptAndArgs = $scriptAndArgsParts -join " "
$executePythonArgument = "-ExecutePythonScript=""$scriptAndArgs"""

$process = Start-Process -FilePath $unrealExe -ArgumentList @(
    $uprojectPath,
    $executePythonArgument,
    "-stdout",
    "-FullStdOutLogOutput"
) -NoNewWindow -Wait -PassThru

if ($process.ExitCode -ne 0) {
    throw "UnrealEditor-Cmd exited with code $($process.ExitCode)"
}
