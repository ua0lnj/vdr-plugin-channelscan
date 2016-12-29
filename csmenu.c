/***************************************************************************
 *   Copyright (C) 2005 by Reel Multimedia;  Author  Markus Hahn           *
 *                 2009 Parts rewritten by Tobias Bratfisch                *
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
 *   csmenu.c user interaction
 *
 ***************************************************************************/

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <vdr/plugin.h>
#include <vdr/interface.h>
#include <vdr/diseqc.h>
#include <vdr/menu.h>
#ifdef REELVDR
#include <vdr/debug.h>
#include <vdr/s2reel_compat.h>
#endif
#include <vdr/sources.h>
#include <vdr/tools.h>
#include "channelscan.h"
#include "csmenu.h"
#include "filter.h"
#include "channellistbackupmenu.h"
#include "rotortools.h"
#ifdef REELVDR
#include "../mcli/mcli_service.h"
#endif

#include <bzlib.h>
#include <zlib.h>

#define CHNUMWIDTH 19
#define DVBFRONTEND "/dev/dvb/adapter%d/frontend0"
#define POLLDELAY 1
#define MENUDELAY 3

#ifndef SYS_DTMB
#ifdef SYS_DMBTH
#define SYS_DTMB SYS_DMBTH
#endif
#endif

using std::ofstream;
using std::string;
using std::vector;
using std::cout;
using std::cerr;
using std::endl;

#define DBGM " debug [cs menu]"
//#define DEBUG_CSMENU(format, args...) printf (format, ## args)
#define DEBUG_CSMENU(format, args...)

// XXX check all static vars!
typedef vector < int >::const_iterator iConstIter;

vector < string > tvChannelNames;
vector < string > radioChannelNames;
vector < string > dataChannelNames;

cMutex mutexNames;
cMutex mutexScan;

volatile int cMenuChannelscan::scanState = ssInit;

int cMenuChannelscan::source = 0;
int cMenuChannelscan::startIp[] = {239,0,0,1};
int cMenuChannelscan::endIp[] = {239,0,0,1};
int cMenuChannelscan::port = 0;
int cMenuChannelscan::timeout = 5;
char cMenuChannelscan::provider[256] = {'I','P','T','V'};

int cMenuChannelscan::currentTuner = 0; // static ?? XXX
cTransponders *cTransponders::instance_ = NULL;

int pollCount;
time_t cht;

extern bool OnlyChannelList;
int Ntv = 0, Nradio = 0;

bool scanning_on_receiving_device = false;

// --- Class cMenuChannelscan ------------------------------------------------
#ifdef REELVDR
cMenuChannelscan::cMenuChannelscan(int src, int freq, int symrate, char pol, bool isWiz):cOsdMenu(tr("Channelscan"), CHNUMWIDTH), isWizardMode(false)
#else
cMenuChannelscan::cMenuChannelscan(int src, int freq, int symrate, char pol, bool isWiz):cOsdMenu(tr("Channelscan"), CHNUMWIDTH), isWizardMode(isWiz)
#endif
{
    data = Setup;
    cTransponders::Create();

    polarization = pol;
    symbolrate = symrate;
    frequency = freq;
    if(src != 0/* && source == 0*/)
        source = src;
    channel = 0;
//    t2systemId = 0;
    streamId = 0;

    detailedScan = 0;
    scanState = 0;
    detailFlag = 0;             //DVB_T
    expertSettings = 0;
    srcTuners = 0;
    pvrCard = 0;
    siFrontend = 0;
    TunerDetection();

    data_ = ScanSetup;
    data_.ServiceType = 0;      //Radio + TV
    serviceTypeTexts[0] = tr("Radio + TV");
    serviceTypeTexts[1] = tr("TV only");
    serviceTypeTexts[2] = tr("HDTV only");
    serviceTypeTexts[3] = tr("Radio only");
    serviceTypeTexts[4] = tr("Radio + TV FTA only");
    serviceTypeTexts[5] = tr("TV FTA only");
    serviceTypeTexts[6] = tr("HDTV FTA only");



    analogType = 0;
    analogTypeTexts[0] = tr("TV");
    analogTypeTexts[1] = tr("FM Radio");

    inputStat = 0;

    stdStat = 0;

    fecTexts[0] = tr("none");
    fecTexts[1] = "1/2";
    fecTexts[2] = "2/3";
    fecTexts[3] = "3/4";
    fecTexts[4] = "4/5";
    fecTexts[5] = "5/6";
    fecTexts[6] = "6/7";
    fecTexts[7] = "7/8";
    fecTexts[8] = "8/9";
    fecTexts[9] = tr("auto");

    fecStat = 3;

    fecTextsS2[0] = tr("none");
    fecTextsS2[1] = "1/2";
    fecTextsS2[2] = "2/3";
    fecTextsS2[3] = "3/4";
    fecTextsS2[4] = "3/5";      //S2
    fecTextsS2[5] = "4/5";
    fecTextsS2[6] = "5/6";
    fecTextsS2[7] = "6/7";
    fecTextsS2[8] = "7/8";
    fecTextsS2[9] = "8/9";
    fecTextsS2[10] = "9/10";    //S2
    fecTextsS2[11] = tr("auto");

    fecStatS2 = 3;

    sysStat = 0;
    sysTexts[0] = "S1/T1";
    sysTexts[1] = "S2/T2";

    plpStat = 0;
    plpTexts[0] = tr("Auto");
    plpTexts[1] = tr("Manual");

    // Cable
    modTexts[0] = "QAM 4/QPSK";
    modTexts[1] = "QAM 16";
    modTexts[2] = "QAM 32";
    modTexts[3] = "QAM 64";
    modTexts[4] = "QAM 128";
    modTexts[5] = "QAM 256";
    modTexts[6] = "QAM 64/256 AUTO";

    // ATSC
    modTextsA[0] = tr("Auto");
    modTextsA[1] = "8 VSB";
    modTextsA[2] = "16 VSB";

    // Sat S2
    modTextsS2[0] = tr("Auto");
    modTextsS2[1] = "QPSK";
    modTextsS2[2] = "PSK 8";
    modTextsS2[3] = "APSK 16";
    modTextsS2[4] = "APSK 32";
    modTextsS2[5] = tr("Detailed search");

    modStat = 6;

    searchTexts[0] = tr("Manual");
    searchTexts[1] = tr("SearchMode$Auto");

    NITScan[0] = tr("No");
    NITScan[1] = tr("Yes");

    numNITScan = 0;

    addNewChannelsToTexts[0] = tr("End of channellist");    // default in Setup
    addNewChannelsToTexts[1] = tr("New channellist");
    addNewChannelsToTexts[2] = tr("Bouquets");

    addNewChannelsTo = 0;       // at the end of the list

    if (!source)
        scanMode = eModeAuto;   // auto scan
    else
        scanMode = eModeManual; // manual scan

    bandwidth = 0;              // 0: auto 1: 7MHz 2: 8MHz

    //sRateItem[2] = "6111";

    sBwTexts[0] = tr("Auto");
    sBwTexts[1] = "7 MHz";
    sBwTexts[2] = "8 MHz";
    sBwTexts[3] = "6 MHz";

    regionTexts[0] = tr("EUR");
    regionTexts[1] = tr("RUS terr.");
    regionTexts[2] = tr("RUS cable");
    regionTexts[3] = tr("China");
    regionTexts[4] = tr("USA");
    regionTexts[5] = tr("Japan");

    regionStat = 0;
    if (!strcasecmp(Setup.OSDLanguage,"ru_RU")) regionStat = 1;

    bandTexts[0] = tr("CCIR");
    bandTexts[1] = tr("OIRT");

    srScanTexts[0] = tr("Intelligent 6900/6875/6111");
    srScanTexts[1] = tr("Try all 6900/6875/6111");
    srScanTexts[2] = tr("Manual");

    srScanMode = 0;

    // CurrentChannel has to be greater than 0!
    currentChannel = (::Setup.CurrentChannel == 0) ? 1 : ::Setup.CurrentChannel;

    if (!source)
    {
        source = InitSource();
    }

    if (cPluginChannelscan::AutoScanStat != AssNone)
    {
        scanMode = eModeAuto;   //  auto scan
        switch (cPluginChannelscan::AutoScanStat)
        {
        case AssDvbS:
            sourceType = SAT;   // SATS2
            break;
        case AssDvbC:
            sourceType = CABLE;
            break;
        case AssDvbT:
            sourceType = TERR;
            break;
        default:
            cPluginChannelscan::AutoScanStat = AssNone;
            esyslog("Channelscan service handling error");  //??
            break;
        }
        cRemote::Put(kRed);
    }

    Set();
}

void cMenuChannelscan::TunerAdd(int device, int adapter,int frontend, int stp,int mtp,char *txt) {
//dsyslog("ADD srcTuners %d device %d adapter %d frontend %d stp %d txt %s mtp %d\n",srcTuners,device,adapter,frontend,stp,txt,mtp);
    srcDevice[srcTuners] = device;
    //vdr valid device number
    srcAdapter[srcTuners] = adapter;
    //vdr valid device number
    srcFrontend[srcTuners] = frontend;
    //vdr valid frontend number
    srcTypes[srcTuners] = stp;
    // sourceType pro tuner
    srcMS[srcTuners] = mtp;
    // tuner can multystream
    srcTexts[srcTuners++] = txt;
    // coresonding tuner discription
}


void cMenuChannelscan::TunerDetection() {
#ifdef USE_MCLI
    cPlugin *plug = cPluginManager::GetPlugin("mcli");
    if (plug && dynamic_cast<cThread*>(plug) && dynamic_cast<cThread*>(plug)->Active()) {
        int stp = 0;
        int mtp = 0;
        mclituner_info_t info;
        for (int i = 0; i < MAX_TUNERS_IN_MENU; i++)
            info.name[i][0] = '\0';
        plug->Service("GetTunerInfo", &info);
        for (int i = 0; i < MAX_TUNERS_IN_MENU; i++) {
            if (info.preference[i] == -1 || strlen(info.name[i]) == 0)
                continue;
            else {
                switch (info.type[i])
                {
                    case FE_QPSK:  // DVB-S
                        stp = SAT;
                        break;
                    case FE_DVBS2: // DVB-S2
                        stp = SATS2;
                        break;
                    case FE_OFDM:  // DVB-T
                        stp = TERR;
                        break;
                    case FE_QAM:   // DVB-C
                        stp = CABLE;
                        break;
                }
                bool alreadyThere = false;
                for (int j = 0; j < srcTuners; j++) {
                    if (stp == srcTypes[j])
                        alreadyThere = true;
                }
                if (alreadyThere == false) {
                    srcTypes[srcTuners] = stp;
                    srcDevice[srcTuners] = i;
                    switch (info.type[i])
                    {
                        case FE_DVBS2:
                            srcTexts[srcTuners] = strdup(tr("Satellite-S2"));
                            break;
                        case FE_QPSK:
                            srcTexts[srcTuners] = strdup(tr("Satellite-S"));
                            break;
                        case FE_OFDM:
                            srcTexts[srcTuners] = strdup(tr("Terrestrial"));
                            break;
                        case FE_QAM:
                            srcTexts[srcTuners] = strdup(tr("Cable"));
                            break;
                    }
                    srcTuners++;
                }
            }
        }
    } else {
#endif
    int stp = 0;
    int mtp = 0;
    cChannel * c = new cChannel;
    cDvbTransponderParameters dtp;
    dtp.SetStreamId(1);
    dtp.SetSystem(DVB_SYSTEM_1);
    dtp.SetPolarization('H');
    dtp.SetModulation(QPSK);
    dtp.SetRollOff(ROLLOFF_35);
    dtp.SetCoderateH(FEC_3_4);

    for (int tuner = 0; tuner < MAXDEVICES; tuner++)
    {
        cDevice *device = cDevice::GetDevice(tuner);
        srcTexts[srcTuners] = NULL;
        bool satip = false, dvb = false, iptv = false, strdev = false, bad_name = false;
        int adapter = 0, frontend = 0;
        stp = -1;

        if (device)
        {

            DEBUG_CSMENU(DBGM "tuner %d numprovsys %d type %s name %s\n",tuner,device->NumProvidedSystems(),(const char*)device->DeviceType(),(const char*)device->DeviceName());
//esyslog("tuner %d numprovsys %d type %s name %s \n",tuner,device->NumProvidedSystems(),(const char*)device->DeviceType(),(const char*)device->DeviceName());
            iptv   = strcmp(device->DeviceType(),"IPTV")   == 0;
            satip  = strcmp(device->DeviceType(),"SAT>IP") == 0;
            strdev = strcmp(device->DeviceType(),"STRDev") == 0;
            dvb    = strspn(device->DeviceType(),"DVB")    == 3;
            bad_name  = strlen(device->DeviceType())       <= 1;

            if (strdev || device->NumProvidedSystems() == 0) continue; //STRDev not supported ;)

            cDvbDevice *dvbdevice = (cDvbDevice*)device;
            if (dvbdevice)
            {
                adapter = dvbdevice->Adapter();
                frontend = dvbdevice->Frontend();
            }
            if (adapter < 0 || frontend < 0) dvbdevice = NULL; //satip and iptv is not a dvbdevice

            if (iptv || satip)
            {
                adapter = iptv ? device_IPTV : device_SATIP;  //100 - iptv, 200 - satip
                frontend = atoi(device->DeviceName()+strlen(device->DeviceType())+1); //get device number
            }

            if (!dvbdevice && !satip && !iptv) continue;                      //unknown device or output device

            DEBUG_CSMENU(DBGM "tuner %d adapter %d frontend %d satip %d iptv %d\n", tuner, adapter, frontend, satip ? 1 : 0, iptv ? 1 : 0);
//esyslog( "tuner %d adapter %d frontend %d satip %d iptv %d\n", tuner, adapter, frontend, satip ? 1 : 0, iptv ? 1 : 0);
            if (device && device->Priority()<0 ) // omit tuners that are recording
            {
                if (device->ProvidesSource(cSource::stTerr))                                      //Terr
                {
                    char *txt = NULL;
                    mtp = 0;

                    if (dvbdevice) // /dev/dvb...
                    {
#if VDRVERSNUM >= 20106
                        /* Provides DVB-T2 FE_CAN_MULTISTREAM  */
                        dtp.SetSystem(DVB_SYSTEM_2);
                        c->SetTransponderData(cSource::stTerr, 1000, 1000, dtp.ToString('T'), true);//create virtual channel
                        if (device->ProvidesTransponder(c))                                       //DVB-T2 multistream
                        {
                            mtp = 1; //multistream
                            stp = TERR2;
                        }
                        else
#endif
                        /* Provides DVB-T2   */
                        {
                            if (dvbdevice->ProvidesDeliverySystem(SYS_DVBT2))                     //DVB-T2
                            {
                                stp = TERR2;
                            }
                        }
                        if (stp == TERR2)
                        {
                            asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-T2 - Terrestrial"), tr("Tuner"), tuner);
                        }
                        else
                        {
                            if (dvbdevice->ProvidesDeliverySystem(SYS_DVBT))                      //DVB-T
                            {
                                asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-T - Terrestrial"), tr("Tuner"), tuner);
                                stp = TERR;
                            }
                        }
                        if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
#ifdef SYS_DTMB
                        if (dvbdevice->ProvidesDeliverySystem(SYS_DTMB))                          //DMB-TH
                        {
                            char *txt = NULL;
                            mtp = 0;
                            asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DMB-TH"), tr("Tuner"), tuner);
                            stp = DMB_TH;
                            if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                        }
#endif
                        if (dvbdevice->ProvidesDeliverySystem(SYS_ISDBT))                         //ISDBT
                        {
                            char *txt = NULL;
                            mtp = 0;
                            asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("ISDB-T"), tr("Tuner"), tuner);
                            stp = ISDB_T;
                            if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                        }
                    }
                    if (satip) //sat>ip device
                    {
                        dtp.SetSystem(DVB_SYSTEM_2);
                        dtp.SetStreamId(1);
                        c->SetTransponderData(cSource::stTerr, 180000, 50000, dtp.ToString('T'), true);//create virtual channel

                        if (device->SwitchChannel(c, device->IsPrimaryDevice()))                  //DVB-T2 multistream
                        {
                            mtp = 1; //multistream
                            stp = TERR2;
                        }
                        else
                        {
                            dtp.SetStreamId(0);
                            c->SetTransponderData(cSource::stTerr, 180000, 50000, dtp.ToString('T'), true);//create virtual channel

                            if (device->SwitchChannel(c, device->IsPrimaryDevice()))              //DVB-T2
                            {
                                stp = TERR2;
                            }
                        }
                        if (stp == TERR2)
                        {
                            asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-T2 - Terrestrial"), tr("Tuner"), tuner);
                        }
                        else                                                                      //DVB-T
                        {
                            asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-T - Terrestrial"), tr("Tuner"), tuner);
                            stp = TERR;
                        }
                        if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                    }
                }
                if (device->ProvidesSource(cSource::stCable))                                     //DVB-C Cable
                {
                    char *txt = NULL;
                    mtp = 0;
                    asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-C - Cable"), tr("Tuner"), tuner);
                    stp = CABLE;
                    if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                }
                if (device->ProvidesSource(cSource::stSat))                                       //Sat
                {
                    char *txt = NULL;
                    mtp = 0;
                    int oldDiSEqC = ::Setup.DiSEqC; //remebber DiSEqC
                    ::Setup.DiSEqC = 0;           //DiSEqC off

                    if (dvbdevice) // /dev/dvb...
                    {
                        dtp.SetSystem(DVB_SYSTEM_2);
                        /* Provides DVB-S2 FE_CAN_MULTISTREAM  */
                        c->SetTransponderData(cSource::stSat, 1000, 1000, dtp.ToString('S'), true);//create virtual channel
                        if (device->ProvidesTransponder(c))                                       //DVB-S2 multistream
                        {
                            mtp = 1;//multistream
                            stp = SATS2;
                        }
                        else
                        /* Provides DVB-S2   */
                        {
                            if (dvbdevice->ProvidesDeliverySystem(SYS_DVBS2))                     //DVB-S2
                            {
                                stp = SATS2;
                            }
                        }
                        if (stp == SATS2)
                        {
                            asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-S2 - Satellite"), tr("Tuner"),  tuner);
                        }
                        else
                        {
                            if (dvbdevice->ProvidesDeliverySystem(SYS_DVBS))                      //DVB-S
                            {
                                asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-S - Satellite"), tr("Tuner"),  tuner);
                                stp = SAT;
                            }
                        }
                        if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                    }
                    if (satip) //sat>ip plugin can DVB-S2 only
                    {
                        mtp = 1; //multistream
                        stp = SATS2;
                        asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-S2 - Satellite"), tr("Tuner"),  tuner);
                        if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                    }
                    ::Setup.DiSEqC = oldDiSEqC;   //restore DiSEqC
                }
                if (device->ProvidesSource('I'<<24))                                              //IPTV
                {
                    char *txt = NULL;
                    mtp = 0;
                    asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("DVB-IP - Network"), tr("Device"), frontend);
                    stp = IPTV;
                    if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                }
                if (device->ProvidesSource('V'<<24))                                              //PVR
                {
                    char *txt = NULL;
                    mtp = 0;
                    asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("Analog"), tr("Tuner"), tuner);
                    stp = ANALOG;
                    if (txt) TunerAdd(tuner,pvrCard,frontend,stp,mtp,txt); //frontend$ = /dev/video$
                    pvrCard++; //how many pvr card
                }
                if (device->ProvidesSource('A'<<24))                                              //ATSC
                {
                    char *txt = NULL;
                    mtp = 0;
                    asprintf(&txt, "%d %s (%s %i)", srcTuners + 1, tr("ATSC"), tr("Tuner"), tuner);
                    stp = ATSC;
                    if (txt) TunerAdd(tuner,adapter,frontend,stp,mtp,txt);
                }
                if (stp == -1)
                    esyslog("UNKNOWN DEVICE %d\n", tuner);
            }
        }
    }
#ifdef USE_MCLI
    }
#endif
}

void cMenuChannelscan::InitLnbs()
{
    lnbs = 0;
    for (cDiseqc * diseqc = Diseqcs.First(); diseqc; diseqc = Diseqcs.Next(diseqc))
    {
        if (diseqc != Diseqcs.First() && diseqc->Source() == Diseqcs.Prev(diseqc)->Source())
            continue;

        DEBUG_CSMENU(DBGM " --Menu --- Diseqc Sources  %d --- \n", diseqc->Source());
        loopSources.push_back(diseqc->Source());
        lnbs++;
    }
}

int cMenuChannelscan::InitSource()
{
    if (Setup.DiSEqC > 0)
    {
        cDiseqc *d = Diseqcs.First();
        if (d)
            source = d->Source();
        else
            source = Sources.First()->Code();
    }
    else
        source = Sources.First()->Code();

    return source;
}

void cMenuChannelscan::Set()
{
    int current = Current();

    srcItem = NULL;
    Clear();

    int blankLines = 3;
    sourceType = srcTypes[currentTuner];

    if (scanMode != eModeManual)
    {
        sysStat = 0; //reset for DVB-T2 autoscan
        streamId = 0;
    }

    DEBUG_CSMENU(DBGM "------------source %d %s %d current %d device %d tuner %d frontend %d\n",source, *cSource::ToString(source), srcTypes[currentTuner], current, srcDevice[currentTuner], currentTuner, srcFrontend[currentTuner]);

    Add(new cMenuEditStraItem(tr("Search Mode"), (int *)&scanMode, 2, searchTexts));
    // filter
    if(sourceType != ANALOG) //no dvb service in analog mode
        Add(new cMenuEditStraItem(tr("Servicetype"), &data_.ServiceType, 4, serviceTypeTexts));
    else
        AddBlankLineItem(1);

    if (scanMode != eModeManual && (sourceType == SAT || sourceType == SATS2 || sourceType == TERR || sourceType == TERR2 || sourceType == CABLE))
        Add(new cMenuEditStraItem(tr("NIT Scan"), (int *)&numNITScan, 2, NITScan));
    else
        AddBlankLineItem(1);
    Add(new cMenuEditStraItem(tr("Add new channels to"), &addNewChannelsTo, 2, addNewChannelsToTexts));

    if(srcAdapter[currentTuner] == device_SATIP)
        Add(new cMenuEditIntItem(tr("SAT>IP Frontend"), &siFrontend, 0, 99));
    else
        AddBlankLineItem(1);

    if (::Setup.UseSmallFont == 0)
        AddBlankLineItem(3);

    if (srcTuners > 0)          // if no tune available : prevent CRASH
        Add(new cMenuEditStraItem(tr("Source"), &currentTuner, srcTuners, srcTexts));
    else
        Add(new cOsdItem(tr("No Tuners available"), osUnknown, false));

    switch (sourceType)
    {
        case SAT:
        case SATS2:
            Add( new cMyMenuEditSrcItem(tr("Position"), &source, currentTuner));    //XXX all sources in Diseqc?
            break;
        case TERR:
        case TERR2:
        case DMB_TH:
            if (scanMode != eModeManual)
                Add(new cMenuEditBoolItem(tr("Detailed search"), &detailFlag, tr("no"), tr("yes")));
            if (scanMode)
                Add(new cMenuEditStraItem(tr("Region"), &regionStat, 4, regionTexts));
            break;
        case ISDB_T:
            if (scanMode != eModeManual)
                Add(new cMenuEditBoolItem(tr("Detailed search"), &detailFlag, tr("no"), tr("yes")));
            if (scanMode)
                Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
            break;
        case CABLE:
            if (symbolrate > 8000) symbolrate = 6900;
            if (scanMode != eModeManual)
            {
                Add(new cMenuEditStraItem(tr("Symbol Rates"), &srScanMode, 3, srScanTexts));
                Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
                if (srScanMode == 2)
                {
                    Add(new cMenuEditIntItem(tr("Symbolrate"), &symbolrate));
                    blankLines -= 1;
                }
                Add(new cMenuEditStraItem(tr("Modulation"), &modStat, 7, modTexts));
                blankLines -= 1;
            }
            break;
        case IPTV:
            if (scanMode != eModeManual)
            {
                Add(new cMenuInfoItem(tr("UDP multicast search")));
                Add(new cMyMenuEditIpItem(tr("Start ip address"), &startIp[0],&startIp[1],&startIp[2],&startIp[3]));
                Add(new cMyMenuEditIpItem(tr("End   ip address"), &endIp[0],&endIp[1],&endIp[2],&endIp[3]));
                Add(new cMenuEditIntItem(tr("UDP port"), &port, 0, 65525));
                Add(new cMenuEditStrItem(tr("Provider's Name"), provider, sizeof(provider)));
                Add(new cMenuEditIntItem(tr("Timeout, sec"), &timeout,1,10));
                blankLines += 3;
            }
            break;
        case ANALOG:
            if (scanMode)
            {
//                Add(new cMenuEditStraItem(tr("Analog search"), &analogType, 2, analogTypeTexts));
                analogType = 0;
//                if (analogType == 0) //TV
                {
                    Add(new cMenuEditStraItem(tr("Standard"), &stdStat, 31, Standard));
                    Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
                }
//                else //RADIO
//                {
//                    Add(new cMenuEditStraItem(tr("Band"), &regionStat, 2, bandTexts));
//                    AddBlankLineItem(1);
//                }
                blankLines += 1;
            }
            blankLines -= 1;
            break;
        case ATSC:
            if (scanMode != eModeManual)
                Add(new cMenuEditBoolItem(tr("Detailed search"), &detailFlag, tr("no"), tr("yes")));
            if (scanMode)
                Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
            if (scanMode != eModeManual)
            {
                Add(new cMenuEditStraItem(tr("Modulation"), &modStat, 3, modTextsA));
                blankLines -= 1;
            }
            break;
    }

    if (scanMode != eModeManual)
        AddBlankLineItem(1);

    if (scanMode == eModeManual)
    {
        if (channel > 69 && regionStat == 1)
            channel = 69;
        if (channel > 181 && regionStat == 0)
            channel = 181;
        if (channel > 40 && regionStat == 2)
            channel = 40;
        if (channel > 57 && regionStat == 3)
            channel = 57;
        if (channel > 83 && regionStat == 5)
            channel = 83;
        if (channel > 62 && regionStat == 6)
            channel = 62;

        switch (sourceType)
        {
            case SAT:
                AddBlankLineItem(3);
                Add(new cMenuEditIntItem(tr("Frequency (MHz)"), &frequency));
                Add(new cMenuEditChrItem(tr("Polarization"), &polarization, "HVLR"));
                Add(new cMenuEditIntItem(tr("Symbolrate"), &symbolrate));
                Add(new cMenuEditStraItem("FEC", &fecStat, 10, fecTexts));
                blankLines += 1;
                break;
            case SATS2:
                Add(new cMenuEditStraItem(tr("DVB generation"), &sysStat, 2, sysTexts));
                AddBlankLineItem(1);
                if(sysStat && srcMS[currentTuner])
                    Add(new cMenuEditIntItem(tr("DVB-S2 stream ID"), &streamId, 0, 65525));
                else
                    AddBlankLineItem(1);
                Add(new cMenuEditIntItem(tr("Frequency (MHz)"), &frequency));
                Add(new cMenuEditChrItem(tr("Polarization"), &polarization, "HVLR"));
                Add(new cMenuEditIntItem(tr("Symbolrate"), &symbolrate));
                Add(new cMenuEditStraItem(tr("Modulation"), &modStat, 6, modTextsS2));
                if(sysStat == DVB_SYSTEM_2)
                    Add(new cMenuEditStraItem("FEC", &fecStatS2, 12, fecTextsS2));      //  new var  S2  Fec
                else
                    Add(new cMenuEditStraItem("FEC", &fecStat, 10, fecTexts));
                break;
            case CABLE:
                srScanMode = 2;
                Add(new cMenuEditIntItem(tr("Channel"), &channel));
                AddBlankLineItem(3);
                if (channel)
                {
                    Add(new cMenuInfoItem(tr("Frequency (kHz)"), frequency = cTransponders::channel2Frequency(regionStat, channel, bandwidth) / 1000, true));
                    blankLines -= 1;
                }
                else
                    Add(new cMenuEditIntItem(tr("Frequency (kHz)"), &frequency));
                AddBlankLineItem(1);
                Add(new cMenuEditIntItem(tr("Symbolrate"), &symbolrate));
                Add(new cMenuEditStraItem(tr("Modulation"), &modStat, 7, modTexts));
                if(channel)
                    Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
                if(!frequency && channel)
                {
                    Add(new cMenuInfoItem(tr("Incorrect channel!")));
                    blankLines -= 1;
                }
                blankLines += 2;
                break;
            case TERR2:
                Add(new cMenuEditIntItem(tr("Channel"), &channel));
                Add(new cMenuEditStraItem(tr("DVB generation"), &sysStat, 2, sysTexts));
                if(sysStat && srcMS[currentTuner])
                {
                    Add(new cMenuEditStraItem(tr("Stream scan"), &plpStat, 2, plpTexts));
                    if(plpStat)
                        Add(new cMenuEditIntItem(tr("DVB-T2 stream ID"), &streamId, 0, 65525));
                    else
                        AddBlankLineItem(1);
                }
                else
                    AddBlankLineItem(2);
            case TERR:
            case DMB_TH:
            case ISDB_T:
            case ATSC:
                if (sourceType != TERR2)
                {
                    Add(new cMenuEditIntItem(tr("Channel"), &channel));
                    AddBlankLineItem(3);
                }
                if (channel)
                {
                    Add(new cMenuInfoItem(tr("Frequency (kHz)"), frequency = cTransponders::channel2Frequency(regionStat, channel, bandwidth) / 1000, true));
                    blankLines -= 1;
                }
                else
                    Add(new cMenuEditIntItem(tr("Frequency (kHz)"), &frequency));
                if (sourceType != DMB_TH && sourceType != ISDB_T && sourceType != ATSC)
                    Add(new cMenuEditStraItem(tr("Bandwidth"), &bandwidth, 3, sBwTexts));
                else if (sourceType == ATSC)
                    AddBlankLineItem(1);
                else
                {
                    if (sourceType == DMB_TH) //8 MHz
                        bandwidth = 2;
                    if (sourceType == ISDB_T) //6 MHz
                        bandwidth = 3;
                    Add(new cMenuInfoItem(tr("Bandwidth"), sBwTexts[bandwidth]));
                }
                AddBlankLineItem(1);
                if (sourceType == ATSC)
                    Add(new cMenuEditStraItem(tr("Modulation"), &modStat, 3, modTextsA));
                else
                    AddBlankLineItem(1);
                if (channel && sourceType != ATSC)
                    Add(new cMenuEditStraItem(tr("Region"), &regionStat, 4, regionTexts));
                if (channel && sourceType == ATSC)
                    Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
                if (!frequency && channel)
                {
                    Add(new cMenuInfoItem(tr("Incorrect channel!")));
                    blankLines -= 1;
                }
                blankLines += 2;
                break;
            case IPTV:
                AddBlankLineItem(2);
                Add(new cMyMenuEditIpItem(tr("UDP multicast address"), &startIp[0],&startIp[1],&startIp[2],&startIp[3]));
                Add(new cMenuEditIntItem(tr("UDP port"), &port, 0, 65525));
                Add(new cMenuEditStrItem(tr("Provider's Name"), provider, sizeof(provider)));
                Add(new cMenuEditIntItem(tr("Timeout, sec"), &timeout,1,10));
                blankLines += 4;
                break;
            case ANALOG:
                if (analogType == 0 && inputStat == 0)
                    Add(new cMenuEditIntItem(tr("Channel"), &channel));
                else
                    AddBlankLineItem(1);
                AddBlankLineItem(1);
                Add(new cMenuEditStraItem(tr("Analog search"), &analogType, 2, analogTypeTexts));
                if (analogType == 1)
                {
                    channel = 0;
                    inputStat = 0;
                }
                AddBlankLineItem(1);
                if(channel && inputStat == 0)
                {
                    Add(new cMenuInfoItem(tr("Frequency (kHz)"), frequency = cTransponders::channel2Frequency(regionStat + 100, channel, bandwidth) / 1000, true));
                    blankLines -= 1;
                }
                else if (inputStat == 0)
                    Add(new cMenuEditIntItem(tr("Frequency (kHz)"), &frequency));
                else
                    AddBlankLineItem(1);
                if (analogType == 0)
                {
                    Add(new cMenuEditStraItem(tr("Input"), &inputStat, 11, pvrInput));
                    Add(new cMenuEditStraItem(tr("Standard"), &stdStat, 31, Standard));
                }
                else
                    blankLines += 2;
                AddBlankLineItem(1);
                if (channel && inputStat == 0)
                    Add(new cMenuEditStraItem(tr("Region"), &regionStat, 6, regionTexts));
                if (!frequency && channel && inputStat == 0)
                {
                    Add(new cMenuInfoItem(tr("Incorrect channel!")));
                    blankLines -= 1;
                }
                blankLines += 3;
                break;
            default:
                esyslog("ERROR in - %s:%d check source type \n", __FILE__, __LINE__);
                break;
        }
    }
    else
    {
        if (sourceType != IPTV)
        {
            blankLines += 7;
        }
    }

//    if (::Setup.UseSmallFont || strcmp(::Setup.OSDSkin, "Reel") == 0)
        blankLines -= 3;

    AddBlankLineItem(blankLines);
    //Check this
    SetInfoBar();

#ifdef REELVDR
    if (cRecordControls::Active())
        Skins.Message(mtWarning, tr("Recording is running !"), 5);
    if (sourceType == SAT || sourceType == SATS2)
        SetHelp(tr("Button$Start"), NULL, tr("Button$Update"), isWizardMode?NULL:tr("Channel sel."));
    else
        SetHelp(tr("Button$Start"), NULL, NULL, isWizardMode?NULL:tr("Channel sel."));
#else
    if (sourceType == SAT || sourceType == SATS2)
        SetHelp(tr("Button$Start"), NULL, tr("Button$Update"), NULL);
    else
        SetHelp(tr("Button$Start"), NULL, NULL, NULL);
#endif

    SetCurrent(Get(current));
    Display();
}

void cMenuChannelscan::SetInfoBar() // Check with  cMenuScanActive
{

    DEBUG_CSMENU(" Menus --- %s -- scanState %d ---  \n", __PRETTY_FUNCTION__, scanState);

    switch (scanState)
    {
#ifndef DEVICE_ATTRIBUTES
    case ssInit:
        if (sourceType == SAT || sourceType == SATS2)
        {
            Add(new cMenuInfoItem(tr("DiSEqC"), static_cast < bool > (::Setup.DiSEqC)));
            if (::Setup.DiSEqC)
                DiseqShow();    // TODO  two columns display
        }
//        else
//            AddBlankLineItem(1);
        break;
#endif
    case ssInterrupted:
        Add(new cMenuInfoItem(tr("Scanning aborted")));
        break;
    case ssSuccess:
        Add(new cMenuInfoItem(tr("Scanning succed")));
        break;
    case ssNoTransponder:
        Add(new cMenuInfoItem(tr("Retrieving transponders failed!")));
        Add(new cMenuInfoItem(tr("Please check satellites.conf.")));
        break;
    case ssNoLock:
        Add(new cMenuInfoItem(tr("Tuner obtained no lock!")));
        Add(new cMenuInfoItem(tr("Please check your connections and satellites.conf.")));
        break;
    case ssFilterFailure:
        Add(new cMenuInfoItem(tr("Unpredictable error occured!")));
        Add(new cMenuInfoItem(tr("Please try again later")));
        break;
    default:
        break;
    }
    Add(new cMenuInfoItem(tr("Information:")));
    if (srcTuners > 0)
        Add(new cMenuInfoItem(tr("Device Name:"),(const char*)cDevice::GetDevice(srcDevice[currentTuner])->DeviceName()));
    Add(new cMenuInfoItem(tr("Entries in current channellist"), Channels.MaxNumber()));

}

void cMenuChannelscan::Store()
{
    // TODO: is this really needed? what kind of settings are modified that need to be saved?
    Setup = data;

    cPlugin *Plugin = cPluginManager::GetPlugin("channelscan");
    Plugin->SetupStore("Provider", cMenuChannelscan::provider);  //store to vdr setup.conf
    Setup.Save();
    ScanSetup = data_;          // ScanSetup is not stored, used by filter only
}

eOSState cMenuChannelscan::ProcessKey(eKeys Key)
{
    if (cMenuChannelscan::scanState == ssInterrupted)
    {
        // get back to the channel that was being played, if Interrupted. If not available go to #1
        if (!Channels.SwitchTo(::Setup.CurrentChannel))
            Channels.SwitchTo(1);
        //printf("%s switching back to %d", __PRETTY_FUNCTION__, ::Setup.CurrentChannel);
        cMenuChannelscan::scanState = ssInit;
    }

    bool HadSubMenu = HasSubMenu();

    int oldDetailedScan = detailedScan;
    int oldScanMode = scanMode;
    oldCurrentTuner = currentTuner;
    int oldsysStat = sysStat;
    int oldplpStat = plpStat;
    int oldSourceStat = currentTuner;
    int oldSRScanMode = srScanMode;
    int oldchannel = channel;
    int oldregionStat = regionStat;
    bool oldExpertSettings = expertSettings;
    int oldmodStat = modStat;
    int oldanalogType = analogType;
    int oldinputStat = inputStat;
    int oldstdStat = stdStat;

    // bool removeFlag = false; // for removing channels after "New Channel (auto added)" bouquet
    std::vector < cChannel * >ChannelsToRemove;

    eOSState state = cOsdMenu::ProcessKey(Key);

    /* TB: make the srcItem select a proper satellite */
    if (srcItem && oldCurrentTuner != currentTuner)
    {
        srcItem->SetCurrentTuner(currentTuner);
        srcItem->ProcessKey(kLeft);
        srcItem->ProcessKey(kRight);
        srcItem->Set();
    }

#ifdef REELVDR
    if (state == osUser1 && !isWizardMode)
        return AddSubMenu(new cMenuSelectChannelList(&data));
    if (state == osUser1 && isWizardMode)
        return osUser1; // go to next step in install wiz.
#endif // REELVDR

    if (HadSubMenu && !HasSubMenu())
        Set();

    if (state == osUnknown && (!HadSubMenu || !HasSubMenu()))
    {

        sourceType = srcTypes[currentTuner];
        if (sourceType == IPTV) frequency = startIp[3] * 10;

        scp.device = srcDevice[currentTuner];

        switch (sourceType)
        {
        case ANALOG:
            scp.adapter = pvrCard;
            break;
        default:
            if (srcAdapter[currentTuner] == device_SATIP && siFrontend > 1)
                scp.adapter = srcAdapter[currentTuner] + siFrontend;
            else
                scp.adapter = srcAdapter[currentTuner];
            break;
        }

        scp.frontend = srcFrontend[currentTuner];
        scp.type = sourceType;  // source type
        scp.source = source;
        scp.frequency = frequency;
        switch (sourceType)
        {
        case DMB_TH:
            scp.system = DTMB;
            break;
        case ISDB_T:
            scp.system = ISDBT;
            break;
        case ANALOG:
            scp.system = stdStat;
            break;
        default:
            scp.system = sysStat;
            break;
        }
        scp.bandwidth = bandwidth;
        scp.polarization = (sourceType == ANALOG) ? inputStat : polarization;
        scp.symbolrate = symbolrate;
        scp.fec = (sysStat == DVB_SYSTEM_2) ? fecStatS2 : fecStat;
        scp.detail = scanMode | (detailFlag << 1);
        ///<  detailFlag=1 -> DVB-T offset search
        ///<  espacialy for british broadcasters
        scp.modulation = (sourceType == ANALOG) ? analogType : modStat;
        ///< on S2 value 5 means all mods
        scp.symbolrate_mode = srScanMode;
        scp.nitscan = numNITScan;
        scp.region = regionStat + (sourceType == ANALOG ? 100 : 0);
        scp.streamId = plpStat ? streamId : NO_STREAM_ID_FILTER; // for auto
//        scp.t2systemId = t2systemId;
        memcpy(&scp.startip,startIp,sizeof(scp.startip));
        memcpy(&scp.endip,endIp,sizeof(scp.endip));
        scp.port = (sourceType != IPTV) ? channel : port; //port for iptv, channel num for other

#if 0
        DEBUG_CSMENU(" DEBUG LOAD TRANSPONDER(S)  ---- \n" " Scanning on Tuner \t%d\n" " SourceType  \t%d \n" " Source \t%d \n" " frequency \t%d \n" " Bandwidth \t%d \n" " Polarisation \t%c \n" " Symbolrate \t%d \n" " fec Stat  \t%d \n" " detail search \t%d \n" " modulationStat  \t%d \n" " Symbolrate mode \t%d region %d\n", scp.device, scp.type, scp.source, scp.frequency, scp.bandwidth, scp.polarization, scp.symbolrate, scp.fec, scp.detail, scp.modulation, scp.symbolrate_mode, scp.region);
#endif

        switch (Key)
        {
        case kOk:
            /* Store();
               SwitchChannel();
               return osContinue; */
        case kRed:
            // Check for recording/free tuners already done before starting the plugin.

            /* if (cRecordControls::Active()) {
               Skins.Message(mtError, tr("Recording is running"));
               break; // donot start ChannelScan
               } */

            //addNewChannels
            //::Setup.AddNewChannels = (addNewChannelsTo!=0);
            switch (addNewChannelsTo)
            {
            case 0:
#ifdef REELVDR
                Setup.AddNewChannels = 0;
//#else
//                ScanSetup.AddNewChannels = 0;
#endif
                break;
            case 1:
                //clear channellist
                {
#ifdef REELVDR
                    //disable livebuffer
                    int LiveBufferTmp =::Setup.PauseKeyHandling;
                    ::Setup.PauseKeyHandling = 0;
#endif
                    //stop replay and set channel edit flag

                    Channels.IncBeingEdited();

                    vector < cChannel * >tmp_channellist;
                    int i;

                    //get pointer to all cChannel objects in Channels-list
                    for (cChannel * ch = Channels.First(); ch; ch = Channels.Next(ch))
                        tmp_channellist.push_back(ch);

                    //Delete all channels from Channels list
                    for (i = 0; i < (int)tmp_channellist.size(); i++)
                        Channels.Del(tmp_channellist[i]);

                    // Renumber
                    Channels.ReNumber();
                    //finished editing channel list
                    Channels.DecBeingEdited();
#ifdef REELVDR
                    //Set back livebuffer since livebuffer flag is once again set&reset in ActiveScan class
                    ::Setup.PauseKeyHandling = LiveBufferTmp;
#endif
                }
                // fallthrough: no break
            case 2:
                //add to own Bouquets
#ifdef REELVDR
                Setup.AddNewChannels = 1;
//#else
//                ScanSetup.AddNewChannels = 1;
#endif
                Setup.Save();
                break;
            default:
                // should not be here
                break;
            }

            //printf("\n\nAddNewChannelsTo: %d\n\n\n", addNewChannelsTo);

            ScanSetup = data_;  // when kRed is pressed, use the selected filters
            scp.source = sourceType == CABLE ? 0x4000 : sourceType == (TERR || TERR2) ? 0xC000 : source;

            ///< TERR:  0; CABLE: 1;  SAT:   2; SATS2: 3

            ///< scanMode == eModeManual: manual scan
            ///< scanMode == eModeAuto: autoscan; take transponder list
            ///< scanMode == 2 is depricated NITScan  is always performed at SAT && Auto scan

            if (scanMode == eModeManual)    // Manual
            {
                cMenuChannelscan::scanState = ssGetChannels;

                if (cTransponders::GetInstance().GetNITStartTransponder())  // free nitStartTransponder_
                    cTransponders::GetInstance().ResetNITStartTransponder(0);
            }

            scp.frequency = scanMode == eModeAuto ? scanMode : frequency;
            cTransponders::GetInstance().Load(&scp);

//            esyslog("%s freq=%d scanMode=%d", __PRETTY_FUNCTION__, scp.frequency, scanMode);
//            esyslog("%s ssGetChannels?=%d", __PRETTY_FUNCTION__, cMenuChannelscan::scanState == ssGetChannels);

            /********** removed the bouquet New Channel (auto added)******************/
            /* not used any more 2009-10-13 by RC (on whish by IB)
               removeFlag = false;
               for (cChannel *c=Channels.First(); c; c = Channels.Next(c)) {
               if (removeFlag) {
               if (c->GroupSep()) // a new bouquet after auto added! Donot delete this
               break;
               ChannelsToRemove.push_back(c);
               } else if (strcasestr(c->Name(),"auto added"))
               removeFlag = true; // remove every channel after this tag but // Leave the tag UNCHANGED
               }

               for (std::vector<cChannel*>::iterator it=ChannelsToRemove.begin(); it != ChannelsToRemove.end(); it++) {
               printf("removing %s\n", (*it)? (*it)->Name(): " ");
               Channels.Del(*it);
               }
             */

            return AddSubMenu(new cMenuScanActive(&scp, isWizardMode));

//        case kGreen:
//            if (sourceType == CABLE)
//            {
//                expertSettings = expertSettings ? false : true;
//                Set();
//            }
//            break;
       case kYellow:
            if (sourceType == SAT || sourceType == SATS2)
            {
                Skins.Message(mtInfo, tr("Transponders update is started"));
                if (cTransponders::GetInstance().GetTpl())
                Skins.Message(mtInfo, tr("Transponders updated!"));
            }
            state = osContinue;
            break;
#ifdef REELVDR
        case kBlue:
            // no channel list selection in install wiz. mode
            if (isWizardMode) return osContinue;

            if (cRecordControls::Active())
            {
                Skins.Message(mtError, tr("Recording is running"));
                break;          // donot selectChannelist
            }
            return AddSubMenu(new cMenuSelectChannelList(&data));
#endif
        default:
            state = osContinue;
        }
    }

    if((kNone == Key) && !HadSubMenu)
        state = osUnknown; // Allow closing of osd

    // forces setup if menu layout should be changed
    if (Key != kNone && !HadSubMenu)
    {
        if (oldDetailedScan != detailedScan || oldScanMode != scanMode || oldsysStat != sysStat || oldExpertSettings != expertSettings || oldSourceStat != currentTuner 
        || oldSRScanMode != srScanMode || oldregionStat != regionStat || oldmodStat != modStat || oldanalogType != analogType || oldinputStat != inputStat 
        || oldstdStat != stdStat || oldplpStat != plpStat)
        {
            oldSourceStat = currentTuner;
            if (sysStat == DVB_SYSTEM_1 && sourceType != CABLE){
               if (oldmodStat == 2 && modStat == 3) modStat = 5;
               if (oldmodStat == 5 && modStat == 4) modStat = 2;
               if (oldmodStat == modStat && (modStat == 3 || modStat == 4)) modStat = 1;
            }
            Set();
        }

        if(oldchannel != channel && (Key < k0 || Key > k9)){
            Set();
            cht = NULL;
        }
        else if (oldchannel != channel)
            cht = time(NULL);
    }
    if (cht && (time(NULL) - cht) > 0 && Key == kNone)  //wait 1 sek for press channel number
    {
        Set();
        cht = NULL;
    }
    //DDD("%s returning %d", __PRETTY_FUNCTION__, state);
    return state;
}

void cMenuChannelscan::DiseqShow()
{

    for (iConstIter iter = loopSources.begin(); iter != loopSources.end(); ++iter)
    {
        char buffer[256];

        snprintf(buffer, sizeof(buffer), "LNB %c: %s", 'A' + (int)(iter - loopSources.begin()), *cSource::ToString(*iter));
        //DLOG ("Show fetch Source [%d] %d %s ", (int) iter-loopSources.begin() , *iter, buffer);
        Add(new cMenuInfoItem(buffer));
    }
}

cMenuChannelscan::~cMenuChannelscan()
{
    for (int i = 0; i < srcTuners; i++)
    {
        if (srcTexts[i])
            free(srcTexts[i]);
    }

    if (HasSubMenu())
        CloseSubMenu();

    tvChannelNames.clear();
    radioChannelNames.clear();
    dataChannelNames.clear();
    cPluginChannelscan::AutoScanStat = AssNone;
    cTransponders::Destroy();

    Store();
}

// taken fron vdr/menu.c
void cMenuChannelscan::AddBlankLineItem(int lines)
{
    for (int i = 0; i < lines; i++)
    {
        cOsdItem *item = new cOsdItem;
        item->SetSelectable(false);
        item->SetText(strndup(" ", 1), false);
        Add(item);
    }
}

void cMenuChannelscan::SwitchChannel()
{
    Channels.SwitchTo(currentChannel);
    /* cChannel *c = GetByNumber(currentChannel);
       if (c)
       device->SwitchChannel(c,true); */
}

// --- cMenuScanActive -------------------------------------------------------
#ifndef DBG
#define DBG "DEBUG [cMenuScanActive]: "
#endif

#define COLUMNWIDTH 24

#ifdef REELVDR
cMenuScanActive::cMenuScanActive(cScanParameters * sp, bool isWiz):cOsdMenu(tr("Scan active"), COLUMNWIDTH), isWizardMode(isWiz)
#else
cMenuScanActive::cMenuScanActive(cScanParameters * sp, bool isWiz):cOsdMenu(tr("Scan active"), COLUMNWIDTH), isWizardMode(false)
#endif
{
    Channels.IncBeingEdited();

    scp = sp;

    DEBUG_CSMENU(" Menus --- %s -- freq  %d ---  \n", __PRETTY_FUNCTION__, scp->frequency);

    oldUpdateChannels =::Setup.UpdateChannels;
    ::Setup.UpdateChannels = 0; // prevent  VDRs own update Channel

#ifdef REELVDR
    LiveBufferTmp =::Setup.PauseKeyHandling;
    ::Setup.PauseKeyHandling = 0;
#endif

    oldChannelNumbers = Channels.MaxNumber();

    // Make class
    tvChannelNames.clear();
    radioChannelNames.clear();
//    esyslog("%s ssGetChannels?=%d", __PRETTY_FUNCTION__, cMenuChannelscan::scanState == ssGetChannels);

    if (cTransponders::GetInstance().GetNITStartTransponder())
    {
        DEBUG_CSMENU(" Menus ---  Set NIT Auto search \n");
        cMenuChannelscan::scanState = ssGetTransponders;
    }
//    esyslog("%s ssGetChannels?=%d", __PRETTY_FUNCTION__, cMenuChannelscan::scanState == ssGetChannels);

    // auto_ptr
    Scan.reset(new cScan());

    isyslog(" start Scanning @ Card %d --- ", scp->device);

    if (!Scan->StartScanning(scp))
    {
        DEBUG_CSMENU(" Scan() returns failure   \n");
        esyslog(ERR "  Tuner Error\n");
        cMenuChannelscan::scanState = ssInterrupted;
    }
    if (scp->type == SAT || scp->type == SATS2)
       while(!Scan->GetCurrentSR()){
            if (cMenuChannelscan::scanState != ssGetChannels) break;
            usleep(100);
    }

    Setup();

    Channels.Save();
}

void cMenuScanActive::Setup()
{
    int num_tv = 0, num_radio = 0;
    int frequency = scp->frequency;
    const char *modTexts[12];
    modTexts[0] = "QPSK";
    modTexts[1] = "QAM 16";
    modTexts[2] = "QAM 32";
    modTexts[3] = "QAM 64";
    modTexts[4] = "QAM 128";
    modTexts[5] = "QAM 256";
    modTexts[6] = "QAM AUTO";
    modTexts[7] = "VSB 8";
    modTexts[8] = "VSB 16";
    modTexts[9] = "PSK 8";
    modTexts[10] = "APSK 16";
    modTexts[11] = "APSK 32";

    const char *fecTexts[12];
    fecTexts[0] = "NONE";
    fecTexts[1] = "1/2";
    fecTexts[2] = "2/3";
    fecTexts[3] = "3/4";
    fecTexts[4] = "4/5";
    fecTexts[5] = "5/6";
    fecTexts[6] = "6/7";
    fecTexts[7] = "7/8";
    fecTexts[8] = "8/9";
    fecTexts[9] = "AUTO";
    fecTexts[10] = "3/5";
    fecTexts[11] = "9/10";

    const char *sysTexts[4];
    sysTexts[0] = "S1";
    sysTexts[1] = "S2";
    sysTexts[2] = "T1";
    sysTexts[3] = "T2";

    Clear();
    mutexNames.Lock();

    vector < string >::iterator tv;
    vector < string >::iterator radio;

    tv = tvChannelNames.begin();
    radio = radioChannelNames.begin();

    num_tv = tvChannelNames.size();
    num_radio = radioChannelNames.size();

    //maxLINES defines the number of TV/Radio channelnames displayed on the OSD
    //DisplayMenu()->MaxItems() get the number from the Skin
    int maxLINES = DisplayMenu()->MaxItems() - 8;
    if ((int)tvChannelNames.size() > maxLINES)
        tv += tvChannelNames.size() - maxLINES;
    if ((int)radioChannelNames.size() > maxLINES)
        radio += radioChannelNames.size() - maxLINES;
    Add(new cMenuInfoItem(tr("TV Channels\tRadio Channels")), COLUMNWIDTH);
//    Add(new cMenuInfoItem(""));
    /// Display channel names
    for (;;)
    {
        if (tv == tvChannelNames.end() && radio == radioChannelNames.end())
            break;

        cMenuScanActiveItem *Item = new cMenuScanActiveItem(tv == tvChannelNames.end()? "" : tv->c_str(), radio == radioChannelNames.end()? "" : radio->c_str());
        Add(Item);
        if (tv != tvChannelNames.end())
            ++tv;
        if (radio != radioChannelNames.end())
            ++radio;
    }

    int nameLines = tvChannelNames.size() > radioChannelNames.size()? tvChannelNames.size() : radioChannelNames.size();

    if (nameLines > maxLINES)
        nameLines = maxLINES;

    mutexNames.Unlock();

    AddBlankLineItem(maxLINES + 1 - nameLines);

    char buffer[80];
    char buffer1[80] = "";

    if (cMenuChannelscan::scanState == ssGetTransponders)
        AddBlankLineItem(1);
    else
    {
        snprintf(buffer, 50, "TV: %i \tRadio: %i ", num_tv, num_radio);
        FILE *fp = fopen("/tmp/cScan.log", "a");
        const time_t tt = time(NULL);
        char *strDate;
        asprintf(&strDate, "%s", asctime(localtime(&tt)));
        strDate[strlen(strDate) - 1] = 0;
        fprintf(fp, "\n%s TV:%d \t Radio: %d", strDate, num_tv, num_radio);
        for (int i = Ntv; i < num_tv; i++)
            fprintf(fp, "\n\t%s", tvChannelNames[i].c_str());
        for (int i = Nradio; i < num_radio; i++)
            fprintf(fp, "\n\t\t%s", radioChannelNames[i].c_str());
        fclose(fp);
        free(strDate);

        Ntv = num_tv;
        Nradio = num_radio;
        Add(new cMenuInfoItem(buffer));
        transponderNum_ = cTransponders::GetInstance().v_tp_.size();
    }

    if (cMenuChannelscan::scanState <= ssGetChannels)
    {

        // TV SD, TV HD, Radio only, TV (HD+SD),  TV TV (HD+SD);
        const char *serviceTxt[7];

        serviceTxt[0] = trNOOP("Radio + TV");
        serviceTxt[1] = trNOOP("TV only");
        serviceTxt[2] = trNOOP("HDTV only");
        serviceTxt[3] = trNOOP("Radio only");
        serviceTxt[4] = trNOOP("Radio + TV FTA only");
        serviceTxt[5] = trNOOP("TV FTA only");
        serviceTxt[6] = trNOOP("HDTV FTA only");

        if (cMenuChannelscan::scanState == ssGetTransponders)
        {
            snprintf(buffer, sizeof(buffer), tr("Retrieving transponder list from %s"), tr(cTransponders::GetInstance().Position().c_str()));
            Add(new cMenuStatusBar(100, Scan->GetCurrentTransponderNr() * 2, 0, 1));
        }
        else if (cMenuChannelscan::scanState == ssGetChannels)
        {
            if (scp->type == SAT || scp->type == SATS2)
            {
                int mod = Scan->GetCurrentModulation();
                int sr = Scan->GetCurrentSR();
                int fec = Scan->GetCurrentFEC();
                if (mod < 0 || mod > 11)
                    mod = 0;
                if (fec < 0 || fec > 14)
                    fec = 0;

                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%iMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency(), tr(serviceTxt[ScanSetup.ServiceType]));
                if (scp->adapter >= device_SATIP) //sat>ip
                    snprintf(buffer1, sizeof(buffer1), "(DVB-%s, %s, SR %i, FEC %s, SAT>IP %d)", sysTexts[Scan->GetCurrentSystem()],modTexts[mod], sr, fecTexts[fec], scp->frontend);
                else
                    snprintf(buffer1, sizeof(buffer1), "(DVB-%s, %s, SR %i, FEC %s, TUNER %d)", sysTexts[Scan->GetCurrentSystem()],modTexts[mod], sr, fecTexts[fec], Scan->CardNR());
            }
            else if (scp->type == CABLE)
            {
                int mod = Scan->GetCurrentModulation();
                int sr = Scan->GetCurrentSR();
                if (mod < 0 || mod > 6)
                    mod = 0;

                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%.1fMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency() / (1000.0 * 1000), tr(serviceTxt[ScanSetup.ServiceType]));
                if (scp->adapter >= device_SATIP) //sat>ip
                    snprintf(buffer1, sizeof(buffer1), "(%s, SR %i, SAT>IP %d)", modTexts[mod], sr, scp->frontend);
                else
                    snprintf(buffer1, sizeof(buffer1), "(%s, SR %i, TUNER %d)", modTexts[mod], sr, Scan->CardNR());
            }
            else if (scp->type == ATSC)
            {
                int mod = Scan->GetCurrentModulation();
                if (mod < 0 || mod > 1)
                    mod = 0;

                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%.1fMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency() / (1000.0 * 1000), tr(serviceTxt[ScanSetup.ServiceType]));
                if (scp->adapter >= device_SATIP) //sat>ip
                    snprintf(buffer1, sizeof(buffer1), "(%s, SAT>IP %d)", modTexts[mod], scp->frontend);
                else
                    snprintf(buffer1, sizeof(buffer1), "(%s, TUNER %d)", modTexts[mod], Scan->CardNR());
            }
            else if (scp->type == TERR || scp->type == TERR2)
            {
                //printf("%s", tr(cTransponders::GetInstance().Position().c_str()));
                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%.3fMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency() / (1000.0 * 1000), tr(serviceTxt[ScanSetup.ServiceType]));
                if (scp->adapter >= device_SATIP) //sat>ip
                    snprintf(buffer1, sizeof(buffer1), "(DVB-%s, SAT>IP %d)", sysTexts[Scan->GetCurrentSystem()+2],scp->frontend);
                else
                    snprintf(buffer1, sizeof(buffer1), "(DVB-%s, TUNER %d)", sysTexts[Scan->GetCurrentSystem()+2],Scan->CardNR());
            }
            else if (scp->type == IPTV)
            {
                //printf("%s", tr(cTransponders::GetInstance().Position().c_str()));
                const char *a = Scan->GetCurrentParameters();
                char *b[15];
                memset(b,'\0',15);
                if (a && *a){
                    const char *c = strrchr(a,'|');
                    memcpy(b,a+16,strlen(a)-16-strlen(c));
                }
                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%s:%d) %s"), tr(cTransponders::GetInstance().Position().c_str()), b, scp->port, tr(serviceTxt[ScanSetup.ServiceType]));
                snprintf(buffer1, sizeof(buffer1), "(UDP multicast, IPTV %d)", scp->frontend);
            }
            else if (scp->type == ANALOG)
            {
                if (scp->polarization == 0) //tv tuner input
                {
                    //printf("%s", tr(cTransponders::GetInstance().Position().c_str()));
                    snprintf(buffer, sizeof(buffer), tr("Scanning %s (%.3fMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency() / 1000.0, tr(serviceTxt[ScanSetup.ServiceType]));
                    snprintf(buffer1, sizeof(buffer1), "(ANALOG, TUNER %d)", Scan->CardNR());
                } else {  //external input
                    snprintf(buffer, sizeof(buffer), tr("Scanning %s (%s) %s"), tr(cTransponders::GetInstance().Position().c_str()), pvrInput[scp->polarization], tr(serviceTxt[ScanSetup.ServiceType]));
                    snprintf(buffer1, sizeof(buffer1), "(ANALOG, TUNER %d)", Scan->CardNR());
                }
            }
            else if (scp->type == DMB_TH)
            {
                //printf("%s", tr(cTransponders::GetInstance().Position().c_str()));
                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%.3fMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency() / (1000.0 * 1000), tr(serviceTxt[ScanSetup.ServiceType]));
                if (scp->adapter >= device_SATIP) //sat>ip future may be..
                    snprintf(buffer1, sizeof(buffer1), "(DMB-TH, SAT>IP %d)", scp->frontend);
                else
                    snprintf(buffer1, sizeof(buffer1), "(DMB-TH, TUNER %d)", Scan->CardNR());
            }
            else if (scp->type == ISDB_T)
            {
                //printf("%s", tr(cTransponders::GetInstance().Position().c_str()));
                snprintf(buffer, sizeof(buffer), tr("Scanning %s (%.3fMHz) %s"), tr(cTransponders::GetInstance().Position().c_str()), Scan->GetCurrentFrequency() / (1000.0 * 1000), tr(serviceTxt[ScanSetup.ServiceType]));
                if (scp->adapter >= device_SATIP) //sat>ip future may be...
                    snprintf(buffer1, sizeof(buffer1), "(ISDB-T, SAT>IP %d)", scp->frontend);
                else
                    snprintf(buffer1, sizeof(buffer1), "(ISDB-T, TUNER %d)", Scan->CardNR());
            }

            Add(new cMenuStatusBar(transponderNum_, Scan->GetCurrentTransponderNr(), Scan->GetCurrentChannelNumber(), 0, Scan->GetCurrentRegion()));
        }
        Add(new cMenuInfoItem(buffer));
        Add(new cMenuInfoItem(buffer1));
        if (cMenuChannelscan::scanState == ssGetChannels)
        {
            if (scp->type != ANALOG || (scp->type == ANALOG && scp->modulation != 1))
                Add(new cMenuStatusBar(100, cDevice::GetDevice(Scan->CardNR())->SignalStrength(), 0, 2));
            if (scp->type != ANALOG)
                Add(new cMenuStatusBar(100, cDevice::GetDevice(Scan->CardNR())->SignalQuality(), 0, 3));
            else
                AddBlankLineItem(1);
        }
    }
    else if (cMenuChannelscan::scanState > ssGetChannels)
    {
        if (cMenuChannelscan::scanState == ssSuccess)
        {
            if ((Channels.MaxNumber() > 1) && Channels.MaxNumber() == oldChannelNumbers)
                Add(new cMenuInfoItem(tr("No new channels found")));
            else if (Channels.MaxNumber() > oldChannelNumbers)
                Add(new cMenuInfoItem(tr("Added new channels"), (Channels.MaxNumber() - oldChannelNumbers)));
        }
        if (cMenuChannelscan::scanState == ssWait)
        {
            Add(new cMenuInfoItem(tr("Press RED to save or OK to finish or Back for new scan")));
            SetHelp(tr("Button$Save"), NULL, NULL, NULL);
        }
        else if (cPluginChannelscan::AutoScanStat == AssNone)
        {
            Add(new cMenuInfoItem(tr("Press OK to finish or Back for new scan")));
            SetHelp(NULL, NULL, NULL, NULL);
        }
        ErrorMessage();
    }

    Display();
}

// show this only cMenuChannelscan
void cMenuScanActive::ErrorMessage()
{
    if (cMenuChannelscan::scanState >= ssInterrupted)
    {
        /* if (cPluginChannelscan::AutoScanStat != AssNone) {
           cPluginChannelscan::AutoScanStat = AssNone;
           cRemote::CallPlugin("install");
           } */
        switch (cMenuChannelscan::scanState)
        {
        case ssInterrupted:
            Skins.Message(mtInfo, tr("Scanning aborted"));
            break;
            // Scanning aborted by user
            /*
               case ssDeviceFailure: Skins.Message(mtError, tr("Tuner error! ")); break;
               // missing device file etc.
               case ssNoTransponder: Skins.Message(mtError, tr("Missing parameter")); break;
               // missing transponderlist due to wrong parameter
               case ssNoLock: Skins.Message(mtError, tr("Tuner error!")); break;
               // reciver error
               case ssFilterFailure: Skins.Message(mtError, tr("DVB services error!")); break;
               // DVB SI or filter error
             */
        default:
            break;
        }
    }
}

void cMenuScanActive::DeleteDummy()
{
    DEBUG_CSMENU(" --- %s --- %d -- \n", __PRETTY_FUNCTION__, Channels.Count());

    if (Channels.Count() < 3)
        return;

    cChannel *channel = Channels.GetByNumber(1);
    if (channel && strcmp(channel->Name(), "ReelBox") == 0)
        Channels.Del(channel);
    Channels.ReNumber();
}

eOSState cMenuScanActive::ProcessKey(eKeys Key)
{
    //if (cPluginChannelscan::AutoScanStat != AssNone && !cMenuChannelscan::scanning)
    if (cPluginChannelscan::AutoScanStat != AssNone && cMenuChannelscan::scanState >= ssInterrupted)
    {
        cPluginChannelscan::AutoScanStat = AssNone;
#ifdef REELVDR
        printf("====== Wiz? %d return  \n", isWizardMode);
        return isWizardMode?osUser1:osEnd;
#else
	return osEnd;
#endif	
    }
    eOSState state = cOsdMenu::ProcessKey(Key);

    if( state == osBack && cMenuChannelscan::scanState == ssWait)
    {
        cMenuChannelscan::scanState =  ssInterrupted;
    }

    if (state == osUnknown)
    {
        switch (Key)
        {
        case kRed:
            if (cMenuChannelscan::scanState == ssWait)
            {
                cMenuChannelscan::scanState = ssContinue;
                state = osContinue;
            }
            break;
        case kOk:
            if (cMenuChannelscan::scanState > ssGetChannels)
            {
                cMenuChannelscan::scanState = ssInit;
                //printf(" cMenuChannelscan Call Channels.Save() \n");
                Channels.Save();
#ifdef REELVDR
                if (!isWizardMode)
                cRemote::Put(kChannels);
                return isWizardMode?osUser1:osEnd; // XXX isWizard return osUser1?
#else
		return osEnd;
#endif		
            }
            else
                state = osContinue;
            break;
        case kStop:
        case kBack:
            // shut down scanning
            cMenuChannelscan::scanState = ssInterrupted;
            // free nitStartTransponder_ firefox
            cTransponders::GetInstance().ResetNITStartTransponder(0);
            //cMenuChannelscan::scanning = false;
            // Channel changed back to ::Setup.CurrentChannel in cMenuChannelscan::ProcessKey()
            return osBack;
        default:
            state = osContinue;
        }
        Setup();
    }
    //DDD("%s returning %d", __PRETTY_FUNCTION__, state);
    return state;
}

void cMenuScanActive::AddBlankLineItem(int lines)
{
    for (int i = 0; i < lines; i++)
    {
        cOsdItem *item = new cOsdItem;
        item->SetSelectable(false);
        item->SetText(strndup(" ", 1), false);
        Add(item);
    }
}

cMenuScanActive::~cMenuScanActive()
{
    Scan->ShutDown();

    tvChannelNames.clear();
    radioChannelNames.clear();
    dataChannelNames.clear();
    // XXX
    //cMenuChannelscan::scanning = false;
    cMenuChannelscan::scanState = ssInterrupted;
    scanning_on_receiving_device = false;

    // restore original settings
    ::Setup.UpdateChannels = oldUpdateChannels;

    // call cMenuChannels if kOk
    DeleteDummy();

    Channels.DecBeingEdited();

    // try going back to the "current" Channel if not then  channel#1
    cChannel *channel = Channels.GetByNumber(::Setup.CurrentChannel);
    if (channel)                // && !scanning_on_receiving_device)
    {
        cDevice::PrimaryDevice()->SwitchChannel(channel, true);
        //printf("\nSleeping %s %d",__PRETTY_FUNCTION__,__LINE__);
        usleep(200 * 1000);
    }
//    else // switch to Channel #1
//        Channels.SwitchTo(1);

#ifdef REELVDR
    // restore original settings
    ::Setup.PauseKeyHandling = LiveBufferTmp;
#endif

}

// --- cMenuScanActiveItem ----------------------------------------------------
cMenuScanActiveItem::cMenuScanActiveItem(const char *TvChannel, const char *RadioChannel)
{
    tvChannel = strdup(TvChannel);
    radioChannel = strdup(RadioChannel);
    char *buffer = NULL;
    asprintf(&buffer, "%s \t%s", tvChannel, radioChannel);
    SetText(buffer, false);
    SetSelectable(false);
}

cMenuScanActiveItem::~cMenuScanActiveItem()
{
    free(tvChannel);
    free(radioChannel);
}

// cMenuEditSrcItem taken fron vdr/menu.c
// --- cMenuEditSrcItem ------------------------------------------------------

cMyMenuEditSrcItem::cMyMenuEditSrcItem(const char *Name, int *Value, int CurrentTuner):cMenuEditIntItem(Name, Value, 0)
{
    source = Sources.Get(*Value);
    currentTuner = CurrentTuner;
    Set();
}

void cMyMenuEditSrcItem::Set(void)
{
    if (source)
    {
        char *buffer = NULL;
        asprintf(&buffer, "%s - %s", *cSource::ToString(source->Code()), source->Description());
        SetValue(buffer);
        free(buffer);
    }
    else
        cMenuEditIntItem::Set();
}

eOSState cMyMenuEditSrcItem::ProcessKey(eKeys Key)
{
    eOSState state = cMenuEditItem::ProcessKey(Key);
    int oldCode = source->Code();
    const cSource *oldSrc = source;
    bool found = false;

    if (state == osUnknown)
    {
        if (NORMALKEY(Key) == kLeft)    // TODO might want to increase the delta if repeated quickly?
        {
            if (cSource::IsSat(source->Code()) && !cPluginManager::GetPlugin("mcli") && Setup.DiSEqC > 0)
            {
                source = oldSrc;
                while (!found && source && (source->Code() & cSource::stSat))
                {
                    for (cDiseqc * p = Diseqcs.First(); p && !found; p = Diseqcs.Next(p))
                    {
                        /* only look at sources configured for this tuner */
                        if (source && (source->Code() != oldSrc->Code()) && (source->Code() == p->Source() || (TunerIsRotor(currentTuner) && IsWithinConfiguredBorders(currentTuner, source))))
                        {
                            *value = source->Code();
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        source = (cSource *) source->Prev();
                }
            }
            else
            {
                if (source && source->Prev())
                {
                    found = true;
                    source = (cSource *) source->Prev();
                    *value = source->Code();
                }
            }
        }
        else if (NORMALKEY(Key) == kRight)
        {
            if (cSource::IsSat(source->Code()) && !cPluginManager::GetPlugin("mcli") && Setup.DiSEqC > 0)
            {
                source = oldSrc;
                while (!found && source && (source->Code() & cSource::stSat))
                {
                    for (cDiseqc * p = Diseqcs.First(); p && !found; p = Diseqcs.Next(p))
                    {
                        /* only look at sources configured for this tuner */
                        if (source && (source->Code() != oldSrc->Code()) && (source->Code() == p->Source() || (TunerIsRotor(currentTuner) && IsWithinConfiguredBorders(currentTuner, source))))
                        {
                            *value = source->Code();
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        source = (cSource *) source->Next();

                }
            }
            else
            {
                if (source && source->Next())
                {
                    found = true;
                    source = (cSource *) source->Next();
                    *value = source->Code();
                }
            }
        }
        else
            return state;       // we don't call cMenuEditIntItem::ProcessKey(Key) here since we don't accept numerical input

        if (!found)
            source = Sources.Get(oldCode);

        Set();
        state = osContinue;
    }
    return state;
}

//taken from rotor rotor plugin

cMenuScanInfoItem::cMenuScanInfoItem(const string & Pos, int f, char pol, int sr, int fecIndex)
{

    const char *pos = Pos.c_str();
    char *buffer = NULL;
    asprintf(&buffer, "%s :\t%s %d %c %d %s", tr("Scanning on transponder"), pos, f, pol, sr, FECToStr(fecIndex));

    SetText(strdup(buffer), false);
    SetSelectable(false);
}

const char *cMenuScanInfoItem::FECToStr(int Index)
{
    switch (Index)
    {
    case 1:
        return "1/2";
    case 2:
        return "2/3";
    case 3:
        return "3/4";
    case 4:
        return "4/5";
    case 5:
        return "5/6";
    case 6:
        return "6/7";
    case 7:
        return "7/8";
    default:
        return tr("Auto");
    }
    return tr("Auto");
}

// --- cMenuStatusBar ----------------------------------------------------

cMenuStatusBar::cMenuStatusBar(int Total, int Current, int Channel, int mode, int region)
{
    int barWidth = 50;
    int percent;
    Current += 1;

    if (Current > Total)
        Current = Total;
    if (Total < 1)
        Total = 1;

    // GetReal EditableWidth
    percent = static_cast < int >(((Current) * barWidth / (Total)));

    char buffer[barWidth + 1];
    int i;

    buffer[0] = '[';
    for (i = 1; i < barWidth; i++)
        i < percent ? buffer[i] = '|' : buffer[i] = ' ';

    buffer[i++] = ']';
    buffer[i] = '\0';

    char *tmp;
    int l = 0;
    if (mode==1)
        l = asprintf(&tmp, "%s", buffer);
    else if (mode==0)
    {
        if (Channel)
        {
            const char *regionTexts[6];
            regionTexts[0] = tr("EUR");
            regionTexts[1] = tr("RUS terr.");
            regionTexts[2] = tr("RUS cable");
            regionTexts[3] = tr("China");
            regionTexts[4] = tr("USA");
            regionTexts[5] = tr("Japan");

            l = asprintf(&tmp, "%s %d / %d  (CH: %d %s)", buffer, Current, Total, Channel, regionTexts[region >= 100 ? region - 100: region]);
        }
        else
            l = asprintf(&tmp, "%s %d / %d", buffer, Current, Total);
    }
    else if (mode==2)
        l = asprintf(&tmp, tr("%s Signal:  %d"), buffer, Current);
    else if (mode==3)
        l = asprintf(&tmp, tr("%s Quality: %d"), buffer, Current);


    SetText(strndup(tmp, l), false);
    SetSelectable(false);
    free(tmp);
}

// --- Class cMenuInfoItem -------------------------------------

cMenuInfoItem::cMenuInfoItem(const char *text, const char *textValue)
{
    char *buffer = NULL;
    asprintf(&buffer, "%s  %s", text, textValue ? textValue : "");

    SetText(strdup(buffer), false);
    SetSelectable(false);
    free(buffer);
}

cMenuInfoItem::cMenuInfoItem(const char *text, int intValue, bool tab)
{
    char *buffer = NULL;
    if (tab)
       asprintf(&buffer, "%s:\t%d", text, intValue);
    else
       asprintf(&buffer, "%s: %d", text, intValue);
    SetText(strdup(buffer), false);
    SetSelectable(false);
    free(buffer);
}

cMenuInfoItem::cMenuInfoItem(const char *text, bool boolValue)
{
    char *buffer = NULL;
    asprintf(&buffer, "%s: %s", text, boolValue ? tr("enabled") : tr("disabled"));

    SetText(strdup(buffer), false);
    SetSelectable(false);
    free(buffer);
}

// --- cMyMenuEditIpItem ------------------------------------------------------

cMyMenuEditIpItem::cMyMenuEditIpItem(const char *Name, int *Value0, int *Value1, int *Value2, int *Value3)
:cMenuEditItem(Name)
{
  value0 = Value0;
  value1 = Value1;
  value2 = Value2;
  value3 = Value3;
  min = 0;
  max = 255;
  if (*value0 < min)
     *value0 = max;
  else if (*value0 > max)
     *value0 = min;
  if (*value1 < min)
     *value1 = max;
  else if (*value1 > max)
     *value1 = min;
  if (*value2 < min)
     *value2 = max;
  else if (*value2 > max)
     *value2 = min;
  if (*value3 < min)
     *value3 = max;
  else if (*value3 > max)
     *value3 = min;
  Set();
}

void cMyMenuEditIpItem::Set(void)
{
     char buf[80];
     switch (pos){
        case 0: snprintf(buf, sizeof(buf), "[%03d].%03d.%03d.%03d", *value0,*value1,*value2,*value3);
             break;
        case 1: snprintf(buf, sizeof(buf), "%03d.[%03d].%03d.%03d", *value0,*value1,*value2,*value3);
             break;
        case 2: snprintf(buf, sizeof(buf), "%03d.%03d.[%03d].%03d", *value0,*value1,*value2,*value3);
             break;
        case 3: snprintf(buf, sizeof(buf), "%03d.%03d.%03d.[%03d]", *value0,*value1,*value2,*value3);
             break;
        default:
             snprintf(buf, sizeof(buf), "%03d.%03d.%03d.%03d", *value0,*value1,*value2,*value3);
             pos = 0;
     }
     SetValue(buf);
}

eOSState cMyMenuEditIpItem::ProcessKey(eKeys Key)
{
  eOSState state = cMenuEditItem::ProcessKey(Key);

  if (state == osUnknown) {
     int newValue0 = *value0;
     int newValue1 = *value1;
     int newValue2 = *value2;
     int newValue3 = *value3;
     bool IsRepeat = Key & k_Repeat;
     Key = NORMALKEY(Key);
     int newpos = pos;
     int ch = 0;
     switch (Key) {
       case kNone: break;
       case k0 ... k9:
            switch (pos) {
              case 0:
                   if (fresh) {
                      newValue0 = 0;
                      fresh = false;
                   }
                   newValue0 = newValue0 * 10 + (Key - k0);
                   break;
              case 1:
                   if (fresh) {
                      newValue1 = 0;
                      fresh = false;
                   }
                   newValue1 = newValue1 * 10 + (Key - k0);
                   break;
              case 2:
                   if (fresh) {
                      newValue2 = 0;
                      fresh = false;
                   }
                   newValue2 = newValue2 * 10 + (Key - k0);
                   break;
              case 3:
                   if (fresh) {
                      newValue3 = 0;
                      fresh = false;
                   }
                   newValue3 = newValue3 * 10 + (Key - k0);
                   break;
            }
            break;
       case kLeft:
            pos--;
            fresh = true;
            if (pos < 0 ) pos = 3;
            break;
       case kRight:
            pos++;
            fresh = true;
            if (pos > 3 ) pos = 0;
            break;
       default:
            if (pos < 0 ) pos = 3;
            if (pos > 3 ) pos = 0;
            return state;
       }
     if (newValue0 != *value0 && (!fresh || min <= newValue0) && newValue0 <= max) {
        *value0 = newValue0;
        ch = 1;
        }
     if (newValue1 != *value1 && (!fresh || min <= newValue1) && newValue1 <= max) {
        *value1 = newValue1;
        ch = 1;
        }
     if (newValue2 != *value2 && (!fresh || min <= newValue2) && newValue2 <= max) {
        *value2 = newValue2;
        ch = 1;
        }
     if (newValue3 != *value3 && (!fresh || min <= newValue3) && newValue3 <= max) {
        *value3 = newValue3;
        ch = 1;
        }
     if (newpos != pos) ch = 1;
     if (ch) Set();
     state = osContinue;
     }
  return state;
}

#ifdef REELVDR
// --- Class cMenuChannelsItem  ------------------------------------------------

cMenuChannelsItem::cMenuChannelsItem(cDirectoryEntry * Entry)
{
    entry_ = Entry;
    char *buffer = NULL;
    isDir_ = false;
    if (Entry->IsDirectory())
    {
        isDir_ = true;
        asprintf(&buffer, "\x82 %s", Entry->Title());
    }
    else
        asprintf(&buffer, "     %s", Entry->Title());

    SetText(buffer, true);
    free(buffer);
}

cMenuChannelsItem::~cMenuChannelsItem()
{

}

// --- Class cMenuSelectChannelList ------------------------------------------------

bool AskBeforeCopyingFavourites()
{
    // ask before copy, if --
    // favourites.conf exists AND has some channels/folders
    int fav_count = -1;
    cPlugin *p = cPluginManager::GetPlugin("reelchannellist");
    if (p) p->Service("favourites count", &fav_count);


    cString favPath = AddDirectory(cPlugin::ConfigDirectory("reelchannellist"),
                                   "favourites.conf");

    bool fileExists = (access(*favPath, R_OK)==0);

    printf("Ask before copying? %d, file exists?%d, fav_count %d\n",
           fileExists && fav_count>0, fileExists, fav_count);
    return fileExists && fav_count>0;
}

cMenuSelectChannelList::cMenuSelectChannelList(cSetup * SetupData):cOsdMenu(tr("Channel lists"), 10)
{
    WizardMode_ = false;
    SetupData_ = SetupData;
    helpKeys_ = -1;
    level_ = current_ = 0;
    DirectoryFiles.Load();

    askOverwriteFav = false;// do not ask this question, initially
    // user's answer to copying default favourites list
    copyDefaultFavourites = false;

    Set();
    hasTimers_ = Timers.Count();
    LiveBufferTmp =::Setup.PauseKeyHandling;

}

cMenuSelectChannelList::~cMenuSelectChannelList()
{
    // restore original settings
    ::Setup.PauseKeyHandling = LiveBufferTmp;
}

cMenuSelectChannelList::cMenuSelectChannelList(const char *newTitle, const char *Path, std::string AdditionalText, bool WizardMode):cOsdMenu(tr("Channel lists"), 10)
{
    WizardMode_ = WizardMode;
    if (newTitle && strlen(newTitle))
        SetTitle(newTitle);
    helpKeys_ = -1;
    SetupData_ = &Setup;
    level_ = current_ = 0;
    additionalText_ = AdditionalText;
    if (Path && strlen(Path))
    {
        string StartPath = Path;
        DirectoryFiles.Load(StartPath);
    }
    else
        DirectoryFiles.Load();

    askOverwriteFav = false;// do not ask this question initally
    // user's answer to copying default favourites list
    copyDefaultFavourites = false;

    Set();

    hasTimers_ = Timers.Count();
    if (WizardMode_)
        SetHelp(NULL, tr("Back"), tr("Skip"), NULL);
    LiveBufferTmp =::Setup.PauseKeyHandling;

}


#define PREDEFINED_CHANNELLIST_STR tr("Load predefined channel list")
#define START_CHANNELSCAN_STR tr("Start channelscan")
#define OVERWRITE_FAV_STR tr("    Overwrite Favourites list")
void cMenuSelectChannelList::Set_Satellite()
{
    Clear();
    SetCols(35);



    if (WizardMode_)
        AddFloatingText(tr("You have the choice of loading a predefined channel list OR starting channelscan to find channels"), 50);
    else
        AddFloatingText(tr("Press OK to reload default channel list"), 50);

    Add(new cOsdItem("", osUnknown, false)); // Blank line
    Add(new cOsdItem("", osUnknown, false)); // Blank line

    Add(new cOsdItem(PREDEFINED_CHANNELLIST_STR));

    if (askOverwriteFav) {
        Add(new cMenuEditBoolItem(OVERWRITE_FAV_STR, &copyDefaultFavourites));
    }

    if(WizardMode_) {
        Add(new cOsdItem("", osUnknown, false)); // Blank line
        Add(new cOsdItem(START_CHANNELSCAN_STR));
    }

    Display();

}

void cMenuSelectChannelList::Set()
{
    //if (Current() >= 0)
    //    current_ = Current();

    current_ = 2;
    Clear();

    if (IsSatellitePath(DirectoryFiles.Path()))
    {
        Set_Satellite();

        if (WizardMode_)
            SetHelp(NULL, tr("Back"), tr("Skip"), NULL);
        else
            SetHelp(NULL, NULL, NULL, tr("Functions"));

        return;
    }

    if (additionalText_.size())
    {
        AddFloatingText(additionalText_.c_str(), 50);
        Add(new cOsdItem("", osUnknown, false));
    }

    Add(new cOsdItem(tr("Please select a channellist:"), osUnknown, false));
    Add(new cOsdItem("", osUnknown, false));

    for (cDirectoryEntry * d = DirectoryFiles.First(); d; d = DirectoryFiles.Next(d))
    {
        cMenuChannelsItem *item = new cMenuChannelsItem(d);
        Add(item);
    }

    SetCurrent(Get(current_));

    SetHelp(NULL, NULL, NULL, tr("Functions"));
    Display();
}

bool IsCablePath(const std::string& path)
{
    bool ret = path.find("Cable") != std::string::npos;
    std::cout << path << " is cable? "<< ret << std::endl;
    return ret;
}
bool IsSatellitePath(const std::string& path)
{
    bool ret = path.find("Satellite") != std::string::npos;
    std::cout << path << " is satellite? "<< ret << std::endl;
    return ret;
}

void CopyFavouritesList(std::string favListPath)
{
    if (!favListPath.size())
    {
        esyslog("%s:%d favourites.conf path empty!\n", __FILE__, __LINE__);
        return;
    }

    std::string destPath = *AddDirectory(cPlugin::ConfigDirectory("reelchannellist"),
                                   "favourites.conf");

    std::string cmd = std::string("cp ") + favListPath
            + std::string(" ") + destPath;

    // copy given favourites list to favourites.conf
    SystemExec(cmd.c_str());


    // ask reelchannellist plugin to reload favourites list
    cPlugin *plugin = cPluginManager::GetPlugin("reelchannellist");
    if (plugin)
        plugin->Service("reload favourites list", &cmd /*any non-null pointer here*/);
    else
        esyslog("reelchannellist plugin not found, not reloading favourites list\n");

}

void CopyFavouritesList()
{
    bool isSat = IsSatellitePath(DirectoryFiles.Path());
    bool isCable = IsCablePath(DirectoryFiles.Path());

    std::string favListToCopy;
    if (isSat)
        favListToCopy = "/usr/share/reel/configs/favourites_DE_de-sat.conf";
    else if (isCable)
        favListToCopy = "/usr/share/reel/configs/favourites_DE_de-cable.conf";

    if (favListToCopy.size())
        CopyFavouritesList(favListToCopy);
    else
        esyslog("not copying favourite list, could not determine Sat or Cable\n");
}

bool cMenuSelectChannelList::ChangeChannelList()
{
    cMenuChannelsItem *ci = dynamic_cast<cMenuChannelsItem *> (Get(Current()));

    ::Setup.PauseKeyHandling = 0;

    if (ci && !ci->IsDirectory() && ci->FileName())
    {
        // new thread?
        std::string cmd = "/etc/bin/etc_backup.sh -c"; // backup channels.conf after switch
        SystemExec(cmd.c_str());

        // copy favourites
        std::string confFile = ci->FileName();
        std::cout << "Selected file name: " << confFile<< std::endl;

        confFile = confFile.substr(confFile.find_last_of('/')+1);

        cmd.clear();
        // cable / satellite?
        if (IsSatellitePath(ci->FileName())
                && (confFile == std::string("Germany.conf")
                    || confFile == std::string("Europe.conf") ))
            cmd = "cp /usr/share/reel/configs/favourites_DE_de-sat.conf";
        else if (IsCablePath(ci->FileName())
                 && (confFile == std::string("Kabel-BW.conf")
                     || confFile == std::string("Kabel-Deutschland.conf") ))
            cmd = "cp /usr/share/reel/configs/favourites_DE_de-cable.conf";


        cString favPath = AddDirectory(cPlugin::ConfigDirectory("reelchannellist"),
                                       "favourites.conf");
        bool overwrite = false;
        bool fileExists = (access(*favPath, R_OK)==0);

        // overwrite existing favourites.conf file ?
        if (cmd.length() && fileExists)
            overwrite = Interface->Confirm(tr("Overwrite existing favourites list, also?"),3);

        // have to copy favourites
        if (cmd.length() && (!fileExists || overwrite))
        {
            cmd += " ";
            cmd += *favPath;

            SystemExec(cmd.c_str());

            std::cout<< cmd <<std::endl;
            // ask reelchannellist plugin to reload favourites list
            cPlugin *plugin = cPluginManager::GetPlugin("reelchannellist");
            if (plugin)
                plugin->Service("reload favourites list", &cmd);
            else
                std::cout << "reelchannellist plugin not found" << std::endl;

        }

        string buff = ci->FileName();
        cerr << " cp \"" << ci->FileName() << "\" to \"" << setup::FileNameFactory("link") << "\"" << endl;
        CopyUncompressed(buff);
        bool ret = Channels.Reload(setup::FileNameFactory("link").c_str());
#if !defined(RBMINI) && !defined(RBLITE) && defined(REELVDR)
        CopyToTftpRoot("/etc/vdr/channels.conf");
#endif
        return ret;
    }
    return false;
}

bool cMenuSelectChannelList::CopyUncompressed(std::string & buff)
{

    //std::string comprFileName(buff + ".bz2");
    //if (buff.find(".bz2") == string::npos)
    //compressed = false;

    std::string outName = (setup::FileNameFactory("link").c_str());
    std::ifstream infile(buff.c_str(), std::ios_base::in | std::ios_base::binary);
    std::ofstream outfile(outName.c_str());

    if (outfile.fail() || outfile.bad())
    {
        cerr << " can`t open output file  " << outName << endl;
        return false;
    }

    if (infile.fail() || infile.bad())
    {
        cerr << " can`t open input file  " << buff << endl;
        return false;
    }

    if (buff.find(".bz2") != string::npos)
    {
        cerr << " using bzip2 decompressor" << endl;
        int bzerror = 0;
        int buf = 0;
        FILE *file = fopen(buff.c_str(), "r");
        if (file && !ferror(file))
        {
            BZFILE *bzfile = BZ2_bzReadOpen(&bzerror, file, 0, 0, NULL, 0);
            if (bzerror != BZ_OK)
            {
                BZ2_bzReadClose(&bzerror, bzfile);  /* TODO: handle error */
                fclose(file);
                return false;
            }
            bzerror = BZ_OK;
            while (bzerror == BZ_OK /* && arbitrary other conditions */ )
            {
                int nBuf = BZ2_bzRead(&bzerror, bzfile, &buf, 1 /* size of buf */ );
                if (nBuf && bzerror == BZ_OK)
                {
                    outfile.put(buf);
                }
            }
            if (bzerror != BZ_STREAM_END)
            {
                BZ2_bzReadClose(&bzerror, bzfile);  /* TODO: handle error */
                fclose(file);
                return false;
            }
            else
            {
                BZ2_bzReadClose(&bzerror, bzfile);
                fclose(file);
                return true;
            }
        }
    }
    else if (buff.find(".gz") != string::npos)
    {
        cerr << " using gzip decompressor " << endl;
        gzFile gzfile = gzopen(buff.c_str(), "r");
        int buf;
        if (gzfile)
        {
            while (!gzeof(gzfile))
            {
                buf = gzgetc(gzfile);
                if (buf != 255)
                    outfile.put(buf);
            }
            gzclose(gzfile);
        }
    }
    else
    {                           // uncompressed
        int c;
        /* copy byte-wise */
        while (infile.good())
        {
            c = infile.get();
            if (infile.good())
                outfile.put(c);
        }
    }

    outfile.close();

    return true;
}


eOSState cMenuSelectChannelList::ProcessKey(eKeys Key)
{
    bool HadSubMenu = HasSubMenu();

    eOSState state = cOsdMenu::ProcessKey(Key);

    if (state == osUser1)
    {
        CloseSubMenu();
        AddSubMenu(new cChannellistBackupMenu(eImport));
        return osContinue;
    }
    else if (state == osUser2)
    {
        CloseSubMenu();
        AddSubMenu(new cChannellistBackupMenu(eExport));
        return osContinue;
    }
    else if (state == osUser3)
    {
        CloseSubMenu();
        DDD("Setup.NetServerIP: %s", Setup.NetServerIP);
        std::string cmd = std::string("getConfigsFromAVGServer.sh ") + Setup.NetServerIP + std::string(" channels.conf");
        Skins.Message(mtInfo, tr("Loading channel list ..."));

        if (SystemExec(cmd.c_str()) == 0)
        {
            Channels.Reload("/etc/vdr/channels.conf");
            Skins.Message(mtInfo, tr("Channellist was installed successfully"));
        }
        else
        {
            Skins.Message(mtInfo, tr("Error while loading channellist"));
        }
        return osContinue;
    }
    if (state == osUser4)
    {
        CloseSubMenu();
        AddSubMenu(new cChannellistBackupMenu(eImport, true));
        return osContinue;
    }
    else if (state == osUser5)
    {
        CloseSubMenu();
        AddSubMenu(new cChannellistBackupMenu(eExport, true));
        return osContinue;
    }

    if (HadSubMenu)
        return osContinue;

    //printf("\n%s\n", __PRETTY_FUNCTION__);
    if (Key == kBack)
    {
        Key = kNone;

        if (level_ > 0)
        {
            Open(true);         // back
            Set();
            return osUnknown;
        }
        else
        {
            if (OnlyChannelList)
            {
                OnlyChannelList = false;
                return osEnd;
            }
            return osBack;
        }
    }
    else
    {
        current_ = 0;
    }

//    eOSState state = cOsdMenu::ProcessKey(Key);

    if (state == osUnknown)
    {
        switch (Key)
        {
        case kOk:
            if (DirState() == 1)
            {                   // if no directory
                Skins.Message(mtInfo, tr("Loading channel list ..."));
                if (ChangeChannelList())
                {
                    if (SetupData_)
                    {
                        cChannel *chan = Channels.GetByNumber(1);
                        while (chan && chan->GroupSep())
                        {
                            chan = (cChannel *) chan->Next();
                        }
                        if (chan && !chan->GroupSep())
                        {
                            if (!SetupData_->SetSystemTime) // maybe set to update time via ntp, don't overwrite
                                SetupData_->SetSystemTime = 1;
                            SetupData_->TimeTransponder = chan->Transponder();
                            SetupData_->TimeSource = chan->Source();
                        }
                        // Don't Save() since it's done in destructor (<-whose silly idea is that?) of parent menu (cMenuChannelScan)
                    }
                    else
                    {
                        cChannel *chan = Channels.GetByNumber(1);
                        while (chan && chan->GroupSep())
                            chan = (cChannel *) chan->Next();
                        if (chan && !chan->GroupSep())
                        {
                            if (!::Setup.SetSystemTime)     // maybe set to update time via ntp, don't overwrite
                                ::Setup.SetSystemTime = 1;
                            ::Setup.TimeTransponder = chan->Transponder();
                            ::Setup.TimeSource = chan->Source();
                            ::Setup.Save();
                        }
                    }
                    Skins.Message(mtInfo, tr("Changes Done"));
                    if (hasTimers_ > 0)
                        Skins.Message(mtInfo, tr("Please check your Timers!"));
                    return osUser1;
                }
                else
                {
                    Skins.Message(mtError, tr("Changes failed"));
                }
            }
            else if (DirState() == 2)
            {
                Open();
                Set();
            }
            else if (DirState() == 0)
            {
                if (IsSatellitePath(DirectoryFiles.Path()))
                {
                    cOsdItem *item = Get(Current());
                    const char *text = NULL;
                    if (item) text = item->Text();

                    if (text && (strstr(text, PREDEFINED_CHANNELLIST_STR) ||
                            strstr(text, OVERWRITE_FAV_STR)))
                    {
                        //ask user for confirmation? Then, ask now if not asked.
                        if (AskBeforeCopyingFavourites() && !askOverwriteFav)
                        {
                            askOverwriteFav = true;
                            printf("Overwrite favourites\n");
                            Set();
                            return osContinue;
                        }
                        printf("loading Europe.conf\n");

                        // load default channel listEurope.conf
                        std::string cmd = "cp /etc/vdr/channels/Satellite/Europe.conf";
                        cmd += " " + setup::FileNameFactory("link");
                        SystemExec(cmd.c_str());

                        bool ret = Channels.Reload(setup::FileNameFactory("link").c_str());
                        if (ret)
                            Skins.Message(mtInfo, tr("Reloaded default channel list"));
                        else
                            Skins.Message(mtError, tr("Error loading default channel list"));

                        // copy default list without question OR user _chose_ to copy default list
                        if (!AskBeforeCopyingFavourites() || copyDefaultFavourites)
                            CopyFavouritesList();

                        /* Set system time from the first normal channel in the list */
                        if (WizardMode_) 
                        {
                            cChannel* chan = Channels.First();

                            // look for the first normal channel in the list
                            while (chan && chan->GroupSep()) chan = Channels.Next(chan);

                            if (chan && !chan->GroupSep()) 
                            {
                                if (!::Setup.SetSystemTime)     // maybe set to update time via ntp, don't overwrite
                                    ::Setup.SetSystemTime = 1;
                                ::Setup.TimeTransponder = chan->Transponder();
                                ::Setup.TimeSource = chan->Source();
                                ::Setup.Save();
                                esyslog("Set TimeTransponder to '%d %s'\n", chan->Number(), chan->Name());
                            } else
                            {
                                esyslog("Not setting TimeTransponder after channellist is reloaded: a channel not found\n");
                            }

                        } // WizardMode_
                        else 
                        {
                            esyslog("Not setting TimeTransponder after channellist is reloaded: not in wizardmode\n");
                        }
                        
                        return WizardMode_?osUser1:osBack;
                    }
                    else if(text && strstr(text, START_CHANNELSCAN_STR))
                    {
                        // Call channescan?
                        cRemote::CallPlugin("channelscan");
                        return WizardMode_?osUser1:osBack;
                    }

                }
                else
                {
                // if directory
                Open(true);     // back
                Set();
                }
            }

            break;

            // case kYellow: state = Delete(); Set(); break;
            // case kBlue: return AddSubMenu(new cMenuBrowserCommands); ; Set(); break; /// make Commands Menu
            // Don`t forget Has || Had Submenus check
        case kBlue:
            if (!WizardMode_)
                AddSubMenu(new cMenuSelectChannelListFunctions());
            return osContinue;
        default:
            break;
        }
    }

    state = osUnknown;
    return state;
}

int cMenuSelectChannelList::DirState()
{
    cMenuChannelsItem *ci = dynamic_cast<cMenuChannelsItem *> (Get(Current()));
    return ci ? ci->IsDirectory()? 2 : 1 : 0;
}

eOSState cMenuSelectChannelList::Open(bool Back)
{
    if (!Back)
        level_++;
    else
    {
        level_--;
        DirectoryFiles.Load(Back);
        return osUnknown;
    }

    cMenuChannelsItem *ci = dynamic_cast<cMenuChannelsItem *> (Get(Current()));

    if (ci && ci->FileName())
    {
        string fname = ci->FileName();
        if (Back)
        {
            DirectoryFiles.Load(Back);
            return osUnknown;
        }
        else
        {
            DirectoryFiles.Load(fname);
            return osUnknown;
        }
    }
    return osUnknown;
}

eOSState cMenuSelectChannelList::Delete()   // backup active list
{
    cMenuChannelsItem *ci = dynamic_cast<cMenuChannelsItem *> (Get(Current()));
    if (ci)
    {
        if (Interface->Confirm(tr("Delete channel list?")))
        {
            if (unlink(ci->FileName()) != 0)
            {
                int errnr = errno;
                esyslog("ERROR [channelscan]: can`t delete file %s errno: %d\n", ci->FileName(), errnr);
            }
        }
    }
    string tmp = "";
    DirectoryFiles.Load(tmp);   // reload current directory
    return osUnknown;
}

void cMenuSelectChannelList::DumpDir()
{
    for (cDirectoryEntry * d = DirectoryFiles.First(); d; d = DirectoryFiles.Next(d))
        DEBUG_CSMENU(" d->Entry: %s, Dir? %s %s  \n", d->Title(), d->IsDirectory()? "YES" : "NO", d->FileName());
}

// --- Class cMenuSelectChannelListFunctions ---------------------------------------
cMenuSelectChannelListFunctions::cMenuSelectChannelListFunctions():cOsdMenu(tr("Channel lists functions"))
{
    Set();
    Display();
}

cMenuSelectChannelListFunctions::~cMenuSelectChannelListFunctions()
{
}

void cMenuSelectChannelListFunctions::Set()
{
    SetHasHotkeys();
    Add(new cOsdItem(hk(tr("Import a channellist")), osUser1, true));
    Add(new cOsdItem(hk(tr("Export current channellist")), osUser2, true));
    Add(new cOsdItem(hk(tr("Import favourites list")), osUser4, true));
    Add(new cOsdItem(hk(tr("Export current favourites list")), osUser5, true));
    if (::Setup.ReelboxModeTemp == eModeClient)
        Add(new cOsdItem(hk(tr("Import channels from ReelBox Avantgarde")), osUser3, true));
}

#endif
