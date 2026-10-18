/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#else
#include <core/winerrno.h>
#endif

#include <core/Exception.h>
#include <core/i18n.h>
#include <network/TcpSocket.h>
#include <network/Socks5.h>

namespace {
  typedef std::chrono::steady_clock Clock;

  // Read exactly the handshake bytes, leaving any RFB greeting in the socket.
  void transfer(int fd, void* data, size_t length, bool sending,
                Clock::time_point deadline)
  {
    char* cursor = static_cast<char*>(data);
    while (length > 0) {
      auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
        deadline - Clock::now()).count();
      if (remaining <= 0)
        throw std::runtime_error(_("SOCKS5 handshake timed out"));
      timeval timeout;
      timeout.tv_sec = remaining / 1000000;
      timeout.tv_usec = remaining % 1000000;
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      int result = select(fd + 1, sending ? nullptr : &fds,
                          sending ? &fds : nullptr, nullptr, &timeout);
      if (result > 0) {
        if (sending)
          result = send(fd, cursor, length, 0);
        else
          result = recv(fd, cursor, length, 0);
        if (result == 0)
          throw std::runtime_error(_("SOCKS5 proxy closed the connection"));
      }
      if (result < 0) {
#ifdef WIN32
        int error = WSAGetLastError();
#else
        int error = errno;
#endif
#ifdef WIN32
        if (error == WSAEINTR)
#else
        if (error == EINTR)
#endif
          continue;
        throw core::socket_error(_("SOCKS5 handshake failed"), error);
      }
      cursor += result;
      length -= result;
    }
  }
}

void network::negotiateSocks5(Socket& socket, const char* host, int port,
                             const char* username, const char* password)
{
  size_t hostLength = strlen(host);
  size_t userLength = strlen(username);
  size_t passwordLength = strlen(password);
  if (hostLength == 0 || hostLength > 255 || port < 1 || port > 65535)
    throw std::invalid_argument(_("Invalid SOCKS5 destination"));
  bool authenticate = userLength != 0;
  if (userLength > 255 || passwordLength > 255 ||
      (authenticate && passwordLength == 0) ||
      (!authenticate && passwordLength != 0))
    throw std::invalid_argument(_("SOCKS5 username and password must each contain 1 to 255 bytes"));

  int fd = socket.getFd();
  auto deadline = Clock::now() + std::chrono::seconds(10);
  // When credentials are supplied, require authentication.
  unsigned char greeting[] = {5, 1, (unsigned char)(authenticate ? 2 : 0)};
  transfer(fd, greeting, sizeof(greeting), true, deadline);
  unsigned char reply[4];
  transfer(fd, reply, 2, false, deadline);
  if (reply[0] != 5 || reply[1] != greeting[2])
    throw std::runtime_error(_("SOCKS5 proxy rejected the authentication method"));

  if (authenticate) {
    std::vector<unsigned char> auth = {1, (unsigned char)userLength};
    auth.insert(auth.end(), username, username + userLength);
    auth.push_back(passwordLength);
    auth.insert(auth.end(), password, password + passwordLength);
    transfer(fd, auth.data(), auth.size(), true, deadline);
    transfer(fd, reply, 2, false, deadline);
    if (reply[0] != 1 || reply[1] != 0)
      throw std::runtime_error(_("SOCKS5 username/password authentication failed"));
  }

  std::vector<unsigned char> request = {5, 1, 0}; // CONNECT
  unsigned char address[16];
  if (inet_pton(AF_INET, host, address) == 1) {
    request.push_back(1);
    request.insert(request.end(), address, address + 4);
  } else if (inet_pton(AF_INET6, host, address) == 1) {
    request.push_back(4);
    request.insert(request.end(), address, address + 16);
  } else {
    request.push_back(3);
    request.push_back(hostLength);
    request.insert(request.end(), host, host + hostLength);
  }
  request.push_back(port >> 8);
  request.push_back(port & 0xff);
  transfer(fd, request.data(), request.size(), true, deadline);
  transfer(fd, reply, 4, false, deadline);
  if (reply[0] != 5 || reply[2] != 0)
    throw std::runtime_error(_("Invalid SOCKS5 proxy response"));
  if (reply[1] != 0) {
    const char* errors[] = {
      N_("SOCKS5 connection succeeded"),
      N_("SOCKS5 proxy general failure"),
      N_("SOCKS5 connection denied by proxy rules"),
      N_("SOCKS5 destination network unreachable"),
      N_("SOCKS5 destination host unreachable"),
      N_("SOCKS5 destination connection refused"),
      N_("SOCKS5 TTL expired"),
      N_("SOCKS5 command not supported"),
      N_("SOCKS5 address type not supported")
    };
    throw std::runtime_error(reply[1] < 9 ? _(errors[reply[1]]) :
                              _("SOCKS5 proxy returned an unknown error"));
  }

  size_t boundLength;
  switch (reply[3]) {
  case 1: boundLength = 4; break;
  case 4: boundLength = 16; break;
  case 3:
    transfer(fd, reply, 1, false, deadline);
    boundLength = reply[0];
    if (boundLength == 0)
      throw std::runtime_error(_("Invalid SOCKS5 bound address"));
    break;
  default:
    throw std::runtime_error(_("Invalid SOCKS5 address type"));
  }
  unsigned char bound[257];
  transfer(fd, bound, boundLength + 2, false, deadline);
}

network::Socket* network::connectSocks5(const char* proxy, const char* host,
                                       int port, const char* username,
                                       const char* password)
{
  std::string proxyHost;
  int proxyPort;
  getHostAndPort(proxy, &proxyHost, &proxyPort, 0);
  if (proxyPort < 1 || proxyPort > 65535)
    throw std::invalid_argument(_("SOCKS5 proxy must specify host:port or [IPv6]:port"));
  std::unique_ptr<Socket> socket(new TcpSocket(proxyHost.c_str(), proxyPort));
  negotiateSocks5(*socket, host, port, username, password);
  return socket.release();
}
