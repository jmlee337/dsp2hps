@echo off
:loop
title Converting DSP
tool\ffmpeg.exe -y -i %1 -ar 32000 -acodec pcm_s16le -map_channel 0.0.0 LEFT.wav -ar 32000 -acodec pcm_s16le -map_channel 0.0.1 RIGHT.wav
tool\DSPADPCM.exe -e LEFT.wav LEFT.dsp
tool\DSPADPCM.exe -e RIGHT.wav RIGHT.dsp
title Writing HPS
set "filename=%1"
set "extension=.hps"
set "fullname=%filename%%extension%"
tool\dsp2hps.exe -l LEFT.dsp -r RIGHT.dsp -o %fullname%
shift
if not "%~1"=="" goto loop
echo Will delete intermediate files
pause
del LEFT.wav
del RIGHT.wav
del LEFT.dsp
del RIGHT.dsp
del LEFT.txt
del RIGHT.txt
