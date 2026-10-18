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

#ifndef __NETWORK_SOCKS5_H__
#define __NETWORK_SOCKS5_H__

namespace network {
  class Socket;

  // Establish a SOCKS5 tunnel on an already connected proxy socket.
  // Hostnames are resolved by the proxy. Throws on failure or timeout.
  void negotiateSocks5(Socket& socket, const char* host, int port,
                       const char* username = "", const char* password = "");

  // proxy must specify a literal port: host:port or [IPv6]:port.
  // The caller owns the returned socket.
  Socket* connectSocks5(const char* proxy, const char* host, int port,
                        const char* username = "", const char* password = "");
}

#endif
