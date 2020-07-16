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
 *  filter.h: collected and adapted pat sdt nit filter from VDR-core 
 *
 ***************************************************************************/

#ifndef CFILTER_H
#define CFILTER_H

#include <map>
#include <set>
#include <vector>
#include <algorithm>

#include <stdint.h>

#include <vdr/eit.h>
#include <vdr/filter.h>
#include <vdr/channels.h>
#include <libsi/section.h>
#include <libsi/descriptor.h>
#include <vdr/tools.h>
#include "transponders.h"

#define CMAXPMTENTRIES 64
#define CMAXSIDENTRIES 256

#define FILTERTIMEOUT 4

extern std::map < int, cTransponder * >transponderMap;
// test
extern std::map < int, const cSatTransponder > hdTransponders;
extern std::map < int, int >TblVersions;

typedef std::map < int, cTransponder * >::const_iterator tpMapIter;
typedef std::map < int, cTransponder * >::iterator tpMapItr;
typedef std::pair < tpMapItr, bool > mapRet;

// test
typedef std::map < int, const cSatTransponder >::const_iterator tpHDMapIter;

class SdtFilter;


class PatFilter:public cFilter
{
  private:
  cMutex mutex;
  cTimeMs timer;
  int patVersion;

    int pSid[CMAXSIDENTRIES];
    int pSidCnt;
  int sid;
    time_t lastPmtScan[CMAXPMTENTRIES];
    int pmtIndex;
    int pmtPid[CMAXPMTENTRIES];
  int pmtId[CMAXPMTENTRIES];
    int Sids[CMAXPMTENTRIES];
    int pmtVersion[CMAXPMTENTRIES];
    int numPmtEntries;
  int GetPmtPid(int Index) { return pmtId[Index] & 0x0000FFFF; }
  int MakePmtId(int PmtPid, int Sid) { return PmtPid | (Sid << 16); }
    bool PmtVersionChanged(int PmtPid, int Sid, int Version, bool SetNewVersion = false);
    int num, pit, pnum;
    SdtFilter *sdtFilter;
    volatile bool endofScan;
    bool SidinSdt(int Sid);
    bool sdtfinished;
    time_t lastFound;
    int waitingForGodot;
    int AddServiceType;
 void SwitchToNextPmtPid(void);
  protected:
    virtual void Process(u_short Pid, u_char Tid, const u_char * Data, int Length);
  public:
    PatFilter(void);
    void SetSdtFilter(SdtFilter * SdtFilter);
    virtual void SetStatus(bool On);
    bool EndOfScan() const
    {
        return endofScan;
    };
    void Trigger(int Sid = -1);
    void SdtFinished(void)
    {
        sdtfinished = true;
    };
    time_t LastFoundTime(void) const
    {
        return lastFound;
    };
    void GetFoundNum(int &current, int &total);
    bool noSDT;
//    int GetNumPmtEntries(){return numPmtEntries;};
};

#if VDRVERSNUM <= 20102
int GetCaDescriptors(int Source, int Transponder, int ServiceId, const unsigned short *CaSystemIds, int BufSize, uchar * Data, int EsPid);
#else
int GetCaDescriptors(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, uchar * Data, int EsPid);
int GetCaPids(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, int *Pids);
#endif

class SdtFilter:public cFilter
{
    friend class PatFilter;
  private:
cMutex mutex;
    int numSid, sid[CMAXSIDENTRIES];
    int usefulSid[CMAXSIDENTRIES];
    int numUsefulSid;
    cSectionSyncer sectionSyncer;
    PatFilter *patFilter;
    int AddServiceType;
    int Rid;
  protected:
    virtual void Process(u_short Pid, u_char Tid, const u_char * Data, int Length);
  public:
    SdtFilter(PatFilter * PatFilter);
    int GetNumUsefulSid(){return numUsefulSid;};
    virtual void SetStatus(bool On);
    void SetRid(int rid){Rid = rid;};
};

class SdtMuxFilter:public cFilter
{
    friend class PatFilter;
  private:
cMutex mutex;
    int numMux;
    int Mux[256];
    cSectionSyncer sectionSyncer;
    PatFilter *patFilter;
  protected:
    virtual void Process(u_short Pid, u_char Tid, const u_char * Data, int Length);
  public:
    SdtMuxFilter(PatFilter * PatFilter);
    int GetNumMux(){return numMux;};
    virtual void SetStatus(bool On);
};


class NitFilter:
public cFilter
{
  private:
#if VDRVERSNUM < 20301
    class cNit
    {
      public:
        u_short networkId;
        char name[MAXNETWORKNAME];
        bool hasTransponder;
    };
#endif
    cSectionSyncer sectionSyncer;
    unsigned int lastCount;
    volatile bool endofScan;
    volatile bool found_;
    std::vector < int > sectionSeen_;
    int t2plp[256];
  protected:
    virtual void Process(u_short Pid, u_char Tid, const u_char * Data, int Length);
  public:
    NitFilter(void);
    ~NitFilter(void);
    virtual void SetStatus(bool On);
    bool EndOfScan()
    {
        return endofScan;
    };
    void Dump();
    bool Found();
    int mode;
    int * T2plp()
    {
        return t2plp;
    }
};


#endif
