# wifi-game-boy-cartridge
A WiFi cartridge for the original Game Boy.

This open source and open hardware Game Boy cartridge uses an ESP8266 to allow WiFi access for custom Game Boy code. First demos include accessing Wikipedia articles from the Game Boy.

Details and instructions can be found on https://there.oughta.be/a/wifi-game-boy-cartridge and an overview is given in the following video:

[![Youtube Video: WiFi Game Boy Cartridge](https://img.youtube.com/vi/QS4fzElm8zk/0.jpg)](https://youtu.be/QS4fzElm8zk)

Also, another article and video have been published on streaming video through this cartridge, which can be found at https://there.oughta.be/gta5-for-the-game-boy and in this video:

[![Youtube Video: Playing GTA5 on the original Game Boy](https://img.youtube.com/vi/pX1opw_gsBs/0.jpg)](https://youtu.be/pX1opw_gsBs)

If you have pull-requests, bug reports or want to contribute new case designs, please do not hesitate to open an issue. For generic discussions, "show and tell" and if you are looking for support for something that is not a problem in the code here, I would recommend [r/thereoughtabe on reddit](https://www.reddit.com/r/thereoughtabe/).

<a href="https://www.buymeacoffee.com/there.oughta.be" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-blue.png" alt="Buy Me A Coffee" height="47" width="174" ></a>

# Short overview of the demos

As the number of demos keeps growing and some of them are just a quick experiment, you can find a few notes here:

## color-stream

This is the code for streaming videos to the Game Boy Color as shown in [in this little video](https://www.youtube.com/watch?v=kHexmGR_rPY). The code is supposed to detect if the cart is in a classic Game Boy (DMG) or a Game Boy color and both, the EEPROM code as well as the ESP8266 code should react accordingly. However, you have to use the right Python script, i.e. `video-stream` or `game-stream` for the original Game Boy or `color-stream` for a Game Boy color.

Note, that this has not been tested much and still has a few glitches. Also note, that I ran into an ffmpeg problem that prevented me from streaming screen captures on Linux (x11grab).

## serial

The oldest demo from the first video. When this is running, the Game Boy should show your IP address and you can connect to it with a telnet client. You can send text between Game Boy and PC and that's it. Note that there is an error in the way the data pins are initialized, causing them to run in open drain mode, which is ok, except for the highest bit which has an external pull-down and therefore stays low forever. For this demo, it does not matter because the character map does not have anything above the 7bit ASCII map anyways, but if you ever need the eighth bit, look at the streaming videos for how to initialize this correctly.

## stream

The streaming code on which the second video (playing GTA5) is based. It allows to show a video stream on the Game Boy and sends back key presses. You need to feed it with one of the Python scripts, either `video-stream.py` or `game-stream.py`. The buttons seem to generate a few ghost presses, which should be fixed by doing a few more readouts for the buttons (not tested).

## wiki

The Wikipedia demo from the first video. Once it has started, you can press start to get an on-screen keyboard, enter the title of a Wikipedia article and press start again to fetch an excerpt of that article. Pressing A shows you the remainder of the article, if any. Just like with the serial demo above, there is an error in the way the data pins are initialized, causing them to run in open drain mode, which is ok, except for the highest bit which has an external pull-down and therefore stays low forever. For this demo, it does not matter because the character map does not have anything above the 7bit ASCII map anyways, but if you ever need the eighth bit, look at the streaming videos for how to initialize this correctly.

# License
The code and the PCB design are released under the GNU General Public Licence 3. The only exception is the cartridge model for the 3d print, which is based on a [Game Boy cartridge by DomesticHacks on Thingiverse](https://www.thingiverse.com/thing:107841), which is released under a CC-BY-NC-SA license, so I have to publish my derived work under CC-BY-NC-SA, too.
