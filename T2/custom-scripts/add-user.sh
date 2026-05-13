#!/bin/sh

TARGET_DIR="$1"

USERNAME="user"
PASSWORD="123"

# cria diretório home
mkdir -p $TARGET_DIR/home/$USERNAME

# adiciona user
echo "$USERNAME:x:1000:1000:User:/home/$USERNAME:/bin/sh" >> $TARGET_DIR/etc/passwd

# adiciona grupo
echo "$USERNAME:x:1000:" >> $TARGET_DIR/etc/group

# define senha (criptografada)
PASS_HASH=$(openssl passwd -1 $PASSWORD)
echo "$USERNAME:$PASS_HASH:0:0:99999:7:::" >> $TARGET_DIR/etc/shadow

# permissões
chown -R 1000:1000 $TARGET_DIR/home/$USERNAME
chmod 755 $TARGET_DIR/home/$USERNAME