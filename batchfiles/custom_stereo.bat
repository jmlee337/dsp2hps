REM SET THE --loop_point PARAMETER IN SECONDS
REM AVAILABLE PRECISION IS 56 / SAMPLE_RATE SECONDS
REM WITH DEFAULT SAMPLE_RATE, THAT'S 0.00175 SECONDS

@echo off
:loop
title Converting DSP
tool\ffmpeg.exe -y -i %1 -ar 32000 -acodec pcm_s16le -map_channel 0.0.0 LEFT.wav -ar 32000 -acodec pcm_s16le -map_channel 0.0.1 RIGHT.wav

set "filename=%1"
tool\DSPADPCM.exe -e LEFT.wav LEFT.dsp
tool\DSPADPCM.exe -e RIGHT.wav RIGHT.dsp

title Writing HPS
set "hps=.hps"
set "fullname=%filename%%hps%"
tool\dsp2hps.exe -l LEFT.dsp -r RIGHT.dsp -o %fullname% --loop_point 17.161
shift
if not "%~1"=="" goto loop

title Done!
echo Will delete intermediate files
pause
del LEFT.wav
del RIGHT.wav
del LEFT.dsp
del RIGHT.dsp
del LEFT.txt
del RIGHT.txt
