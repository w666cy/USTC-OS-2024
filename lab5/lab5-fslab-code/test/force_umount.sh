if [ $# -ne 1 ]; then
    echo "请提供要强制写在的挂载点"
    echo "使用方法：$0 [挂载点]"
    exit 1
fi
fusermount -zu $1