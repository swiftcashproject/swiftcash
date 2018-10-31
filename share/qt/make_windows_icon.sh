#!/bin/bash
# create multiresolution windows icon
#mainnet
ICON_SRC=../../src/qt/res/icons/swiftcash.png
ICON_DST=../../src/qt/res/icons/swiftcash.ico
convert ${ICON_SRC} -resize 16x16 swiftcash-16.png
convert ${ICON_SRC} -resize 32x32 swiftcash-32.png
convert ${ICON_SRC} -resize 48x48 swiftcash-48.png
convert swiftcash-16.png swiftcash-32.png swiftcash-48.png ${ICON_DST}
#testnet
ICON_SRC=../../src/qt/res/icons/swiftcash_testnet.png
ICON_DST=../../src/qt/res/icons/swiftcash_testnet.ico
convert ${ICON_SRC} -resize 16x16 swiftcash-16.png
convert ${ICON_SRC} -resize 32x32 swiftcash-32.png
convert ${ICON_SRC} -resize 48x48 swiftcash-48.png
convert swiftcash-16.png swiftcash-32.png swiftcash-48.png ${ICON_DST}
rm swiftcash-16.png swiftcash-32.png swiftcash-48.png
