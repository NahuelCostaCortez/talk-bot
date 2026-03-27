@echo off
chcp 65001 >nul
"C:\Users\costanahuel\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1-full_build\bin\ffmpeg.exe" ^
  -hide_banner ^
  -nostdin ^
  -loglevel error ^
  -f dshow ^
  -i audio="@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\wave_{95CA59B1-6B4D-4A7C-8F46-1F33A3262646}" ^
  -ac 1 ^
  -ar 24000 ^
  -f s16le -
