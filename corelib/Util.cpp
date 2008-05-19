/*
* Copyright (c) 2003-2008 Rony Shapiro <ronys@users.sourceforge.net>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/
/// \file Util.cpp
//-----------------------------------------------------------------------------

#include "sha1.h"
#include "BlowFish.h"
#include "PWSrand.h"
#include "PwsPlatform.h"
#include "corelib.h"
#include "os/pws_tchar.h"
#include "os/dir.h"

#include <stdio.h>
#include <sys/timeb.h>
#include <time.h>
#include "Util.h"

// used by CBC routines...
static void xormem(unsigned char* mem1, const unsigned char* mem2, int length)
{
  for (int x = 0; x < length; x++)
    mem1[x] ^= mem2[x];
}

//-----------------------------------------------------------------------------
//Overwrite the memory
// used to be a loop here, but this was deemed (1) overly paranoid 
// (2) The wrong way to scrub DRAM memory 
// see http://www.cs.auckland.ac.nz/~pgut001/pubs/secure_del.html 
// and http://www.cypherpunks.to/~peter/usenix01.pdf 

#ifdef _WIN32
#pragma optimize("",off)
#endif
void trashMemory(void* buffer, size_t length)
{
  ASSERT(buffer != NULL);
  // {kjp} no point in looping around doing nothing is there?
  if (length > 0) {
    memset(buffer, 0x55, length);
    memset(buffer, 0xAA, length);
    memset(buffer, 0x00, length);
  }
}
#ifdef _WIN32
#pragma optimize("",on)
#endif
void trashMemory(LPTSTR buffer, size_t length)
{
  trashMemory((unsigned char *) buffer, length * sizeof(buffer[0]));
}

void trashMemory(CString &cs_buffer)
{
#ifdef _WIN32
  TCHAR *lpszString = cs_buffer.GetBuffer(cs_buffer.GetLength());
  trashMemory( (void *) lpszString, cs_buffer.GetLength() * sizeof(lpszString[0]));
  cs_buffer.ReleaseBuffer();
#else
  ASSERT(0); // XXX notyet
#endif
}

/**
Burn some stack memory
@param len amount of stack to burn in bytes
*/
void burnStack(unsigned long len)
{
  unsigned char buf[32];
  trashMemory(buf, sizeof(buf));
  if (len > (unsigned long)sizeof(buf))
    burnStack(len - sizeof(buf));
}

void ConvertString(const CMyString &text,
                   unsigned char *&txt,
                   int &txtlen)
{
  LPCTSTR txtstr = LPCTSTR(text); 
  txtlen = text.GetLength();

#ifndef UNICODE
  txt = (unsigned char *)txtstr; // don't delete[] (ugh)!!!
#else
#ifdef _WIN32
  txt = new unsigned char[2*txtlen]; // safe upper limit
  int len = WideCharToMultiByte(CP_ACP, 0, txtstr, txtlen,
    LPSTR(txt), 2*txtlen, NULL, NULL);
  ASSERT(len != 0);
#else
  mbstate_t mbs;
  size_t len = wcsrtombs(NULL, &txtstr, 0, &mbs);
  txt = new unsigned char[len+1];
  len = wcsrtombs((char *)txt, &txtstr, len, &mbs);
  ASSERT(len != (size_t)-1);
#endif
  txtlen = len;
  txt[len] = '\0';
#endif /* UNICODE */
}

//Generates a passkey-based hash from stuff - used to validate the passkey
void GenRandhash(const CMyString &a_passkey,
                 const unsigned char* a_randstuff,
                 unsigned char* a_randhash)
{
  int pkeyLen = 0;
  unsigned char *pstr = NULL;

  ConvertString(a_passkey, pstr, pkeyLen);

  /*
  tempSalt <- H(a_randstuff + a_passkey)
  */
  SHA1 keyHash;
  keyHash.Update(a_randstuff, StuffSize);
  keyHash.Update(pstr, pkeyLen);

#ifdef UNICODE
  trashMemory(pstr, pkeyLen);
  delete[] pstr;
#endif

  unsigned char tempSalt[20]; // HashSize
  keyHash.Final(tempSalt);

  /*
  tempbuf <- a_randstuff encrypted 1000 times using tempSalt as key?
  */

  BlowFish Cipher(tempSalt, sizeof(tempSalt));

  unsigned char tempbuf[StuffSize];
  memcpy((char*)tempbuf, (char*)a_randstuff, StuffSize);

  for (int x=0; x<1000; x++)
    Cipher.Encrypt(tempbuf, tempbuf);

  /*
  hmm - seems we're not done with this context
  we throw the tempbuf into the hasher, and extract a_randhash
  */
  keyHash.Update(tempbuf, StuffSize);
  keyHash.Final(a_randhash);
}

size_t _writecbc(FILE *fp, const unsigned char* buffer, int length, unsigned char type,
                 Fish *Algorithm, unsigned char* cbcbuffer)
{
  const unsigned int BS = Algorithm->GetBlockSize();
  size_t numWritten = 0;

  // some trickery to avoid new/delete
  unsigned char block1[16];

  unsigned char *curblock = NULL;
  ASSERT(BS <= sizeof(block1)); // if needed we can be more sophisticated here...

  // First encrypt and write the length of the buffer
  curblock = block1;
  // Fill unused bytes of length with random data, to make
  // a dictionary attack harder
  PWSrand::GetInstance()->GetRandomData(curblock, BS);
  // block length overwrites 4 bytes of the above randomness.
  putInt32(curblock, length);

  // following new for format 2.0 - lengthblock bytes 4-7 were unused before.
  curblock[sizeof(length)] = type;

  if (BS == 16) {
    // In this case, we've too many (11) wasted bytes in the length block
    // So we store actual data there:
    // (11 = BlockSize - 4 (length) - 1 (type)
    const int len1 = (length > 11) ? 11 : length;
    memcpy(curblock+5, buffer, len1);
    length -= len1;
    buffer += len1;
  }

  xormem(curblock, cbcbuffer, BS); // do the CBC thing
  Algorithm->Encrypt(curblock, curblock);
  memcpy(cbcbuffer, curblock, BS); // update CBC for next round

  numWritten = fwrite(curblock, 1, BS, fp);

  if (length > 0 ||
      (BS == 8 && length == 0)) { // This part for bwd compat w/pre-3 format
    unsigned int BlockLength = ((length+(BS-1))/BS)*BS;
    if (BlockLength == 0 && BS == 8)
      BlockLength = BS;

    // Now, encrypt and write the (rest of the) buffer
    for (unsigned int x=0; x<BlockLength; x+=BS) {
      if ((length == 0) || ((length%BS != 0) && (length-x<BS))) {
        //This is for an uneven last block
        PWSrand::GetInstance()->GetRandomData(curblock, BS);
        memcpy(curblock, buffer+x, length % BS);
      } else
        memcpy(curblock, buffer+x, BS);
      xormem(curblock, cbcbuffer, BS);
      Algorithm->Encrypt(curblock, curblock);
      memcpy(cbcbuffer, curblock, BS);
      numWritten += fwrite(curblock, 1, BS, fp);
    }
  }
  trashMemory(curblock, BS);
  return numWritten;
}

/*
* Reads an encrypted record into buffer.
* The first block of the record contains the encrypted record length
* We have the usual ugly problem of fixed buffer lengths in C/C++.
* allocate the buffer here, to ensure that it's long enough.
* *** THE CALLER MUST delete[] IT AFTER USE *** UGH++
*
* (unless buffer_len is zero)
*
* Note that the buffer is a byte array, and buffer_len is number of
* bytes. This means that any data can be passed, and we don't
* care at this level if strings are char or wchar_t.
*
* If TERMINAL_BLOCK is non-NULL, the first block read is tested against it,
* and -1 is returned if it matches. (used in V3)
*/
size_t _readcbc(FILE *fp,
         unsigned char* &buffer, unsigned int &buffer_len, unsigned char &type,
         Fish *Algorithm, unsigned char* cbcbuffer,
         const unsigned char *TERMINAL_BLOCK)
{
  const unsigned int BS = Algorithm->GetBlockSize();
  size_t numRead = 0;

  // some trickery to avoid new/delete
	// Initialize memory.  (Lockheed Martin) Secure Coding  11-14-2007
  unsigned char block1[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned char block2[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned char block3[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned char *lengthblock = NULL;

  ASSERT(BS <= sizeof(block1)); // if needed we can be more sophisticated here...
  
	// Safety check.  (Lockheed Martin) Secure Coding  11-14-2007
  if ((BS > sizeof( block1 )) || (BS == 0))
	  return 0;

  lengthblock = block1;

  buffer_len = 0;
  numRead = fread(lengthblock, 1, BS, fp);
  if (numRead != BS) {
    return 0;
  }

  if (TERMINAL_BLOCK != NULL &&
    memcmp(lengthblock, TERMINAL_BLOCK, BS) == 0)
    return static_cast<size_t>(-1);

  unsigned char *lcpy = block2;
  memcpy(lcpy, lengthblock, BS);

  Algorithm->Decrypt(lengthblock, lengthblock);
  xormem(lengthblock, cbcbuffer, BS);
  memcpy(cbcbuffer, lcpy, BS);

  int length = getInt32(lengthblock);

  // new for 2.0 -- lengthblock[4..7] previously set to zero
  type = lengthblock[sizeof(int)]; // type is first byte after the length

  if (length < 0) { // sanity check
    TRACE("_readcbc: Read negative length - aborting\n");
    buffer = NULL;
    buffer_len = 0;
    trashMemory(lengthblock, BS);
    return 0;
  }

  buffer_len = length;
  buffer = new unsigned char[(length/BS)*BS +2*BS]; // round upwards
  unsigned char *b = buffer;

  // Initialize memory.  (Lockheed Martin) Secure Coding  11-14-2007
  memset(b, 0, (length/BS)*BS +2*BS);

  if (BS == 16) {
    // length block contains up to 11 (= 16 - 4 - 1) bytes
    // of data
    const int len1 = (length > 11) ? 11 : length;
    memcpy(b, lengthblock+5, len1);
    length -= len1;
    b += len1;
  }

  unsigned int BlockLength = ((length+(BS-1))/BS)*BS;
  // Following is meant for lengths < BS,
  // but results in a block being read even
  // if length is zero. This is wasteful,
  // but fixing it would break all existing pre-3.0 databases.
  if (BlockLength == 0 && BS == 8)
    BlockLength = BS;

  trashMemory(lengthblock, BS);

  if (length > 0 ||
      (BS == 8 && length == 0)) { // pre-3 pain
    unsigned char *tempcbc = block3;
    numRead += fread(b, 1, BlockLength, fp);
    for (unsigned int x=0; x<BlockLength; x+=BS) {
      memcpy(tempcbc, b + x, BS);
      Algorithm->Decrypt(b + x, b + x);
      xormem(b + x, cbcbuffer, BS);
      memcpy(cbcbuffer, tempcbc, BS);
    }
  }

  if (buffer_len == 0) {
    // delete[] buffer here since caller will see zero length
    delete[] buffer;
  }
  return numRead;
}

// PWSUtil implementations

void PWSUtil::strCopy(LPTSTR target, size_t tcount, const LPCTSTR source, size_t scount)
{
#if (_MSC_VER >= 1400)
  (void) _tcsncpy_s(target, tcount, source, scount);
#else
  tcount = 0; // shut up warning;
  (void)_tcsncpy(target, source, scount);
#endif
}

size_t PWSUtil::strLength(const LPCTSTR str)
{
  return _tcslen(str);
}

/**
* Returns the current length of a file.
*/
long PWSUtil::fileLength(FILE *fp)
{
  long pos;
  long len;

  pos = ftell( fp );
  fseek( fp, 0, SEEK_END );
  len = ftell( fp );
  fseek( fp, pos, SEEK_SET );

  return len;
}

const TCHAR *PWSUtil::UNKNOWN_XML_TIME_STR = _T("1970-01-01 00:00:00");
const TCHAR *PWSUtil::UNKNOWN_ASC_TIME_STR = _T("Unknown");

CMyString PWSUtil::ConvertToDateTimeString(const time_t &t, const int result_format)
{
  CMyString ret;
  if (t != 0) {
    TCHAR time_str[80], datetime_str[80];
#if _MSC_VER >= 1400
    struct tm st;
    errno_t err;
    err = localtime_s(&st, &t);  // secure version
    ASSERT(err == 0);
    if ((result_format & TMC_EXPORT_IMPORT) == TMC_EXPORT_IMPORT)
      _stprintf_s(datetime_str, 20, _T("%04d/%02d/%02d %02d:%02d:%02d"),
      st.tm_year+1900, st.tm_mon+1, st.tm_mday, st.tm_hour,
      st.tm_min, st.tm_sec);
    else if ((result_format & TMC_XML) == TMC_XML)
      _stprintf_s(datetime_str, 20, _T("%04d-%02d-%02dT%02d:%02d:%02d"),
      st.tm_year+1900, st.tm_mon+1, st.tm_mday, st.tm_hour,
      st.tm_min, st.tm_sec);
    else if ((result_format & TMC_LOCALE) == TMC_LOCALE) {
      SYSTEMTIME systime;
      systime.wYear = (WORD)st.tm_year+1900;
      systime.wMonth = (WORD)st.tm_mon+1;
      systime.wDay = (WORD)st.tm_mday;
      systime.wDayOfWeek = (WORD) st.tm_wday;
      systime.wHour = (WORD)st.tm_hour;
      systime.wMinute = (WORD)st.tm_min;
      systime.wSecond = (WORD)st.tm_sec;
      systime.wMilliseconds = (WORD)0;
      TCHAR szBuf[80];
      VERIFY(::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, szBuf, 80));
      GetDateFormat(LOCALE_USER_DEFAULT, 0, &systime, szBuf, datetime_str, 80);
      szBuf[0] = _T(' ');  // Put a blank between date and time
      VERIFY(::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STIMEFORMAT, &szBuf[1], 79));
      GetTimeFormat(LOCALE_USER_DEFAULT, 0, &systime, szBuf, time_str, 80);
      _tcscat_s(datetime_str, 80, time_str);
    } else {
      err = _tasctime_s(datetime_str, 32, &st);  // secure version
      ASSERT(err == 0);
    }
    ret = datetime_str;
#else
    TCHAR *t_str_ptr;
    struct tm *st;
    st = localtime(&t);
    ASSERT(st != NULL); // null means invalid time
    if ((result_format & TMC_EXPORT_IMPORT) == TMC_EXPORT_IMPORT) {
      _stprintf(datetime_str, _T("%04d/%02d/%02d %02d:%02d:%02d"),
                st->tm_year+1900, st->tm_mon+1, st->tm_mday,
                st->tm_hour, st->tm_min, st->tm_sec);
      t_str_ptr = datetime_str;
    } else if ((result_format & TMC_XML) == TMC_XML) {
      _stprintf(time_str, _T("%04d-%02d-%02dT%02d:%02d:%02d"),
                st->tm_year+1900, st->tm_mon+1, st->tm_mday,
                st->tm_hour, st->tm_min, st->tm_sec);
      t_str_ptr = datetime_str;
    } else if ((result_format & TMC_LOCALE) == TMC_LOCALE) {
      SYSTEMTIME systime;
      systime.wYear = (WORD)st->tm_year+1900;
      systime.wMonth = (WORD)st->tm_mon+1;
      systime.wDay = (WORD)st->tm_mday;
      systime.wDayOfWeek = (WORD) st->tm_wday;
      systime.wHour = (WORD)st->tm_hour;
      systime.wMinute = (WORD)st->tm_min;
      systime.wSecond = (WORD)st->tm_sec;
      systime.wMilliseconds = (WORD)0;
      TCHAR szBuf[80];
      VERIFY(::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, szBuf, 80));
      GetDateFormat(LOCALE_USER_DEFAULT, 0, &systime, szBuf, datetime_str, 80);
      szBuf[0] = _T(' ');  // Put a blank between date and time
      VERIFY(::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STIMEFORMAT, &szBuf[1], 79));
      GetTimeFormat(LOCALE_USER_DEFAULT, 0, &systime, szBuf, time_str, 80);
      _tcscat(datetime_str, time_str);
      t_str_ptr = datetime_str;
    } else
      t_str_ptr = _tasctime(st);

    ret = t_str_ptr;
#endif
  } else {
    switch (result_format) {
      case TMC_ASC_UNKNOWN:
        ret = UNKNOWN_ASC_TIME_STR;
        break;
      case TMC_XML:
        ret = UNKNOWN_XML_TIME_STR;
        break;
      default:
        ret = _T("");
    }
  }
  // remove the trailing EOL char.
  ret.TrimRight();
  return ret;
}

CMyString PWSUtil::GetNewFileName(const CMyString &oldfilename, const CString &newExtn)
{
  stringT inpath(oldfilename);
  stringT drive, dir, fname, ext;
  stringT outpath;

  if (pws_os::splitpath(inpath, drive, dir, fname, ext)) {
    ext = newExtn;
    outpath = pws_os::makepath(drive, dir, fname, ext);
  } else
    ASSERT(0);
  return CMyString(outpath.c_str());
}

void PWSUtil::IssueError(const CString &csFunction)
{
#ifdef _DEBUG
  LPVOID lpMsgBuf;
  LPVOID lpDisplayBuf;

  const DWORD dw = GetLastError();

  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                NULL,
                dw,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR) &lpMsgBuf,
                0, NULL);

  lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
                                    (lstrlen((LPCTSTR)lpMsgBuf) + csFunction.GetLength() + 40) * sizeof(TCHAR)); 
  wsprintf((LPTSTR)lpDisplayBuf, TEXT("%s failed with error %d: %s"), 
                                      csFunction, dw, lpMsgBuf); 
  MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK); 

  LocalFree(lpMsgBuf);
  LocalFree(lpDisplayBuf);
#else
  csFunction;
#endif
}

CString PWSUtil::GetTimeStamp()
{
  struct _timeb timebuffer;
#if (_MSC_VER >= 1400)
  _ftime_s(&timebuffer);
#else
  _ftime(&timebuffer);
#endif
  CMyString cmys_now = ConvertToDateTimeString(timebuffer.time, TMC_EXPORT_IMPORT);

  CString cs_now;
  cs_now.Format(_T("%s.%03hu"), cmys_now, timebuffer.millitm);

  return cs_now;
}

/*
  Produce a printable version of memory dump (hex + ascii)

  paramaters:
    memory  - pointer to memory to format
    length  - length to format
    maxnum  - maximum characters dumped per line

  return:
    CString containing output buffer
*/
void PWSUtil::HexDump(unsigned char *pmemory, const int length, 
                      const CString cs_prefix, const int maxnum)
{
#ifdef _DEBUG
  unsigned char *pmem;
  CString cs_outbuff, cs_hexbuff, cs_charbuff;
  int i, j, len(length);
  unsigned char c;

  pmem = pmemory;
  while (len > 0) {
    // Show offset for this line.
    cs_charbuff.Empty();
    cs_hexbuff.Empty();
    cs_outbuff.Format(_T("%s: %08x *"), cs_prefix, pmem);

    // Format hex portion of line and save chars for ascii portion
    if (len > maxnum)
      j = maxnum;
    else
      j = len;

    for (i = 0; i < j; i++) {
      c = *pmem++;

      if ((i % 4) == 0 && i != 0)
        cs_outbuff += _T(' ');

      cs_hexbuff.Format(_T("%02x"), c);
      cs_outbuff += cs_hexbuff;

      if (c >= 32 && c < 127)
        cs_charbuff += (TCHAR)c;
      else
        cs_charbuff += _T('.');
    }

    j = maxnum - j;

    // Fill out hex portion of short lines.
    for (i = j; i > 0; i--) {
      if ((i % 4) != 0)
        cs_outbuff += _T("  ");
      else
        cs_outbuff += _T("   ");
    }

    // Add ASCII character portion to line.
    cs_outbuff += _T("* |");
    cs_outbuff += cs_charbuff;

    // Fill out end of short lines.
    for (i = j; i > 0; i--)
      cs_outbuff += _T(' ');

    cs_outbuff += _T('|');

    // Next line
    len -= maxnum;

    TRACE(_T("%s\n"), cs_outbuff);
  };
#else
  pmemory; length; cs_prefix; maxnum;
#endif
}

CString PWSUtil::Base64Encode(const BYTE *strIn, size_t len)
{
  CString cs_Out;
  static const CHAR base64ABC[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  cs_Out.Empty();
  for (DWORD i = 0; i < (DWORD)len; i += 3) {
    LONG l = ( ((LONG)strIn[i]) << 16 ) | 
               (((i + 1) < len) ? (((LONG)strIn[i + 1]) << 8) : 0) | 
               (((i + 2) < len) ? ((LONG)strIn[i + 2]) : 0);

    cs_Out += base64ABC[(l >> 18) & 0x3F];
    cs_Out += base64ABC[(l >> 12) & 0x3F];
    if (i + 1 < len) cs_Out += base64ABC[(l >> 6) & 0x3F];
    if (i + 2 < len) cs_Out += base64ABC[(l ) & 0x3F];
  }

  switch (len % 3) {
    case 1:
      cs_Out += '=';
    case 2:
      cs_Out += '=';
  } 

  return cs_Out;
}

void PWSUtil::Base64Decode(const LPCTSTR sz_inString, BYTE* &outData, size_t &out_len)
{
  static const char szCS[]=
    "=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  int iDigits[4] = {0,0,0,0};

  CString cs_inString(sz_inString);

  size_t st_length = 0;
  const int in_length = cs_inString.GetLength();

  int i1, i2, i3;
  for (i2 = 0; i2 < (int)in_length; i2 += 4) {
    iDigits[0] = iDigits[1] = iDigits[2] = iDigits[3] = -1;

    for (i1 = 0; i1 < sizeof(szCS) - 1; i1++) {
      for (i3 = i2; i3 < i2 + 4; i3++) {
        if (i3 < (int)in_length &&  cs_inString[i3] == szCS[i1])
          iDigits[i3 - i2] = i1 - 1;
      }
    }

    outData[st_length] = ((BYTE)iDigits[0] << 2);

    if (iDigits[1] >= 0) {
      outData[st_length] += ((BYTE)iDigits[1] >> 4) & 0x3;
    }

    st_length++;

    if (iDigits[2] >= 0) {
      outData[st_length++] = (((BYTE)iDigits[1] & 0x0f) << 4)
        | (((BYTE)iDigits[2] >> 2) & 0x0f);
    }

    if (iDigits[3] >= 0) {
      outData[st_length++] = (((BYTE)iDigits[2] & 0x03) << 6)
        | ((BYTE)iDigits[3] & 0x3f);
    }
  }

  out_len = st_length;
}


static const int MAX_TTT_LEN = 64; // Max tooltip text length
CMyString PWSUtil::NormalizeTTT(const CMyString &in)
{
  CMyString ttt;
  if (in.GetLength() >= MAX_TTT_LEN) {
    ttt = in.Left(MAX_TTT_LEN/2-6) + 
      _T(" ... ") + in.Right(MAX_TTT_LEN/2);
  } else {
    ttt = in;
  }
  return ttt;
}

bool PWSUtil::MatchesString(CMyString string1, CMyString &csObject,
                            const int &iFunction)
{
  const int sb_len = string1.GetLength();
  const int ob_len = csObject.GetLength();

  // Negative = Case   Sensitive
  // Positive = Case INsensitive
  switch (iFunction) {
    case -MR_EQUALS:
    case  MR_EQUALS:
      return ((ob_len == sb_len) &&
             (((iFunction < 0) && (csObject.Compare((LPCTSTR)string1) == 0)) ||
              ((iFunction > 0) && (csObject.CompareNoCase((LPCTSTR)string1) == 0))));
    case -MR_NOTEQUAL:
    case  MR_NOTEQUAL:
      return (((iFunction < 0) && (csObject.Compare((LPCTSTR)string1) != 0)) ||
              ((iFunction > 0) && (csObject.CompareNoCase((LPCTSTR)string1) != 0)));
    case -MR_BEGINS:
    case  MR_BEGINS:
      if (ob_len >= sb_len) {
        csObject = csObject.Left(sb_len);
        return (((iFunction < 0) && (string1.Compare((LPCTSTR)csObject) == 0)) ||
                ((iFunction > 0) && (string1.CompareNoCase((LPCTSTR)csObject) == 0)));
      } else {
        return false;
      }
    case -MR_NOTBEGIN:
    case  MR_NOTBEGIN:
      if (ob_len >= sb_len) {
        csObject = csObject.Left(sb_len);
        return (((iFunction < 0) && (string1.Compare((LPCTSTR)csObject) != 0)) ||
                ((iFunction > 0) && (string1.CompareNoCase((LPCTSTR)csObject) != 0)));
      } else {
        return false;
      }
    case -MR_ENDS:
    case  MR_ENDS:
      if (ob_len > sb_len) {
        csObject = csObject.Right(sb_len);
        return (((iFunction < 0) && (string1.Compare((LPCTSTR)csObject) == 0)) ||
                ((iFunction > 0) && (string1.CompareNoCase((LPCTSTR)csObject) == 0)));
      } else {
        return false;
      }
    case -MR_NOTEND:
    case  MR_NOTEND:
      if (ob_len > sb_len) {
        csObject = csObject.Right(sb_len);
        return (((iFunction < 0) && (string1.Compare((LPCTSTR)csObject) != 0)) ||
                ((iFunction > 0) && (string1.CompareNoCase((LPCTSTR)csObject) != 0)));
      } else
        return true;
    case -MR_CONTAINS:
      return (csObject.Find((LPCTSTR)string1) != -1);
    case  MR_CONTAINS:
    {
      csObject.MakeLower();
      CString subgroupLC(string1);
      subgroupLC.MakeLower();
      return (csObject.Find((LPCTSTR)subgroupLC) != -1);
    }
    case -MR_NOTCONTAIN:
      return (csObject.Find((LPCTSTR)string1)== -1);
    case  MR_NOTCONTAIN:
    {
      csObject.MakeLower();
      CString subgroupLC(string1);
      subgroupLC.MakeLower();
      return (csObject.Find((LPCTSTR)subgroupLC) == -1);
    }
    default:
      ASSERT(0);
  }

  return true; // should never get here!
}

bool PWSUtil::MatchesInteger(const int &num1, const int &num2, const int &iValue,
                             const int &iFunction)
{
  switch (iFunction) {
    case MR_EQUALS:
      return iValue == num1;
    case MR_NOTEQUAL:
      return iValue != num1;
    case MR_BETWEEN:
      return iValue >= num1 && iValue <= num2;
    case MR_LT:
      return iValue < num1;
    case MR_LE:
      return iValue <= num1;
    case MR_GT:
      return iValue > num1;
    case MR_GE:
      return iValue >= num1;
    default:
      ASSERT(0);
  }
  return false;
}

bool PWSUtil::MatchesDateTime(const time_t &time1, const time_t &time2, const time_t &tValue,
                              const int &iFunction)
{
  switch (iFunction) {
    case MR_EQUALS:
      return tValue == time1;
    case MR_NOTEQUAL:
      return tValue != time1;
    case MR_BETWEEN:
      return tValue >= time1 && tValue <= time2;
    case MR_BEFORE:
      return tValue < time1;
    case MR_AFTER:
      return tValue > time1;
    default:
      ASSERT(0);
  }
  return false;
}

bool PWSUtil::MatchesBool(const bool bValue, const int &iFunction)
{
  bool rc;

  if (bValue) {
    if (iFunction == MR_EQUALS ||
        iFunction == MR_ACTIVE ||
        iFunction == MR_PRESENT)
      rc = true;
    else
      rc = false;
  } else {
    if (iFunction == MR_NOTEQUAL ||
        iFunction == MR_INACTIVE ||
        iFunction == MR_NOTPRESENT)
      rc = true;
    else
      rc = false;
  }
  return rc;
}
