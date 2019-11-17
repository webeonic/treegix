#!/bin/bash


# CONFIGURATION

TREEGIX_SERVER="localhost";
TREEGIX_PORT="10051";

TREEGIX_SENDER="~treegix/bin/treegix_sender";

KEY="snmptraps";
HOST="snmptraps";

# END OF CONFIGURATION

read hostname
read ip
read uptime
read oid
read address
read community
read enterprise

oid=`echo $oid|cut -f2 -d' '`
address=`echo $address|cut -f2 -d' '`
community=`echo $community|cut -f2 -d' '`
enterprise=`echo $enterprise|cut -f2 -d' '`

oid=`echo $oid|cut -f11 -d'.'`
community=`echo $community|cut -f2 -d'"'`

str="$hostname $address $community $enterprise $oid"

$TREEGIX_SENDER -z $TREEGIX_SERVER -p $TREEGIX_PORT -s $HOST -k $KEY -o "$str"
