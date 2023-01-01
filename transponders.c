/***************************************************************************
 *   Copyright (C) 2005 by Reel Multimedia                                 *
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
 *   transponders.c provides Select Channels(.conf) via service interface
 *   Todo Autoscanner  for install wizard
 *
 ***************************************************************************/

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "transponders.h"
#include <vdr/plugin.h>
#ifdef REELVDR
#include <vdr/s2reel_compat.h>
#endif
#include <vdr/sources.h>
#include <linux/dvb/frontend.h>
#include "channelscan.h"
#include <bzlib.h>
#include <vdr/diseqc.h>

//#define DEBUG_TRANSPONDER(format, args...) printf (format, ## args)
#define DEBUG_TRANSPONDER(format, args...)
//#define DEBUG_printf(format, args...) printf (format, ## args)
#define DEBUG_printf(format, args...)

using std::cerr;
using std::endl;
using std::string;
using std::ifstream;
using std::ofstream;
using std::stringstream;
using std::cout;
using std::vector;

extern const char* Standard[];
extern const char* pvrInput[];

/* Notation for Europe (region 0)
   channel 1-4: VHF band 1
    5-13:   VHF band 3
   21-69:   UHF band 4/5
   101-110/111-120: cable frequencies (aka Sonderkanal/Midband/Superband S1-S20)
   121-141: cable frquencies (aka Hyperband, S21-S41)
   173/181: D73 and D81 in C-networks (Germany)

   Russia air (region 1)
   channel
   1-5 VHF band 1/2
   6-12 VHF band 3
   21-69 UHF band 4/5

   Russia cable (region 2)
   cable channel
   1-8 between 5-6 air
   11-40 between 12-21 air

   China air (region 3)
   1-3 VHF band 1
   4-5 VHF band 2
   6-12 VHF band 3
   13-57 UHF band 4/5

   China cable ?

   USA atsc (region 4)

   Japan ISDB (region 5)

   Region n+100 use for analog frequency.

*/

//----------- Class Transponder -------------------------------


cTransponder::cTransponder(int Frequency):channelNum_(0), frequency_(Frequency), symbolrate_(0), system_(0), scanned_(false)
{
};
/*

Regions - < 100 digital, > 100 analog

*/
int cTransponders::channel2Frequency(int region, int channel, int &bandwidth)
{
    int offset = 0; //for PVR analog video frequency region +=100

    if (region == 0 || region == 100) //EUR
    {
        if (region == 100) offset = 2750000;
        if(bandwidth == -1)
            bandwidth = 7000000;
        if (channel >= 1 && channel <= 4)
        {
            return 38000000 - offset + (channel - 1) * 7000000;
        }
        if (channel >= 5 && channel <= 13)
        {
            return 177500000 - offset + 7000000 * (channel - 5);
        }
        if (channel >= 21 && channel <= 69)
        {
            if(bandwidth == -1)
                bandwidth = 8000000;
            return 474000000 - offset + 8000000 * (channel - 21);
        }
        if (channel == 101)
            return 107500000 - offset;

        if (channel == 102 || channel == 103)
        {
            if(bandwidth == -1)
                bandwidth = 8000000;
            return 113000000- offset + 8000000 * (channel - 102);
        }
        if (channel >= 104 && channel <= 110)
            return 128000000 - offset + 7000000 * (channel - 104);       // Fixme +500khz Offset?

        if (channel >= 111 && channel <= 120)
            return 233000000 - offset + 7000000 * (channel - 111);       // Fixme +500khz Offset?

        if (channel >= 121 && channel <= 141)
        {
            if(bandwidth == -1)
                bandwidth = 8000000;
            return 306000000 - offset + 8000000 * (channel - 121);
        }
        if (channel == 173)
        {
            if(bandwidth == -1)
                bandwidth = 8000000;
            return 73000000 - offset;
        }
        if (channel == 181)
        {
            if(bandwidth == -1)
                bandwidth = 8000000;
            return 81000000 - offset;
        }
    }
    else if (region == 1 || region == 101) //RUS эфир
    {
        if (region == 101) offset = 2750000;
        if(bandwidth == -1)
            bandwidth = 8000000;
        if (channel >= 1 && channel <= 2)
        {
            return 52500000 - offset + (channel - 1) * 8000000;
        }
        if (channel >= 3 && channel <= 5)
        {
            return 80000000 - offset + (channel - 3) * 8000000;
        }
        if (channel >= 6 && channel <= 12)
        {
            return 178000000 - offset + (channel - 6) * 8000000;
        }
        if (channel >= 21 && channel <= 69)
        {
            return 474000000 - offset + 8000000 * (channel - 21);
        }
    }
    else if (region == 2 || region == 102) //RUS кабель
    {
        if (region == 102) offset = 2750000;
        if(bandwidth == -1)
            bandwidth = 8000000;
        if (channel >= 1 && channel <= 8)
        {
            return 114000000 - offset + (channel - 1) * 8000000;
        }
        if (channel >= 11 && channel <= 40)
        {
            return 234000000 - offset + (channel - 11) * 8000000;
        }
    }
    else if (region == 3 || region == 103) //CHINA
    {
        if (region == 103) offset = 2750000;
        if(bandwidth == -1)
            bandwidth = 8000000;
        if (channel >= 1 && channel <= 3)
        {
            return 52500000 - offset + (channel - 1) * 8000000;
        }
        if (channel >= 4 && channel <= 5)
        {
            return 80000000 - offset + (channel - 4) * 8000000;
        }
        if (channel == 6)
        {
            return 171000000 - offset + (channel - 6) * 8000000;
        }
        if (channel == 7)
        {
            return 179000000 - offset + (channel - 7) * 8000000;
        }
        if (channel >= 8 && channel <= 10)
        {
            return 187000000 - offset + (channel - 8) * 8000000;
        }
        if (channel == 11)
        {
            return 211000000 - offset + (channel - 11) * 8000000;
        }
        if (channel == 12)
        {
            return 219000000 - offset + (channel - 12) * 8000000;
        }
        if (channel >= 13 && channel <= 24)
        {
            return 474000000 - offset + 8000000 * (channel - 13);
        }
        if (channel >= 25 && channel <= 57)
        {
            return 610000000 - offset + 8000000 * (channel - 25);
        }
    }
    else if (region == 4 || region == 104) //USA
    {
        if (region == 104) offset = 1750000;
        if(bandwidth == -1)
            bandwidth = 6000000;
        if (channel >= 2 && channel <= 4)
        {
            return 57000000 - offset + (channel - 2) * 6000000;
        }
        if (channel >= 5 && channel <= 6)
        {
            return 76000000 - offset + (channel - 5) * 6000000;
        }
        if (channel >= 7 && channel <= 13)
        {
            return 174000000 - offset + (channel - 7) * 6000000;
        }
        if (channel >= 14 && channel <= 83)
        {
            return 470000000 - offset + (channel - 14) * 6000000;
        }
    }
    else if (region == 5 || region == 105) //Japan
    {
        if (region == 105) offset = 1750000;
        if(bandwidth == -1)
            bandwidth = 6000000;
        if (channel >= 1 && channel <= 3)
        {
            return 93000000 - offset + (channel - 1) * 6000000;
        }
        if (channel >= 4 && channel <= 7)
        {
            return 173000000 - offset + (channel - 4) * 6000000;
        }
        if (channel >= 8 && channel <= 12)
        {
            return 195000000 - offset + (channel - 8) * 6000000;
        }
        if (channel >= 13 && channel <= 62)
        {
            return 473000000 - offset + (channel - 13) * 6000000;
        }
    }

    return 0;
}

int cTransponder::Modulation() const
{
    return modulation_;
}

int cTransponder::Symbolrate() const
{
    return symbolrate_;
};

void cTransponder::SetSymbolrate(int sr)
{
    symbolrate_ = sr;
};

void cTransponder::SetModulation(int mod)
{
    modulation_ = mod;
};

int cTransponder::IntToFec(int val)
{
    // needed for old transponder lists
    switch (val)
    {
    case 12:
        return FEC_1_2;
    case 23:
        return FEC_2_3;
    case 34:
        return FEC_3_4;
    case 45:
        return FEC_4_5;
    case 56:
        return FEC_5_6;
    case 67:
        return FEC_6_7;
    case 78:
        return FEC_7_8;
    case 89:
        return FEC_8_9;
        /// S2 FECs : list taken from linux/frontend.h
    case 35:
        return FEC_3_5;
    case 910:
        return FEC_9_10;
    default:
        return FEC_AUTO;
    }
    return FEC_NONE;
}

int cTransponders::StatToS2Fec(int mod)
{

    int stat2fecs2[] = {
	FEC_NONE,
        FEC_1_2,
        FEC_2_3,
        FEC_3_4,
        FEC_3_5,                //S2
        FEC_4_5,
        FEC_5_6,
        FEC_6_7,
        FEC_7_8,
        FEC_8_9,
        FEC_9_10,               //S2
        FEC_AUTO,
    };

    DEBUG_printf(" cTransponders::StatToSFec(%d) return %d \n", mod, stat2fecs2[mod]);
    return stat2fecs2[mod];

}

int cTransponders::StatToFec(int mod)
{

    int stat2fec[] = {
	FEC_NONE,
        FEC_1_2,
        FEC_2_3,
        FEC_3_4,
        FEC_4_5,
        FEC_5_6,
        FEC_6_7,
        FEC_7_8,
        FEC_8_9,
        FEC_AUTO,
    };

    DEBUG_printf(" cTransponders::StatToSFec(%d) return %d \n", mod, stat2fec[mod]);
    return stat2fec[mod];

}

int cTransponders::StatToS2Mod(int mod)
{
    switch (mod)
    {
    case 0:
        return -1;
    case 1:
        return QPSK;
    case 2:
        return PSK_8;
    case 3:
        return APSK_16;
    case 4:
        return APSK_32;
    case 5:
        return 999;             // try all
    }
    return PSK_8;
}


int cTransponder::StrToMod(string mod)
{
    char *Mod = (char*)&mod[0];

    if (!strcmp(Mod,"QPSK")) return QPSK;

    if (!strcmp(Mod,"8PSK")) return PSK_8;

    if (!strcmp(Mod,"16APSK")) return APSK_16;

    if (!strcmp(Mod,"32APSK")) return APSK_32;

    if (!strcmp(Mod,"ALL")) return 999;

    return -1;
}


//----------- Class cSatTransponder -------------------------------

cSatTransponder::cSatTransponder():cTransponder(0), pol_(' '), fec_(9)
{
    modulation_ = 0;
    system_ = DVB_SYSTEM_1;
    streamId_ = 0;
    DEBUG_TRANSPONDER(DBGT " new cSatTransponder\n");
}

cSatTransponder::cSatTransponder(int Frequency, char Pol, int SymbolRate, int Modulation, int FEC, int RollOff, int System, int StreamId):cTransponder(Frequency), pol_(Pol), fec_(FEC), rolloff_(RollOff)
{
    channelNum_ = 0;
    modulation_ = Modulation;
    symbolrate_ = SymbolRate;
    system_     = System;
    streamId_   = StreamId;

    DEBUG_TRANSPONDER(DBGT " new cSatTransponder(sys: %d,f: %d,p: %c,sRate: %d,mod:%d ,fec: %d, stream:%d\n", system_, frequency_, pol_, symbolrate_, modulation_, fec_, streamId_);
}

bool cSatTransponder::SetTransponderData(cChannel * c, int Code)
{
    DEBUG_TRANSPONDER(DBGT " SetSatTransponderData(source:%d,f:%6d,p:%c,sRate:%d,mod:%3d,fec:%d,sys:%d,stream:%d\n", Code, frequency_, pol_, symbolrate_, modulation_, fec_, system_, streamId_);
    cDvbTransponderParameters dtp (c->Parameters());
    dtp.SetCoderateH(fec_);
    dtp.SetModulation(modulation_);
    dtp.SetSystem(system_);
    dtp.SetRollOff(rolloff_);
    dtp.SetPolarization(pol_);
    dtp.SetStreamId(streamId_);
    return c->SetTransponderData(Code, frequency_, symbolrate_, dtp.ToString('S'), true);
}


bool cSatTransponder::Parse(const string & Line)
{

    DEBUG_TRANSPONDER(" %s  %s \n", __PRETTY_FUNCTION__, Line.c_str());
    string tpNumb(Line);

    int index = tpNumb.find_first_of('=');
    if (index == -1)
        return false;

    tpNumb = tpNumb.erase(0, index + 1);

    //   chop  string
    string tmp = Line.substr(index + 1);

    // get Frequenz
    index = tmp.find_first_of(',');

    if (index == -1)
        return false;

    string freq = tmp;
    freq.erase(index);

    // get polarisation
    string polar = tmp.substr(index + 1);
    index = polar.find_first_of(',');
    if (index == -1)
        return false;

    // get symbol rate
    string symRate = polar.substr(index + 1);
    polar.erase(index);

    index = symRate.find_first_of(',');
    if (index == -1)
        return false;

    string sFec = symRate.substr(index + 1);
    symRate.erase(index);

    index = sFec.find_first_of(',');
    string sys, mod, stream;

    if (index == -1 && ScanSetup.tplUpdateType == 0) //Use Sat.url
    {
        system_ = DVB_SYSTEM_1;
        modulation_ = QPSK;
    }
    else if (index == -1)
        return false;
    else
    {
        sys = sFec.substr(index + 1);
        sFec.erase(index);

        if (ScanSetup.tplUpdateType == 0)  //Use Sat.url
            index = sys.find_first_of(';');
        else                               //Use kingofsat.net
            index = sys.find_first_of(',');

        if (index != -1)
        {
            mod = sys.substr(index + 1);
            sys.erase(index);

            if (ScanSetup.tplUpdateType == 0)  //Use Sat.url
            {
                index = mod.find_first_of(';');
                if (index != -1)
                {
                    stream = mod.substr(index + 1);
                    mod.erase(index);
                }
            }
            else                               //Use kingofsat.net
            {
                index = mod.find_first_of(' ');
                if (index != -1)
                {
//                    stream = mod.substr(index + 1);
                    mod.erase(index);
                }
            }
        }
    }

    frequency_ = strToInt(freq.c_str());
    if (frequency_ == -1)
        return false;
    pol_ = toupper(polar[0]);
    symbolrate_ = strToInt(symRate.c_str());
    if (symbolrate_ == -1)
        return false;
    fec_ = IntToFec(strToInt(sFec.c_str()));

    if (!strcmp((char*)&sys[0],"S2"))
    {
        system_ = DVB_SYSTEM_2;
        modulation_ = StrToMod(mod);
        if (modulation_ < 0)
            modulation_ = PSK_8;
        streamId_ = strToInt(stream.c_str());
        if (streamId_ < 0)
            streamId_ = 0;
    }

    //printf("Parse: Freq: %i FEC: %i (%s)\n", frequency_, fec_,sFec.c_str() );

    //DEBUG_TRANSPONDER(" transp.c Parse()  return true f:%d p%c sRate %d fec %d \n", frequency_,pol_,symbolrate_,fec_);
    // dsyslog (" transp.c Parse()  return true f:%d p%c sRate %d fec %d ", frequency_,pol_,symbolrate_,fec_);

    return true;
}

int cSatTransponder::RollOff() const
{
    return rolloff_;
}

int cSatTransponder::FEC() const
{
    return fec_;
}

void cSatTransponder::SetFEC(int fec)
{
    fec_ = fec;
}

void cSatTransponder::SetRollOff(int rolloff)
{
    rolloff_ = rolloff;
}

void cSatTransponder::PrintData() const
{
    printf("%d,%c,%d,%d\n", frequency_, pol_, symbolrate_, fec_);
}

char cSatTransponder::Polarization() const
{
    return pol_;
};

void cSatTransponder::SetStreamId(int strid)
{
    streamId_ = strid;
}

//----------- Class cTerrTransponder -------------------------------

cTerrTransponder::cTerrTransponder(int ChannelNr, int Frequency, int Bandwidth, int System, int StreamId):cTransponder(Frequency)
{
    channelNum_ = ChannelNr;
    symbolrate_ = 27500;
    system_ = System;
    bandwidth_ = Bandwidth;
    // fec is called Srate in vdr
    fec_h_ = FEC_AUTO;
    fec_l_ = FEC_AUTO;
    hierarchy_ = HIERARCHY_NONE;
    modulation_ = QAM_AUTO;
    guard_ = GUARD_INTERVAL_AUTO;
    transmission_ = TRANSMISSION_MODE_AUTO;
    streamId_ = StreamId;

    DEBUG_TRANSPONDER(DBGT " SetTerrTransponderData(system: %d,f:%d, bw: %d , stream: %d\n", system_, frequency_, bandwidth_, streamId_);

}

cTerrTransponder::~cTerrTransponder()
{
}

bool cTerrTransponder::SetTransponderData(cChannel * c, int Code)
{
    int type = cSource::stTerr;
    cDvbTransponderParameters dtp(c->Parameters());
    dtp.SetBandwidth(bandwidth_);
    dtp.SetSystem(system_);
    dtp.SetModulation(modulation_);
    dtp.SetHierarchy(hierarchy_);
    dtp.SetCoderateH(fec_h_);
    dtp.SetCoderateL(fec_l_);
    dtp.SetGuard(guard_);
    dtp.SetTransmission(transmission_);
    dtp.SetStreamId(streamId_);
    return c->SetTransponderData(type, frequency_, symbolrate_, dtp.ToString('T'), true); //
}

void cTerrTransponder::PrintData() const
{
    printf("%d,%d,%d-%d\n", frequency_, symbolrate_, fec_l_, fec_h_);
}

void cTerrTransponder::SetStreamId(int strid)
{
    streamId_ = strid;
}

//----------- Class cCableTransponder -------------------------------

cCableTransponder::cCableTransponder(int ChannelNr, int Frequency, int sRate, int Mod):cTransponder(Frequency)
{
    DEBUG_TRANSPONDER(DBGT " new cCableTransponder Channel: %d f: %d, sRate :%d  mod :%d\n", ChannelNr, Frequency, sRate, Mod);

    channelNum_ = ChannelNr;
    symbolrate_ = sRate;
    fec_h_ = FEC_AUTO;
    modulation_ = Mod;
}

cCableTransponder::~cCableTransponder()
{
}

bool cCableTransponder::SetTransponderData(cChannel * c, int Code)
{
    int type = cSource::stCable;

    DEBUG_TRANSPONDER(DBGT " SetCableTransponderData(f:%d, m :%d ,sRate: %d, fec %d\n", frequency_, modulation_, symbolrate_, fec_h_);
    cDvbTransponderParameters dtp(c->Parameters());
    dtp.SetModulation(modulation_);
    dtp.SetCoderateH(fec_h_);
    return c->SetTransponderData(type, frequency_, symbolrate_, dtp.ToString('C'), true);
}

void cCableTransponder::PrintData() const
{
    printf("%d,%d,%d\n", frequency_, symbolrate_, fec_h_);
}

//----------- Class cAtscTransponder -------------------------------

cAtscTransponder::cAtscTransponder(int ChannelNr, int Frequency, int Mod):cTransponder(Frequency)
{
    int modTab[3] = {
        0,
        VSB_8,
        VSB_16,
    };

    DEBUG_TRANSPONDER(DBGT " new cAtscTransponder Channel: %d f: %d, mod :%d\n", ChannelNr, Frequency, Mod);

    channelNum_ = ChannelNr;
    modulation_ = modTab[Mod];
}

cAtscTransponder::~cAtscTransponder()
{
}

bool cAtscTransponder::SetTransponderData(cChannel * c, int Code)
{
    int type = cSource::stAtsc;

    DEBUG_TRANSPONDER(DBGT " SetAtscTransponderData(f:%d, m :%d\n", frequency_, modulation_);
    cDvbTransponderParameters dtp(c->Parameters());
    dtp.SetModulation(modulation_);
    return c->SetTransponderData(type, frequency_, symbolrate_, dtp.ToString('A'), true);
}

void cAtscTransponder::PrintData() const
{
    printf("%d\n", frequency_);
}

//----------- Class cIptvTransponder -------------------------------

cIptvTransponder::cIptvTransponder(int Frequency, cString Parameters):cTransponder(Frequency)
{
    channelNum_ = 0;
    parameters_ = Parameters;

    DEBUG_TRANSPONDER(DBGT " new cIptvTransponder f: %d, p %s \n", Frequency, (const char*)Parameters);
}

cIptvTransponder::~cIptvTransponder()
{
}

bool cIptvTransponder::SetTransponderData(cChannel * c, int Code)
{
    int type = ('I' << 24);

    DEBUG_TRANSPONDER(DBGT " SetIptvTransponderData f:%d, p:%s\n", frequency_, (const char*)parameters_);

    return c->SetTransponderData(type, frequency_, 0, parameters_, true);
}

void cIptvTransponder::PrintData() const
{
    printf("%d, %s\n", frequency_, (const char *)parameters_);
}

//----------- Class cPvrTransponder -------------------------------

cPvrTransponder::cPvrTransponder(int ChannelNr, int Frequency, cString Parameters):cTransponder(Frequency)
{
    channelNum_ = ChannelNr;
    parameters_ = Parameters;

    DEBUG_TRANSPONDER(DBGT " new cPvrTransponder Channel: %d f: %d, p %s \n", ChannelNr, Frequency, (const char*)Parameters);
}

cPvrTransponder::~cPvrTransponder()
{
}

bool cPvrTransponder::SetTransponderData(cChannel * c, int Code)
{
    int type = ('V' << 24);

    DEBUG_TRANSPONDER(DBGT " SetPvrTransponderData f:%d, p:%s\n", frequency_, (const char*)parameters_);

    return c->SetTransponderData(type, frequency_, 0, parameters_, true);
}

void cPvrTransponder::PrintData() const
{
    printf("%d, %s\n", frequency_, (const char *)parameters_);
}

//----------- Class Transponders -------------------------------

cTransponders::cTransponders():sourceCode_(0)
{
}

void cTransponders::Load(cScanParameters * scp)
{
    DEBUG_TRANSPONDER(DBGT "  %s \n", __PRETTY_FUNCTION__);
    Clear();

    sourceCode_ = scp->source;

    if (scp->type == SAT || scp->type == SATS2)
    {
        lockMs_ = 500;

        if (scp->frequency < 5) //  auto
        {
            if ((scp->nitscan && !LoadNitTransponder(sourceCode_)) || !scp->nitscan)
            {
                fileName_ = TplFileName(sourceCode_);
                position_ = SetPosition(fileName_);
                // load old single SAT transponder list
                LoadTpl(fileName_);
            }
        }
        else
        {                       // manual single transponder
            int system = 0, streamId = 0, fec = 0;

            if (scp->type == SATS2)
            {
                if (scp->system == DVB_SYSTEM_2){
                   fec = StatToS2Fec(scp->fec);
                // TODO explain
                } else fec = StatToFec(scp->fec);
                system = scp->system;
                streamId = scp->streamId > 0 ? scp->streamId : 0;
            }
            scp->modulation = StatToS2Mod(scp->modulation);

            DEBUG_TRANSPONDER(DBGT " Load single SatTransponderData(f:\"%d\", pol[%c] -- symbolRate  %d,  modulation %d  fec %d sys %d stream %d\n", scp->frequency, scp->polarization,
             scp->symbolrate, scp->modulation, fec, system, streamId);

            cSatTransponder *t = new cSatTransponder(scp->frequency, scp->polarization, scp->symbolrate, scp->modulation, fec, 0, system, streamId);
            //printf("%s:%i new cSatTransponder created with fec %i\n", __FILE__, __LINE__, scp->fec);

            fileName_ = TplFileName(sourceCode_);
            position_ = SetPosition(fileName_); // need this to display Satellite info
            fileName_.clear();  // donot load transponder list during manual scan
            // what about description_ ? //TODO

            v_tp_.push_back(t);
        }

    }
    else if (scp->type == TERR || scp->type == TERR2 || scp->type == DMB_TH || scp->type == ISDB_T)
    {
        position_ = tr("Terrestrial");

        if (scp->frequency == 1)
            CalcTerrTpl(0, scp);
        else
        {
            int channel = scp->port;
            int bandwidth = 0;
            int system = 0, streamId = 0;
            if (scp->bandwidth == 2)
                bandwidth = 8000000;
            if (scp->bandwidth == 1)
                bandwidth = 7000000;
            if (scp->bandwidth == 0)
            {
                if (scp->region == 0 && scp->frequency < 300000)
                    bandwidth = 7000000;
                else
                    bandwidth = 8000000;
            }
            if (scp->type == TERR2 && scp->streamId > 0)
            {
                streamId = scp->streamId;
            }
            if (scp->type != TERR)
            {
                system = scp->system;
            }

            cTerrTransponder *t = new cTerrTransponder(channel, scp->frequency * 1000, bandwidth, system, streamId);
            v_tp_.push_back(t);
        }
    }
    else if (scp->type == CABLE)
    {
        position_ = tr("Cable");
        if (scp->frequency == 1)
            CalcCableTpl(0, scp);
        else
        {
            int channel = scp->port;
            int sRate;
            if (scp->symbolrate_mode == 2)  /// XXX check this markus
                sRate = scp->symbolrate;
            else
                sRate = 6900;

            cCableTransponder *t = new cCableTransponder(channel, scp->frequency * 1000, sRate, scp->modulation);
            v_tp_.push_back(t);
        }
    }
    else if (scp->type == ATSC)
    {
        position_ = tr("Atsc");
        if (scp->frequency == 1)
            CalcAtscTpl(0, scp);
        else
        {
            int channel = scp->port;

            cAtscTransponder *t = new cAtscTransponder(channel, scp->frequency * 1000, scp->modulation);
            v_tp_.push_back(t);
        }
    }
    else if (scp->type == IPTV)
    {
        position_ = tr("IPTV");
        if (scp->frequency == 1) //scan ip range
        {
            if (scp->ipmode == 0)
                CalcIptvTpl(0, scp);
            else
            {
                fileName_ = TplFileName(sourceCode_);
                // load ip subnet
                LoadIpl(fileName_, scp);
            }
        }
        else                    //scan single ip
        {
            int sidScan = 1;
            int pidScan = 1;
            const char *proto = "UDP";
            int frequency;
#ifdef DVBCHANPATCH
            frequency = scp->startip[1]*10000000+scp->startip[2]*10000+scp->startip[3]*10;
#else
            frequency = scp->frequency;
#endif

            cString parameters_;
            parameters_ = cString::sprintf("S=%d|P=%d|F=%s|U=%d.%d.%d.%d|A=%d", pidScan, sidScan, proto, scp->startip[0],
            scp->startip[1],scp->startip[2],scp->startip[3], scp->port);

            cIptvTransponder *t = new cIptvTransponder(frequency, parameters_);

            v_tp_.push_back(t);
        }
    }
    else if (scp->type == ANALOG)
    {
        position_ = tr("Analog");

        if (scp->frequency == 1){
            CalcPvrTpl(0, scp);
        }
        else
        {
            int channel = scp->port;
            int frequency = scp->frequency;

            cString parameters_;
            if (scp->modulation == 0)
            {
                cString card = (scp->adapter == 1) ? "" : cString::sprintf("CARD%d|",scp->frontend);

                if (scp->polarization == 0)
                    parameters_ = cString::sprintf("TV|%s%s", (const char*)card, Standard[scp->system]);
                else
                {
                    parameters_ = cString::sprintf("%s|%s%s", pvrInput[scp->polarization], (const char*)card, Standard[scp->system]);
                    frequency = 1;
                }
            }
            else
                parameters_ = cString::sprintf("RADIO");

            cPvrTransponder *t = new cPvrTransponder(channel, frequency, parameters_);
            v_tp_.push_back(t);
        }
    }
    else
        esyslog(DBGT "   Wrong  sourceCode %d\n", sourceCode_);

    DEBUG_TRANSPONDER(DBGT "  %s end \n", __PRETTY_FUNCTION__);
}

bool cTransponders::LoadNitTransponder(int Source)
{

    DEBUG_TRANSPONDER(DBGT " %s ... \"%d\" \n", __PRETTY_FUNCTION__, Source);

    int found = 0;

    position_ = *cSource::ToString(Source);

    // generate filelist
    vector < string > fileList;
    fileList.push_back(TplFileName(0));
    fileList.push_back(TplFileName(0) + ".bz2");
    fileList.push_back(TplFileName(1));
    fileList.push_back(TplFileName(1) + ".bz2");

    stringstream buffer;

    for (consStrIter it = fileList.begin(); it != fileList.end(); ++it)
    {
        if ((*it).find(".bz2") != std::string::npos)
        {
            ifstream transponderList((*it).c_str(), std::ios_base::in | std::ios_base::binary);

            if (transponderList.fail())
            {
                DEBUG_TRANSPONDER(DBGT " can not load %s  try to load next list\n", (*it).c_str());
                transponderList.close();
                transponderList.clear();
                continue;
            }
            //cerr << " using bzip2 decompressor" << endl;
            int bzerror = 0;
            int buf = 0;
            FILE *file = fopen((*it).c_str(), "r");
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
                        buffer.put(buf);
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
        else
        {
            ifstream transponderList((*it).c_str(), std::ios_base::in);

            if (transponderList.fail())
            {
                DEBUG_TRANSPONDER(DBGT " can not load %s  try to load next list\n", fileName_.c_str());
                transponderList.close();
                transponderList.clear();
                continue;
            }

            /* copy byte-wise */
            int c;
            while (transponderList.good())
            {
                c = transponderList.get();
                if (transponderList.good())
                    buffer.put(c);
            }
        }
    }

    int lc = 0;

    while (buffer.good() && !buffer.eof())
    {
        lc++;

        string line;
        getline(buffer, line);

        // skip lines with #
        if (line.find('#') == 0)
            continue;

        if (line.find(position_) == 0)
        {

            // second entry support
            if (found == lc - 1)
            {
                cSatTransponder *t = new cSatTransponder();
                if (t->Parse(line))
                    v_tp_.push_back(t);
            }
            else
            {
                found = lc;
                nitStartTransponder_.reset(new cSatTransponder);
                nitStartTransponder_->Parse(line);
                DEBUG_TRANSPONDER(DBGT " found first  entry  %d: %s \n", lc, line.c_str());
            }
        }
        else
        {
            if (found > 0 && found == lc - 1)
            {
                DEBUG_TRANSPONDER(DBGT " return true  found: %d: lc:%d \n", found, lc);
                return true;
            }
        }
    }

    if (!found)
        esyslog("ERROR: [channelscan] in  %s :  no values for \"%s\"\n", fileName_.c_str(), position_.c_str());

    return found;
}


bool cTransponders::LoadTpl(const string & tplFileName)
{

    lockMs_ = 500;
    DEBUG_TRANSPONDER(DBGT "LoadSatTpl  %s\n", tplFileName.c_str());

    string tplFileNameComp(tplFileName + ".bz2");
    ifstream transponderList(tplFileNameComp.c_str(), std::ios_base::in | std::ios_base::binary);
    stringstream buffer;

    if (!transponderList)
    {
        esyslog("ERROR: [channelscan] can`t open LoadSatTpls %s --- try uncompressed \n", tplFileNameComp.c_str());

        transponderList.close();
        transponderList.clear();
        transponderList.open(tplFileName.c_str());
        if (!transponderList)
        {
            esyslog("ERROR: [channelscan] can`t open LoadSatTpls %s\n", tplFileName.c_str());
            return false;
        }
        int c;
        /* copy byte-wise */
        while (transponderList.good())
        {
            c = transponderList.get();
            if (transponderList.good())
                buffer.put(c);
        }
    }
    else
    {
        // TODO choose decompressor by file name ending
        //cerr << " using bzip2 decompressor" << endl;
        int bzerror = 0;
        int buf = 0;
        FILE *file = fopen(tplFileNameComp.c_str(), "r");
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
                    buffer.put(buf);
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

    string line;
    int lc = 0;

    while (!buffer.eof())
    {
        getline(buffer, line);
        if (line.find('[') == line.npos)
        {
            cSatTransponder *t = new cSatTransponder();
            if (t->Parse(line))
                v_tp_.push_back(t);
        }
        lc++;
    }

    transponderList.close();

    return true;
}

bool cTransponders::LoadIpl(const string & iplFileName, cScanParameters * scp)
{

    DEBUG_TRANSPONDER(DBGT "LoadIpl  %s\n", iplFileName.c_str());

    ifstream transponderList(iplFileName.c_str(), std::ios_base::in | std::ios_base::binary);
    stringstream buffer;

    if (!transponderList)
    {
        esyslog("ERROR: [channelscan] can`t open LoadIpls %s \n", iplFileName.c_str());
        return false;
    }

    int c;
    /* copy byte-wise */
    while (transponderList.good())
    {
        c = transponderList.get();
        if (transponderList.good())
            buffer.put(c);
    }

    string line;
    while (!buffer.eof())
    {
        int pos=0;
        size_t find;

        getline(buffer, line);
        // parse subnet start address
        for(int a = 0;a < 4; a++)
        {
            if(a < 3)
                find = line.find('.', pos);
            else
                find = line.find('-', pos);
            if (find != line.npos)
            {
                scp->startip[a]=stoi(line.substr(pos,find-pos));
                pos=find+1;
            }
            else goto error;
        }
        // parse subnet end address
        for(int a = 0;a < 4; a++)
        {
            if(a < 3)
                find = line.find('.', pos);
            else
                find = line.find(':', pos);
            if (find != line.npos)
            {
                scp->endip[a]=stoi(line.substr(pos,find-pos));
                pos=find+1;
            }
            else goto error;
        }
        //parse ip port
        find = line.length();
        scp->port=stoi(line.substr(pos,find-pos));

        DEBUG_TRANSPONDER(DBGT "LoadIpl subnet start %d.%d.%d.%d end %d.%d.%d.%d port %d\n",
            scp->startip[0], scp->startip[1], scp->startip[2], scp->startip[3], scp->endip[0], scp->endip[1], scp->endip[2], scp->endip[3], scp->port);
        // create transponders
        CalcIptvTpl(0, scp);
    }
    transponderList.close();
    return true;
error:
    transponderList.close();
    return false;
}

/*
For transponder list update use Sat.url file in Transponders directory.
Each line is a url of transponder's file.
There may be several addresses, will be used first complete downloading.
*/

bool cTransponders::GetTpl()
{
    std::string buffer, out, urlFile;
    bool result;
    char tplAddress[200];
    char *tplFile, *fileType;
    result = true;

    out = cPlugin::ConfigDirectory();
    out += "/transponders/";

    if (ScanSetup.tplUpdateType == 0) //Use Sat.url
    {
        urlFile = out + "Sat.url";

        FILE *file = fopen(urlFile.c_str(), "r");
        if (file && !ferror(file))
        {
            while ( fscanf(file,"%s",tplAddress) != EOF)
            {
                DEBUG_TRANSPONDER(DBGT "----- %s\n",tplAddress);

                buffer = std::string("wget ") + tplAddress + std::string(" -N -P ") + out;
                DEBUG_TRANSPONDER(DBGT "---- %s\n",buffer.c_str());

                result = SystemExec(buffer.c_str());
                if (!result)
                    break;
            }
            fclose(file);
        }
        if (result)
            goto fail;

/* see filename ending for decompressor choose */
/* .zip and .bz2 can use now                   */
/* unzip and bunzip2 must be in system         */

        tplFile = strrchr(tplAddress,'/') + 1;
        fileType = strrchr(tplAddress,'.') + 1;

        if (!strcasecmp(fileType,"zip"))
        {
            buffer = std::string("unzip -o ") + out + tplFile + std::string(" -d ") + out;
            DEBUG_TRANSPONDER(DBGT "---- %s\n",buffer.c_str());
        }
        else if (!strcasecmp(fileType,"bz2"))
        {
            buffer = std::string("bunzip2 -f ") + out + tplFile;
            DEBUG_TRANSPONDER(DBGT "---- %s\n",buffer.c_str());
        }
        else
        {
            esyslog("error [channelscan] unknown archive.\n");
            goto fail;
        }

        result = SystemExec(buffer.c_str());
        if (result)
            goto fail;

        return true;
    }
    else //Use www.kingofsat.net
    {
        int source;

        if (Setup.DiSEqC > 0)
        {
            for (cDiseqc * diseqc = Diseqcs.First(); diseqc; diseqc = Diseqcs.Next(diseqc))
            {
                source = diseqc->Source();
                std::string sat = (const char*)cSource::ToString(source);
                sat = sat.erase(0,1);
                if (!sat.substr(sat.find(".", 0) + 1, 1).compare(std::string{"0"}.c_str()))
                    sat = sat.erase(sat.find(".", 0), 2);

                 buffer = std::string("wget -t 2 -q --content-disposition 'https://ru.kingofsat.net/dl.php?pos=") + sat + std::string("&fkhz=0'") + std::string(" -N -P ") + out;
                 //DEBUG_TRANSPONDER(DBGT "---- %s\n",buffer.c_str());
                 result = SystemExec(buffer.c_str());
            }
        }
        else
        {
            for (cSource * Source = Sources.First(); Source; Source = Sources.Next(Source))
            {
                source = Source->Code();
                std::string sat = (const char*)cSource::ToString(source);
                sat = sat.erase(0,1);
                if (!sat.substr(sat.find(".", 0) + 1, 1).compare(std::string{"0"}.c_str()))
                    sat = sat.erase(sat.find(".", 0), 2);

                buffer = std::string("wget -t 2 -q --content-disposition 'https://ru.kingofsat.net/dl.php?pos=") + sat + std::string("&fkhz=0'") + std::string(" -N -P ") + out;
                //DEBUG_TRANSPONDER(DBGT "---- %s\n",buffer.c_str());

                result = SystemExec(buffer.c_str());
            }
        }
        return true;
    }

fail:
    Skins.Message(mtError, tr("Transponders update failed!"));
    return false;
}

void cTransponders::CalcCableTpl(bool Complete, cScanParameters * scp)
{
    int bandwidth = -1;
    int f, channel = 0;
    int sRate, qam;
    int region = scp->region;

    position_ = tr("Cable");
    Clear();
    /// refactor: mov for "try all" scan all oher values to here

    if (scp->symbolrate_mode == 2)  /// check this markus
        sRate = scp->symbolrate;
    else
        sRate = 6900;

    qam = scp->modulation;

    if (region == 0)
    {  //EUR
        // Give the user the most popular cable channels first, speeds up SR auto detect
        for (channel = 121; channel < 200; channel++)
        {
            f = channel2Frequency(0, channel, bandwidth);
            if (f)
            {
                cCableTransponder *t = new cCableTransponder(channel, f, sRate, qam);
                v_tp_.push_back(t);
            }
        }

        for (channel = 101; channel < 121; channel++)
        {
            f = channel2Frequency(0, channel, bandwidth);
            if (f)
            {
                cCableTransponder *t = new cCableTransponder(channel, f, sRate, qam);
                v_tp_.push_back(t);
            }
        }

        for (channel = 1; channel < 100; channel++)
        {
            f = channel2Frequency(0, channel, bandwidth);
            if (f)
            {
                cCableTransponder *t = new cCableTransponder(channel, f, sRate, qam);
                v_tp_.push_back(t);
            }
        }
    }
    else
    { //RUS + CHINA + ...
        for (channel = 1; channel < 100; channel++)
        {
            f = channel2Frequency(region, channel, bandwidth);
            if (f)
            {
                cCableTransponder *t =
                new cCableTransponder(channel, f, sRate, qam);
                v_tp_.push_back(t);
            }
        }
    }
}

void cTransponders::CalcTerrTpl(bool Complete, cScanParameters * scp)
{
    Clear();
    int f;
    int channel;
    int bandwidth = -1;
    int region = scp->region;
    int system = 0;

    position_ = tr("Terrestrial");
    if (scp->type != TERR2) system = scp->system; //for terr && terr2 seach on terr2 type


    if (region == 0)
    { //EUR

        for (channel = 5; channel <= 69; channel++) //no cable channels ??
        {
            f = channel2Frequency(0, channel, bandwidth);
            if (f)
            {
                cTerrTransponder *t = new cTerrTransponder(channel, f, bandwidth, system, NO_STREAM_ID_FILTER);
                v_tp_.push_back(t);
            }
        }
    }
    else
    { //RUS + CHINA + ...
        for (channel = 1; channel < 100; channel++)
        {
            f = channel2Frequency(region, channel, bandwidth);
            if (f)
            {
                cTerrTransponder *t = new cTerrTransponder(channel, f, bandwidth, scp->system, 0);
                v_tp_.push_back(t);
            }
        }
    }
}

void cTransponders::CalcAtscTpl(bool Complete, cScanParameters * scp)
{
    Clear();
    int f;
    int channel;
    int bandwidth = -1;
    int region = scp->region;
    int vsb = scp->modulation;

    position_ = tr("Atsc");

//    if (region == 4 || region == 5) //need ??
    { //USA + Japan
        for (channel = 1; channel < 100; channel++)
        {
            f = channel2Frequency(region, channel, bandwidth);
            if (f)
            {
                cAtscTransponder *t = new cAtscTransponder(channel, f, vsb);
                v_tp_.push_back(t);
            }
        }
    }
}

void cTransponders::CalcIptvTpl(bool Complete, cScanParameters * scp)
{

    position_ = tr("IPTV");
    int channel = 0;
    int sidScan = 1;
    int pidScan = 1;
    const char *proto = "UDP";
    int frequency;
    int ip[4];
    int cnt = 10;

    for (ip[0] = scp->startip[0];(ip[0] <= scp->endip[0]); ip[0]++)
    {
        for (ip[1] = ip[0] == scp->startip[0] ? scp->startip[1] : 0;(ip[1] <= scp->endip[1]); ip[1]++)
        {
            for (ip[2] = ip[1] == scp->startip[1] ? scp->startip[2] : 0;(ip[2] <= scp->endip[2]); ip[2]++)
            {
                for (ip[3] = ip[2] == scp->startip[2] ? scp->startip[3] : 1;(ip[3] <= scp->endip[3]); ip[3]++)
                {

#ifdef DVBCHANPATCH
                    frequency = ip[1]*10000000+ip[2]*10000+ip[3]*10;
#else
                    frequency = cnt;
#endif

                    cString parameters_;
                    parameters_ = cString::sprintf("S=%d|P=%d|F=%s|U=%d.%d.%d.%d|A=%d", pidScan, sidScan, proto, ip[0],
                    ip[1],ip[2],ip[3], scp->port);

                    cIptvTransponder *t = new cIptvTransponder(frequency, parameters_);

                    v_tp_.push_back(t);
                    cnt += 10;
                    channel++;
                }
            }
        }
    }
}

void cTransponders::CalcPvrTpl(bool Complete, cScanParameters * scp)
{
    Clear();
    int f;
    int channel = 0;
    int bandwidth = -1;
    int region = scp->region;
    if (region < 100) esyslog ("not analog region!\n");

    position_ = tr("Analog");

    cString parameters_;

/*-------------------------------- TV -----------------------------------------------*/
    if (scp->modulation == 0)
    {
        cString card = (scp->adapter == 1) ? "" : cString::sprintf("CARD%d|",scp->frontend);

        parameters_ = cString::sprintf("TV|%s%s", (const char*)card, Standard[scp->system]);;

        if (region == 100)
        { //EUR

            for (channel = 1; channel < 200; channel++)
            {
                f = channel2Frequency(region, channel, bandwidth);
                if (f)
                {
                    cPvrTransponder *t = new cPvrTransponder(channel, f / 1000, parameters_);
                    v_tp_.push_back(t);
                }
            }
        }
        else
        { //RUS + China + USA + Japan
            for (channel = 1; channel < 100; channel++)
            {
                f = channel2Frequency(region, channel, bandwidth);
                if (f)
                {
                    cPvrTransponder *t = new cPvrTransponder(channel, f / 1000, parameters_);
                    v_tp_.push_back(t);
                }
            }
        }
    }
/*---------------------------------- RADIO -------------------------------------------*/
    else  //TODO
    {
        parameters_ = cString::sprintf("RADIO");
        if (scp->region == 101)
        {//OIRT
            for (f = 65900000;f <= 74000000; f += 10000)
            {
                cPvrTransponder *t = new cPvrTransponder(++channel, f / 1000, parameters_);
                v_tp_.push_back(t);
            }
        }
        else
        {//CCIR
            for (f = 85700000;f <= 108000000; f += 50000)
            {
                cPvrTransponder *t = new cPvrTransponder(++channel, f / 1000, parameters_);
                v_tp_.push_back(t);
            }
        }
    }

}


string cTransponders::SetPosition(const string & tplPath)
{
    if (cSource::IsSat(sourceCode_))
    {
        string tmp = fileName_.substr(tplPath.find_last_of('/') + 1);
        int index = tmp.find_last_of('.');
        tmp.erase(index);
        return tmp;
    }
    else if (cSource::IsAtsc(sourceCode_))
        return "ATSC";
    else if (cSource::IsTerr(sourceCode_))
        return "DVB-T";
    else if (cSource::IsCable(sourceCode_))
        return "DVB-C";
    else
        return "";
}

string cTransponders::TplFileName(int satCodec)
{
    string tmp = cPlugin::ConfigDirectory();
    tmp += "/transponders/";

    if (satCodec == 0)
    {
        //tmp += "SatNitScan";
        tmp += "Sat";
        isyslog("INFO [channelscan]: Please update Transponder lists \n");
    }
    else if (satCodec == 1)
        tmp += "NIT";
    else if (satCodec == 0xE000)
        tmp += "Iptv";
    else
    {
        if (!ScanSetup.tplFileType)
            tmp += *cSource::ToString(satCodec);
        else
        {
            int a = cSource::Position(satCodec);
            if (cSource::Position(satCodec) < 0) a = 3600 + cSource::Position(satCodec);

            tmp += cString::sprintf("%04d", a);
        }
    }

    if(ScanSetup.tplFileType == 0)
        tmp += ".tpl";
    else
        tmp += ".ini";

    DEBUG_TRANSPONDER(DBGT "Transponder filename %s\n",tmp.c_str());

    return tmp;
}

void cTransponders::Clear()
{
    for (TpIter iter = v_tp_.begin(); iter != v_tp_.end(); ++iter)
    {
        delete *iter;
        *iter = NULL;
    }

    v_tp_.clear();
}

bool cTransponders::MissingTransponder(int Transponder)
{
    for (constTpIter iter = v_tp_.begin(); iter != v_tp_.end(); ++iter)
    {
        if (Transponder == (*iter)->Frequency())
            return false;
    }
    return true;
}

cTransponders::~cTransponders()
{
    Clear();
}

cTransponders & cTransponders::GetInstance()
{
    assert(instance_);
    return *instance_;
}

void cTransponders::Create()
{
    if (!instance_)
    {
        instance_ = new cTransponders();
    }
}
void cTransponders::Destroy()
{
    delete instance_;
    instance_ = NULL;
}

cTransponder *cTransponders::GetNITStartTransponder()
{
    return nitStartTransponder_.get();

}

int cTransponders::SourceCode() const
{
    return sourceCode_;
}

std::string cTransponders::Position() const
{
    return position_;
}

std::string cTransponders::Description() const
{
    return description_;
}

int cTransponders::LockMs() const
{
    return lockMs_;
}

void cTransponders::ResetNITStartTransponder(cSatTransponder * v)
{
    nitStartTransponder_.reset(v);
}

#ifndef REELVDR

namespace setup
{
    // TEST this !!
    string FileNameFactory(string FileType)
    {
        string configDir = cPlugin::ConfigDirectory();
        string::size_type pos = configDir.find("plugin");
        configDir.erase(pos - 1);

        if (FileType == "configDirecory")   // vdr config base directory
            DEBUG_TRANSPONDER(DBGT " Load configDir %s  \n", configDir.c_str());
        else if (FileType == "setup")   // returns plugins/setup dir; change to  "configDir"
            configDir += "/plugins/setup";

        DEBUG_TRANSPONDER(DBGT " Load configDir %s  \n", configDir.c_str());
        return configDir;
    }
}

#endif
