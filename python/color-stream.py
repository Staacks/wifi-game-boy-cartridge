#Same function as game-stream, but for the Game Boy Color

import socket
import itertools
import subprocess
import time
from pynput.keyboard import Key, Controller

host = "192.168.2.148"
port = 4242

command = [ "ffmpeg",
            '-hide_banner',
            '-loglevel', 'error',
            #'-f', 'x11grab', #Note that this did not work for me. Some combination of x11grab and the filters used below seem to cause ffmpeg to get stuck. This is not related to the Game Boy stream, but also happens when the output is just a regular video file.
            #'-video_size', '1333x1200', #Grab 10:9 section from a 1920x1200 screen
            #'-video_size', '1200x1080', #Grab 10:9 section from a 1920x1200 screen
            #'-framerate', '30',
            #'-i', ':0.0+360,60',
            '-i', 'futurama.mp4',
            '-filter_complex', '[0:v]crop=800:720:240:0,scale=160x144,eq=saturation=2.0,split[v0][v1];color=black:16x128[c];[v1]palettegen=max_colors=6:stats_mode=single:reserve_transparent=false,split[pal][vpal];[vpal][c]vstack[palstack];[v0][pal]paletteuse=new=1[vout];[vout][palstack]hstack[out]',
            '-map', '[out]',
            '-f', 'image2pipe',
            '-pix_fmt', 'rgb24',
            '-vcodec', 'rawvideo',
            '-']

pipe = subprocess.Popen(command, stdout = subprocess.PIPE, bufsize=1000000)

keyboard = Controller()

def get2bppBytes(pixels, paletteMapping):
    a = 0x00
    b = 0x00
    for i in range(0,8):
        rgb = (pixels[3*i], pixels[3*i+1], pixels[3*i+2])
        val = paletteMapping[rgb]
        a |= (((val & 0x01) << 7) >> i)
        b |= (((val & 0x02) << 6) >> i)
    return [a, b]

def pickTilePalette(i, data, palette, paletteMapping):
    x = i % 20
    y = i // 20
    count = [0]*8
    for j in range(0,8):
        for k in range(0,8):
            index = 3*(x*8+j + (y*8+k)*(160+16))
            rgb = (data[index], data[index+1], data[index+2])
            for paletteIndex in range(0,8):
                if rgb in palette[paletteIndex]:
                    count[paletteIndex] += 1
    maxCount = 0
    tilePalette = 0
    for j in range(0,8):
        if count[j] > maxCount:
            maxCount = count[j]
            tilePalette = j
    return tilePalette

def getTile(i, data, palette, paletteMapping):
    paletteIndex = pickTilePalette(i, data, palette, paletteMapping)
    pal = palette[paletteIndex]
    x = i % 20
    y = i // 20
    out = []
    for j in range(0,8):
        start = 3*(x*8 + (y*8+j)*(160+16))
        out.extend(get2bppBytes(data[start:start+3*8], paletteMapping[paletteIndex]))
    return out, paletteIndex

def paletteIndexToOffset(i):
    return 3*(160 + i) if i < 16 else 3*(320 + i)

def getPalette(data):
    colors = [  (data[paletteIndexToOffset(i)],data[paletteIndexToOffset(i)+1],data[paletteIndexToOffset(i)+2])    for i in range(0,6)]
    entries = []
    entries.append([colors[j] for j in [0,1,2,5]])
    entries.append([colors[j] for j in [0,1,3,5]])
    entries.append([colors[j] for j in [0,1,4,5]])
    entries.append([colors[j] for j in [0,2,3,5]])
    entries.append([colors[j] for j in [0,2,4,5]])
    entries.append([colors[j] for j in [0,3,4,5]])
    entries.append([colors[j] for j in [0,1,2,3]])
    entries.append([colors[j] for j in [2,3,4,5]])
    return entries, colors

def getPaletteMapping(pal, colors):
    map = []
    for i in range(0, 8):
        submap = {}
        for j in range(0,6):
            mind = 1000
            rgb1 = colors[j]
            for k in range(0,4):
                rgb2 = pal[i][k]
                d = abs(rgb1[0]-rgb2[0]) + abs(rgb1[1]-rgb2[1]) + abs(rgb1[2]-rgb2[2])
                if d < mind:
                    mind = d
                    submap[rgb1] = k
        map.append(submap)
    return map

def paletteData(pal):
    data = []
    for i in range(0, 8):
        for j in range(0,4):
            (r,g,b) = pal[i][j]
            data.append(((g & 0x38) << 2) | (r >> 3))
            data.append(((b & 0xf8) >> 1) | (g >> 6))
    return data

def setKeyboardKey(state, mask, key):
    if state & mask > 0:
        keyboard.press(key)
    else:
        keyboard.release(key)

def translateToKeyboard(state):
    setKeyboardKey(state, 0b00000001, "a")
    setKeyboardKey(state, 0b00000010, "b")
    setKeyboardKey(state, 0b00000100, Key.backspace)
    setKeyboardKey(state, 0b00001000, Key.space)
    setKeyboardKey(state, 0b00010000, Key.right)
    setKeyboardKey(state, 0b00100000, Key.left)
    setKeyboardKey(state, 0b01000000, Key.up)
    setKeyboardKey(state, 0b10000000, Key.down)

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.connect((host, port))
    start = 0
    end = 0
    while True:

        raw_image = pipe.stdout.read(3*(160+16)*144)
        if end - start < 0.05: #If sending took more than 20ms last time, the Game Boy still had another frame to process, so we will drop one frame for the sake of lower latency
            palette, colors = getPalette(raw_image)
            paletteMapping = getPaletteMapping(palette, colors)
            tiles = [getTile(i, raw_image, palette, paletteMapping) for i in range(0,360)]
            #print("--------------")
            #for i in range(0,8):
            #    print(raw_image[i*176:i*176+24])
            #print("Tile:")
            #print(tiles[0])
            #print("Palette:")
            #print(palette)
            #print(paletteData(palette))
            #print("--------------")
            tileData, tilePalettes = zip(*tiles)
            ldata = []
            for i in range(0,9):
                ldata.append(tilePalettes[20*i:20*i+20])
                ldata.extend(tileData[20*i:20*i+20])
            ldata.append(paletteData(palette))
            for i in range(9,18):
                ldata.append(tilePalettes[20*i:20*i+20])
                ldata.extend(tileData[20*i:20*i+20])

            data = bytes(list(itertools.chain.from_iterable(ldata)))
            #print(len(data))
            start = time.time()
            sock.sendall(data)
            end = time.time()
            #print(end-start)
        else:
            start = 0
            end = 0
            print("Droped one frame.")

        try:
            joypad_data = sock.recv(1, socket.MSG_DONTWAIT)
#            if len(joypad_data) > 0:
#                translateToKeyboard(joypad_data[0])
        except BlockingIOError as e:
            pass


