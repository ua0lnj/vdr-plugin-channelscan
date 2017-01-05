#!/bin/bash

conf="$1"
pl="iptv.m3u"

if [ conf == "" ]
then

exit
fi

echo "конвертируем из $conf в $pl"
echo "#EXTM3U" > $pl
cat $conf | 
{
read start
    while read stroka1
        do
#echo $stroka1
            name="`echo ${stroka1} | cut -f 1 -d ':' | cut -f 1 -d ';' `"
            url="`echo ${stroka1} | cut -f 3 -d ':' `"
            proto="`echo ${url} | cut -f 3 -d '|' | cut -b 3- `"
            ip="`echo ${url} | cut -f 4 -d '|' | cut -b 3- `"
            port="`echo ${url} | cut -f 5 -d '|' | cut -b 3- `"
#echo $name

            echo "#EXTINF:0,$name" >> $pl
            echo "$proto://@$ip:$port" >> $pl

        done
}

echo "список каналов записан в $pl"
