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
# PUBSUB AUTO-LOAD
# =========================

mkdir -p $TARGET_DIR/etc/init.d
cat > $TARGET_DIR/etc/init.d/S99pubsub << 'EOF'
#!/bin/sh
insmod /lib/modules/$(uname -r)/updates/pubsub.ko
mknod /dev/pubsub c $(grep pubsub /proc/devices | awk '{print $1}') 0
chmod 666 /dev/pubsub
EOF
chmod +x $TARGET_DIR/etc/init.d/S99pubsub

echo $BASE_DIR
ls $BASE_DIR
cp $BASE_DIR/../apps/hello $BASE_DIR/target/usr/bin


# HTTPD
mkdir -p $TARGET_DIR/var/www/cgi-bin/
cp $BASE_DIR/../custom-scripts/httpd-laucher.sh $BASE_DIR/target/etc/init.d/S50httpd



COMPILER=$BASE_DIR/output/host/bin/i686-buildroot-linux-gnu-gcc

KDIR=$BASE_DIR/build/linux-6.12.27
if [ -d "$KDIR" ]; then
    make -C $BASE_DIR/../modules/pubsub/ install
fi

if [ -f "$COMPILER" ]; then
    $COMPILER -Wall -o $BASE_DIR/../apps/pubsub-teste \
        $BASE_DIR/../apps/pubsub-teste.c
    cp $BASE_DIR/../apps/pubsub-teste $TARGET_DIR/usr/bin/pubsub-teste
    chmod +x $TARGET_DIR/usr/bin/pubsub-teste

    $COMPILER -Wall -o $BASE_DIR/../apps/pubsub-multi \
        $BASE_DIR/../apps/pubsub-multi.c
    cp $BASE_DIR/../apps/pubsub-multi $TARGET_DIR/usr/bin/pubsub-multi
    chmod +x $TARGET_DIR/usr/bin/pubsub-multi
fi