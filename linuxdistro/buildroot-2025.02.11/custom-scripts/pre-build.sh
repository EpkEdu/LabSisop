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
echo $BASE_DIR
ls $BASE_DIR
cp $BASE_DIR/../apps/hello $BASE_DIR/target/usr/bin
cp $BASE_DIR/../custom-scripts/hello.sh $BASE_DIR/target/etc/init.d/S50hello
chmod +x $BASE_DIR/target/etc/init.d/S50hello

# HTTPD
mkdir -p $TARGET_DIR/var/www/cgi-bin/
cp $BASE_DIR/../custom-scripts/httpd-launcher.sh $BASE_DIR/target/etc/init.d/S50httpd
cp $BASE_DIR/../apps/monitor $TARGET_DIR/var/www/cgi-bin/monitor
chmod 777 $TARGET_DIR/var/www/cgi-bin
chmod 777 $TARGET_DIR/var/www/cgi-bin/monitor
chmod +x $BASE_DIR/target/etc/init.d/S50httpd



mkdir -p $TARGET_DIR/root/.ssh
cat ~/.ssh/id_ed25519.pub >> $TARGET_DIR/root/.ssh/authorized_keys