/*
 * Socket classes
 *      Copyright (c) 2008 d4rk
 *      Copyright (C) 2008-2013 Team XBMC
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

#include "system.h"

#ifdef HAS_EVENT_SERVER

#include "Socket.h"
#include <vector>

using namespace SOCKETS;

#ifdef WINSOCK_VERSION
#define close closesocket
#endif

/**********************************************************************/
/* CPosixUDPSocket                                                    */
/**********************************************************************/

bool CPosixUDPSocket::Bind(bool localOnly, int port, int range)
{
#ifdef WINSOCK_VERSION
  const char yes = 1;
  const char no  = 0;
#else
  int yes = 1;
  int no  = 0;
#endif
  // close any existing sockets
  Close();

  // If we can, create a socket that works with IPv6 and IPv4.
  // If not, try an IPv4-only socket (we don't want to end up
  // with an IPv6-only socket).
  if (!localOnly) // Only bind loopback to ipv4. TODO : Implement dual bindinds.
  {
    m_iSock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (m_iSock != INVALID_SOCKET)
    {
      if (setsockopt(m_iSock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(&no)) == -1)
      {
        close(m_iSock);
        m_iSock = INVALID_SOCKET;
      }
      else
      {
        SOCKET testSocket = socket(AF_INET6, SOCK_DGRAM, 0);
        setsockopt(testSocket, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(&no));
        // Try to bind a socket to validate ipv6 status
        for (m_iPort = port; m_iPort <= port + range; ++m_iPort)
        {
          m_addr.SetAddress("::", m_iPort);
          if (bind(testSocket, m_addr.sa(), CNetwork::sa_len(m_addr.sa())) >= 0)
          {
            closesocket(testSocket);
            m_ipv6Socket = true;
            break;
          }
        }
        if (!m_ipv6Socket)
        {
          CLog::Log(LOGWARNING, "UDP: Unable to bind to advertised ipv6, fallback to ipv4");
          closesocket(testSocket);
          close(m_iSock);
          m_iSock = INVALID_SOCKET;
        }
      }
    }
  }

  if (m_iSock == INVALID_SOCKET)
    m_iSock = socket(AF_INET, SOCK_DGRAM, 0);

  if (m_iSock == INVALID_SOCKET)
  {
#ifdef TARGET_WINDOWS
    int ierr = WSAGetLastError();
    CLog::Log(LOGERROR, "UDP: Could not create socket %d", ierr);
    // hack for broken third party libs
    if (ierr == WSANOTINITIALISED)
    {
      WSADATA wd;
      if (WSAStartup(MAKEWORD(2,2), &wd) != 0)
        CLog::Log(LOGERROR, "UDP: WSAStartup failed");
    }
#else
    CLog::Log(LOGERROR, "UDP: Could not create socket");
#endif    
    CLog::Log(LOGERROR, "UDP: %s", strerror(errno));
    return false;
  }

  // make sure we can reuse the address
  if (setsockopt(m_iSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))==-1)
  {
    CLog::Log(LOGWARNING, "UDP: Could not enable the address reuse options");
    CLog::Log(LOGWARNING, "UDP: %s", strerror(errno));
  }

  std::string bindAddress = "::";
  // bind to any address or localhost
  if (m_ipv6Socket && localOnly)
  {
    bindAddress = "::1";
  }
  else if (!m_ipv6Socket)
  {
    if (localOnly)
      bindAddress = "127.0.0.1";
    else
      bindAddress = "0.0.0.0";
  }

  // bind the socket ( try from port to port+range )
  for (m_iPort = port; m_iPort <= port + range; ++m_iPort)
  {
    m_addr.SetAddress(bindAddress, m_iPort);
    if (bind(m_iSock, m_addr.sa(), CNetwork::sa_len(m_addr.sa())) != 0)
    {
      CLog::Log(LOGWARNING, "UDP: Error binding socket on port %d (ipv6 : %s)", m_iPort, m_ipv6Socket ? "true" : "false" );
      CLog::Log(LOGWARNING, "UDP: %s", strerror(errno));
    }
    else
    {
      CLog::Log(LOGNOTICE, "UDP: Listening on port %d (ipv6 : %s)", m_iPort, m_ipv6Socket ? "true" : "false");
      SetBound();
      SetReady();
      break;
    }
  }

  // check for errors
  if (!Bound())
  {
    CLog::Log(LOGERROR, "UDP: No suitable port found");
    Close();
    return false;
  }

  return true;
}

void CPosixUDPSocket::Close()
{
  if (m_iSock>=0)
  {
    close(m_iSock);
    m_iSock = INVALID_SOCKET;
  }
  SetBound(false);
  SetReady(false);
}

int CPosixUDPSocket::Read(CAddress& addr, const int buffersize, void *buffer)
{
  if (m_ipv6Socket)
    addr.SetAddress("::");

  struct sockaddr *sa = addr.sa();
  socklen_t len;

  return (int)recvfrom(m_iSock, (char*)buffer, (size_t)buffersize, 0, sa, &len);
}

int CPosixUDPSocket::SendTo(const CAddress& addr, const int buffersize,
                          const void *buffer)
{
  return (int)sendto(m_iSock, (const char *)buffer, (size_t)buffersize, 0,
                     (const struct sockaddr *)addr.sa(), CNetwork::sa_len((struct sockaddr *)addr.sa()));
}

/**********************************************************************/
/* CSocketFactory                                                     */
/**********************************************************************/

CUDPSocket* CSocketFactory::CreateUDPSocket()
{
  return new CPosixUDPSocket();
}

/**********************************************************************/
/* CSocketListener                                                    */
/**********************************************************************/

CSocketListener::CSocketListener()
{
  Clear();
}

void CSocketListener::AddSocket(CBaseSocket *sock)
{
  // WARNING: not threadsafe (which is ok for now)

  if (sock && sock->Ready())
  {
    m_sockets.push_back(sock);
    FD_SET(sock->Socket(), &m_fdset);
#ifndef WINSOCK_VERSION
    if (sock->Socket() > m_iMaxSockets)
      m_iMaxSockets = sock->Socket();
#endif
  }
}

bool CSocketListener::Listen(int timeout)
{
  if (m_sockets.size()==0)
  {
    CLog::Log(LOGERROR, "SOCK: No sockets to listen for");
    throw LISTENEMPTY;
  }

  m_iReadyCount = 0;
  m_iCurrentSocket = 0;

  FD_ZERO(&m_fdset);
  for (unsigned int i = 0 ; i<m_sockets.size() ; i++)
  {
    FD_SET(m_sockets[i]->Socket(), &m_fdset);
  }

  // set our timeout
  struct timeval tv;
  int rem = timeout % 1000;
  tv.tv_usec = rem * 1000;
  tv.tv_sec = timeout / 1000;

  m_iReadyCount = select(m_iMaxSockets+1, &m_fdset, NULL, NULL, (timeout < 0 ? NULL : &tv));

  if (m_iReadyCount<0)
  {
    CLog::Log(LOGERROR, "SOCK: Error selecting socket(s)");
    Clear();
    throw LISTENERROR;
  }
  else
  {
    m_iCurrentSocket = 0;
    return (m_iReadyCount>0);
  }
}

void CSocketListener::Clear()
{
  m_sockets.clear();
  FD_ZERO(&m_fdset);
  m_iReadyCount = 0;
  m_iMaxSockets = 0;
  m_iCurrentSocket = 0;
}

CBaseSocket* CSocketListener::GetFirstReadySocket()
{
  if (m_iReadyCount<=0)
    return NULL;

  for (int i = 0 ; i < (int)m_sockets.size() ; i++)
  {
    if (FD_ISSET((m_sockets[i])->Socket(), &m_fdset))
    {
      m_iCurrentSocket = i;
      return m_sockets[i];
    }
  }
  return NULL;
}

CBaseSocket* CSocketListener::GetNextReadySocket()
{
  if (m_iReadyCount<=0)
    return NULL;

  for (int i = m_iCurrentSocket+1 ; i<(int)m_sockets.size() ; i++)
  {
    if (FD_ISSET(m_sockets[i]->Socket(), &m_fdset))
    {
      m_iCurrentSocket = i;
      return m_sockets[i];
    }
  }
  return NULL;
}

#endif // HAS_EVENT_SERVER
