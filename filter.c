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
 *  filter.c: collected and adapted pat sdt nit filter from VDR-core
 *
 ***************************************************************************/

#include <linux/dvb/frontend.h>
#include <map>
#include <vector>
#include <utility>

#ifdef REELVDR
#include <vdr/s2reel_compat.h>
#endif
//#include <vdr/channels.h>
#include <libsi/section.h>
#include <libsi/descriptor.h>

#include "csmenu.h"
#include "filter.h"

using std::vector;
using std::map;
using std::set;
using std::make_pair;


std::map < int, cTransponder * >transponderMap;
std::map < int, int >TblVersions;
std::map < int, const cSatTransponder > hdTransponders;
//std::list<int, cTransponder> hdTransponders;

#define DBGSDT " debug [sdt filter] "
//#define DEBUG_SDT(format, args...) printf (format, ## args)
#define DEBUG_SDT(format, args...)

#if 0
#define DEBUG_printf(format, args...) printf (format, ## args)
#else
#define DEBUG_printf(format, args...)
#endif

//#define DEBUG_PAT_PMT
#ifdef DEBUG_PAT_PMT
#define DBGPAT(a...) { cString s = cString::sprintf(a); fprintf(stderr, "%s\n", *s); dsyslog("%s", *s); }
#else
#define DBGPAT(a...)
#endif

#define PMT_SCAN_TIMEOUT  1000 // ms

#define ST_ALL           0
#define ST_TV_ONLY       1
#define ST_HDTV_ONLY     2
#define ST_RADIO_ONLY    3
#define ST_ALL_FTA_ONLY  4
#define ST_TV_FTA_ONLY   5
#define ST_HDTV_FTA_ONLY 6


// --- cCaDescriptor ---------------------------------------------------------

class cCaDescriptor:public cListObject
{
  private:
    int caSystem;
    int caPid;
    int esPid;
    int length;
    uchar *data;
  public:
    cCaDescriptor(int CaSystem, int CaPid, int EsPid, int Length, const uchar * Data);
    virtual ~ cCaDescriptor();
    bool operator==(const cCaDescriptor & arg) const;
    int CaSystem(void) const
    {
        return caSystem;
    }
    int CaPid(void)
    {
        return caPid;
    }
    int EsPid(void) const
    {
        return esPid;
    }
    int Length(void) const
    {
        return length;
    }
    const uchar *Data(void) const
    {
        return data;
    }
};

cCaDescriptor::cCaDescriptor(int CaSystem, int CaPid, int  EsPid, int Length, const uchar * Data)
{
    caSystem = CaSystem;
    esPid = EsPid;
    length = Length + 6;
    data = MALLOC(uchar, length);
    data[0] = SI::CaDescriptorTag;
    data[1] = length - 2;
    data[2] = (caSystem >> 8) & 0xFF;
    data[3] = caSystem & 0xFF;
    data[4] = ((CaPid >> 8) & 0x1F) | 0xE0;
    data[5] = CaPid & 0xFF;
    if (Length)
        memcpy(&data[6], Data, Length);
}

cCaDescriptor::~cCaDescriptor()
{
    free(data);
}

bool cCaDescriptor::operator==(const cCaDescriptor & arg) const
{
    return esPid == arg.esPid && length == arg.length && memcmp(data, arg.data, length) == 0;
}

// --- cCaDescriptors --------------------------------------------------------

class cCaDescriptors:public cListObject
{
  private:
    int source;
    int transponder;
    int serviceId;
    int pmtPid;
    int numCaIds;
    int caIds[MAXCAIDS + 1];
    cList < cCaDescriptor > caDescriptors;
    void AddCaId(int CaId);
  public:
    cCaDescriptors(int Source, int Transponder, int ServiceId, int PmtPid);
    bool operator==(const cCaDescriptors & arg) const;
    bool Is(int Source, int Transponder, int ServiceId);
    bool Is(cCaDescriptors * CaDescriptors);
    bool Empty(void)
    {
        return caDescriptors.Count() == 0;
    }
    void AddCaDescriptor(SI::CaDescriptor * d, int EsPid);

    int GetCaDescriptors(const int *CaSystemIds, int BufSize, uchar * Data, int EsPid);
#if VDRVERSNUM > 20102
    int GetCaPids(const int *CaSystemIds, int BufSize, int *Pids);
#endif
    const int *CaIds(void)
    {
        return caIds;
    }

};

cCaDescriptors::cCaDescriptors(int Source, int Transponder, int ServiceId, int PmtPid)
{
    source = Source;
    transponder = Transponder;
    serviceId = ServiceId;
    numCaIds = 0;
    caIds[0] = 0;
    pmtPid = PmtPid;
}

bool cCaDescriptors::operator==(const cCaDescriptors & arg) const
{
#if VDRVERSNUM < 20301
    cCaDescriptor *ca1 = caDescriptors.First();
    cCaDescriptor *ca2 = arg.caDescriptors.First();
#else
    const cCaDescriptor *ca1 = caDescriptors.First();
    const cCaDescriptor *ca2 = arg.caDescriptors.First();
#endif

    while (ca1 && ca2)
    {
        if (!(*ca1 == *ca2))
            return false;
        ca1 = caDescriptors.Next(ca1);
        ca2 = arg.caDescriptors.Next(ca2);
    }
    return !ca1 && !ca2;
}

bool cCaDescriptors::Is(int Source, int Transponder, int ServiceId)
{
    return source == Source && transponder == Transponder && serviceId == ServiceId;
}

bool cCaDescriptors::Is(cCaDescriptors * CaDescriptors)
{
    return Is(CaDescriptors->source, CaDescriptors->transponder, CaDescriptors->serviceId);
}

int cCaDescriptors::GetCaDescriptors(const int *CaSystemIds, int BufSize, uchar *Data, int EsPid)
{
    if (!CaSystemIds || !*CaSystemIds)
        return 0;
    if (BufSize > 0 && Data)
    {
        int length = 0;
        for (cCaDescriptor *d = caDescriptors.First(); d; d = caDescriptors.Next(d))
        {
            if (EsPid < 0 || d->EsPid() == EsPid)
            {
               const int *caids = CaSystemIds;
               do
               {
                  if (*caids == 0xFFFF || d->CaSystem() == *caids)
                     {
                        if (length + d->Length() <= BufSize)
                        {
                            memcpy(Data + length, d->Data(), d->Length());
                            length += d->Length();
                        }
                        else
                            return -1;
                     }
                }
                while (*++caids);
            }
        }
        return length;
    }
    return -1;
}

#if VDRVERSNUM > 20102
int cCaDescriptors::GetCaPids(const int *CaSystemIds, int BufSize, int *Pids)
{
  if (!CaSystemIds || !*CaSystemIds)
     return 0;
  if (BufSize > 0 && Pids) {
     int numPids = 0;
     for (cCaDescriptor *d = caDescriptors.First(); d; d = caDescriptors.Next(d)) {
         const int *caids = CaSystemIds;
         do {
            if (*caids == 0xFFFF || d->CaSystem() == *caids) {
               if (numPids + 1 < BufSize) {
                  Pids[numPids++] = d->CaPid();
                  Pids[numPids] = 0;
                  }
               else
                  return -1;
               }
            } while (*++caids);
         }
     return numPids;
     }
  return -1;
}
#endif

// --- cCaDescriptorHandler --------------------------------------------------

class cCaDescriptorHandler:public cList < cCaDescriptors >
{
  private:
    cMutex mutex;
  public:
    int AddCaDescriptors(cCaDescriptors * CaDescriptors);
    // Returns 0 if this is an already known descriptor,
    // 1 if it is an all new descriptor with actual contents,
    // and 2 if an existing descriptor was changed.

    int GetCaDescriptors(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, uchar * Data, int EsPid);
#if VDRVERSNUM > 20102
    int GetCaPids(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, int *Pids);
#endif
};

int cCaDescriptorHandler::AddCaDescriptors(cCaDescriptors * CaDescriptors)
{
    cMutexLock MutexLock(&mutex);
    for (cCaDescriptors * ca = First(); ca; ca = Next(ca))
    {
        if (ca->Is(CaDescriptors))
        {
            if (*ca == *CaDescriptors)
            {
                delete CaDescriptors;
                return 0;
            }
            Del(ca);
            Add(CaDescriptors);
            return 2;
        }
    }
    Add(CaDescriptors);
    return CaDescriptors->Empty()? 0 : 1;
}

int cCaDescriptorHandler::GetCaDescriptors(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, uchar * Data, int EsPid)
{
    cMutexLock MutexLock(&mutex);
    for (cCaDescriptors * ca = First(); ca; ca = Next(ca))
    {
        if (ca->Is(Source, Transponder, ServiceId))
            return ca->GetCaDescriptors(CaSystemIds, BufSize, Data, EsPid);
    }
    return 0;
}

#if VDRVERSNUM > 20102
int cCaDescriptorHandler::GetCaPids(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, int *Pids)
{
  cMutexLock MutexLock(&mutex);
  for (cCaDescriptors *ca = First(); ca; ca = Next(ca)) {
      if (ca->Is(Source, Transponder, ServiceId))
         return ca->GetCaPids(CaSystemIds, BufSize, Pids);
      }
  return 0;
}
#endif

cCaDescriptorHandler CaDescriptorHandler;

int GetCaDescriptors(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, uchar * Data, int EsPid)
{
    return CaDescriptorHandler.GetCaDescriptors(Source, Transponder, ServiceId, CaSystemIds, BufSize, Data, EsPid);
}

#if VDRVERSNUM > 20102
int GetCaPids(int Source, int Transponder, int ServiceId, const int *CaSystemIds, int BufSize, int *Pids)
{
  return CaDescriptorHandler.GetCaPids(Source, Transponder, ServiceId, CaSystemIds, BufSize, Pids);
}
#endif

// --- PatFilter ------------------------------------------------------------

PatFilter::PatFilter(void)
{
sdtFilter = NULL;
  Trigger(0);
  Set(0x00, 0x00);  // PAT
#ifdef REELVDR
    Set(0x1fff, 0x42);          // decrease latency
#endif
sdtfinished = false;
noSDT = false;
endofScan = false;


}

void PatFilter::SetSdtFilter(SdtFilter * SdtFilter)
{
    sdtFilter = SdtFilter;
}


void PatFilter::SetStatus(bool On)
{
  cMutexLock MutexLock(&mutex);
  DBGPAT("PAT filter set status %d", On);
  cFilter::SetStatus(On);
  Trigger();
  num = 0;
}

void PatFilter::Trigger(int Sid)
{
  cMutexLock MutexLock(&mutex);
  patVersion = -1;
  pmtIndex = -1;
  numPmtEntries = 0;
  if (Sid >= 0) {
     sid = Sid;
     DBGPAT("PAT filter trigger SID %d", Sid);
     }
}

bool PatFilter::PmtVersionChanged(int PmtPid, int Sid, int Version, bool SetNewVersion)
{
  int Id = MakePmtId(PmtPid, Sid);
  for (int i = 0; i < numPmtEntries; i++) {
      if (pmtId[i] == Id) {
         if (pmtVersion[i] != Version) {
            if (SetNewVersion)
               pmtVersion[i] = Version;
            else
               DBGPAT("PMT %d  %2d %5d %2d -> %2d", Transponder(), i, PmtPid, pmtVersion[i], Version);
            return true;
            }
         break;
         }
      }
  return false;
}

void PatFilter::SwitchToNextPmtPid(void)
{
  if (pmtIndex >= 0) {
     Del(GetPmtPid(pmtIndex), SI::TableIdPMT);
     pmtIndex = (pmtIndex + 1) % numPmtEntries;
     Add(GetPmtPid(pmtIndex), SI::TableIdPMT);
     }
}

void PatFilter::Process(u_short Pid, u_char Tid, const u_char *Data, int Length)
{
  cMutexLock MutexLock(&mutex);
  if (Pid == 0x00) {
     if (Tid == SI::TableIdPAT) {
        SI::PAT pat(Data, false);
        if (!pat.CheckCRCAndParse())
           return;
        if (pat.getVersionNumber() != patVersion) {
           DBGPAT("PAT %d ver %d -> getver %d", Transponder(), patVersion, pat.getVersionNumber());
           if (pmtIndex >= 0) {
              Del(GetPmtPid(pmtIndex), SI::TableIdPMT);
              pmtIndex = -1;
              }
           numPmtEntries = 0;
           SI::PAT::Association assoc;
           for (SI::Loop::Iterator it; pat.associationLoop.getNext(assoc, it); ) {
               if (!assoc.isNITPid() && numPmtEntries < MAXPMTENTRIES) {
                  DBGPAT("    PMT pid %2d %5d  SID %5d", numPmtEntries, assoc.getPid(), assoc.getServiceId());
                  pmtId[numPmtEntries] = MakePmtId(assoc.getPid(), assoc.getServiceId());
                  pmtVersion[numPmtEntries] = -1;
                  if (sid == assoc.getServiceId()) {
                     pmtIndex = numPmtEntries;
                     DBGPAT("sid = %d pmtIndex = %d", sid, pmtIndex);
                     }
                  numPmtEntries++;
                  }
               }
           if (numPmtEntries > 0 && pmtIndex < 0)
              pmtIndex = numPmtEntries;
           Add(GetPmtPid(pmtIndex), SI::TableIdPMT);
           patVersion = pat.getVersionNumber();
           timer.Set(PMT_SCAN_TIMEOUT);
           }
        }
     }
  else if (Tid == SI::TableIdPMT && Source() && Transponder()) {
     timer.Set(PMT_SCAN_TIMEOUT);
     SI::PMT pmt(Data, false);
     if (!pmt.CheckCRCAndParse())
        return;
     if (!PmtVersionChanged(Pid, pmt.getTableIdExtension(), pmt.getVersionNumber())) {
        SwitchToNextPmtPid();
        return;
        }
#if VDRVERSNUM < 20301
     if (!Channels.Lock(true, 10))
#else
     bool ChannelsModified = false;
     cStateKey StateKey;
     cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 10);
     if (!Channels)
#endif
        return;
     PmtVersionChanged(Pid, pmt.getTableIdExtension(), pmt.getVersionNumber(), true);
     SwitchToNextPmtPid();

     cChannel *channel = NULL;
#if VDRVERSNUM < 20301
     if (cSource::IsType(Source(), 'I'))
     {
          for (cChannel *cChannel = Channels.First(); cChannel; cChannel = Channels.Next(cChannel))
           {
               if (!strcmp(cChannel->Parameters(), Channel()->Parameters()))
               {
                  channel = cChannel;
                  break;
               }
           }
        }
     else
        channel = Channels.GetByServiceID(Source(), Transponder(), pmt.getServiceId());
#else
     if (cSource::IsType(Source(), 'I'))
     {
          for (cChannel *cChannel = Channels->First(); cChannel; cChannel = Channels->Next(cChannel))
           {
               if (!strcmp(cChannel->Parameters(), Channel()->Parameters()))
               {
                  channel = cChannel;
                  break;
               }
           }
     }
     else
        channel = Channels->GetByServiceID(Source(), Transponder(), pmt.getServiceId());
#endif
     if (channel) {
        SI::CaDescriptor *d;
        cCaDescriptors *CaDescriptors = new cCaDescriptors(channel->Source(), channel->Transponder(), channel->Sid(), Pid);
        // Scan the common loop:
        for (SI::Loop::Iterator it; (d = (SI::CaDescriptor*)pmt.commonDescriptors.getNext(it, SI::CaDescriptorTag)); ) {
            CaDescriptors->AddCaDescriptor(d, 0);
            delete d;
            }
        // Scan the stream-specific loop:
        SI::PMT::Stream stream;
        int Vpid = 0;
        int Ppid = 0;
        int Vtype = 0;
        int Apids[MAXAPIDS + 1] = { 0 }; // these lists are zero-terminated
        int Atypes[MAXAPIDS + 1] = { 0 };
        int Dpids[MAXDPIDS + 1] = { 0 };
        int Dtypes[MAXDPIDS + 1] = { 0 };
        int Spids[MAXSPIDS + 1] = { 0 };
        uchar SubtitlingTypes[MAXSPIDS + 1] = { 0 };
        uint16_t CompositionPageIds[MAXSPIDS + 1] = { 0 };
        uint16_t AncillaryPageIds[MAXSPIDS + 1] = { 0 };
        char ALangs[MAXAPIDS][MAXLANGCODE2] = { "" };
        char DLangs[MAXDPIDS][MAXLANGCODE2] = { "" };
        char SLangs[MAXSPIDS][MAXLANGCODE2] = { "" };
        int Tpid = 0;
        int NumApids = 0;
        int NumDpids = 0;
        int NumSpids = 0;
        for (SI::Loop::Iterator it; pmt.streamLoop.getNext(stream, it); ) {
            bool ProcessCaDescriptors = false;
            int esPid = stream.getPid();
            switch (stream.getStreamType()) {
              case 1: // STREAMTYPE_11172_VIDEO
              case 2: // STREAMTYPE_13818_VIDEO
              case 0x1B: // H.264
                      Vpid = esPid;
                      Ppid = pmt.getPCRPid();
                      Vtype = stream.getStreamType();
                      ProcessCaDescriptors = true;
                      break;
              case 3: // STREAMTYPE_11172_AUDIO
              case 4: // STREAMTYPE_13818_AUDIO
              case 0x0F: // ISO/IEC 13818-7 Audio with ADTS transport syntax
              case 0x11: // ISO/IEC 14496-3 Audio with LATM transport syntax
                      {
                      if (NumApids < MAXAPIDS) {
                         Apids[NumApids] = esPid;
                         Atypes[NumApids] = stream.getStreamType();
                         SI::Descriptor *d;
                         for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                             switch (d->getDescriptorTag()) {
                               case SI::ISO639LanguageDescriptorTag: {
                                    SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                    SI::ISO639LanguageDescriptor::Language l;
                                    char *s = ALangs[NumApids];
                                    int n = 0;
                                    for (SI::Loop::Iterator it; ld->languageLoop.getNext(l, it); ) {
                                        if (*ld->languageCode != '-') { // some use "---" to indicate "none"
                                           if (n > 0)
                                              *s++ = '+';
                                           strn0cpy(s, I18nNormalizeLanguageCode(l.languageCode), MAXLANGCODE1);
                                           s += strlen(s);
                                           if (n++ > 1)
                                              break;
                                           }
                                        }
                                    }
                                    break;
                               default: ;
                               }
                             delete d;
                             }
                         NumApids++;
                         }
                      ProcessCaDescriptors = true;
                      }
                      break;
              case 5: // STREAMTYPE_13818_PRIVATE
              case 6: // STREAMTYPE_13818_PES_PRIVATE
              //XXX case 8: // STREAMTYPE_13818_DSMCC
                      {
                      int dpid = 0;
                      int dtype = 0;
                      char lang[MAXLANGCODE1] = { 0 };
                      SI::Descriptor *d;
                      for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                          switch (d->getDescriptorTag()) {
                            case SI::AC3DescriptorTag:
                            case SI::EnhancedAC3DescriptorTag:
                                 dpid = esPid;
                                 dtype = d->getDescriptorTag();
                                 ProcessCaDescriptors = true;
                                 break;
                            case SI::SubtitlingDescriptorTag:
                                 if (NumSpids < MAXSPIDS) {
                                    Spids[NumSpids] = esPid;
                                    SI::SubtitlingDescriptor *sd = (SI::SubtitlingDescriptor *)d;
                                    SI::SubtitlingDescriptor::Subtitling sub;
                                    char *s = SLangs[NumSpids];
                                    int n = 0;
                                    for (SI::Loop::Iterator it; sd->subtitlingLoop.getNext(sub, it); ) {
                                        if (sub.languageCode[0]) {
                                           SubtitlingTypes[NumSpids] = sub.getSubtitlingType();
                                           CompositionPageIds[NumSpids] = sub.getCompositionPageId();
                                           AncillaryPageIds[NumSpids] = sub.getAncillaryPageId();
                                           if (n > 0)
                                              *s++ = '+';
                                           strn0cpy(s, I18nNormalizeLanguageCode(sub.languageCode), MAXLANGCODE1);
                                           s += strlen(s);
                                           if (n++ > 1)
                                              break;
                                           }
                                        }
                                    NumSpids++;
                                    }
                                 break;
                            case SI::TeletextDescriptorTag:
                                 Tpid = esPid;
                                 break;
                            case SI::ISO639LanguageDescriptorTag: {
                                 SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                 strn0cpy(lang, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
                                 }
                                 break;
                            default: ;
                            }
                          delete d;
                          }
                      if (dpid) {
                         if (NumDpids < MAXDPIDS) {
                            Dpids[NumDpids] = dpid;
                            Dtypes[NumDpids] = dtype;
                            strn0cpy(DLangs[NumDpids], lang, MAXLANGCODE1);
                            NumDpids++;
                            }
                         }
                      }
                      break;
              case 0x80: // STREAMTYPE_USER_PRIVATE
                      if (Setup.StandardCompliance == STANDARD_ANSISCTE) { // DigiCipher II VIDEO (ANSI/SCTE 57)
                         Vpid = esPid;
                         Ppid = pmt.getPCRPid();
                         Vtype = 0x02; // compression based upon MPEG-2
                         ProcessCaDescriptors = true;
                         break;
                         }
                      // fall through
              case 0x81: // STREAMTYPE_USER_PRIVATE
                      if (Setup.StandardCompliance == STANDARD_ANSISCTE) { // ATSC A/53 AUDIO (ANSI/SCTE 57)
                         char lang[MAXLANGCODE1] = { 0 };
                         SI::Descriptor *d;
                         for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                             switch (d->getDescriptorTag()) {
                               case SI::ISO639LanguageDescriptorTag: {
                                    SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                    strn0cpy(lang, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
                                    }
                                    break;
                               default: ;
                               }
                            delete d;
                            }
                         if (NumDpids < MAXDPIDS) {
                            Dpids[NumDpids] = esPid;
                            Dtypes[NumDpids] = SI::AC3DescriptorTag;
                            strn0cpy(DLangs[NumDpids], lang, MAXLANGCODE1);
                            NumDpids++;
                            }
                         ProcessCaDescriptors = true;
                         break;
                         }
                      // fall through
              case 0x82: // STREAMTYPE_USER_PRIVATE
                      if (Setup.StandardCompliance == STANDARD_ANSISCTE) { // STANDARD SUBTITLE (ANSI/SCTE 27)
                         //TODO
                         break;
                         }
                      // fall through
              case 0x83 ... 0xFF: // STREAMTYPE_USER_PRIVATE
                      {
                      char lang[MAXLANGCODE1] = { 0 };
                      bool IsAc3 = false;
                      SI::Descriptor *d;
                      for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); ) {
                          switch (d->getDescriptorTag()) {
                            case SI::RegistrationDescriptorTag: {
                                 SI::RegistrationDescriptor *rd = (SI::RegistrationDescriptor *)d;
                                 // http://www.smpte-ra.org/mpegreg/mpegreg.html
                                 switch (rd->getFormatIdentifier()) {
                                   case 0x41432D33: // 'AC-3'
                                        IsAc3 = true;
                                        break;
                                   default:
                                        //printf("Format identifier: 0x%08X (pid: %d)\n", rd->getFormatIdentifier(), esPid);
                                        break;
                                   }
                                 }
                                 break;
                            case SI::ISO639LanguageDescriptorTag: {
                                 SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
                                 strn0cpy(lang, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
                                 }
                                 break;
                            default: ;
                            }
                         delete d;
                         }
                      if (IsAc3) {
                         if (NumDpids < MAXDPIDS) {
                            Dpids[NumDpids] = esPid;
                            Dtypes[NumDpids] = SI::AC3DescriptorTag;
                                strn0cpy(DLangs[NumDpids], lang, MAXLANGCODE1);
                            NumDpids++;
                            }
                         ProcessCaDescriptors = true;
                         }
                      }
                      break;
              default: ;//printf("PID: %5d %5d %2d %3d %3d\n", pmt.getServiceId(), stream.getPid(), stream.getStreamType(), pmt.getVersionNumber(), Channel->Number());
              }
            if (ProcessCaDescriptors) {
               for (SI::Loop::Iterator it; (d = (SI::CaDescriptor*)stream.streamDescriptors.getNext(it, SI::CaDescriptorTag)); ) {
                   CaDescriptors->AddCaDescriptor(d, esPid);
                   delete d;
                   }
               }
            }
#if VDRVERSNUM < 20301
           channel->SetPids(Vpid, Ppid, Vtype, Apids, Atypes, ALangs, Dpids, Dtypes, DLangs, Spids, SLangs, Tpid);
           channel->SetCaIds(CaDescriptors->CaIds());
           channel->SetSubtitlingDescriptors(SubtitlingTypes, CompositionPageIds, AncillaryPageIds);
           channel->SetCaDescriptors(CaDescriptorHandler.AddCaDescriptors(CaDescriptors));
#else
           ChannelsModified |= channel->SetPids(Vpid, Ppid, Vtype, Apids, Atypes, ALangs, Dpids, Dtypes, DLangs, Spids, SLangs, Tpid);
           ChannelsModified |= channel->SetCaIds(CaDescriptors->CaIds());
           ChannelsModified |= channel->SetSubtitlingDescriptors(SubtitlingTypes, CompositionPageIds, AncillaryPageIds);
           ChannelsModified |= channel->SetCaDescriptors(CaDescriptorHandler.AddCaDescriptors(CaDescriptors));
#endif
            num++;
            lastFound = time(NULL);

        }
#if VDRVERSNUM < 20301
     Channels.Unlock();
#else
     StateKey.Remove(ChannelsModified);
#endif
     }
  if (timer.TimedOut()) {
     if (pmtIndex >= 0)
        DBGPAT("PMT timeout %d", pmtIndex);
     SwitchToNextPmtPid();
     timer.Set(PMT_SCAN_TIMEOUT);
     }

    DBGPAT("PAT sdtfinished %d nosdt %d numpmt %d num %d\n",sdtfinished,noSDT,numPmtEntries,num);
    if ((sdtfinished || (noSDT && numPmtEntries>0)) && num >= numPmtEntries)
    {
        DBGPAT("PAT num %i sid %i  EOS \n", num, numPmtEntries);
        endofScan = true;
    }
}

void PatFilter::GetFoundNum(int &current, int &total)
{
    current = num;
    total = numPmtEntries;
    if (total > 1000 || total < 0)
        total = 0;
}

// --- cSdtFilter ------------------------------------------------------------

SdtFilter::SdtFilter(PatFilter * PatFilter)
{
    patFilter = PatFilter;

    numSid = 0;
    Rid = 0;
    numUsefulSid = 0;
    Set(0x11, 0x42);            // SDT
#ifdef REELVDR
    Set(0x1fff, 0x42);
#endif
    AddServiceType = ScanSetup.ServiceType;
}

void SdtFilter::SetStatus(bool On)
{
cMutexLock MutexLock(&mutex);
    cFilter::SetStatus(On);
    sectionSyncer.Reset();
}

void SdtFilter::Process(u_short Pid, u_char Tid, const u_char * Data, int Length)
{
    cMutexLock MutexLock(&mutex);
    const time_t tt = time(NULL);
    char *strDate;
    asprintf(&strDate, "%s", asctime(localtime(&tt)));
    DEBUG_printf("\nSdtFilter::Process IN %s\n", strDate);

    if (!(Source() && Transponder()))
        return;
    SI::SDT sdt(Data, false);
    if (!sdt.CheckCRCAndParse())
        return;

    if (!sectionSyncer.Sync(sdt.getVersionNumber(), sdt.getSectionNumber(), sdt.getLastSectionNumber()))
        return;
#if VDRVERSNUM < 20301
    if (!Channels.Lock(true, 10))
    {
#else
    bool ChannelsModified = false;
    cStateKey StateKey;
    cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 10);

    if (!Channels)
    {
        sectionSyncer.Repeat(); // let's not miss any section of the SDT
#endif
        return;
    }
    SI::SDT::Service SiSdtService;
    for (SI::Loop::Iterator it; sdt.serviceLoop.getNext(SiSdtService, it);)
    {
        cChannel *channel = NULL;
#if VDRVERSNUM < 20301
        if (cSource::IsType(Source(), 'I'))
        {
            for (cChannel *cChannel = Channels.First(); cChannel; cChannel = Channels.Next(cChannel))
            {
                if (!strcmp(cChannel->Parameters(), Channel()->Parameters()))
                {
                    channel = cChannel;
                    break;
                }
            }
        }
        else
            channel = Channels.GetByServiceID(Source(), Transponder(), SiSdtService.getServiceId());

        if (!channel)
            channel = Channels.GetByChannelID(tChannelID(Source(), 0, Transponder(), SiSdtService.getServiceId()));
#else
        if (cSource::IsType(Source(), 'I'))
        {
            for (cChannel *cChannel = Channels->First(); cChannel; cChannel = Channels->Next(cChannel))
            {
                if (!strcmp(cChannel->Parameters(), Channel()->Parameters()))
                {
                    channel = cChannel;
                    break;
                }
            }
        }
        else
            channel = Channels->GetByServiceID(Source(), Transponder(), SiSdtService.getServiceId());

        if (!channel)
            channel = Channels->GetByChannelID(tChannelID(Source(), 0, Transponder(), SiSdtService.getServiceId()));

        if (channel)
            channel->SetSeen();
#endif
        cLinkChannels *LinkChannels = NULL;
        SI::Descriptor * d;

        for (SI::Loop::Iterator it2; (d = SiSdtService.serviceDescriptors.getNext(it2));)
        {
            switch (d->getDescriptorTag())
            {
            case SI::ServiceDescriptorTag:
                {
                    SI::ServiceDescriptor * sd = (SI::ServiceDescriptor *) d;
                    char NameBufDeb[1024];
                    char ShortNameBufDeb[1024];

                    sd->serviceName.getText(NameBufDeb, ShortNameBufDeb, sizeof(NameBufDeb), sizeof(ShortNameBufDeb));

                    DEBUG_SDT(DBGSDT "Name %s --  ServiceType: %X: AddServiceType %d, Sid %i, running %i \n", NameBufDeb, sd->getServiceType(), AddServiceType, SiSdtService.getServiceId(), SiSdtService.getRunningStatus());

                    switch (sd->getServiceType())
                    {
                    case 0x01: // digital television service
                    case 0x02: // digital radio sound service
                    case 0x03: // DVB Subtitles
                    case 0x04: // NVOD reference service
                    case 0x05: // NVOD time-shifted service
                    case 0x11: // digital television service MPEG-2 HD  ??? never seen !
                    case 0x16:
                    case 0x19: // digital television service MPEG-4 HD
                    case 0xC3: // some french channels like kiosk
                    case 0x86: // ?? Astra 28.2 MPEG-4 HD

                        {
//                             esyslog("%s sd->getServiceType()=%d  AddServiceType=%d CAmode %d",__FUNCTION__, sd->getServiceType(), AddServiceType, SiSdtService.getFreeCaMode());
/*                            if(SiSdtService.getFreeCaMode() && (AddServiceType == ST_ALL_FTA_ONLY || AddServiceType == ST_TV_FTA_ONLY || AddServiceType == ST_HDTV_FTA_ONLY)) // (! FTA) break
                            {
                                DEBUG_SDT(DBGSDT "+++++++++++++++  NOT FTA CHANNEL: Skip %s +++++++++++++++ \n", NameBufDeb);
                                break;
                            }
*/
                            if (!(sd->getServiceType() == 0x11 || sd->getServiceType() == 0x19) && (AddServiceType == ST_HDTV_ONLY || AddServiceType == ST_HDTV_FTA_ONLY))  // (! HD TV && HDOnly) break
                            {
                                DEBUG_SDT(DBGSDT "+++++++++++++++  NO Found HD CHANNEL: Skip %s +++++++++++++++ \n", NameBufDeb);
                                break;
                            }
                            // Add only radio
                            if (sd->getServiceType() != 2 && AddServiceType == ST_RADIO_ONLY) // (TV && radioOnly) break
                            {
                                DEBUG_SDT(DBGSDT " Add nur Radio  aber nur TV Sender gefunden  SID skip %d \n", sd->getServiceType());
                                break;
                            }
                            // Add only tv
                            if (sd->getServiceType() == 2 && (AddServiceType == ST_TV_ONLY || AddServiceType == ST_HDTV_ONLY || AddServiceType == ST_TV_FTA_ONLY || AddServiceType == ST_HDTV_FTA_ONLY))  // RadioSender && (TVonly || HDTvonly) break
                            {
                                DEBUG_SDT(DBGSDT " Add nur TV  aber nur RadioSender gefunden  SID skip %d \n", sd->getServiceType());
                                break;
                            }
                            char NameBuf[1024];
                            char ShortNameBuf[1024];
                            char ProviderNameBuf[1024];
                            sd->serviceName.getText(NameBuf, ShortNameBuf, sizeof(NameBuf), sizeof(ShortNameBuf));
                            char *pn = compactspace(NameBuf);
                            char *ps = compactspace(ShortNameBuf);
                            //for stupid providers....
                            if (!strlen(pn))
                            {
                                const char *name = "NO NAME";
                                strcpy(NameBuf, name);
                            }
                            if (!*ps && cSource::IsCable(Source()))
                            {
                                // Some cable providers don't mark short channel names according to the
                                // standard, but rather go their own way and use "name>short name":
                                char *p = strchr(pn, '>'); // fix for UPC Wien
                                if (p && p > pn)
                                {
                                    *p++ = 0;
                                    strcpy(ShortNameBuf, skipspace(p));
                                }
                            }
                            // Avoid ',' in short name (would cause trouble in channels.conf):
                            for (char *p = ShortNameBuf; *p; p++)
                            {
                                if (*p == ',')
                                *p = '.';
                            }
                            sd->providerName.getText(ProviderNameBuf, sizeof(ProviderNameBuf));
                            char *pp = compactspace(ProviderNameBuf);
                            if (cSource::IsType(Source(), 'I')) pp = cMenuChannelscan::provider;

                            if (SiSdtService.getRunningStatus() > SI::RunningStatusNotRunning || SiSdtService.getRunningStatus() == SI::RunningStatusUndefined) // see DVB BlueBook A005 r5, section 4.3
                            {
                                mutexNames.Lock();
                                switch (sd->getServiceType())
                                {
                                case 0x1:
                                case 0x11:
                                case 0x16:
                                case 0x19:
                                case 0xC3:
                                case 0x86:
                                    tvChannelNames.push_back(NameBuf);  // if service wanted
                                    break;
                                case 0x2:
                                    radioChannelNames.push_back(NameBuf);   // if service wanted
                                    break;
                                default:;
                                    //dataChannelNames.push_back(NameBuf);
                                }
                                mutexNames.Unlock();
                                usefulSid[numSid] = 1;
                                numUsefulSid++;
                            }
                            else
                                usefulSid[numSid] = 0;

                            sid[numSid++] = SiSdtService.getServiceId();

                            if (channel)
                            {
                                DEBUG_SDT(DBGSDT "---------------------- Channelscan Add Chanel pn %s ps %s pp %s --------------\n", pn, ps, pp);
#if VDRVERSNUM < 20301
                                channel->SetId(sdt.getOriginalNetworkId(), sdt.getTransportStreamId(), SiSdtService.getServiceId(), channel->Rid());

                                //if (Setup.UpdateChannels >= 1)
                                channel->SetName(pn, ps, pp);
                                // Using SiSdtService.getFreeCaMode() is no good, because some
                                // tv stations set this flag even for non-encrypted channels :-(
                                // The special value 0xFFFF was supposed to mean "unknown encryption"
                                // and would have been overwritten with real CA values later:
                                // channel->SetCa(SiSdtService.getFreeCaMode() ? 0xFFFF : 0);
                            }
                            else if (*pn)
                            {
                                channel = Channels.NewChannel(Channel(), pn, ps, pp, sdt.getOriginalNetworkId(), sdt.getTransportStreamId(), SiSdtService.getServiceId(), Rid);
#else
                                ChannelsModified |= channel->SetId(Channels, sdt.getOriginalNetworkId(), sdt.getTransportStreamId(), SiSdtService.getServiceId(), channel->Rid());

                                //if (Setup.UpdateChannels >= 1)
                                ChannelsModified |= channel->SetName(pn, ps, pp);
                                // Using SiSdtService.getFreeCaMode() is no good, because some
                                // tv stations set this flag even for non-encrypted channels :-(
                                // The special value 0xFFFF was supposed to mean "unknown encryption"
                                // and would have been overwritten with real CA values later:
                                // channel->SetCa(SiSdtService.getFreeCaMode() ? 0xFFFF : 0);
                            }
                            else if (*pn)
                            {
                                channel = Channels->NewChannel(Channel(), pn, ps, pp, sdt.getOriginalNetworkId(), sdt.getTransportStreamId(), SiSdtService.getServiceId(), Rid);
#endif
                                patFilter->Trigger(SiSdtService.getServiceId());
                                if (SiSdtService.getServiceId() == 0x12)
                                {
                                    DEBUG_SDT(DBGSDT "-------- found ServiceID for PremiereDirekt!  %s - %s - %s --------- \n", pn, ps, pp);
                                    //eitFilter->Trigger();
                                }
                            }
                        }       // end case Digital TV services
                    }
                }
                break;
            case SI::NVODReferenceDescriptorTag:
                {
                    SI::NVODReferenceDescriptor * nrd = (SI::NVODReferenceDescriptor *) d;
                    SI::NVODReferenceDescriptor::Service Service;
                    for (SI::Loop::Iterator it; nrd->serviceLoop.getNext(Service, it);)
                    {
#if VDRVERSNUM < 20301
                        cChannel *link = Channels.GetByChannelID(tChannelID(Source(),
                                                                            Service.getOriginalNetworkId(),
                                                                            Service.getTransportStream(),
                                                                            Service.getServiceId()));

                        if (!link)
                        {
                            usefulSid[numSid] = 0;

                            sid[numSid++] = SiSdtService.getServiceId();
                            link = Channels.NewChannel(Channel(), "NVOD", "", "", Service.getOriginalNetworkId(), Service.getTransportStream(), Service.getServiceId());
#else
                        cChannel *link = Channels->GetByChannelID(tChannelID(Source(),
                                                                            Service.getOriginalNetworkId(),
                                                                            Service.getTransportStream(),
                                                                            Service.getServiceId()));

                        if (!link)
                        {
                            usefulSid[numSid] = 0;

                            sid[numSid++] = SiSdtService.getServiceId();
                            link = Channels->NewChannel(Channel(), "NVOD", "", "", Service.getOriginalNetworkId(), Service.getTransportStream(), Service.getServiceId());
#endif
                            patFilter->Trigger(SiSdtService.getServiceId());
                        }

                        if (link)
                        {
                            if (!LinkChannels)
                                LinkChannels = new cLinkChannels;
                            LinkChannels->Add(new cLinkChannel(link));
                        }
                    }
                }
                break;
            default:;
            }
            delete d;
        }
        if (LinkChannels)
        {
            if (channel)
#if VDRVERSNUM < 20301
                channel->SetLinkChannels(LinkChannels);
#else
                ChannelsModified |= channel->SetLinkChannels(LinkChannels);
#endif
            else
                delete LinkChannels;
        }
    }

#if VDRVERSNUM < 20301
     Channels.Unlock();
#else
     StateKey.Remove(ChannelsModified);
#endif
    if (sdt.getSectionNumber() == sdt.getLastSectionNumber())
    {
        patFilter->SdtFinished();
        SetStatus(false);
    }
    const time_t ttout = time(NULL);
    asprintf(&strDate, "%s", asctime(localtime(&ttout)));
    DEBUG_printf("\n\nSdtFilter::Process OUT :%4.1fsec: %s\n", (float)difftime(ttout, tt), strDate);
}

// --- cSdtMuxFilter ------------------------------------------------------------

SdtMuxFilter::SdtMuxFilter(PatFilter * PatFilter)
{
    patFilter = PatFilter;
//source = cSource::stNone;
    numMux = 0;
    Set(0x11, 0x46);            // SDT other mux
#ifdef REELVDR
    Set(0x1fff, 0x46);
#endif
}

void SdtMuxFilter::SetStatus(bool On)
{
cMutexLock MutexLock(&mutex);
    cFilter::SetStatus(On);
    sectionSyncer.Reset();
}

void SdtMuxFilter::Process(u_short Pid, u_char Tid, const u_char * Data, int Length)
{
    cMutexLock MutexLock(&mutex);
    const time_t tt = time(NULL);
    char *strDate;
    int a, found = 0;

    asprintf(&strDate, "%s", asctime(localtime(&tt)));
    DEBUG_printf("\nSdtMuxFilter::Process IN %s\n", strDate);

    if (!(Source() && Transponder()))
        return;
    SI::SDT sdt(Data, false);
    if (!sdt.CheckCRCAndParse())
        return;

    DEBUG_printf("found stream %x\n",sdt.getTransportStreamId());

    for (a = 0;a < numMux;a++)
    {
        if(Mux[a] == sdt.getTransportStreamId())
            found = 1;
    }
    if (!found) //add TransportStreamId to Mux[]
    {
        Mux[numMux] = sdt.getTransportStreamId();
        numMux++;
        DEBUG_printf("add mux %x num mux %d\n",sdt.getTransportStreamId(),numMux);
    }
    const time_t ttout = time(NULL);
    asprintf(&strDate, "%s", asctime(localtime(&ttout)));
    DEBUG_printf("\n\nSdtMuxFilter::Process OUT :%4.1fsec: %s\n", (float)difftime(ttout, tt), strDate);
}


// --- NitFilter  ---------------------------------------------------------
/* Nitscan not add channels.
 * Nitscan add new transponders.
 */

#ifdef DBG
# undef DBG
#endif
#define DBGNIT  " DEBUG [cs-nit]: "
//#define DEBUG_NIT(format, args...) printf (format, ## args)
#define DEBUG_NIT(format, args...)

#define MAXNETWORKNAME Utf8BufSize(256)

NitFilter::NitFilter()
{
    lastCount = 0;
    found_ = endofScan = false;
    Set(0x10, 0x40);            // NIT
#ifdef REELVDR
    Set(0x1fff, 0x42);          // decrease latency
#endif
    vector < int >tmp(64, 0);
    sectionSeen_ = tmp;
    mode = SAT;
    memset(t2plp,0,sizeof(t2plp));
}

void NitFilter::SetStatus(bool On)
{
    cFilter::SetStatus(true);
    sectionSyncer.Reset();
    lastCount = 0;
    found_ = endofScan = false;
    vector < int >tmp(64, 0);
    sectionSeen_ = tmp;
}

NitFilter::~NitFilter()
{
}

bool NitFilter::Found()
{
    return found_;
}

void NitFilter::Process(u_short Pid, u_char Tid, const u_char * Data, int Length)
{
  SI::NIT nit(Data, false);
  if (!nit.CheckCRCAndParse())
     return;
  if (!sectionSyncer.Sync(nit.getVersionNumber(), nit.getSectionNumber(), nit.getLastSectionNumber()))
     return;

    int getTransponderNum = 0;

    // return if we have seen the Table already
    int cnt = ++TblVersions[nit.getVersionNumber()];
    if (cnt > nit.getLastSectionNumber() + 1)
    {
        DEBUG_NIT(DBGNIT "DEBUG [nit]: ++  NIT Version %d found %d times \n", cnt, nit.getVersionNumber());
        endofScan = true;
        return;
    }
    found_ = endofScan = false;

// set 1 to debug
  if (0) {
     char NetworkName[MAXNETWORKNAME] = "";
     SI::Descriptor *d;
     for (SI::Loop::Iterator it; (d = nit.commonDescriptors.getNext(it)); ) {
         switch (d->getDescriptorTag()) {
           case SI::NetworkNameDescriptorTag: {
                SI::NetworkNameDescriptor *nnd = (SI::NetworkNameDescriptor *)d;
                nnd->name.getText(NetworkName, MAXNETWORKNAME);
                }
                break;
           default: ;
           }
         delete d;
         }
     DEBUG_NIT(DBGNIT ": %02X %2d %2d %2d %s %d %d '%s'\n", Tid, nit.getVersionNumber(), nit.getSectionNumber(), nit.getLastSectionNumber(), *cSource::ToString(Source()), nit.getNetworkId(), Transponder(), NetworkName);
     }

  sectionSeen_[nit.getSectionNumber()]++;
#if VDRVERSNUM >= 20301
  cStateKey StateKey;
  cChannels *Channels = cChannels::GetChannelsWrite(StateKey, 10);
  if (!Channels) {
     sectionSyncer.Repeat(); // let's not miss any section of the NIT
     return;
     }
  bool ChannelsModified = false;
#endif

  SI::NIT::TransportStream ts;
  for (SI::Loop::Iterator it; nit.transportStreamLoop.getNext(ts, it); ) {

      DEBUG_NIT(DBGNIT " -- found TS_ID %d\n",  ts.getTransportStreamId());

      SI::Descriptor *d;

      SI::Loop::Iterator it2;
      SI::FrequencyListDescriptor *fld = (SI::FrequencyListDescriptor *)ts.transportStreamDescriptors.getNext(it2, SI::FrequencyListDescriptorTag);
      int NumFrequencies = fld ? fld->frequencies.getCount() + 1 : 1;
      int Frequencies[NumFrequencies] = {0};
      if (fld) {
         int ct = fld->getCodingType();
         if (ct > 0) {
            int n = 1;
            for (SI::Loop::Iterator it3; fld->frequencies.hasNext(it3); ) {
                int f = fld->frequencies.getNext(it3);
                switch (ct) {
                  case 1: f = BCD2INT(f) / 100; break;
                  case 2: f = BCD2INT(f) / 10; break;
                  case 3: f = f * 10;  break;
                  default: ;
                  }
                Frequencies[n++] = f;
                DEBUG_NIT(DBGNIT "    Frequencies[%d] = %d\n", n - 1, f);
                }
            }
         else
            NumFrequencies = 1;
         }
      delete fld;

        int StreamId = 0;
        int System = DVB_SYSTEM_1;
        int T2SystemId = 0;
        int Bandwidth = 0;

      for (SI::Loop::Iterator it2; (d = ts.transportStreamDescriptors.getNext(it2)); ) {
          switch (d->getDescriptorTag()) {
            case SI::S2SatelliteDeliverySystemDescriptorTag: {
                    if (mode != SATS2 && mode != SAT) break;// to prevent incorrect nit
                    SI::S2SatelliteDeliverySystemDescriptor *sd = (SI::S2SatelliteDeliverySystemDescriptor *)d;
                    System = DVB_SYSTEM_2;
                    StreamId = sd->getInputStreamIdentifier();
                 }
                 break;
            case SI::SatelliteDeliverySystemDescriptorTag: {
                   if (mode != SATS2 && mode != SAT) break;// to prevent incorrect nit
                 SI::SatelliteDeliverySystemDescriptor *sd = (SI::SatelliteDeliverySystemDescriptor *)d;
                 int Source = cSource::FromData(cSource::stSat, BCD2INT(sd->getOrbitalPosition()), sd->getWestEastFlag());
                 int Frequency = Frequencies[0] = BCD2INT(sd->getFrequency()) / 100;
                 static char Polarizations[] = { 'H', 'V', 'L', 'R' };
                 char Polarization = Polarizations[sd->getPolarization()];
                 static int CodeRates[] = { FEC_NONE, FEC_1_2, FEC_2_3, FEC_3_4, FEC_5_6, FEC_7_8, FEC_8_9, FEC_3_5, FEC_4_5, FEC_9_10, FEC_AUTO, FEC_AUTO, FEC_AUTO, FEC_AUTO, FEC_AUTO, FEC_NONE };
                 int CoderateH = CodeRates[sd->getFecInner()];
                 static int Modulations[] = { QAM_AUTO, QPSK, PSK_8, QAM_16 };
                 int Modulation = Modulations[sd->getModulationType()];
                 if (System == DVB_SYSTEM_1 && sd->getModulationSystem()) System = DVB_SYSTEM_2;
                 static int RollOffs[] = { ROLLOFF_35, ROLLOFF_25, ROLLOFF_20, ROLLOFF_AUTO };
                 int RollOff = sd->getModulationSystem() ? RollOffs[sd->getRollOff()] : ROLLOFF_AUTO;
                 int SymbolRate = BCD2INT(sd->getSymbolRate()) / 10;
                 getTransponderNum++;
                 found_=true;
                    for (int n = 0; n < NumFrequencies; n++)
                    {
                        if(Frequencies[n] == 0)continue;
                        cSatTransponder *t = new cSatTransponder(Frequencies[n], Polarization,
                                                        SymbolRate, Modulation, CoderateH, RollOff, System, StreamId);


                        if (!t)
                        {
                            esyslog("FatalError new cSatTransponder %d failed\n", Frequencies[n]);
                        }
                        else
                        {
                            mapRet ret = transponderMap.insert(make_pair(Frequencies[n], t));
                            if (ret.second)
                                esyslog(" New transponder n %d f: %d  p: %c sr: %d (mod_si: %d  mod: %d, ro %d )    \n", n, Frequencies[n],
                                                      Polarization, SymbolRate, sd->getModulationType(), Modulation, RollOff);
                            else
                                delete t;
                        }
                    }
                 }
                 break;
            case SI::CableDeliverySystemDescriptorTag: {
                 if (mode != CABLE) break; // to prevent incorrect nit
                 SI::CableDeliverySystemDescriptor *sd = (SI::CableDeliverySystemDescriptor *)d;
                 int Source = cSource::FromData(cSource::stCable);
                 int Frequency = Frequencies[0] = BCD2INT(sd->getFrequency()) / 10;
                 static int Modulations[] = { QPSK, QAM_16, QAM_32, QAM_64, QAM_128, QAM_256, QAM_AUTO };
                 int Modulation = Modulations[min(sd->getModulation(), 6)];
                 int SymbolRate = BCD2INT(sd->getSymbolRate()) / 10;
                 getTransponderNum++;
                 found_=true;
                    for (int n = 0; n < NumFrequencies; n++)
                    {
                        if(Frequencies[n] == 0)continue;
                        cCableTransponder *t = new cCableTransponder(0, Frequencies[n], SymbolRate, Modulation);
                        if (!t)
                        {
                            esyslog("FatalError new cCableTransponder %d failed\n", Frequencies[n]);
                        }
                        else
                        {
                            mapRet ret = transponderMap.insert(make_pair(Frequencies[n], t));
                            if (ret.second)
                                esyslog(" New transponder f: %d \n", Frequencies[n]);
                            else
                                delete t;
                        }
                    }
                }
                break;
            case SI::ExtensionDescriptorTag: {
                 SI::ExtensionDescriptor *sd = (SI::ExtensionDescriptor *)d;
                 switch (sd->getExtensionDescriptorTag()) {
                   case SI::T2DeliverySystemDescriptorTag: {
                            if (mode != TERR && mode != TERR2) break; // to prevent incorrect nit
                            SI::T2DeliverySystemDescriptor *td = (SI::T2DeliverySystemDescriptor *)d;
                            System = DVB_SYSTEM_2;
                            if (Frequencies[0] == 0 && NumFrequencies == 1) //no frequency table, mean plpid scan on current transponder
                                t2plp[td->getPlpId()] = 1;
                                StreamId = td->getPlpId();
                                T2SystemId = td->getT2SystemId();
                                if (td->getExtendedDataFlag()) {
                                    static int T2Bandwidths[] = { 8000000, 7000000, 6000000, 5000000, 10000000, 1712000, 0, 0 };
                                    Bandwidth = T2Bandwidths[td->getBandwidth()];
                                }
                                getTransponderNum++;
                                found_=true;
                                for (int n = 0; n < NumFrequencies; n++)
                                {
                                    if(Frequencies[n] == 0)continue;
                                    cTerrTransponder *t = new cTerrTransponder(0, Frequencies[n], Bandwidth, System, StreamId);
                                    if (!t)
                                    {
                                        esyslog("FatalError new cTerrTransponder %d failed\n", Frequencies[n]);
                                    }
                                    else
                                    {
                                        mapRet ret = transponderMap.insert(make_pair(Frequencies[n], t));
                                        if (ret.second)
                                            esyslog(" New transponder f: %d plpid %d\n", Frequencies[n], StreamId);
                                        else
                                            delete t;
                                    }
                                }

                        }
                        break;
                   default: ;
                   }
                 }
                 break;
            case SI::T2DeliverySystemDescriptorTag: { //incorrect t2deliverysystem tag, use in some regions of Russia for plpid
                 if (mode != TERR && mode != TERR2) break; // to prevent incorrect nit
                 SI::CharArray a = d->getData();
                 System = DVB_SYSTEM_2;
                 t2plp[a[3]] = 1;
                 DEBUG_NIT(DBGNIT "found t2 plpid %d %d\n",a[3],t2plp[a[3]]);
                 found_=true;
                 }
                 break;
            case SI::TerrestrialDeliverySystemDescriptorTag: {
                 if (mode != TERR && mode != TERR2) break; // to prevent incorrect nit
                 SI::TerrestrialDeliverySystemDescriptor *sd = (SI::TerrestrialDeliverySystemDescriptor *)d;

                 int Source = cSource::FromData(cSource::stTerr);
                 int Frequency = Frequencies[0] = sd->getFrequency() * 10;
                 static int Bandwidths[] = { 8000000, 7000000, 6000000, 5000000, 0, 0, 0, 0 };
                 if (System == DVB_SYSTEM_1) Bandwidth = Bandwidths[sd->getBandwidth()];
                   getTransponderNum++;
                   found_=true;
                    for (int n = 0; n < NumFrequencies; n++)
                    {
                        if(Frequencies[n] == 0)continue;
                        cTerrTransponder *t = new cTerrTransponder(0, Frequencies[n], Bandwidth, System, StreamId);
                        if (!t)
                        {
                            esyslog("FatalError new cTerrTransponder %d failed\n", Frequencies[n]);
                        }
                        else
                        {
                            mapRet ret = transponderMap.insert(make_pair(Frequencies[n], t));
                            if (ret.second)
                                esyslog(" New transponder f: %d \n", Frequencies[n]);
                            else
                                delete t;
                        }
                    }
                }
                break;
/* Is this need for channelscan ?? */
#if VDRVERSNUM >= 20108
            case SI::LogicalChannelDescriptorTag:
                 if (Setup.StandardCompliance == STANDARD_NORDIG) {
                    SI::LogicalChannelDescriptor *lcd = (SI::LogicalChannelDescriptor *)d;
                    SI::LogicalChannelDescriptor::LogicalChannel LogicalChannel;
                    for (SI::Loop::Iterator it4; lcd->logicalChannelLoop.getNext(LogicalChannel, it4); ) {
                        if (LogicalChannel.getVisibleServiceFlag()) {
                           int lcn = LogicalChannel.getLogicalChannelNumber();
                           int sid = LogicalChannel.getServiceId();
#if VDRVERSNUM >= 20301
                           for (cChannel *Channel = Channels->First(); Channel; Channel = Channels->Next(Channel)) {
                               if (!Channel->GroupSep() && Channel->Transponder() == Transponder() && Channel->Nid() == ts.getOriginalNetworkId() && Channel->Tid() == ts.getTransportStreamId()) {
                                  ChannelsModified |= Channel->SetLcn(lcn);
#else
                           for (cChannel *Channel = Channels.First(); Channel; Channel = Channels.Next(Channel)) {
                               if (!Channel->GroupSep() && Channel->Transponder() == Transponder() && Channel->Nid() == ts.getOriginalNetworkId() && Channel->Tid() == ts.getTransportStreamId()) {
                                  Channel->SetLcn(lcn);
#endif
                                  break;
                                  }
                               }
                           }
                        }
                    }
                 break;
            case SI::HdSimulcastLogicalChannelDescriptorTag:
                 if (Setup.StandardCompliance == STANDARD_NORDIG) {
                    SI::HdSimulcastLogicalChannelDescriptor *lcd = (SI::HdSimulcastLogicalChannelDescriptor *)d;
                    SI::HdSimulcastLogicalChannelDescriptor::HdSimulcastLogicalChannel HdSimulcastLogicalChannel;
                    for (SI::Loop::Iterator it4; lcd->hdSimulcastLogicalChannelLoop.getNext(HdSimulcastLogicalChannel, it4); ) {
                        if (HdSimulcastLogicalChannel.getVisibleServiceFlag()) {
                           int lcn = HdSimulcastLogicalChannel.getLogicalChannelNumber();
                           int sid = HdSimulcastLogicalChannel.getServiceId();
#if VDRVERSNUM >= 20301
                           for (cChannel *Channel = Channels->First(); Channel; Channel = Channels->Next(Channel)) {
                               if (!Channel->GroupSep() && Channel->Transponder() == Transponder() && Channel->Nid() == ts.getOriginalNetworkId() && Channel->Tid() == ts.getTransportStreamId()) {
                                  ChannelsModified |= Channel->SetLcn(lcn);
#else
                           for (cChannel *Channel = Channels.First(); Channel; Channel = Channels.Next(Channel)) {
                               if (!Channel->GroupSep() && Channel->Transponder() == Transponder() && Channel->Nid() == ts.getOriginalNetworkId() && Channel->Tid() == ts.getTransportStreamId()) {
                                  Channel->SetLcn(lcn);
#endif
                                  break;
                                  }
                               }
                           }
                        }
                    }
                 break;
#endif
            default: ;
            }
          delete d;
          }
      }
#if VDRVERSNUM >= 20301
    StateKey.Remove(ChannelsModified);
#endif

    DEBUG_NIT(DBGNIT " ++  End of ProcessLoop MapSize: %d  lastCount %d   \n", (int)transponderMap.size(), lastCount);
    DEBUG_NIT(DBGNIT " -- moreThanOneSection %d\n", nit.moreThanOneSection());

    if (!nit.moreThanOneSection())
    {
        endofScan = true;
    }
    else
    {
        endofScan = true;
        DEBUG_NIT(DBGNIT "DEBUG [nit]:  -- LastSectionNumber  %d\n", nit.getLastSectionNumber());

        //for (int i = 0; i<sectionSeen.size();i++)
        for (int i = 0; i < nit.getLastSectionNumber() + 1; i++)
        {
            DEBUG_NIT(DBGNIT "DEBUG [nit]:  -- Seen[%d] %s\n", i, sectionSeen_[i] ? "YES" : "NO");

            if (sectionSeen_[i] == 0)
            {
                endofScan = false;
                break;
            }
        }
    }

    if (endofScan == true)
    {
        DEBUG_NIT (DBGNIT "DEBUG [channescan ]: filter.c  End of ProcessLoop newMap size: %d  \n", (int)transponderMap.size());
        vector < int >tmp(64, 0);
        sectionSeen_ = tmp;
    }

    lastCount = transponderMap.size();
    DEBUG_NIT(DBGNIT "DEBUG [channescan ]: set endofScan %s \n", endofScan ? "TRUE" : "FALSE");
}
