"""make a color conversion table from 15000"""
import os
import sys
import argparse
from PIL import Image

def error(msg):
    """Error message"""
    print(msg)
    sys.exit(-1)

def cmp565(pixel_list,orgpixel_list):
    """Comparison"""
    table = {}
    col = 32
    row = 32
    for co in range(col):
        for ro in range(row):
            pix = pixel_list[ro*32+co]
            pr = (pix[0] >> 3) & 0x1F
            pg = (pix[1] >> 2) & 0x3F
            pb = (pix[2] >> 3) & 0x1F
            pi = (pr << 11) + (pg << 5) + pb

            opix = orgpixel_list[ro*32+co]
            opr = (opix[0] >> 3) & 0x1F
            opg = (opix[1] >> 2) & 0x3F
            opb = (opix[2] >> 3) & 0x1F
            opi = (opr << 11) + (opg << 5) + opb
            #print(f"opi {opi} - pi {pi}")
            table[opi] = pi
    return table

#
if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='make a color conversion table from original file')
    parser.add_argument('-t', '--transparent', default=48530, type=int,
                        help='Transparent color')
    parser.add_argument('-o','--original',type=str, default="15000.bmp", help='Original Image file')
    parser.add_argument('imagefile',type=str,help='Image file to convert')
    args = parser.parse_args()

    if not os.path.exists(args.imagefile):
        error('not exists: ' + args.imagefile)

    org = Image.open(args.original).convert('RGB')
    orgpixels = list(org.getdata())
    img = Image.open(args.imagefile).convert('RGB')
    imgpixels = list(img.getdata())
    tbl = cmp565(imgpixels,orgpixels)
    print("{",end = "")
    for item in tbl:
        print(item,end=",")
    print("}")
    print("{",end = "")
    for key,value in tbl.items():
        print(value,end=",")
    print("}")
