/***************************************************************************
 *   Copyright (C) 2005 by Reel Multimedia;  Author:  Markus Hahn          *
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
 *  scan.c provides scanning through given tansponder lists
 *
 ***************************************************************************/


#include <stdio.h>
#include <time.h>
#ifndef DEVICE_ATTRIBUTES
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#endif
#include <linux/videodev2.h>
#include <vdr/device.h>
#ifdef REELVDR
#include <vdr/s2reel_compat.h>
#endif
#include <vdr/sources.h>
#include <vdr/dvbdevice.h>
#include <vdr/diseqc.h>
#include "channelscan.h"
#include "csmenu.h"
#include "scan.h"
//#include "debug.h"
#include "rotortools.h"

//#define SCAN_DELAY 25           // test tp 19.2E 12207/12522
#define SCAN_DELAY 5            // test tp 19.2E 12207/12522
#define DVBS_LOCK_TIMEOUT 3000  // 3000ms earlier
#define DVBS2_LOCK_TIMEOUT 3000
#define DVBC_LOCK_TIMEOUT 3000

#define DBGSCAN "DEBUG [scan]"

#ifdef DEBUG_SCAN
#define LOG_SCAN(x...) dsyslog(x)
#else
#define LOG_SCAN(x...)
#endif

#if 0
#define DEBUG_printf(format, args...) printf (format, ## args)
#else
#define DEBUG_printf(format, args...)
#endif

using std::cout;

cScan::cScan()
{
    origUpdateChannels = Setup.UpdateChannels;
    LOG_SCAN(DBGSCAN "  %s  \n", __PRETTY_FUNCTION__);

    ::Setup.UpdateChannels = 0; // to prevent VDRs own update
    cTransponders & transponders = cTransponders::GetInstance();
    sourceCode = transponders.SourceCode();
    ::Setup.CurrentChannel = cDevice::CurrentChannel();

    newChannels = 0;
    cardnr = -1;
    transponderNr = 0;
    channelNumber = 0;
    frequency = 0;
    system = 0;
    foundNum = 0;
    nitScan = false;
    parameters = "";
    otherMux = 0;

    nitFilter_ = NULL;
    PFilter = NULL;
    SFilter = NULL;
    SMFilter = NULL;
    EFilter = NULL;
}

//--------- Destructor ~cScan -----------------------------------

cScan::~cScan()
{
    Cancel(5);

    LOG_SCAN(DBGSCAN "  %s  \n", __PRETTY_FUNCTION__);
    ::Setup.UpdateChannels = origUpdateChannels;


    if (cMenuChannelscan::scanState <= ssGetChannels)
        cMenuChannelscan::scanState = ssInterrupted;
    scanning_on_receiving_device = false;

    if (nitFilter_)
    {
        cDevice::GetDevice(cardnr)->Detach(nitFilter_);
        delete nitFilter_;
        nitFilter_ = NULL;
    }

    if (PFilter)
    {
        cDevice::GetDevice(cardnr)->Detach(PFilter);
        delete PFilter;
        PFilter = NULL;
    }
    if (SFilter)
    {
        cDevice::GetDevice(cardnr)->Detach(SFilter);
        delete SFilter;
        SFilter = NULL;
    }
    if (EFilter)
    {
        cDevice::GetDevice(cardnr)->Detach(EFilter);
        delete EFilter;
        EFilter = NULL;
    }
    LOG_SCAN("DEBUG [channelscan]  %s end cTransponders::Destroy(); \n", __PRETTY_FUNCTION__);
}

void cScan::ShutDown()
{
    if (cMenuChannelscan::scanState <= ssGetChannels)
        cMenuChannelscan::scanState = ssInterrupted;

    scanning_on_receiving_device = false;
    Cancel(5);
}

bool cScan::StartScanning(cScanParameters * scp)
{

    // activate Network Information
    //  assume nit scan
    scanParameter_ = *scp;
    if (scp->nitscan == 1 && scp->type != IPTV && scp->type != ANALOG)
    {
        nitScan = true;
    }
    else nitScan = false;

    LOG_SCAN(DBGSCAN "  %s  %s \n", __PRETTY_FUNCTION__, nitScan ? "AUTO" : "MANUELL");

    cTransponders & transponders = cTransponders::GetInstance();
    // nit scan takes transponder lists from SI Network Information Table

    if (transponders.GetNITStartTransponder() && nitScan)
    {
        LOG_SCAN(DBGSCAN " tp --   ssGetTransponders (NIT) \n");
        cMenuChannelscan::scanState = ssGetTransponders;
    }
    else
    {
        LOG_SCAN(DBGSCAN " tp --   ssGetChannels  \n");
        cMenuChannelscan::scanState = ssGetChannels;
    }

    if (cMenuChannelscan::scanState != ssGetTransponders && transponders.v_tp_.size() == 0)
    {
        LOG_SCAN(DBGSCAN "  %s   return FALSE  \n", __PRETTY_FUNCTION__);
        esyslog(" Empty Transponderlist size %d\n",(int)transponders.v_tp_.size());
        cMenuChannelscan::scanState = ssNoTransponder;
        return false;
    }

    //  terrestrial auto
    detailedSearch = scp->detail;
    //  cable auto
    srModes = scp->symbolrate_mode; // auto, all, fix

    // Reset internal scan states
    lastLocked = 1;             // for safety
    srstat = -1;
    symbolrate = 0;
    modulation = 0;
    fec = 0;
    lastMod = 0;

    cPlugin *plug = cPluginManager::GetPlugin("mcli");
    if (plug && dynamic_cast<cThread*>(plug) && dynamic_cast<cThread*>(plug)->Active())
        cardnr = scp->type;
    else {
        cardnr = scp->device;
    }

    sourceType = scp->type;

    LOG_SCAN("SET DEBUG [scan.c] scanning_on_receiving_device %d  TRUE \n", cardnr);
    scanning_on_receiving_device = true;

    if (!cDevice::GetDevice(cardnr))
        return false;

    Start();
    return true;
}

//-------------------------------------------------------------------------
void cScan::waitSignal(void)
{
#ifdef DEVICE_ATTRIBUTES
    uint64_t t;
    int n;
    t = 0;
    device->GetAttribute("is.mcli", &t);
    if (t)
    {
        usleep(500 * 1000);
        for (n = 0; n < 10; n++)
        {
            t = 0;
            device->GetAttribute("fe.lastseen", &t);
            if ((time(0) - t) < 2)
                break;
            usleep(500 * 1000);
        }
    }
    else
        sleep(3);
#else
    sleep(3);
#endif
}

//-------------------------------------------------------------------------
int cScan::waitLock(int timeout)
{
#ifdef DEVICE_ATTRIBUTES
    uint64_t lck = 0;
    do
    {
        device->GetAttribute("fe.status", &lck);
        if (lck & FE_HAS_LOCK)
            return 1;
        else if (timeout <= 0)
            return 0;
        usleep(100 * 1000);
        timeout -= 100;
    }
    while (1);
#else
    return device->HasLock(timeout);
#endif
}

//-------------------------------------------------------------------------
uint16_t cScan::getSignal()
{
    uint16_t value = 0;
#ifndef DEVICE_ATTRIBUTES
       value = device->SignalStrength();
#else
    if (device)
    {
        uint64_t v = 0;
        device->GetAttribute("fe.signal", &v);
        value = v;
    }
#endif
    return value;
}

uint16_t cScan::getSNR()
{
    uint16_t value = 0;
#ifndef DEVICE_ATTRIBUTES
       value = device->SignalQuality();
#else
    if (device)
    {
        uint64_t v = 0;
        device->GetAttribute("fe.snr", &v);
        value = v;
    }
#endif
    return value;
}

uint16_t cScan::getStatus()
{
    fe_status_t value;
    memset(&value, 0, sizeof(fe_status_t));
#ifndef DEVICE_ATTRIBUTES
       value = device->HasLock(0) ? FE_HAS_LOCK : FE_TIMEDOUT;
#else
    if (device)
    {
        uint64_t v = 0;
        device->GetAttribute("fe.status", &v);
        value = (fe_status_t) v;
    }
#endif
    return value;
}

//-------------------------------------------------------------------------

int cScan::ScanServices(bool noSDT)
{
    time_t tt_;
    char *strDate;
    int a = 0;
    otherMux = 0;

    cMenuChannelscan::scanState = ssGetChannels;
#ifdef WITH_EIT
    LOG_SCAN("DEBUG [channelscan]: With EIT\n");
    EFilter = new cEitFilter();
#endif
    PFilter = new PatFilter();
    SFilter = new SdtFilter(PFilter);
    SMFilter = new SdtMuxFilter(PFilter);

    PFilter->SetSdtFilter(SFilter);
    PFilter->noSDT = noSDT;

    /* SAT>IP use Rid to assign frontend */
    if (scanParameter_.adapter > 200)
        SFilter->SetRid(scanParameter_.adapter - 200);

    device->AttachFilter(SFilter);
    device->AttachFilter(PFilter);
    device->AttachFilter(SMFilter);
#ifdef WITH_EIT
    device->AttachFilter(EFilter);
#endif

    time_t start = time(NULL);

    int foundSids = 0;
    foundNum = totalNum = 0;
    // Heuristic: Delay scan timeout if Sids or Services withs PIDs are found
    tt_ = time(NULL);
    DEBUG_printf("%s beforeloop:%4.2fs:\n", __PRETTY_FUNCTION__, (float)difftime(tt_, tt));

    int i = 0;
    while (!PFilter->EndOfScan() && (
                                        /* (time(NULL) - start < SCAN_DELAY && cMenuChannelscan::scanning) || */
                                        (time(NULL) - start < (sourceType == IPTV ? cMenuChannelscan::timeout : SCAN_DELAY)
                                        + (SFilter->GetNumUsefulSid() ? SCAN_DELAY : 0) && cMenuChannelscan::scanState == ssGetChannels
                                         /*  || (time(NULL) -PFilter->LastFoundTime() < SCAN_DELAY) */
                                        )))
    {
        PFilter->GetFoundNum(foundNum, totalNum);

        if (totalNum && !foundSids)
        {
            start = time(NULL);
            foundSids = 1;
        }

        //DEBUG_printf("\nSleeping %s %d",__PRETTY_FUNCTION__,__LINE__);
        usleep(200 * 1000);
        //usleep(0 * 1000); // inside loop
        // no difference in time/performance when usleep is commented out

        i++;

        tt_ = time(NULL);
        DEBUG_printf("%s %d INloop:%4.2fs:\n", __PRETTY_FUNCTION__, i, (float)difftime(tt_, start));
    }

    tt_ = time(NULL);
    DEBUG_printf("%s afterloop:%4.2fs\n", __PRETTY_FUNCTION__, (float)difftime(tt_, tt));

    /* if (cMenuChannelscan::scanState >= ssInterrupted && !PFilter->EndOfScan()) {
       LOG_SCAN("DEBUG [channelscan]: ScanServices aborted %d \n", cMenuChannelscan::scanState);
       } */

    tt_ = time(NULL);
    DEBUG_printf("%s afterIF endofscan() :%4.2fs:\n", __PRETTY_FUNCTION__, (float)difftime(tt_, tt));

    //DEBUG_printf("\nSleeping %s %d",__PRETTY_FUNCTION__,__LINE__);
    // usleep(200 * 1000);
    PFilter->GetFoundNum(foundNum, totalNum);
    tt_ = time(NULL);

    if (SFilter->GetNumUsefulSid() == 0 && totalNum > 0) totalNum *= -1;
    if (scanParameter_.streamId > -2) //tuner support mplp
        otherMux = SMFilter->GetNumMux();

    DEBUG_printf("%s after GetFountNum() :%4.2fs:\n", __PRETTY_FUNCTION__, (float)difftime(tt_, tt));

    device->Detach(PFilter);
    device->Detach(SFilter);
    device->Detach(SMFilter);
#ifdef WITH_EIT
    device->Detach(EFilter);
#endif
    tt_ = time(NULL);
    DEBUG_printf("%s after detach filters :%4.2fs:\n", __PRETTY_FUNCTION__, (float)difftime(tt_, tt));

    if (PFilter)
        delete PFilter;
    PFilter = NULL;
    if (SFilter)
        delete SFilter;
    SFilter = NULL;
    if (SMFilter)
        delete SMFilter;
    SMFilter = NULL;
    if (EFilter)
        delete EFilter;
    EFilter = NULL;

    if (nitScan)
    {
        ScanNitServices();      // XXX
    }

    const time_t ttout = time(NULL);
    a = asprintf(&strDate, "%s", asctime(localtime(&ttout)));
    DEBUG_printf("%s OUT:%4.2fms: %s\n", __PRETTY_FUNCTION__, (float)difftime(ttout, tt) * 1000, strDate);
    free(strDate);
    if (a) {}; //remove make warning
    if (tt_ > 0) {}; //remove make warning

    return totalNum;
}

//-------------------------------------------------------------------------
void cScan::ScanNitServices()
{
    LOG_SCAN("DEBUG [cs]; ScanNITService \n");
    nitFilter_ = new NitFilter;
    nitFilter_->mode = sourceType;

    time_t start = time(NULL);

    device->AttachFilter(nitFilter_);

    // updates  status bar in cMenuScanActive
    if (cMenuChannelscan::scanState == ssGetTransponders)
        transponderNr = 0;

    WaitForRotorMovement(cardnr);

    /* use vdr native positioner support */
    if(device->Positioner())
    {
        while(device->Positioner()->IsMoving())
        {
            sleep(1);
        }
    }


    while (!nitFilter_->EndOfScan() && (time(NULL) - start < (SCAN_DELAY * 4) && (cMenuChannelscan::scanState == ssGetTransponders || cMenuChannelscan::scanState == ssGetChannels)))
    {
        WaitForRotorMovement(cardnr);

        /* use vdr native positioner support */
        if(device->Positioner())
        {
            while(device->Positioner()->IsMoving())
            {
                sleep(1);
            }
        }

        if (nitFilter_->Found())
            start = time(NULL);
        // we don`t know how many tranponder we get
        if (cMenuChannelscan::scanState == ssGetTransponders)
            transponderNr = (int)(time(NULL) - start);
        usleep(200 * 1000);     // inside loop
    }
    //save t2 plps if NIT found
    if(nitFilter_->Found())
        memcpy(t2plp,(int*)nitFilter_->T2plp(),sizeof(t2plp));

    device->Detach(nitFilter_);

    if (nitFilter_)
        delete nitFilter_;
    nitFilter_ = NULL;

    // display 100% in status bar
    // while (initalscan &&  transponderNr < 100 && cMenuChannelscan::scanning)

    while (transponderNr < 100 && cMenuChannelscan::scanState == ssGetTransponders)
    {
        DEBUG_printf("Sleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
        usleep(10 * 1000);      // inside loop: only counter incremented!
        // sleep longer so the progress bar doesnot start from 0 again
        transponderNr += 2;
    }
    if (cMenuChannelscan::scanState == ssGetTransponders)
        transponderNr = 0;
}

//-------------------------------------------------------------------------
void cScan::ScanDVB_S(cTransponder * tp, cChannel * c)
{
    int maxmods = scanParameter_.type == SATS2 ? 5 : 1;
    int lockTimeout;
    int foundServices = 0, a = 0;

   // esyslog("%s cTransponder* tp = %x  cChannel *c = %x", __PRETTY_FUNCTION__);
//    esyslog("maxmods = %d sys %d\n",maxmods, tp->System());

    // skip HD Transonders on SD Tuner
    if (maxmods == 1 && tp->System() == DVB_SYSTEM_2)
        return;

    if (device->IsPrimaryDevice()) cDevice::PrimaryDevice()->StopReplay();

    unsigned  int nRadio = radioChannelNames.size();
    unsigned  int nTv = tvChannelNames.size();

    cDvbTransponderParameters dtp(c->Parameters());

    tp->SetTransponderData(c, sourceCode);

    /* skip transponders if no LNB - can not be received, IF fequency out of range*/
    if (Setup.DiSEqC && cMenuChannelscan::scanState == ssGetTransponders)
    {
        const cDiseqc *diseqc = Diseqcs.Get(device->CardIndex()+1, c->Source(), c->Frequency(), dtp.Polarization(), NULL);
        if(diseqc)
        {
            int lof = diseqc->Lof();
            if ((abs(lof - c->Frequency()) > 2150) || (abs(lof - c->Frequency()) < 950))
                return;
        }
        else return;
    }
    else
    {
        if (c->Frequency() < Setup.LnbSLOF)
        {
            int lof = Setup.LnbFrequLo;
            if ((abs(lof - c->Frequency()) > 2150) || (abs(lof - c->Frequency()) < 950))
                return;
        }
        else
        {
            int lof = Setup.LnbFrequHi;
            if ((abs(lof - c->Frequency()) > 2150) || (abs(lof - c->Frequency()) < 950))
                return;
        }
    }

    if (tp->Modulation() != 999 && maxmods > 1) maxmods = 1;

    for (int mod = 0; mod < maxmods; mod++)//need for 999 modulation, try all
    {
        if (tp->Scanned())
            break;

        switch (mod)
        {
        case 0: //set qpsk for dvb-s and psk-8 for dvb-s2 then select auto modulation, manual modulation set as is
             if (tp->Modulation() == 999) continue;
             if (tp->Modulation() == -1 && tp->System() == DVB_SYSTEM_1)
                tp->SetModulation(QPSK);
             if (tp->Modulation() == -1 && tp->System() == DVB_SYSTEM_2)
                tp->SetModulation(PSK_8);
             if (tp->System() == DVB_SYSTEM_2)
                static_cast < cSatTransponder * >(tp)->SetRollOff(ROLLOFF_35);
             LOG_SCAN("DEBUG [DVB_S]; trying AUTO: freq: %d srate:%d   mod: %d  \n",
                tp->Frequency(), tp->Symbolrate(), tp->Modulation());
             break;
        case 1:
             tp->SetModulation(QPSK);
             if (tp->System() == DVB_SYSTEM_2)
                static_cast < cSatTransponder * >(tp)->SetRollOff(ROLLOFF_35);
             LOG_SCAN("DEBUG [DVB_S]; trying QPSK: freq: %d srate:%d   mod: %d  \n",
                tp->Frequency(), tp->Symbolrate(), tp->Modulation());
             break;
        case 2:
             tp->SetModulation(PSK_8);
             if (tp->System() == DVB_SYSTEM_2)
                static_cast < cSatTransponder * >(tp)->SetRollOff(ROLLOFF_35);
             LOG_SCAN("DEBUG [DVB_S]; trying PSK8: freq: %d srate:%d   mod: %d  \n",
                tp->Frequency(), tp->Symbolrate(), tp->Modulation());
             break;
        case 3:
             if (tp->System() != DVB_SYSTEM_2) continue;
             tp->SetModulation(APSK_16);
             static_cast < cSatTransponder * >(tp)->SetRollOff(ROLLOFF_35);
             LOG_SCAN("DEBUG [DVB_S]; trying APSK16: freq: %d srate:%d   mod: %d  \n",
                tp->Frequency(), tp->Symbolrate(), tp->Modulation());
             break;
        case 4:
             if (tp->System() != DVB_SYSTEM_2) continue;
             tp->SetModulation(APSK_32);
             static_cast < cSatTransponder * >(tp)->SetRollOff(ROLLOFF_35);
             LOG_SCAN("DEBUG [DVB_S]; trying APSK32: freq: %d srate:%d   mod: %d  \n",
                tp->Frequency(), tp->Symbolrate(), tp->Modulation());
             break;

        default:
            LOG_SCAN("No such modulation %d", mod);
            break;
        }

        modulation = tp->Modulation();
        symbolrate = tp->Symbolrate();
        system = tp->System();
        fec = static_cast < cSatTransponder * >(tp)->FEC();

        LOG_SCAN("DEBUG [DVB_S] - channel: freq: %d srate:%d   mod: %d  \n",
             c->Frequency(), tp->Symbolrate(), tp->Modulation());

        /* SAT>IP use Rid to assign frontend */
        if (scanParameter_.adapter > 200)
            c->SetId(0, 0, 0, scanParameter_.adapter - 200);

        if (!device->ProvidesTransponder(c)) continue;

        if (!device->SwitchChannel(c, device->IsPrimaryDevice()))
        {
            LOG_SCAN("SwitchChannel(%d)  failed\n", c->Frequency());
#if 0
            LOG_SCAN("try Switch rotor \n");

            struct
            {
                cDevice *device;
                cChannel *channel;
            } data;

            data.device = device;
            data.channel = c;

            cPlugin *p = cPluginManager::GetPlugin("rotor");
            if (p)
            {
                isyslog("Info [channelscan] Switch rotor \n");
                p->Service("Rotor-SwitchChannel", &data);
                DEBUG_printf("\nSleeping %s %d\n",__PRETTY_FUNCTION__,__LINE__);
                usleep(100 * 1000); // inside loop, inside if // wait for rotor
            }
            else
#endif
                break;
        }
        else                    // switch succeeded
        {
            /* use vdr native positioner support */
            if(device->Positioner())
            {
                while(device->Positioner()->IsMoving())
                {
                    sleep(1);
                }
            }

            waitSignal();

            if (lastLocked)
            sleep(3);       // avoid sticky lock

            if (static_cast < cSatTransponder * >(tp)->System() == 1)
                lockTimeout = DVBS2_LOCK_TIMEOUT;
            else
                lockTimeout = DVBS_LOCK_TIMEOUT;

            if (device->HasLock(lockTimeout))
            {
                LOG_SCAN("debug  [scan] -- Got LOCK @ transponder %d  mod  %d fec %d  ---------- \n",
                     c->Transponder(), dtp.Modulation(), dtp.CoderateH());

                if (cMenuChannelscan::scanState == ssGetTransponders)
                {
                    ScanNitServices();
                    return;
                }
                else if (cMenuChannelscan::scanState == ssGetChannels)
                {
                  DEBUG_printf("\nCalling ScanServices()\n");
                    foundServices = ScanServices();
                    if (!foundServices && waitLock(100))    // Still lock, but no channels -> retry
                       foundServices = ScanServices();
                    DEBUG_printf("\nScanServices Done\n");
                }
                DEBUG_printf("\n nTv=%d  tvChannelNames.size()=%d\n",nTv, tvChannelNames.size());

                if (nRadio < radioChannelNames.size() || nTv < tvChannelNames.size())
                {
                  tp->SetScanned();
                  DEBUG_printf("\nSET SCANNED\n");
                }
            }
            else
            {
                lastLocked = 0;
                LOG_SCAN("debug  [scan] -- NO LOCK @ transponder %d  mod  %d fec %d ---------- \n",
                     c->Transponder(), dtp.Modulation(), dtp.CoderateH());
            }

        }
    }
  const time_t ttout = time(NULL);
  char *strDate;
  a = asprintf(&strDate,"%s", asctime(localtime(&ttout)));
  DEBUG_printf("\n%s OUT:%4.2fsec: %s\n",__PRETTY_FUNCTION__,(float)difftime(ttout,tt),strDate);
  if (a) {}; //remove make warning
}

//-------------------------------------------------------------------------

// detail bit 0: wait longer
// detail bit 1: search also +-166kHz offset

void cScan::ScanDVB_T(cTransponder * tp, cChannel * c)
{
    int timeout = 2000;
    int response, n, m, s, plps, p;
    int frequency_orig = tp->Frequency();
    region = scanParameter_.region;
    int Muxs = 0;
    plps = 0;
    memset(t2plp,0,sizeof(t2plp));

    if (device->IsPrimaryDevice()) cDevice::PrimaryDevice()->StopReplay();

    /* For Nit transponders */
    if (frequency_orig < 1000000)
    {
        frequency_orig *= 1000;
        tp->SetFrequency(frequency_orig);
    }

    int offsets[3] = { 0, -166666, 166666 };
    int maxsys = scanParameter_.type == TERR2 ? 2 : 1; //for auto scan need try TERR and TERR2
    if (scanParameter_.frequency > 5) maxsys = 1; //for manual scan not need try both TERR and TERR2

    tp->SetFrequency(frequency_orig);

    for (s = 0; s < maxsys; s++)  //need reset sysStat and streamId in cmenu.c for autoscan
    {
        for (n = 0; n < (detailedSearch & 2 ? 3 : 1); n++)
        {
            LOG_SCAN("DEBUG [channelscan]:  DVB_T detailedSearch %X \n", detailedSearch);
            if (cMenuChannelscan::scanState >= ssInterrupted)
               return;

            if (s > 0) tp->SetSystem(DVB_SYSTEM_2);

            tp->SetFrequency(frequency_orig + offsets[n]);
            frequency = tp->Frequency();
            tp->SetTransponderData(c, sourceCode);
            system = tp->System();

            /* scan mplp transponders
             * 2 way - from NIT and try all plps
             * if SDT filter found some MUX on plp 0, scan plp finished when numbers of plps = numbers of MUX
             * second way is a longest
             */
            int r = 1;
            if (system == DVB_SYSTEM_2 && scanParameter_.streamId == (int)NO_STREAM_ID_FILTER) r = 256;

            /* t2 mplp loop */
            for (p = 0; p < r; p++)
            {
                if (t2plp[p])
                {
                    (dynamic_cast < cTerrTransponder * >(tp))->SetStreamId(p);
                    tp->SetTransponderData(c, sourceCode);
                }
                else if ((scanParameter_.detail == 1 || scanParameter_.streamId == (int)NO_STREAM_ID_FILTER) && !nitScan)
                {
                    (dynamic_cast < cTerrTransponder * >(tp))->SetStreamId(p);
                    tp->SetTransponderData(c, sourceCode);
                }
                else if (nitScan && p > 0) continue;

                /* SAT>IP use Rid to assign frontend */
                if (scanParameter_.adapter > 200)
                    c->SetId(0, 0, 0, scanParameter_.adapter - 200);

                // retune with offset
                if (!device->SwitchChannel(c, device->IsPrimaryDevice()))
                {
                    esyslog("SwitchChannel(c)  failed\n");
                    break;
                }

                LOG_SCAN("Tune %i \n", frequency);
                DEBUG_printf("\nSleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
                usleep(1500 * 1000);    // inside loop
                if (lastLocked)
                {
                    DEBUG_printf("\nSleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
                    sleep(2);           // avoid false lock
                }
                for (m = 0; m < (detailedSearch & 1 ? 8 : 2); m++)
                {
                    response = getStatus();
                    LOG_SCAN("DEBUG [channelscan]:  DVB_T detailedSearch  %X  m %d \n", detailedSearch, m);
                    LOG_SCAN("RESPONSE %x\n", response);

                    if ((response & 0x10) == 0x10)  // Lock
                        break;
                    if ((response & 15) > 2)
                    {                   // Almost locked, give it some more time
                        DEBUG_printf("\nSleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
                        sleep(1);
                    }
                }
                if (cMenuChannelscan::scanState >= ssInterrupted)
                    return;

                if (device->HasLock(timeout))
                {
                    LOG_SCAN(DBGSCAN "  ------ HAS LOCK ------\n");
                    ScanServices(); //dtmb & isdb-t ??
                    lastLocked = 1;
                    plps++;
                }
                else
                    lastLocked = 0;

                if (p == 0) Muxs = otherMux;
                LOG_SCAN(DBGSCAN "found %d total %d mux %d muxs %d plps %d t2plp %d\n",foundNum,totalNum,otherMux,Muxs,plps,t2plp[p]);
                if (plps == 0) break; //must be streamId = 0
                if (!nitScan && plps > Muxs) break;
            }
            if(lastLocked)return;
        }
    }
}

//-------------------------------------------------------------------------
/*
  Scan algorithm for DVB-C:

  Try 256QAM after 64QAM only if signal strength is high enough
  If initial mod is != 64QAM, then no 64QAM/256QAM auto detection is done

  "intelligent symbolrate scan": srModes  0
  Try 6900/6875/6111 until lock is achieved, then use found rate for all subsequent scans

  "try all  symbolrate scan": srModes  1
  Try 6900/6875/6111  on every transponder

  "fixed symbolrate scan": srModes 2 on manual scan

  Wait additional 3s when last channel had lock to avoid false locking
 */

void cScan::ScanDVB_C(cTransponder * tp, cChannel * c)
{
    int timeout = DVBC_LOCK_TIMEOUT;
    int str1 = 0;
    int srtab[3] = { 6900, 6875, 6111 };
    int fixedModulation = 0;
    region = scanParameter_.region;

    if (device->IsPrimaryDevice()) cDevice::PrimaryDevice()->StopReplay();

    /* For Nit transponders */
    frequency = tp->Frequency();
    if (frequency < 1000000)
    {
        frequency *= 1000;
        tp->SetFrequency(frequency);
    }

    LOG_SCAN("DEBUG channelscan DVB_C f:  %f, Modulation %d SR %d\n", frequency / 1e6, tp->Modulation(), tp->Symbolrate());

    if (tp->Modulation() != QAM_AUTO)
        fixedModulation = 1;

    modulation = tp->Modulation();

    for (int sr = 0; sr < 3; sr++)
    {
        LOG_SCAN("DEBUG [channelscan]: srModes %d \n", srModes);


        switch (srModes)
        {
        case 0:                // auto
            if (srstat != -1)   // sr not found
            {
                LOG_SCAN("Use default symbol rate %i\n", srtab[srstat]);
                LOG_SCAN("Use  probed  symbol rate  %i\n", srtab[srstat]);
                tp->SetSymbolrate(srtab[srstat]);
                break;
            }
            // there is intentionaly no break !!!
        case 1:                // try all  symbol rates
            LOG_SCAN("try symbol rate  %i\n", srtab[sr]);
            tp->SetSymbolrate(srtab[sr]);
            break;
        case 2:                // fix sybol rate
            LOG_SCAN("try symbol rate  %i\n", tp->Symbolrate());
            /// already set
            break;
        default:
            LOG_SCAN("Error, check csmenu.c DVB_C \n");
            break;
        }

        // try 64QAM/256QAM
        for (int mods = 5; mods < 7; mods++)
        {
            if (cMenuChannelscan::scanState != ssGetChannels)
                return;

            if (!fixedModulation)
            {
                if ((lastMod + mods) & 1)
                    modulation = QAM_64;
                else
                    modulation = QAM_256;
            }
            symbolrate = tp->Symbolrate();  // status

            tp->SetModulation(modulation);
            tp->SetTransponderData(c, sourceCode);

            /* SAT>IP use Rid to assign frontend */
            if (scanParameter_.adapter > 200)
                c->SetId(0, 0, 0, scanParameter_.adapter - 200);

            if (!device->SwitchChannel(c, device->IsPrimaryDevice()))
            {
                esyslog("Primary SwitchChannel(c)  failed\n");
                break;
            }

            if (lastLocked)
            {
                LOG_SCAN("Wait last lock %d \n", mods);
                DEBUG_printf("Sleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
                sleep(1);       // avoid sticky last lock
            }

            waitSignal();

            if (waitLock(timeout))
            {
                LOG_SCAN(DBGSCAN "  ------------- HAS LOCK -------------     \n");
                if (!ScanServices())
                    ScanServices(); // Lock without services? retry
                lastLocked = 1;
                if (tp->Modulation() == QAM_256)
                    lastMod = 1;
                else
                    lastMod = 0;
                if (srModes == 0 && srstat == -1)
                    srstat = sr;    // remember SR for the remaining scan                    
                return;
            }

            lastLocked = 0;
            str1 = getSignal();

            if (fixedModulation && mods != 0)
                break;
            if (str1 < 0x3000 && str1)
            {
                sleep(1);
                str1 = getSignal();
                if (str1 < 0x3000 && str1)  // filter out glitches
                    break;
            }
        }
        // leave SR try loop without useable signal strength (even in "try all" mode)
        if ((str1 && str1 < 0x3000) || (srModes == 0 && srstat != -1) || srModes == 2)
            break;
    }
}

void cScan::ScanATSC(cTransponder * tp, cChannel * c)
{
    int timeout = 1000;
    int fixedModulation = 0;
    region = scanParameter_.region;

    frequency = tp->Frequency();

    if (device->IsPrimaryDevice()) cDevice::PrimaryDevice()->StopReplay();

    /* ??? ATSC ??? For Nit transponders */
    frequency = tp->Frequency();
    if (frequency < 1000000)
    {
        frequency *= 1000;
        tp->SetFrequency(frequency);
    }

    LOG_SCAN("DEBUG channelscan ATSC f:  %f, Modulation %d \n", frequency / 1e6, tp->Modulation());

    modulation = tp->Modulation();
    if (tp->Modulation() != 0)
        fixedModulation = 1;

    for (int mods = 0; mods < 2; mods++)
    {
        if (fixedModulation && mods != 0)
            break;

        if (cMenuChannelscan::scanState != ssGetChannels)
            return;

        if (!fixedModulation)
        {
            if (!mods)
                modulation = VSB_8;
            else
                modulation = VSB_16;
        }

        tp->SetModulation(modulation);
        tp->SetTransponderData(c, sourceCode);

        /* SAT>IP use Rid to assign frontend */
        if (scanParameter_.adapter > 200)
            c->SetId(0, 0, 0, scanParameter_.adapter - 200);

        if (!device->SwitchChannel(c, device->IsPrimaryDevice()))
        {
            esyslog("Primary SwitchChannel(c)  failed\n");
            break;
        }

        if (lastLocked)
        {
            LOG_SCAN("Wait last lock %d \n", mods);
            DEBUG_printf("Sleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
            sleep(1);       // avoid sticky last lock
        }

        if (device->HasLock(timeout))
        {
            LOG_SCAN(DBGSCAN "  ------ HAS LOCK ------\n");
            ScanServices(); //atsc ??

            lastLocked = 1;
            return;
        }
        lastLocked = 0;
    }
}

//-------------------------------------------------------------------------
void cScan::ScanDVB_I(cTransponder * tp, cChannel * c)
{
    int services;

    LOG_SCAN("DEBUG [channelscan]:  DVB_I Search \n");
    if (cMenuChannelscan::scanState >= ssInterrupted)
        return;

    tp->SetTransponderData(c, sourceCode);
    cDevice::PrimaryDevice()->StopReplay();
    cDevice::PrimaryDevice()->SwitchChannel(c,true);

/* ------ find channel with same parameters in vdr's channels---- */
    cChannel *Channel = NULL;
#if VDRVERSNUM < 20301
    for (cChannel *CChannel = Channels.First(); CChannel; CChannel = Channels.Next(CChannel))
#else
    cStateKey StateKey;
    const cChannels *Channels = cChannels::GetChannelsRead(StateKey, 10);
    if (!Channels)
        return;

    for (const cChannel *CChannel = Channels->First(); CChannel; CChannel = Channels->Next(CChannel))
#endif
    {
        if(cSource::IsType(CChannel->Source(), 'I') && !strcmp(CChannel->Parameters(),tp->Parameters()))
        {
            Channel = (cChannel*) CChannel;
            break;
        }
    }
#if VDRVERSNUM >= 20301
    StateKey.Remove();
#endif
#define CHANNELMARKOBSOLETE "OBSOLETE"
/* --------- scan SDT PAT ------------------- */
    usleep(1000*1000); //can be false traffic from previous channel

    services = ScanServices();

/* If services > 0 mean found SDT, if services < 0 mean found PAT without SDT, if services = 0 mean no DVB stream */

    if(services)
    {
/*   ------------------- NO SDT BUT PAT-----------------------    */
/*some iptv udp multicast channels have no SDT, but have PAT & PMT */
        if (services < 0)
        {
            const char * provider = cMenuChannelscan::provider;

/* ----------------- get udp address from parameter ------*/
            const char *param = tp->Parameters();
            char *ip[15];
            memset(ip,'\0',sizeof(ip));
            if (param && *param)
            {
                const char *end = strrchr(param,'|');
                memcpy(ip, param + 16, strlen(param) - 16 - strlen(end));
            }
/* ----------- generate sid, nid, tid, rid -------------- */
            char net[15],*snet;
            memcpy(net,ip,15);
            int id[4];
            snet = strtok(net,".");
            id[0]=atoi(snet);
            for (int a = 1;a<4;a++)
            {
                snet = strtok(NULL,".");
                id[a]=atoi(snet);
            }
/*------------ create new channel ---------------------- */
            if (!Channel)
            {
#if VDRVERSNUM < 20301
                cChannel *newchannel = Channels.NewChannel((const cChannel *)c, (const char*)ip, "", provider, id[2], id[1], id[0], id[3]);
                newchannel->CopyTransponderData(c);
#else
                cStateKey StateKey;
                cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 10);
                if (!Channels)
                    return;
                cChannel *newchannel = Channels->NewChannel((const cChannel *)c, (const char*)ip, "", provider, id[2], id[1], id[0], id[3]);
                newchannel->CopyTransponderData(c);
                StateKey.Remove();
#endif
            }
            else
            {
/*------------ if channel was scanned and have OBSOLETE it is may be a new channel, so set new name for it ---------*/
/* Is this need ?? */
/* А если было случайное определение как отсутствующий канал? Лучше тогда вручную название поменять. */
//                if (endswith(Channel->Name(), CHANNELMARKOBSOLETE))
//                {
//                    Channel->SetName((const char*)ip, "", provider);
//                }
#ifdef DVBCHANPATCH
                if (Channel->Frequency() != c->Frequency())
                    Channel->CopyTransponderData(c);
#endif
            }
/*---------- scan pids in PAT PMT without SDT -------------------------------------*/
            services = ScanServices(true);
        }
        cDevice::PrimaryDevice()->StopReplay();
        return;
    }
/* ---- channel found in vdr's channels but no traffic detected - mean OBSOLETE channel ----*/
    if (Channel)
    {
         bool OldShowChannelNamesWithSource = Setup.ShowChannelNamesWithSource;
         Setup.ShowChannelNamesWithSource = false;
#if VDRVERSNUM < 20301
         if (!endswith(Channel->Name(), CHANNELMARKOBSOLETE))
            Channel->SetName(cString::sprintf("%s %s", Channel->Name(), CHANNELMARKOBSOLETE), Channel->ShortName(), cString::sprintf("%s %s", CHANNELMARKOBSOLETE, Channel->Provider()));
#else
         bool ChannelsModified = false;
         cStateKey StateKey;
         cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 10);
         if (!Channels)
             return;
         if (!endswith(Channel->Name(), CHANNELMARKOBSOLETE))
            ChannelsModified |= Channel->SetName(cString::sprintf("%s %s", Channel->Name(), CHANNELMARKOBSOLETE), Channel->ShortName(), cString::sprintf("%s %s", CHANNELMARKOBSOLETE, Channel->Provider()));
         StateKey.Remove(ChannelsModified);
#endif
         Setup.ShowChannelNamesWithSource = OldShowChannelNamesWithSource;
    }
    cDevice::PrimaryDevice()->StopReplay();
    return;
}

void cScan::ScanAnalog(cTransponder * tp, cChannel * c)
{
    region = scanParameter_.region;

    LOG_SCAN("DEBUG [channelscan]: Analog Search \n");
    if (cMenuChannelscan::scanState >= ssInterrupted)
        return;

    frequency = tp->Frequency();

/*-- search channel in vdr's channels ----*/
    cChannel *channel = NULL;
#if VDRVERSNUM < 20301
    for (cChannel *CChannel = Channels.First(); CChannel; CChannel = Channels.Next(CChannel))
#else
    cStateKey StateKey;
    const cChannels *Channels = cChannels::GetChannelsRead(StateKey, 10);
    if (!Channels)
        return;

    for (const cChannel *CChannel = Channels->First(); CChannel; CChannel = Channels->Next(CChannel))
#endif
    {
        if(cSource::IsType(CChannel->Source(), 'V') && !strcmp(CChannel->Parameters(),tp->Parameters()) && CChannel->Frequency() == frequency)
        {
            channel = (cChannel*) CChannel;
            break;
        }
    }
#if VDRVERSNUM >= 20301
    StateKey.Remove();
#endif
    const char *p = "analog";
    int t =  (int) 0.5 + (frequency * 16) / 1000;

    if (scanParameter_.polarization > 0)
    {
        p = "external";
        t = 9000 + 10*scanParameter_.frontend+scanParameter_.polarization+scanParameter_.system;
    }

    cString ch = cString::sprintf(";%s:%d:%s:V:0:%d:300:0:0:1:0:%d:0", p,frequency,tp->Parameters(),scanParameter_.modulation ? 0 : 301, t);

    if (!c->Parse(*ch)) return;

    cDevice::PrimaryDevice()->SwitchChannel(c,true);

    LOG_SCAN("Tune %i \n", frequency);
    DEBUG_printf("\nSleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
//    usleep(1500 * 1000);
    if (lastLocked)
    {
        DEBUG_printf("\nSleeping %s %d\n", __PRETTY_FUNCTION__, __LINE__);
        sleep(2);           // avoid false lock
    }
    lastLocked = 0;
    sleep(3);

    if (cMenuChannelscan::scanState >= ssInterrupted)
        return;

    if(getSignal() > 5)
    {
        lastLocked = 1;
    }

    if (!channel)
    {
        if (scanParameter_.modulation == 0)
        { //TV search
            cString name;
            cString card = (scanParameter_.adapter == 1) ? "" : cString::sprintf("CARD%d ", scanParameter_.frontend);
            const char *region = "";
            cChannel *newchannel = NULL;

            if (scanParameter_.polarization == 0)
            { //TV input
                if (lastLocked)
                {
                    if (tp->ChannelNum() && scanParameter_.region >= 100) //for channel name
                    {
                        switch (scanParameter_.region)
                        {
                            case 100: region = tr("EUR");
                                break;
                            case 101: region = tr("RUS terr.");
                                break;
                            case 102: region = tr("RUS cable");
                                break;
                            case 103: region = tr("China");
                                break;
                            case 104: region = tr("USA");
                                break;
                            case 105: region = tr("Japan");
                                break;
                            default:;
                        }
                    }

                    if (frequency > 300000)
                        name = cString::sprintf("UHF %02d %s", tp->ChannelNum() ? tp->ChannelNum() : frequency, region);
                    else
                        name = cString::sprintf("VHF %02d %s", tp->ChannelNum() ? tp->ChannelNum() : frequency, region);

                    cString ch = cString::sprintf("%s;%s:%d:%s:V:0:301:300:305:0:1:0:%d:0", *name,p,frequency,tp->Parameters(), t);

                    newchannel = new cChannel;

                    if (!newchannel->Parse(*ch))
                        return;
                }
            }
            else
            { //external input
                char* n = strdup(tp->Parameters());
                strreplace(n,"|","-");

                name = cString::sprintf("%s%s", (const char*)card, n);

                cString ch = cString::sprintf("%s;%s:%d:%s:V:0:301:300:305:0:1:0:%d:0", *name,p,frequency,tp->Parameters(), t);

                newchannel = new cChannel;
                if (!newchannel->Parse(*ch))
                    return;
                if (!device->ProvidesChannel(newchannel))
                    return;
            }

            if(newchannel)
            {
#if VDRVERSNUM < 20301
                Channels.Add(newchannel);
                Channels.ReNumber();
#else
                cStateKey StateKey;
                cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 100);
                if (!Channels)
                    return;
                Channels->Add(newchannel);
                Channels->ReNumber();
                StateKey.Remove();
#endif
                tvChannelNames.push_back(newchannel->Name());
            };
            cDevice::PrimaryDevice()->StopReplay();
            return;
        }
        else
        { //RADIO search
            cMenuChannelscan::scanState = ssWait;

            //No way how to detekt success radio search now, use manual channel add
            while (cMenuChannelscan::scanState == ssWait)
            {
                usleep(100000);
            }

            if (cMenuChannelscan::scanState >= ssInterrupted)
                return;

            if (cMenuChannelscan::scanState == ssContinue) //manual save channel
            {
                cMenuChannelscan::scanState = ssGetChannels;

                double f = (double) frequency / 1000;

                cString name = cString::sprintf("FM %d.%02d",(int)f,(int)((f-(int)f)*100.0));

                cChannel *newchannel;
                cString ch = cString::sprintf("%s;FM radio:%d:%s:V:0:0:300:0:0:1:0:%d:0", *name,frequency,tp->Parameters(), t);
                newchannel = new cChannel;
                if (!newchannel->Parse(*ch))
                    return;
#if VDRVERSNUM < 20301
                Channels.Add(newchannel);
                Channels.ReNumber();
#else
                cStateKey StateKey;
                cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 100);
                if (!Channels)
                    return;
                Channels->Add(newchannel);
                Channels->ReNumber();
                StateKey.Remove();
#endif
                radioChannelNames.push_back(newchannel->Name());
                return;
            }
        }
    }
    else
    { //channel in vdr's channels

        if (scanParameter_.modulation == 0)
        {//TV input
            if ((lastLocked && scanParameter_.polarization == 0) || scanParameter_.polarization != 0)
                tvChannelNames.push_back(channel->Name());
        }
        else   //radio
            radioChannelNames.push_back(channel->Name());
    }
    cDevice::PrimaryDevice()->StopReplay();
    return;
}

//---------  cMainMenu::Action()  -----------------------------------

void cScan::Action()
{
    // the one and only "scanning = true" !
    int i = 0, a = 0;
    int sys = cardnr;           // for mcli-plugin, the system is tunneled over cardnr. It is *not* the device number!

    cPlugin *plug = cPluginManager::GetPlugin("mcli");
    if (plug && dynamic_cast<cThread*>(plug) && dynamic_cast<cThread*>(plug)->Active())
    {
        cardnr = 0;             // Start at first device
        bool gotIt = false;

        device = cDevice::GetDevice(cardnr);
        while (device)
        {
            // Actually, this probing is quite useless, as a new virtual tuner can provide ALL available systems
            // and the selection-constraint was already done in the menu
            // So this is more a check if the tuner is currently free
            switch (sys)
            {
            case 0:            //TERR
                if (device->ProvidesSource(cSource::stTerr))
                    gotIt = true;
                break;
            case 1:            // CABLE
                if (device->ProvidesSource(cSource::stCable))
                    gotIt = true;
                break;
            case 2:            // SAT
                if (device->ProvidesSource(sourceCode))
                    gotIt = true;
                break;
            case 3:            // SATS2
                if (device->ProvidesSource(sourceCode))
                    gotIt = true;
                break;
            default:
                break;
            }
            if (gotIt == true)
                break;
            i++;
            device = cDevice::GetDevice(++cardnr);
        }
    }
    else
    {
        device = cDevice::GetDevice(cardnr);
        sys=sourceType;
    }
    std::unique_ptr < cChannel > c(new cChannel);

    cTransponders & transponders = cTransponders::GetInstance();

    transponderMap.clear();
    hdTransponders.clear();
    TblVersions.clear();

    if (!device)
    {
        cMenuChannelscan::scanState = ssDeviceFailure;
        return;                 // No matching tuner found
    }

#define EXTRA_FILTERS 0
#if EXTRA_FILTERS
    // FIX for empty channellist 
    // Add a filter - so, netceiver can access PID with OpenFilter() and have a tuner-lock

#define PAT_F 0
#if PAT_F
    PatFilter *tmp_pfilter = NULL;
    SdtFilter *tmp_sfilter = NULL;
#endif

    NitFilter *tmp_nfilter = NULL;

    //Attaching filters to devices not necessary for this FIX ? 
    if (nitScan)                // not for manual scan
    {
        tmp_nfilter = new NitFilter;
        tmp_nfilter->mode = sys;

        device->AttachFilter(tmp_nfilter);
    }
#if PAT_F
    else
    {                           /* manual scan, then start these filters */
        tmp_pfilter = new PatFilter;
        tmp_sfilter = new SdtFilter(tmp_pfilter);
        tmp_pfilter->SetSdtFilter(tmp_sfilter);

        device->AttachFilter(tmp_pfilter);
        device->AttachFilter(tmp_sfilter);
        tmp_pfilter->SetStatus(false);
        tmp_sfilter->SetStatus(false);
    }
#endif
    // end FIX
#endif
    if (cMenuChannelscan::scanState == ssGetTransponders)
    {
        cTransponder *t = transponders.GetNITStartTransponder();
        if (t && (sys == SAT || sys == SATS2))
        {
            // fetch sat Transponders
           //esyslog("%s !!!!!! FETCH tps START !!!!! ssGetChannels?=%d",__PRETTY_FUNCTION__,cMenuChannelscan::scanState == ssGetChannels);
//             ScanDVB_S(t, c.get()); // why ScanDVB_S() when ScanNitServices() gets the tp list!?
            /* TB: feed a dummy channel into VDR so it doesn't turn the rotor back to it's old position */
#ifdef REELVDR
            cChannel dummyChan;
            dummyChan.SetNumber(1);
            cDvbTransponderParameters dtp (dummyChan.Parameters());
            dtp.SetPolarization(scanParameter_.polarization);
            dummyChan.SetTransponderData(scanParameter_.source, 9 /*scanParameter_.frequency */ , scanParameter_.symbolrate, dtp.ToString('S'), true);
            device->SetChannel(&dummyChan, false);
            WaitForRotorMovement(cardnr);
            ScanNitServices();
#else
             ScanDVB_S(t, c.get()); // why ScanDVB_S() when ScanNitServices() gets the tp list!?
#endif
             //esyslog("%s !!!!!! FETCH tps END !!!!! ssGetChannels?=%d",__PRETTY_FUNCTION__,cMenuChannelscan::scanState == ssGetChannels);
        }
        if (cMenuChannelscan::scanState == ssGetTransponders)
        {
            AddTransponders(sys);
            if (transponders.v_tp_.size() < 1)
            {
                // give up,  load legacy transponder list
                scanParameter_.nitscan = 0;
                transponders.Load(&scanParameter_);
            }
            cMenuChannelscan::scanState = ssGetChannels;
        }
        else
        {

#if EXTRA_FILTERS
            if (tmp_nfilter)
            {

                device->Detach(tmp_nfilter);

                delete tmp_nfilter;

            }
#if PAT_F
            if (tmp_pfilter)
            {
                device->Detach(tmp_pfilter);
                delete tmp_pfilter;
            }
            if (tmp_sfilter)
            {
                device->Detach(tmp_sfilter);
                delete tmp_sfilter;
            }
#endif

#endif
            return;
        }
    }

    if (sys == ANALOG)
        cDevice::PrimaryDevice()->StopReplay();

    constTpIter tp = transponders.v_tp_.begin();

    time_t startTime = time(NULL);

    while (Running() && cMenuChannelscan::scanState == ssGetChannels)
    {
        WaitForRotorMovement(cardnr);

        /* use vdr native positioner support */
        if(device->Positioner())
        {
            while(device->Positioner()->IsMoving())
            {
                sleep(1);
            }
        }

        LOG_SCAN("DEBUG [channelscan]: loop through  transponders ++++  size %d ++++++++\n", (int)transponders.v_tp_.size());
        unsigned int oldTransponderMapSize = transponderMap.size();
        int i = 1, ntv, nradio;
        time_t t_in, t_out;
        std::vector < cTransponder * >scannedTps;
        while (tp != transponders.v_tp_.end())
        {
            //if (!cMenuChannelscan::scanning) break;
            if (cMenuChannelscan::scanState != ssGetChannels)
                break;
            ntv = tvChannelNames.size();
            nradio = radioChannelNames.size();

            // get counter
            transponderNr = tp - transponders.v_tp_.begin();
            channelNumber = (*tp)->ChannelNum();
            frequency = (*tp)->Frequency();
            parameters = (*tp)->Parameters();

            LOG_SCAN("DEBUG [channelscan]: scan f: %d  \n", frequency);

            if (sys == TERR || sys == TERR2 || sys == DMB_TH || sys == ISDB_T)
                ScanDVB_T(*tp, c.get());
            else if (sys == CABLE)
                ScanDVB_C(*tp, c.get());
            else if (sys == ATSC)
                ScanATSC(*tp, c.get());
            else if (sys == IPTV)
                ScanDVB_I(*tp, c.get());
            else if (sys == ANALOG)
                ScanAnalog(*tp, c.get());
            else if (sys == SAT || sys == SATS2)
            {
                DEBUG_printf("\nCalled ScanDVB_S() \n");
                t_in = time(NULL);
                // loop over a previously scanned transponders
                bool alreadyScanned = false;
                FILE *fp = fopen("/tmp/alreadyScanned.txt", "a");
                for (constTpIter prevTp = scannedTps.begin(); prevTp != scannedTps.end(); prevTp++)
                    /* TB: checking equality of frequencies is not enough! */
                    /*     Polarization has also to be checked... */
                    /*     Modulation and FEC also ... */
                    if ((dynamic_cast < cSatTransponder * >(*prevTp))->FEC() == (dynamic_cast < cSatTransponder * >(*tp))->FEC() && (dynamic_cast < cSatTransponder * >(*prevTp))->Modulation() == (dynamic_cast < cSatTransponder * >(*tp))->Modulation() && (dynamic_cast < cSatTransponder * >(*prevTp))->Polarization() == (dynamic_cast < cSatTransponder * >(*tp))->Polarization() && /*(*tp)->SourceCode() ==  (*prevTp)->SourceCode() && */ abs((*prevTp)->Frequency() - frequency) <= 1)   // from the same SAT source ?
                    {
                        alreadyScanned = true;
                        fprintf(fp, "(%s:%d) %d Mhz already scanned\n", __FILE__, __LINE__, frequency);
                        break;
                    }
                fclose(fp);

                if (alreadyScanned == false)
                {
                    ScanDVB_S(*tp, c.get());
                    scannedTps.push_back(*tp);
                }

                t_out = time(NULL);
                fp = fopen("/tmp/cScan.log", "a");
                const time_t tt = time(NULL);
                char *strDate;
                a = asprintf(&strDate, "%s", asctime(localtime(&tt)));
                strDate[strlen(strDate) - 1] = 0;
                fprintf(fp, "\n\n%s tp=%4d, %6d(%d) TV:%4d Radio:%4d in %3d sec", strDate, i, frequency, !alreadyScanned, (int)tvChannelNames.size() - ntv, (int)radioChannelNames.size() - nradio, (int)difftime(t_out, t_in));
                fclose(fp);

                fp = fopen("/tmp/tScan.log", "a");
                fprintf(fp, "\n\ntp=%4d, %6d/%2d/%5d TV:%4d Radio:%4d in %3dsec new:%3d", i, frequency, (*tp)->Modulation(), (*tp)->Symbolrate(), (int)tvChannelNames.size() - ntv, (int)radioChannelNames.size() - nradio, (int)difftime(t_out, t_in), (int)tvChannelNames.size() - ntv + (int)radioChannelNames.size() - nradio);
                fclose(fp);

                free(strDate);
            }

            ++tp;
            ++i;                // tp number
        }

        if (cMenuChannelscan::scanState <= ssGetChannels && transponderMap.size() > oldTransponderMapSize)
        {
            size_t pos = transponders.v_tp_.size();
            AddTransponders(sys);
            tp = transponders.v_tp_.begin() + pos;
        }
        else
            break;
    }
#if EXTRA_FILTERS
    if (tmp_nfilter)
    {
        device->Detach(tmp_nfilter);
        delete tmp_nfilter;
    }
#if PAT_F
    if (tmp_pfilter)
    {
        device->Detach(tmp_pfilter);
        delete tmp_pfilter;
    }
    if (tmp_sfilter)
    {
        device->Detach(tmp_sfilter);
        delete tmp_sfilter;
    }
#endif

// end fix3
#endif

    int duration = time(NULL) - startTime;

    LOG_SCAN(" --- End of transponderlist. End of scan! Duratation %d ---\n", duration);
    // avoid warning wihout debug

    if (cMenuChannelscan::scanState == ssGetChannels)
        cMenuChannelscan::scanState = ssSuccess;

    LOG_SCAN(DBGSCAN " End of scan scanState: ssSuccess!\n");
#if VDRVERSNUM < 20301
    Channels.Save();
#else
    cStateKey StateKey;
    cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 100);
    if (!Channels)
        return;
    Channels->Save();
    StateKey.Remove();
#endif
    DumpHdTransponder();
    ClearMap();
    if (a) {}; //remove make warning
    if (duration > 0) {}; //remove make warning
}

//-------------------------------------------------------------------------

void cScan::AddTransponders(int system)
{
    LOG_SCAN(" --- AddTransponders \n");
    int cnt = 0;
    cTransponders & transponders = cTransponders::GetInstance();
    for (tpMapIter itr = transponderMap.begin(); itr != transponderMap.end(); ++itr)
    {
        if (cMenuChannelscan::scanState == ssGetTransponders || transponders.MissingTransponder(itr->first))
        {
            if(system == SAT || system == SATS2){
                 cSatTransponder *tp = dynamic_cast < cSatTransponder * >(itr->second);
                 tp->PrintData();
            }
            else if(system == CABLE){
                cCableTransponder *tp = dynamic_cast < cCableTransponder * >(itr->second);
                tp->PrintData();
            }
            else if(system == TERR || system == TERR2 || system == DMB_TH || system == ISDB_T){
                cTerrTransponder *tp = dynamic_cast < cTerrTransponder * >(itr->second);
                tp->PrintData();
            }
            cnt++
            DEBUG_printf("%d=", cnt);
            transponders.v_tp_.push_back(itr->second);
        }
    }
}

void cScan::DumpHdTransponder()
{
    while (!hdTransponders.empty())
    {

        std::map < int, cSatTransponder >::const_iterator it;
        cSatTransponder t = hdTransponders.begin()->second;

        LOG_SCAN("HD transponder : %6d -> mod %d srate %d  fec %d rolloff %d \n", (*hdTransponders.begin()).first, t.Modulation(), t.Symbolrate(), t.FEC(), t.RollOff());

        hdTransponders.erase(hdTransponders.begin());
    }

}

void cScan::ClearMap()
{
    cTransponders::GetInstance().Clear();

    while (!transponderMap.empty())
    {
        if (transponderMap.begin()->second)
            LOG_SCAN("delete tp: %6d\n", (*transponderMap.begin()).first);
        else
            LOG_SCAN(" tp: %6d deleted already\n", (*transponderMap.begin()).first);

        transponderMap.erase(transponderMap.begin());
    }
}
