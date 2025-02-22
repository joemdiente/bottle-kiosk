### hotspot folder for HEX put flash/hotspot for haplite put hotspot only
:local HSFilePath "hotspot";
### enable Random MAC synchronizer
:local isRandomMacSyncFix 0;

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
};
# Random Mac
:if ($isRandomMacSyncFix=1) do={
  :local cmac $"mac-address";
  :foreach AU in=[/ip hotspot active find user="$user"] do={
    :local amac [/ip hotspot active get $AU mac-address];
    :if ($cmac!=$amac) do={  /ip hotspot active remove [/ip hotspot active find mac-address="$amac"]; }
  }
}