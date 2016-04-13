# dsp2hps
I heavily reworked MeleeHps to be way more user friendly and produce slightly better results
seriously, it now literally takes **5 seconds** to go from audiofile.anyformat to audiofile.hps

**[Download](https://drive.google.com/open?id=0B79OwbM8T752UlZTdWRjRTVOZEk)**

changelog v3 (2016/04/13):

* rewrote the conversion from DSP to HPS in C++
* since this is sufficiently removed from what MeleeHps was, I'm calling my tool 'dsp2hps'
* calculates all block header values correctly to remove all pops from the final audio. to my knowledge, this is the only tool to write the correct P/S, hist1, and hist2 values in the HPS block headers
* I've uploaded my code to github: https://github.com/jmlee337/dsp2hps

changelog v2 (2016/04/01):

* you can convert files in batch! Just drag them all onto run.bat
* [here's a vid](https://drive.google.com/open?id=0B79OwbM8T752T0FNSTFld01MU0U)

changelog v1 (2016/03/31):

* no more opening with audacity and clicking and saving to format the file correctly
* no more fiddling with hex editors
* no more copy pasting samples-this and blocks-that
* drag-and-drop or invoke from the command line
* more accurate block headers mean fewer/smaller pops during playback
* parameterized output file names let you do a bunch in a row without overwriting your output

## **How do I use this?**
how to use (video form):

* [here you go](https://drive.google.com/open?id=0B79OwbM8T752YW9vMHhjUXcxOFE)

how to use (text form):
* move your audio file (in any format) to the MeleeHps folder
* drag it onto 'run.bat'
* that's it.
* you can also invoke from the command line: normalloop.bat myaudiofile.ogg

That's so easy! What's the catch??

* you still have to make sure your source audio file loops correctly
* this only works for 'normal' loops, not 'custom' loops

## **How'd you do it??? (technical details)**

* **audacity**
i included ffmpeg, which can convert from any format to 16bit WAV 32000 kHz from the command line


* **hex editors**
dsp2hps automatically adds the correct padding. I discovered that DSP data must be 32-byte aligned! 16-byte alignment also seemed to work for me, but all the stock HSPs are 32-byte aligned. This must be why achilles sometimes found that adding a pad in a hex editor didn't always work the first time.


* **samples/blocks**
well simply, that data is all calculable from the DSP files themselves.


* **drag and drop**
the whole conversion process can be done in one batch script


* **more accurate block headers**
the original MeleeHps used the first block header in the file for every single block. dsp2hps writes every block header 100% correctly, including the hist1 and hist2 values that require decoding the DSP block to set correctly. This completely eliminates the auditory pops common with the original MeleeHps. I included the most complete documentation I could assemble for the format of HSP audio files in the code


* **output file names**
just a small quality of life edit. the output file now has the format [inputfilename].hps, so songname.mp3 would produce songname.mp3.hps as the output. This means you can run a bunch in a row and move out all your hps files once you're done

## **what are you working on next?**

* ~~rewriting all the main.asm + assembler business in an actual programming language to make the world a little more sane~~
* ~~further parameterizing so you'll be able to drag all the files onto run.bat and convert all of them in one go~~
* ~~writing hist1 and hist2 in the HSP headers correctly to remove popping all together~~
* Adding support for custom loops (loop points at any arbitrary sample, not just block boundaries)
