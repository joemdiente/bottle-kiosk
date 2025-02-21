# RouterOS commands

# Add tasks in scheduler to Reset Daily and Monthly Income 
/system scheduler add interval=1d name="Reset Daily Income" on-event="/system script set source=\"0\" todayincome " policy=ftp,reboot,read,write,policy,test,password,sniff,sensitive,romon start-date=Sep/28/2021 start-time=00:00:00;
/system scheduler add interval=30d name="Reset Monthly Income" on-event="/system script set source=\"0\" monthlyincome " policy=ftp,reboot,read,write,policy,test,password,sniff,sensitive,romon start-date=Sep/28/2021 start-time=00:00:00;

#
/system script add dont-require-permissions=no name=todayincome owner=admin policy=ftp,reboot,read,write,policy,test,password,sniff,sensitive,romon source="0";
/system script add dont-require-permissions=no name= monthlyincome owner=admin policy=ftp,reboot,read,write,policy,test,password,sniff,sensitive,romon source="0";

#
/ip hotspot walled-garden ip add action=accept disabled=no dst-address-list=SapientiaVendo
/ip firewall filter add action=accept chain=input place-before=0 comment=SapientiaVendo src-address-list=SapientiaVendo

### enable telegram notification, change from 0 to 1 if you want to enable telegram
:local isTelegram 0;
###replace telegram token
:local iTBotToken "xxxxxxxxxx:xxxxxxxxxxxxx-xxxxxxxxxxxxxxx-xxxxx";
###replace telegram chat id / group id
:local iTGrChatID "xxxxxxxxxxxxxx";
### hotspot folder for HEX put flash/hotspot for haplite put hotspot only
:local HSFilePath "hotspot";
### enable Random MAC synchronizer
:local isRandomMacSyncFix 0;

### enable JuanFi online monitoring 0 = DoNotSend,  1=send data to api
:local apiSend 0;
### derive from the JuanFi online monitoring, create account in genman.projectdorsu.com
:local URLvendoID 5;

# Get User Data
:local aUsrNote [/ip hotspot user get $user comment];
:local aUsrNote [:toarray $aUsrNote];
:local iUsrTime [:totime ($aUsrNote->0)];
:local iSaleAmt [:tonum ($aUsrNote->1)];
:local iExtCode ($aUsrNote->2);
:local iVdoName ($aUsrNote->3);
:local iTimeMin [/ip hotspot user get $user limit-uptime];
:local iUserReg [/system scheduler find name=$user];

# Check User Data
:if (($iTimeMin>0) and ($iUsrTime>=0) and (($iUserReg="") or ($iExtCode=1))) do={
  /ip hotspot user set $user comment="";
  :local iFileMac;
  :local mac $"mac-address";
  :for i from=0 to=([:len $mac] - 1) do={
    :local chr [:pick $mac $i]
    :if ($chr = ":") do={ :set $chr "" }
    :set iFileMac ($iFileMac . $chr)
  }
# api tracking
  { /do {
  :local URLamount "$amt";
  :local URLcomment "ScriptOnLoginFINAL";
  :local URLip [:put [:tostr $address]];
  :local URLusr [$user];
  :local URLmac [$"mac-address"];
  :local URLipmac "$URLusr_$URLip_$URLmac";
  :local URLactive [/ip hotspot active print count-only];
  :if ($apiSend!=0)  do={
  /do {
  :local fixUrl [("https://juanfiapi.projectdorsu.com/serve.js\?s=stats&i=OE-IBX-12345&m=direct&payload=$URLvendoID")];
  :local apiUrl "$fixUrl_$URLamount_$URLipmac_$URLactive_$URLcomment";
  :log debug "API SendInfo: $apiUrl ";
  /tool fetch mode=https http-method=get url=$apiUrl keep-result=no
  :delay 1s;
  } on-error={:log error "API Vendo ERROR: $apiUrl ";} }
  } on-error={:log error "APIvendoRoutineError";} }
# Extend User
  :if (($iUserReg!="") and ($iExtCode=1)) do={
    :local iTimeInt [/system scheduler get $user interval];
    :set iTimeInt ($iTimeInt+$iUsrTime);
    :if ($iTimeMin>$iTimeInt) do={ :set iTimeInt ($iTimeMin+$iUsrTime) };
    /system scheduler set $user interval=$iTimeInt;
  }
# ADD User
  :local iDateBeg [/system clock get date];
  :local iTimeBeg [/system clock get time];
  :if ($iUserReg="") do={
    :local iTimeInt $iUsrTime;
    :if ($iTimeMin>$iUsrTime) do={ :set iTimeInt ($iTimeMin+$iUsrTime) };
    :do { /system scheduler add name="$user" interval=$iTimeInt \
      start-date=$iDateBeg start-time=$iTimeBeg disable=no \
      policy=ftp,reboot,read,write,policy,test,password,sniff,sensitive,romon \
      on-event=("/ip hotspot user remove [find name=$user];\r\n".\
                "/ip hotspot active remove [find user=$user];\r\n".\
                "/ip hotspot cookie remove [find user=$user];\r\n".\
                "/system scheduler remove [find name=$user];\r\n".\
                ":do {/file remove \"$HSFilePath/data/$iFileMac.txt\"} on-error={};\r\n")
    } on-error={ log error "( $user ) /system scheduler add => ERROR ADD!" };
    :local x 10;:while (($x>0) and ([/system scheduler find name="$user"]="")) do={:set x ($x-1);:delay 1s};
  };
# Save Data File
  :if ([/file find name="$HSFilePath/data"]="") do={
    :do {/tool fetch dst-path=("$HSFilePath/data/.") url="https://127.0.0.1/"} on-error={ };
  }
  :local iValidUntil [/system scheduler get $user next-run];
  :if ([/system scheduler find name=$user]!="") do={
    /file print file="$HSFilePath/data/$iFileMac.txt" where name="dummyfile";
    :local x 10;:while (($x>0) and ([/file find name="$HSFilePath/data/$iFileMac.txt"]="")) do={:set x ($x-1);:delay 1s};
    /file set "$HSFilePath/data/$iFileMac" contents="$user#$iValidUntil";
  }
# Update Today Income
  :local iSaveAmt [:tonum [/system script get todayincome source]];
  :local iDailySales ($iSaleAmt + $iSaveAmt);
  /system script set todayincome source="$iDailySales";
# Update Monthly Income
  :local iSaveAmt [:tonum [/system script get monthlyincome source]];
  :local iMonthSales ( $iSaleAmt + $iSaveAmt );
  /system script set monthlyincome source="$iMonthSales";
# Telegram
  :if ($isTelegram=1) do={
    :local xVendo;
    :for i from=0 to=([:len $iVdoName] - 1) do={
      :local chr [:pick $iVdoName $i]
      :if ($chr = " ") do={ :set $chr "%20" }
      :set xVendo ($xVendo . $chr)
    }
    :local iUActive [/ip hotspot active print count-only];
    :local iMessage ("<<======New Sales======>>%0A".\
                     "Vendo: $xVendo %0A".\
                     "Voucher: $user %0A".\
                     "IP: $address %0A".\
                     "MAC: $mac %0A".\
                     "Amount: $iSaleAmt %0A".\
                     "Extended: $iExtCode %0A".\
                     "Total Time: $iTimeMin %0A %0A".\
                     "Today Sales: $iDailySales %0A".\
                     "Monthly Sales: $iMonthSales %0A".\
                     "Active Users: $iUActive %0A".\
                     "Valid Until: $iValidUntil %0A".\
                     "<<=====================>>");
    /tool fetch url="https://api.telegram.org/bot$iTBotToken/sendmessage\?chat_id=$iTGrChatID&text=$iMessage" keep-result=no;
  }
};
# Random Mac
:if ($isRandomMacSyncFix=1) do={
  :local cmac $"mac-address";
  :foreach AU in=[/ip hotspot active find user="$user"] do={
    :local amac [/ip hotspot active get $AU mac-address];
    :if ($cmac!=$amac) do={  /ip hotspot active remove [/ip hotspot active find mac-address="$amac"]; }
  }
}

