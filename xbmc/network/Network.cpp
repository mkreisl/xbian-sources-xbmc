/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "settings/Settings.h"
#include "Network.h"
#include "messaging/ApplicationMessenger.h"
#include "network/NetworkServices.h"
#include "utils/log.h"
#ifdef TARGET_WINDOWS
#include "utils/SystemInfo.h"
#include "platform/win32/WIN32Util.h"
#include "utils/CharsetConverter.h"
#endif
#include "utils/StringUtils.h"
#include "xbmc/interfaces/AnnouncementManager.h"

using namespace KODI::MESSAGING;

/* slightly modified in_ether taken from the etherboot project (http://sourceforge.net/projects/etherboot) */
bool in_ether (const char *bufp, unsigned char *addr)
{
  if (strlen(bufp) != 17)
    return false;

  char c;
  const char *orig;
  unsigned char *ptr = addr;
  unsigned val;

  int i = 0;
  orig = bufp;

  while ((*bufp != '\0') && (i < 6))
  {
    val = 0;
    c = *bufp++;

    if (isdigit(c))
      val = c - '0';
    else if (c >= 'a' && c <= 'f')
      val = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      val = c - 'A' + 10;
    else
      return false;

    val <<= 4;
    c = *bufp;
    if (isdigit(c))
      val |= c - '0';
    else if (c >= 'a' && c <= 'f')
      val |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      val |= c - 'A' + 10;
    else if (c == ':' || c == '-' || c == 0)
      val >>= 4;
    else
      return false;

    if (c != 0)
      bufp++;

    *ptr++ = (unsigned char) (val & 0377);
    i++;

    if (*bufp == ':' || *bufp == '-')
      bufp++;
  }

  if (bufp - orig != 17)
    return false;

  return true;
}

int NetworkAccessPoint::getQuality() const
{
  // Cisco dBm lookup table (partially nonlinear)
  // Source: "Converting Signal Strength Percentage to dBm Values, 2002"
  int quality;
  if (m_dBm >= -10) quality = 100;
  else if (m_dBm >= -20) quality = 85 + (m_dBm + 20);
  else if (m_dBm >= -30) quality = 77 + (m_dBm + 30);
  else if (m_dBm >= -60) quality = 48 + (m_dBm + 60);
  else if (m_dBm >= -98) quality = 13 + (m_dBm + 98);
  else if (m_dBm >= -112) quality = 1 + (m_dBm + 112);
  else quality = 0;
  return quality;
}

int NetworkAccessPoint::FreqToChannel(float frequency)
{
  int IEEE80211Freq[] = {2412, 2417, 2422, 2427, 2432,
                         2437, 2442, 2447, 2452, 2457,
                         2462, 2467, 2472, 2484,
                         5180, 5200, 5210, 5220, 5240, 5250,
                         5260, 5280, 5290, 5300, 5320,
                         5745, 5760, 5765, 5785, 5800, 5805, 5825};
  int IEEE80211Ch[] =   {   1,    2,    3,    4,    5,
                            6,    7,    8,    9,   10,
                           11,   12,   13,   14,
                           36,   40,   42,   44,   48,   50,
                           52,   56,   58,   60,   64,
                          149,  152,  153,  157,  160,  161,  165};
  // Round frequency to the nearest MHz
  int mod_chan = (int)(frequency / 1000000 + 0.5f);
  for (unsigned int i = 0; i < sizeof(IEEE80211Freq) / sizeof(int); ++i)
  {
    if (IEEE80211Freq[i] == mod_chan)
      return IEEE80211Ch[i];
  }
  return 0; // unknown
}


CNetwork::CNetwork() :
  m_bStop(false)
{
  m_signalNetworkChange.Reset();
}

CNetwork::~CNetwork()
{
  m_bStop = true;
  m_updThread->StopThread(false);
  m_signalNetworkChange.Set();
  CApplicationMessenger::GetInstance().PostMsg(TMSG_NETWORKMESSAGE, SERVICES_DOWN, 0);
  m_updThread->StopThread(true);
}

std::string CNetwork::GetIpStr(const struct sockaddr *sa)
{
  std::string result;
  if (!sa)
    return result;

  char s[INET6_ADDRSTRLEN] = {0};
  switch(sa->sa_family)
  {
  case AF_INET:
    inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), s, INET6_ADDRSTRLEN);
    break;
  case AF_INET6:
    inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), s, INET6_ADDRSTRLEN);
    break;
  default:
    return result;
  }

  result = s;
  return result;
}

std::string CNetwork::GetIpStr(unsigned long address)
{
  struct in_addr in = { htonl(address) };
  std::string addr = inet_ntoa(in);

  return addr;
}

int CNetwork::ParseHex(char *str, unsigned char *addr)
{
   int len = 0;

   while (*str)
   {
      int tmp;
      if (str[1] == 0)
         return -1;
      if (sscanf(str, "%02x", (unsigned int *)&tmp) != 1)
         return -1;
      addr[len] = tmp;
      len++;
      str += 2;
   }

   return len;
}

bool CNetwork::ConvIPv4(const std::string &address, struct sockaddr_in *sa, unsigned short port)
{
  if (address.empty())
    return false;

  struct in_addr sin_addr;
  int ret = inet_pton(AF_INET, address.c_str(), &sin_addr);

  if (ret > 0 && sa)
  {
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    memcpy(&(sa->sin_addr), &sin_addr, sizeof(struct in_addr));
  }

  return (ret > 0);
}

bool CNetwork::ConvIPv6(const std::string &address, struct sockaddr_in6 *sa, unsigned short port)
{
  if (address.empty())
    return false;

  struct in6_addr sin6_addr;
  int ret = inet_pton(AF_INET6, address.c_str(), &sin6_addr);

  if (ret > 0 && sa)
  {
    sa->sin6_family = AF_INET6;
    sa->sin6_port = htons((u_int16_t)port);
    memcpy(&(sa->sin6_addr), &sin6_addr, sizeof(struct in6_addr));
  }

  return (ret > 0);
}

struct sockaddr_storage *CNetwork::ConvIP(const std::string &address, unsigned short port)
{
  struct sockaddr_storage *sa = NULL;

  if (!address.empty())
    if (sa = (struct sockaddr_storage *) malloc(sizeof(struct sockaddr_storage)))
      if (ConvIPv4(address, (struct sockaddr_in  *) sa, port)
      ||  ConvIPv6(address, (struct sockaddr_in6 *) sa, port))
        return sa;

  if (sa)
    free(sa);

  return NULL;
}

bool CNetwork::GetHostName(std::string& hostname)
{
  char hostName[128];
  if (gethostname(hostName, sizeof(hostName)))
    return false;

#ifdef TARGET_WINDOWS
  std::string hostStr;
  g_charsetConverter.systemToUtf8(hostName, hostStr);
  hostname = hostStr;
#else
  hostname = hostName;
#endif
  return true;
}

bool CNetwork::IsLocalHost(const std::string& hostname)
{
  if (hostname.empty())
    return false;

  if (StringUtils::StartsWith(hostname, "127.")
      || (hostname == "::1")
      || StringUtils::EqualsNoCase(hostname, "localhost"))
    return true;

  std::string myhostname;
  if (GetHostName(myhostname)
      && StringUtils::EqualsNoCase(hostname, myhostname))
    return true;

  myhostname = CanonizeIPv6(hostname);
  if (ConvIPv4(myhostname) || ConvIPv6(myhostname))
  {
    for (auto &&iface: GetInterfaceList())
      if (iface && iface->GetCurrentIPAddress() == myhostname)
        return true;
  }

  return false;
}

CNetworkInterface* CNetwork::GetFirstConnectedInterface()
{
   for (auto &&iface: GetInterfaceList())
     if (iface && iface->IsConnected())
       return iface;

   return NULL;
}

bool CNetwork::AddrMatch(const std::string &addr, const std::string &match_ip, const std::string &match_mask)
{
  if (ConvIPv4(addr) && ConvIPv4(match_ip) && ConvIPv4(match_mask))
  {
    unsigned long address = ntohl(inet_addr(addr.c_str()));
    unsigned long subnet = ntohl(inet_addr(match_mask.c_str()));
    unsigned long local = ntohl(inet_addr(match_ip.c_str()));
    return ((address & subnet) == (local & subnet));
  }
  else if (!g_application.getNetwork().SupportsIPv6())
  {
    return false;
  }
  else
  {
    struct sockaddr_in6 address;
    struct sockaddr_in6 local;
    struct sockaddr_in6 subnet;
    if (!ConvIPv6(addr, &address) || !ConvIPv6(match_ip, &local)
     || !ConvIPv6(match_mask, &subnet))
      return false;

    // mask matching of IPv6 follows same rule as for IPv4, only
    // difference is the storage object of IPv6 address what
    // is 16 segments of 8bit information (16bytes => 128bits)
    // lets assume we match fd::2 against fd::1/16. this means
    // for illustration:
    // 00fd:0000:0000:0000:0000:0000:0000:0002
    // to
    // 00fd:0000:0000:0000:0000:0000:0000:0001 with mask
    // ffff:0000:0000:0000:0000:0000:0000:0000
    // as with IPv4, addr1 & mask == addr2 & mask - for each segment

    // despite the comment explaining uint_8[16] structure
    // (what at the time of writing this text was valid for OSX/Linux/BSD)
    // rather let's use type independent construction. this is because (OSX????)
    // .h files commented the internal s6_addr structure type inside
    // sockaddr_in6 as not being mandatory specified by RFC - devil never sleeps.
    unsigned int m;
    for (m = 0; m < sizeof(address.sin6_addr.s6_addr); m++)
      if ((address.sin6_addr.s6_addr[m] & subnet.sin6_addr.s6_addr[m]) !=
          (  local.sin6_addr.s6_addr[m] & subnet.sin6_addr.s6_addr[m]))
      {
        m = -1;
      }

    // in case of matching addresses, we loop through each segment,
    // leaving m with final value of 16.
    // if we don't match, m is set to <unsigned int>.max() and for() is ended.
    // RESULT: any value of m smaller than <unsigned int>.max() indicates success.
    if (m < (unsigned int)~0)
      return true;
  }

  return false;
}

bool CNetwork::HasInterfaceForIP(const std::string &address)
{
  if (address.empty())
    return false;

  for (auto &&iface: GetInterfaceList())
    if (iface && iface->IsConnected() &&
        AddrMatch(address, iface->GetCurrentIPAddress(), iface->GetCurrentNetmask()))
        return true;

  return false;
}

bool CNetwork::HasInterfaceForIP(unsigned long address)
{
  std::string addr = GetIpStr(address);
  return HasInterfaceForIP(addr);
}

bool CNetwork::IsConnected()
{
   return GetFirstConnectedInterface() != NULL;
}

CNetworkInterface* CNetwork::GetInterfaceByName(const std::string& name)
{
   for (auto &&iface: GetInterfaceList())
     if (iface && iface->GetName() == name)
       return iface;

   return NULL;
}

void CNetwork::NetworkMessage(EMESSAGE message, int param)
{
  switch( message )
  {
    case SERVICES_UP:
      if (GetInterfaceList().empty())
      {
        CLog::Log(LOGDEBUG, "%s - There is no configured network interface. Not starting network services",__FUNCTION__);
        break;
      }

      CLog::Log(LOGDEBUG, "%s - Starting network services",__FUNCTION__);
      CNetworkServices::GetInstance().Start();
      break;

    case SERVICES_DOWN:
      CLog::Log(LOGDEBUG, "%s - Signaling network services to stop",__FUNCTION__);
      CNetworkServices::GetInstance().Stop(false); // tell network services to stop, but don't wait for them yet
      CLog::Log(LOGDEBUG, "%s - Waiting for network services to stop",__FUNCTION__);
      CNetworkServices::GetInstance().Stop(true); // wait for network services to stop
      break;

    case NETWORK_CHANGED:
      m_signalNetworkChange.Set();
      ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::Network, "network", "OnInterfacesChange");
      if (CSettings::GetInstance().GetBool("network.restartservices"))
      {
        CLog::Log(LOGDEBUG, "%s - Network setup changed. Will restart network services",__FUNCTION__);
        NetworkMessage(SERVICES_DOWN, 0);
        NetworkMessage(SERVICES_UP, 0);
      }
      break;
  }
}

bool CNetwork::WakeOnLan(const char* mac)
{
  int i, j, packet;
  unsigned char ethaddr[8];
  unsigned char buf [128];
  unsigned char *ptr;

  if (GetFirstConnectedFamily() == AF_INET6)
    return false;

  // Fetch the hardware address
  if (!in_ether(mac, ethaddr))
  {
    CLog::Log(LOGERROR, "%s - Invalid hardware address specified (%s)", __FUNCTION__, mac);
    return false;
  }

  // Setup the socket
  if ((packet = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
  {
    CLog::Log(LOGERROR, "%s - Unable to create socket (%s)", __FUNCTION__, strerror (errno));
    return false;
  }
 
  // Set socket options
  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  saddr.sin_port = htons(9);

  unsigned int value = 1;
  if (setsockopt (packet, SOL_SOCKET, SO_BROADCAST, (char*) &value, sizeof( unsigned int ) ) == SOCKET_ERROR)
  {
    CLog::Log(LOGERROR, "%s - Unable to set socket options (%s)", __FUNCTION__, strerror (errno));
    closesocket(packet);
    return false;
  }
 
  // Build the magic packet (6 x 0xff + 16 x MAC address)
  ptr = buf;
  for (i = 0; i < 6; i++)
    *ptr++ = 0xff;

  for (j = 0; j < 16; j++)
    for (i = 0; i < 6; i++)
      *ptr++ = ethaddr[i];
 
  // Send the magic packet
  if (sendto (packet, (char *)buf, 102, 0, (struct sockaddr *)&saddr, sizeof (saddr)) < 0)
  {
    CLog::Log(LOGERROR, "%s - Unable to send magic packet (%s)", __FUNCTION__, strerror (errno));
    closesocket(packet);
    return false;
  }

  closesocket(packet);
  CLog::Log(LOGINFO, "%s - Magic packet send to '%s'", __FUNCTION__, mac);
  return true;
}

// ping helper
static const char* ConnectHostPort(SOCKET soc, const struct sockaddr_in& addr, struct timeval& timeOut, bool tryRead)
{
  // set non-blocking
#ifdef TARGET_WINDOWS
  u_long nonblocking = 1;
  int result = ioctlsocket(soc, FIONBIO, &nonblocking);
#else
  int result = fcntl(soc, F_SETFL, fcntl(soc, F_GETFL) | O_NONBLOCK);
#endif

  if (result != 0)
    return "set non-blocking option failed";

  result = connect(soc, (struct sockaddr *)&addr, sizeof(addr)); // non-blocking connect, will fail ..

  if (result < 0)
  {
#ifdef TARGET_WINDOWS
    if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
    if (errno != EINPROGRESS)
#endif
      return "unexpected connect fail";

    { // wait for connect to complete
      fd_set wset;
      FD_ZERO(&wset); 
      FD_SET(soc, &wset); 

      result = select(FD_SETSIZE, 0, &wset, 0, &timeOut);
    }

    if (result < 0)
      return "select fail";

    if (result == 0) // timeout
      return ""; // no error

    { // verify socket connection state
      int err_code = -1;
      socklen_t code_len = sizeof (err_code);

      result = getsockopt(soc, SOL_SOCKET, SO_ERROR, (char*) &err_code, &code_len);

      if (result != 0)
        return "getsockopt fail";

      if (err_code != 0)
        return ""; // no error, just not connected
    }
  }

  if (tryRead)
  {
    fd_set rset;
    FD_ZERO(&rset); 
    FD_SET(soc, &rset); 

    result = select(FD_SETSIZE, &rset, 0, 0, &timeOut);

    if (result > 0)
    {
      char message [32];

      result = recv(soc, message, sizeof(message), 0);
    }

    if (result == 0)
      return ""; // no reply yet

    if (result < 0)
      return "recv fail";
  }

  return 0; // success
}

bool CNetwork::PingHost(const std::string &ipaddr, unsigned short port, unsigned int timeOutMs, bool readability_check)
{
  if (port == 0) // use icmp ping
    return PingHostImpl (ipaddr, timeOutMs);

  struct sockaddr_storage *addr = ConvIP(ipaddr, port);

  if (!addr)
    return false;

  SOCKET soc = socket(addr->ss_family, SOCK_STREAM, 0);

  const char* err_msg = "invalid socket";

  if (soc != INVALID_SOCKET)
  {
    struct timeval tmout; 
    tmout.tv_sec = timeOutMs / 1000; 
    tmout.tv_usec = (timeOutMs % 1000) * 1000; 

    err_msg = ConnectHostPort (soc, (const struct sockaddr_in&)addr, tmout, readability_check);

    (void) closesocket (soc);
  }

  if (err_msg && *err_msg)
  {
#ifdef TARGET_WINDOWS
    std::string sock_err = CWIN32Util::WUSysMsg(WSAGetLastError());
#else
    std::string sock_err = strerror(errno);
#endif

    CLog::Log(LOGERROR, "%s(%s:%d) - %s (%s)", __FUNCTION__, ipaddr.c_str(), port, err_msg, sock_err.c_str());
  }

  free(addr);
  return err_msg == 0;
}

std::string CNetwork::CanonizeIPv6(const std::string &address)
{
  std::string result = address;

  struct sockaddr_in6 addr;
  if (!ConvIPv6(address, &addr))
    return result;

  result = GetIpStr((const sockaddr*)&addr);
  return result;
}

int CNetwork::PrefixLengthIPv6(const std::string &address)
{
  struct sockaddr_in6 mask;
  if (!ConvIPv6(address, &mask))
    return -1;

  unsigned int m;
  unsigned int segment_size = 128 / sizeof(mask.sin6_addr.s6_addr);
  auto segment_tmax = mask.sin6_addr.s6_addr[0];
  segment_tmax = -1;

  // let's assume mask being ff80:: - in binary form:
  // 11111111:10000000
  // as prefix-length is count of leftmost contiguous bits,
  // we can simply check how many segments are full of bits (0xff).
  // this * nr_bits in segment + individual bits from the first segment
  // not full is our prefix-length.
  for (m = 0;
       m < sizeof(mask.sin6_addr.s6_addr) && (mask.sin6_addr.s6_addr[m] == segment_tmax);
       m += 1);

  m *= segment_size;
  // if we didn't match all segments (prefixlength not /128)
  if (m < 128)
  {
    // we shift left until we get all bits zero,
    // then we have final length (in this case it is /9)
    auto leftover_bits = mask.sin6_addr.s6_addr[m / segment_size];
    do {
      m++;
    } while (leftover_bits <<= 1);
  }

  return m;
}

//creates, binds and listens a tcp socket on the desired port. Set bindLocal to
//true to bind to localhost only. The socket will listen over ipv6 if possible
//and fall back to ipv4 if ipv6 is not available on the platform.
int CreateTCPServerSocket(const int port, const bool bindLocal, const int backlog, const char *callerName)
{
  struct sockaddr_storage addr;
  int    sock = -1;

#ifdef WINSOCK_VERSION
  int yes = 1;
  int no = 0;
#else
  unsigned int yes = 1;
  unsigned int no = 0;
#endif
  
  // first try ipv6
  if ((sock = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)) >= 0)
  {
    // in case we're on ipv6, make sure the socket is dual stacked
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&no, sizeof(no)) < 0)
    {
#ifdef _MSC_VER
      std::string sock_err = CWIN32Util::WUSysMsg(WSAGetLastError());
#else
      std::string sock_err = strerror(errno);
#endif
      CLog::Log(LOGWARNING, "%s Server: Only IPv6 supported (%s)", callerName, sock_err.c_str());
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in6 *s6;
    memset(&addr, 0, sizeof(addr));
    addr.ss_family = AF_INET6;
    s6 = (struct sockaddr_in6 *) &addr;
    s6->sin6_port = htons(port);
    if (bindLocal)
      s6->sin6_addr = in6addr_loopback;
    else
      s6->sin6_addr = in6addr_any;

    if (bind( sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_in6)) < 0)
    {
      closesocket(sock);
      sock = -1;
      CLog::Log(LOGDEBUG, "%s Server: Failed to bind ipv6 serversocket on port %d, trying ipv4 (error was %s (%d))",
                           callerName, port, strerror(errno), errno);
    }
  }
  
  // ipv4 fallback
  if (sock < 0 && (sock = socket(PF_INET, SOCK_STREAM, 0)) >= 0)
  {
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in  *s4;
    memset(&addr, 0, sizeof(addr));
    addr.ss_family = AF_INET;
    s4 = (struct sockaddr_in *) &addr;
    s4->sin_port = htons(port);
    if (bindLocal)
      s4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else
      s4->sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind( sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_in)) < 0)
    {
      closesocket(sock);
      CLog::Log(LOGERROR, "%s Server: Failed to bind ipv4 serversocket on port %d, trying ipv4 (error was %s (%d))",
                           callerName, port, strerror(errno), errno);
      return INVALID_SOCKET;
    }
  }
  else if (sock < 0)
  {
    CLog::Log(LOGERROR, "%s Server: Failed to create serversocket", callerName);
    return INVALID_SOCKET;
  }

  if (listen(sock, backlog) < 0)
  {
    closesocket(sock);
    CLog::Log(LOGERROR, "%s Server: Failed to set listen", callerName);
    return INVALID_SOCKET;
  }

  return sock;
}
