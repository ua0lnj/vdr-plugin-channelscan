/***************************************************************************
 *   Copyright (C) 2005 by Reel Multimedia;  Author  Markus Hahn           *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *
 ***************************************************************************
 *
 *   csmenu.h user interaction - declaration file
 *
 ***************************************************************************/

#ifndef __CSMENU_H
#define __CSMENU_H


#include <string>
#include <vector>

#include <unistd.h>

#include <vdr/osdbase.h>
#include <vdr/channels.h>
#include <vdr/menuitems.h>
#include <vdr/thread.h>
#include <vdr/sources.h>
#include <vdr/config.h>

#include "scan.h"
#include "transponders.h"
#include "dirfiles.h"

#define DVB_SYSTEM_1 0
#define DVB_SYSTEM_2 1
#define DTMB         2
#define ISDBT        3

#define device_IPTV  100
#define device_SATIP 200

bool IsCablePath(const std::string& path);
bool IsSatellitePath(const std::string& path);

extern cMutex mutexNames;
extern std::vector < std::string > tvChannelNames;
extern std::vector < std::string > radioChannelNames;
extern std::vector < std::string > dataChannelNames;

#ifdef DEBUG_CHANNELSCAN
extern std::vector < std::string > tvChannelList;
extern std::vector < std::string > radioChannelList;
extern std::vector < std::string > dataChannelList;
#endif

enum eScanMode
{ eModeManual = 0, eModeAuto = 1 };

class cMyMenuEditSrcItem;
extern bool scanning_on_receiving_device;

// --- cMenuChannelscan  ----------------------------------------------------

class cMenuChannelscan:public cOsdMenu
{
  private:
    cSetup data;

    /* moved from Setup menu */
    cScanSetup data_;
    const char *serviceTypeTexts[7];
    const char *analogTypeTexts[2];

    int detailFlag;
    int autoSearch;
    static int source;
    int frequency;
    int channel;
//    int t2systemId;
    int streamId;
    int symbolrate;
    char *srcTexts[MAXDEVICES * MAXRECEIVERS];
    int srcTypes[MAXDEVICES * MAXRECEIVERS];
    int srcDevice[MAXDEVICES * MAXRECEIVERS];
    int srcAdapter[MAXDEVICES * MAXRECEIVERS];
    int srcFrontend[MAXDEVICES * MAXRECEIVERS];
    int srcTuners;
    int srcMS[MAXDEVICES * MAXRECEIVERS];//multystream
    int pvrCard;
    const char *fecTexts[10];
    const char *fecTextsS2[12];
    const char *modTexts[7];
    const char *modTextsA[3];
    const char *sysTexts[2];
    const char *modTextsS2[6];
    const char *plpTexts[2];
    const char *searchTexts[2];
    const char *NITScan[2];
    const char *srScanTexts[3];
    const char *sBwTexts[4];
    const char *regionTexts[6];
    const char *bandTexts[2];
    const char *iptvTexts[2];
    const char *addNewChannelsToTexts[3];
    int addNewChannelsTo;

    int fecStat;
    int fecStatS2;
    int modStat;
    eScanMode scanMode;
    char polarization;
    int bandwidth;
    int detailedScan;
    int srScanMode;
    int regionStat;
    int sysStat;
    int plpStat;
    int numNITScan;
    int analogType;
    int inputStat;
    int iptvScanMode;
    int stdStat;
    int siFrontend;

    static int startIp[4];
    static int endIp[4];
    static int port;

    cScanParameters scp;

    int loopIndex;
    std::vector < int >loopSources;
    static int currentTuner;
    int oldCurrentTuner;
    int sourceType;

    static int channelNumber;
    bool expertSettings;

    int lnbs;
    int currentChannel;

    bool isWizardMode;
    void Set();
    void InitLnbs();
    void TunerDetection();
    void SetInfoBar();
    int InitSource();
    void TunerAdd(int device, int adapter, int frontend, int stp,int mpt,char *txt);
    cMyMenuEditSrcItem *srcItem;
  public:
    cMenuChannelscan(int source = 0, int freq = 12073, int symrate = 27500, char pol = 'H', bool isWizardMode_=false);
     ~cMenuChannelscan();
    virtual eOSState ProcessKey(eKeys Key);
    void SwitchChannel();
    void DiseqShow();
    void Store();
    void AddBlankLineItem(int line, bool selectable = false);
    static int timeout;
    static volatile int scanState;
    ///< internal scan state used to display correct messages in menues
    static char provider[256];
};

// --- cMenuScanActive ----------------------------------------------------


class cMenuScanActive:public cOsdMenu
{
  private:
    int oldChannelNumbers;
    int oldUpdateChannels;
    int LiveBufferTmp;
    bool nitScan_;
    int transponderNum_;
    std::auto_ptr < cScan > Scan;
    cScanParameters *scp;
    bool isWizardMode;
    void ErrorMessage();
  public:
    cMenuScanActive(cScanParameters * sp, bool isWizardMode_=false);
    ~cMenuScanActive();
    eOSState ProcessKey(eKeys Key);
    void AddBlankLineItem(int lines);
    void DeleteDummy();
    void Setup();
    virtual bool NoAutoClose(void)
    {
        return true;
    }
};
// --- cMenuScanActiveItem ----------------------------------------------------

class cMenuScanActiveItem:public cOsdItem
{
  private:
    char *tvChannel;
    char *radioChannel;
  public:
    cMenuScanActiveItem();
    cMenuScanActiveItem(const char *TvChannel, const char *RadioChannel);
     ~cMenuScanActiveItem();
    void SetValues();
};

// taken from menu.c
// --- cMenuSetupBase ---------------------------------------------------------
class cMenuSetupBase:public cMenuSetupPage
{
  protected:
    cSetup data;
    virtual void Store(void);
  public:
    cMenuSetupBase(void);
};


// --- cMenuSetupLNB ---------------------------------------------------------

class cMenuSetupLNB:public cMenuSetupBase
{
  private:
    void Setup(void);
    void SetHelpKeys(void);
    cSource *source;

    const char *useDiSEqcTexts[4];
    const char *lofTexts[5];
    bool diseqcConfRead;
    int diseqObjNumber;
    int lnbNumber;
    int currentchannel;
    int waitMs;
    int repeat;

  public:
    cMenuSetupLNB(void);
    virtual eOSState ProcessKey(eKeys Key);
};

// --- Class cMyMenuEditSrcItem  ----------------------------------------------------

class cMyMenuEditSrcItem:public cMenuEditIntItem
{
  private:
    const cSource *source;
    int currentTuner;
  public:
    virtual void Set(void);
    void SetCurrentTuner(int nrTuner)
    {
        currentTuner = nrTuner;
    }
    cMyMenuEditSrcItem(const char *Name, int *Value, int currentTuner);
    eOSState ProcessKey(eKeys Key);
};

// --- Class cMenuScanInfoItem   ----------------------------------------------------
class cMenuScanInfoItem:public cOsdItem
{
  public:
    cMenuScanInfoItem(const std::string & pos, int f, char c, int sr, int fecIndex);
    const char *FECToStr(int Index);
};

// --- Class cMenuStatusBar   ----------------------------------------------------
class cMenuStatusBar:public cOsdItem
{
    int total;
    int part;
  public:
    cMenuStatusBar(int total, int current, int channelNr, int mode = 0, int region = 0);
    int LiteralCentStat(char **str);
};

// --- Class cMenuStatusBar   ----------------------------------------------------
class cMenuInfoItem:public cOsdItem
{
  public:
    cMenuInfoItem(const char *text, const char *textValue = NULL);
    cMenuInfoItem(const char *text, int intValue, bool tab = false);
    cMenuInfoItem(const char *text, bool boolValue);
};

class cMyMenuEditIpItem : public cMenuEditItem {
protected:
  int *value0;
  int *value1;
  int *value2;
  int *value3;
  int min, max;
  int pos;
//  const char *minString, *maxString;
  virtual void Set(void);
public:
  cMyMenuEditIpItem(const char *Name, int *Value0, int *Value1, int *Value2, int *Value3);
  virtual eOSState ProcessKey(eKeys Key);
  };

#ifdef REELVDR
// --- Class cMenuChannelsItem   ----------------------------------------------------

class cMenuChannelsItem:public cOsdItem
{
  private:
    cDirectoryEntry * entry_;
    bool isDir_;
  public:
    cMenuChannelsItem(cDirectoryEntry * Entry);
    ~cMenuChannelsItem();
    const char *FileName(void)
    {
        return entry_->FileName();
    }
    bool IsDirectory(void)
    {
        return entry_->IsDirectory();
    }
};

// --- cMenuSelectChannelList  ----------------------------------------------------

class cMenuSelectChannelList:public cOsdMenu
{
  private:
    int helpKeys_;
    int level_;
    int current_;
    int hasTimers_;
    std::string oldDir_;
    int DirState();
    bool ChangeChannelList();
    bool CopyUncompressed(std::string & buff);
    int LiveBufferTmp;
    cSetup *SetupData_;
    bool WizardMode_;
    std::string additionalText_;

    eOSState Open(bool Back = false);
    int copyDefaultFavourites;
    bool askOverwriteFav;
  public:
    virtual eOSState ProcessKey(eKeys Key);
    cMenuSelectChannelList(cSetup * SetupData = NULL);
    cMenuSelectChannelList(const char *newTitle, const char *Path, std::string AdditionalText, bool WizardMode = false);
     ~cMenuSelectChannelList();
    const char **ChannelLists();
    void Set();
    void Set_Satellite();

    eOSState Store();
    eOSState Delete();
    void DumpDir();
};

// --- cMenuSelectChannelListFunctions  -------------------------------------------
class cMenuSelectChannelListFunctions:public cOsdMenu
{
  public:
    cMenuSelectChannelListFunctions();
    ~cMenuSelectChannelListFunctions();

    void Set();
};

#endif // REELVDR

// copy 'fromPath' to favourites.conf, overwrite if necessary 
void CopyFavouritesList(std::string fromPath);

// decide if cable's fav list or satellite's fav. list should be copied
// and call CopyFavouritesList(std::string) with appropriate source for fav. list
void CopyFavouritesList();

#endif // __CSMENU_H
