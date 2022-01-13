import socket
import itertools
import subprocess

host = "192.168.2.148"
port = 4242

command = [ "ffmpeg",
            '-ss', '0:02:27',
            '-i', 'rick-roll.mp4',
            '-i', 'palette.png',
            '-filter_complex', '[0:v]crop=1200:1080:360:0,scale=160x144[v0];[v0][1:v]paletteuse[out]',
            '-map', '[out]',
            '-f', 'image2pipe',
            '-pix_fmt', 'gray8',
            '-s', '160x144',
            '-r', '20',
            '-vcodec', 'rawvideo', '-']
pipe = subprocess.Popen(command, stdout = subprocess.PIPE, bufsize=1000000)

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

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.connect((host, port))
    i = 0
    while True:
        raw_image = pipe.stdout.read(160*144)
        data = bytes(list(itertools.chain.from_iterable([getTile(i, raw_image) for i in range(0,360)])))
        sock.sendall(data)
        i += 1
        print(i)

