"""Convert a png image to RGB565 and 8bit big endian dump list"""
import os
import sys
import argparse
from PIL import Image

def error(msg):
    """show error"""
    print(msg)
    sys.exit(-1)

def conv565(pixel_list,transparent):
    """conversion"""
    count = 0
    col = 32
    row = 32
    for c in range(col):
        for r in range(row):
            pix = pixel_list[r*32+c]
            r = (pix[0] >> 3) & 0x1F
            g = (pix[1] >> 2) & 0x3F
            b = (pix[2] >> 3) & 0x1F
            i = (r << 11) + (g << 5) + b
            if i == transparent:
                i = 0
            ib = i >> 8
            il = i & 0xff
            if count % 1024 == 1023:
                print(f"0x{ib:02X}, 0x{il:02X}")
            elif count % 8 == 7:
                print(f"0x{ib:02X}, 0x{il:02X}, ")
            else:
                print(f"0x{ib:02X}, 0x{il:02X}",end=", ")
            count += 1

#    for pix in pixel_list:
#        r = (pix[0] >> 3) & 0x1F
#        g = (pix[1] >> 2) & 0x3F
#        b = (pix[2] >> 3) & 0x1F
#        i = (r << 11) + (g << 5) + b
#        ib = i >> 8
#        il = i & 0xff
#        if count % 1024 == 1023:
#          print(f"0x{il:02x}, 0x{ib:02x}")
#        elif count % 8 == 7:
#          print(f"0x{il:02x}, 0x{ib:02x}, ")
#        else:          
#          print(f"0x{il:02x}, 0x{ib:02x}",end=", ")
#        count += 1

##
if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Convert image to 16bit(RGB565) hex with 8 bit dump')
    parser.add_argument('-t', '--transparent', default=48530, type=int,
                        help='Transparent color')
    parser.add_argument('imagefile',type=str,help='Image file to convert')
    args = parser.parse_args()

    if not os.path.exists(args.imagefile):
        error('not exists: ' + args.imagefile)

    img = Image.open(args.imagefile).convert('RGB')
    pixels = list(img.getdata())
    print ("{")
    conv565(pixels,args.transparent)
    print ("}")
