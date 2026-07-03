# file: custom-scripts/pre-build.sh

#!/bin/sh

TARGET_DIR="$1"

# =========================
# NETWORK CONFIG
# =========================

HOST=$(ip route show | head -n 1 | awk '{print $9}')

sed "s/\[IP-DO-HOST\]/$HOST/g" \
    $BASE_DIR/../custom-scripts/network-config \
    > $BASE_DIR/../custom-scripts/S41network-config

mkdir -p $TARGET_DIR/etc/init.d
cp $BASE_DIR/../custom-scripts/S41network-config $TARGET_DIR/etc/init.d/
chmod +x $TARGET_DIR/etc/init.d/S41network-config

# =========================
# USER CONFIG
# =========================

USERNAME="user"
PASSWORD="123"

# garante arquivos base
touch $TARGET_DIR/etc/passwd
touch $TARGET_DIR/etc/group
touch $TARGET_DIR/etc/shadow

# evita duplicação
if ! grep -q "^$USERNAME:" $TARGET_DIR/etc/passwd; then

    mkdir -p $TARGET_DIR/home/$USERNAME

    echo "$USERNAME:x:1000:1000:User:/home/$USERNAME:/bin/sh" >> $TARGET_DIR/etc/passwd
    echo "$USERNAME:x:1000:" >> $TARGET_DIR/etc/group

    PASS_HASH=$(openssl passwd -1 $PASSWORD)
    echo "$USERNAME:$PASS_HASH:0:0:99999:7:::" >> $TARGET_DIR/etc/shadow

    chown -R 1000:1000 $TARGET_DIR/home/$USERNAME
    chmod 755 $TARGET_DIR/home/$USERNAME
fi

# =========================

mkdir -p $TARGET_DIR/etc/init.d



echo $BASE_DIR
ls $BASE_DIR
cp $BASE_DIR/../apps/hello $BASE_DIR/target/usr/bin


# HTTPD
mkdir -p $TARGET_DIR/var/www/cgi-bin/
cp $BASE_DIR/../custom-scripts/httpd-laucher.sh $BASE_DIR/target/etc/init.d/S50httpd



COMPILER=$BASE_DIR/host/bin/i686-buildroot-linux-gnu-gcc

KDIR=$BASE_DIR/build/linux-6.12.27
if [ -d "$KDIR" ]; then
    make -C $BASE_DIR/../modules/sstf/ install
fi

if [ -f "$COMPILER" ]; then
  

     $COMPILER -Wall -o $BASE_DIR/../apps/sstf_teste \
    $BASE_DIR/../apps/sstf_teste.c
cp $BASE_DIR/../apps/sstf_teste $TARGET_DIR/usr/bin/sstf_teste
chmod +x $TARGET_DIR/usr/bin/sstf_teste  
    
fi
$BASE_DIR/../output/host/bin/i686-linux-gcc $BASE_DIR/../apps/schedrt/query.c -o $BASE_DIR/target/usr/bin/schedrt-query
$BASE_DIR/../output/host/bin/i686-linux-gcc $BASE_DIR/../apps/schedrt/dl.c -o $BASE_DIR/target/usr/bin/schedrt-dl
$BASE_DIR/../output/host/bin/i686-linux-gcc $BASE_DIR/../apps/disk-test/raw.c -o $BASE_DIR/target/usr/bin/disk-test-raw

#o sor mandou fazer o teste abrindo varios fork de leitura pra testar o escalonador

echo "=== DEBUG SSTF ===" 
echo "TARGET_DIR=$TARGET_DIR"
echo "BASE_DIR=$BASE_DIR"
echo "COMPILER=$COMPILER"

$COMPILER -Wall -o $BASE_DIR/../apps/sstf_teste \
    $BASE_DIR/../apps/sstf_teste.c
echo "Compilou: $?"
cp $BASE_DIR/../apps/sstf_teste $TARGET_DIR/usr/bin/sstf_teste
echo "Copiou: $?"
chmod +x $TARGET_DIR/usr/bin/sstf_teste

$COMPILER -Wall -o $BASE_DIR/../apps/disktest \
    $BASE_DIR/../apps/disktest.c
cp $BASE_DIR/../apps/disktest $TARGET_DIR/usr/bin/disktest
chmod +x $TARGET_DIR/usr/bin/disktest

$COMPILER -Wall -pthread -o $BASE_DIR/../apps/thread_runner \
    $BASE_DIR/../apps/thread_runner.c
cp $BASE_DIR/../apps/thread_runner $TARGET_DIR/usr/bin/thread_runner
chmod +x $TARGET_DIR/usr/bin/thread_runner