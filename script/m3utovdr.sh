#!/bin/bash

pl="$1"
conf="iptvchannels.conf"
prov="DSV"
if [ pl == "" ]
then

exit
fi

echo "конвертируем из $pl в $conf"

cat $pl | 
{
read start
if [ "`echo $start | grep -i '#extm3u'`" ]
then
    while read stroka1
        do
#echo $stroka1
           if [ "`echo $stroka1 | grep -i '#extinf'`" ]
           then
             name="`echo ${stroka1} | cut -f 2 -d ',' | sed 's/'"$(printf '\015')"'$//g'`"
#             echo  $name >> $conf
           else


         F="`echo ${stroka1}| cut -f 1 -d ':'|tr a-z A-Z`"
         U="`echo ${stroka1}| cut -f 2 -d ':'|cut -f 2 -d '@'`"
         A="`echo ${stroka1}|cut -f 3 -d ':' | sed 's/'"$(printf '\015')"'$//g'`"
         T="`echo ${U}|cut -f 2 -d '.'``echo ${U}|cut -f 3 -d '.'``echo ${U}|cut -f 4 -d '.'`0"
echo "${name};${prov}:${T}:S=1|P=1|F=${F}|U=${U}|A=${A}:I:0:0:0:0:0:1:0:0:0"
echo "${name};${prov}:${T}:S=1|P=1|F=${F}|U=${U}|A=${A}:I:0:0:0:0:0:1:0:0:0" >> $conf

           fi

        done
fi

}

echo "список каналов записан в $conf"
sort -t'|' +3.2 -3.13  $conf -o $conf.sort
echo "список каналов отсортирован в $conf"