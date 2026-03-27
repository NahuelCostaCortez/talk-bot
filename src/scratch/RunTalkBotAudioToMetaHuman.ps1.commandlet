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
$unrealExe = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

if (-not (Test-Path $unrealExe)) {
    throw "UnrealEditor-Cmd.exe not found at $unrealExe"
}

if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before running this script. UnrealEditor.exe is currently open."
}

$arguments = @(
    $uprojectPath,
    "-run=MyProject.TalkBotMetaHuman",
    "-MetaHumanPath=$MetaHumanPath",
    "-StorageRoot=$StorageRoot",
    "-Mood=$Mood",
    "-ProcessMask=$ProcessMask",
    "-HeadMovementMode=$HeadMovementMode",
    "-stdout",
    "-FullStdOutLogOutput"
)

if ($Latest -or [string]::IsNullOrWhiteSpace($WavPath)) {
    $arguments += "-Latest"
}

if (-not [string]::IsNullOrWhiteSpace($WavPath)) {
    $arguments += "-WavPath=$WavPath"
}

$output = & $unrealExe @arguments 2>&1
$exitCode = $LASTEXITCODE
$output | ForEach-Object { $_ }
$success = $output | Where-Object { "$_" -like "*TalkBot MetaHuman processing complete*" }

if (-not $success) {
    throw "UnrealEditor-Cmd exited with code $exitCode without reaching the TalkBot MetaHuman completion marker."
}

if ($exitCode -ne 0) {
    Write-Warning "UnrealEditor-Cmd exited with code $exitCode after completing the TalkBot MetaHuman pipeline. Continuing because the success marker was found."
}
