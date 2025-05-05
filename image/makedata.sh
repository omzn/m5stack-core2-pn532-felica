#!/bin/bash
# rpg-aquatan用に書かれたpngの4コマキャラクターデータをArduinoで使える形式に変換する
#
# $ ./makeArduinoData.sh file
#
#   Pipfile込みでcharaconv565.pyが必要．
#   $ pipenv install 

BASE=`basename $1 .png`

height=`identify $BASE.png | cut -f 3 -d ' ' | cut -d 'x' -f 2`
iter=$((height/32*4-4))
magick $BASE.png -crop 32x32 $BASE.bmp
echo 'const unsigned char aqua_bmp [][4][2048] PROGMEM = {'
for i in `seq 0 4 $iter`; do
  echo '{'
  pipenv run python characonv565.py $BASE-$((i+0)).bmp -t 48530
  echo ','
  pipenv run python characonv565.py $BASE-$((i+1)).bmp -t 48530
  echo ','
  pipenv run python characonv565.py $BASE-$((i+2)).bmp -t 48530
  echo ','
  pipenv run python characonv565.py $BASE-$((i+3)).bmp -t 48530
  echo '},'
done
echo '};'
rm $BASE-*.bmp
