#!/bin/bash
set -e

script_dir="$(dirname "$0")"
vfat_dir=/tmp/_vfat/
tmp_img=/tmp/_oslab_tmp_fat16_image.img

if [ $# -ne 1 ]; then
    echo "请提供文件名"
    echo "使用方法：$0 [文件名]"
    exit 1
fi

# generate image
rm -f $tmp_img
mkfs.fat -C -F 16 -r 512 -R 32 -s 4 -S 512 $tmp_img $((32*1024))

# mount image
mkdir -p $vfat_dir
if mountpoint -q $vfat_dir; then
    sudo umount "$vfat_dir"
fi
sudo mount -t vfat \
    --options "time_offset=480,iocharset=ascii,uid=$(id -u ${USER}),gid=$(id -g ${USER})" \
    $tmp_img $vfat_dir

# generate test files
python3 $script_dir/generate_test_files.py $vfat_dir
sudo umount $vfat_dir

filename="$1"
cat $tmp_img > $filename
