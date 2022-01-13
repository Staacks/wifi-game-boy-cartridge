#Pretty much the same as video-stream, but it also receives and reacts on button presses

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
            '-f', 'x11grab',
            '-video_size', '1333x1200', #Grab 10:9 section from a 1920x1200 screen, adapt to your needs
            '-framerate', '20',
            '-i', ':0.0+293,0', #Offset to center the image. Adapt if necessary
            '-i', 'palette.png',
            '-filter_complex', '[0:v]scale=160x144[v0];[v0][1:v]paletteuse[out]',
            '-map', '[out]',
            '-f', 'image2pipe',
            '-pix_fmt', 'gray8',
            '-vcodec', 'rawvideo', '-']

pipe = subprocess.Popen(command, stdout = subprocess.PIPE, bufsize=1000000)

keyboard = Controller()

def get2bppBytes(pixels):
    a = 0x00
    b = 0x00
    for i in range(0,8):
        a |= (((pixels[i] & 0x40) << 1) >> i)
        b |= ((pixels[i] & 0x80) >> i)
    return [a, b]

def getTile(i, data):
    x = i % 20
    y = i // 20
    out = []
    for j in range(0,8):
        start = x*8 + (y*8+j)*160
        out.extend(get2bppBytes(data[start:start+8]))
    return out

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

        raw_image = pipe.stdout.read(160*144)
        if end - start < 0.05: #If sending took more than 20ms last time, the Game Boy still had another frame to process, so we will drop one frame for the sake of lower latency
            data = bytes(list(itertools.chain.from_iterable([getTile(i, raw_image) for i in range(0,360)])))
            start = time.time()
            sock.sendall(data)
            end = time.time()
            print(end-start)
        else:
            start = 0
            end = 0
            print("Droped one frame.")

        try:
            joypad_data = sock.recv(1, socket.MSG_DONTWAIT)
            if len(joypad_data) > 0:
                translateToKeyboard(joypad_data[0])
        except BlockingIOError as e:
            pass


