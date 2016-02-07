#!/bin/bash
# ------------------------------------------------------------------------------
# Travis CI scripts 
# Copyright(c) pgRouting Contributors
#
# Install pgRouting prerequesits
# ------------------------------------------------------------------------------

set -e

POSTGRESQL_VERSION="$1"
PGUSER="$2"


if test "$POSTGRESQL_VERSION" = "9.5" ; then

    echo "Installing postgresql 9.5 & postgis for 9.5 "
    sudo apt-get install -y postgresql-9.5 postgresql-9.5-postgis
    sudo /etc/init.d/postgresql stop 

fi

echo "tarting server"
sudo /etc/init.d/postgresql start $POSTGRESQL_VERSION


sudo cp /usr/lib/postgresql/$POSTGRESQL_VERSION/bin/pg_config /usr/bin/pg_config

echo "Installing pgtap ... this may take some time."
wget https://github.com/theory/pgtap/archive/master.zip
unzip master.zip
cd pgtap-master
make
make installcheck
sudo make install
cd ..

