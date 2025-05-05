#!/bin/bash
BASE=$1

magick $BASE.png -crop 32x32+0+0 $BASE.bmp
pipenv run python makeconvtable.py $BASE.bmp -t 48530
rm $BASE.bmp
