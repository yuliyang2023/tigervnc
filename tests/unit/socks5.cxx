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

#include <gtest/gtest.h>
#include <future>
#include <memory>
#include <stdexcept>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include <network/Socks5.h>
#include <network/TcpSocket.h>

using Bytes = std::vector<unsigned char>;

class Socks5 : public testing::Test {
protected:
  std::unique_ptr<network::Socket> client;
  int peer = -1;
  std::future<void> server;

  void SetUp() override {
    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    client.reset(new network::TcpSocket(sockets[0]));
    peer = sockets[1];
    timeval timeout = {2, 0};
    setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  }

  void TearDown() override {
    client.reset();
    if (server.valid()) {
      try { server.get(); }
      catch (const std::exception& e) { ADD_FAILURE() << e.what(); }
    }
    close(peer);
  }

  void expect(const Bytes& expected) {
    Bytes actual(expected.size());
    size_t offset = 0;
    while (offset < actual.size()) {
      int count = recv(peer, actual.data() + offset, actual.size() - offset, 0);
      if (count <= 0) throw std::runtime_error("mock proxy read failed");
      offset += count;
    }
    EXPECT_EQ(actual, expected);
  }

  void reply(const Bytes& bytes) {
    // Deliberately fragment every handshake response.
    for (unsigned char byte : bytes)
      if (send(peer, &byte, 1, 0) != 1)
        throw std::runtime_error("mock proxy write failed");
  }

  void start(const Bytes& request, const Bytes& response,
             bool authenticated = false, bool authFailure = false) {
    server = std::async(std::launch::async,
      [this, request, response, authenticated, authFailure] {
        expect({5, 1, (unsigned char)(authenticated ? 2 : 0)});
        reply({5, (unsigned char)(authenticated ? 2 : 0)});
        if (authenticated) {
          expect({1, 4, 'u', 's', 'e', 'r', 4, 'p', 'a', 's', 's'});
          reply({1, (unsigned char)(authFailure ? 1 : 0)});
          if (authFailure) return;
        }
        expect(request);
        reply(response);
      });
  }

  Bytes domainRequest() {
    return {5, 1, 0, 3, 11, 'v', 'n', 'c', '.', 'i', 'n', 'v', 'a', 'l', 'i', 'd',
            0x17, 0x0d}; // vnc.invalid:5901, must not resolve locally
  }
};

TEST_F(Socks5, DomainAndRfbGreeting) {
  start(domainRequest(), {5, 0, 0, 1, 127, 0, 0, 1, 0, 42, 'R', 'F', 'B'});
  network::negotiateSocks5(*client, "vnc.invalid", 5901);
  server.get();
  char greeting[3];
  ASSERT_EQ(recv(client->getFd(), greeting, 3, MSG_WAITALL), 3);
  EXPECT_EQ(std::string(greeting, 3), "RFB");
}

TEST_F(Socks5, AuthenticationAndDomainReply) {
  start(domainRequest(), {5, 0, 0, 3, 3, 'b', 'n', 'd', 0, 42}, true);
  EXPECT_NO_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901,
                                          "user", "pass"));
}

TEST_F(Socks5, AuthenticationRejected) {
  start({}, {}, true, true);
  EXPECT_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901,
                                       "user", "pass"), std::runtime_error);
}

TEST_F(Socks5, IPv4) {
  start({5, 1, 0, 1, 192, 0, 2, 1, 0, 80},
        {5, 0, 0, 1, 0, 0, 0, 0, 0, 0});
  EXPECT_NO_THROW(network::negotiateSocks5(*client, "192.0.2.1", 80));
}

TEST_F(Socks5, IPv6) {
  Bytes request = {5, 1, 0, 4};
  request.resize(19, 0);
  request.insert(request.end(), {1, 0x17, 0x0c});
  Bytes response = {5, 0, 0, 4};
  response.resize(22, 0);
  start(request, response);
  EXPECT_NO_THROW(network::negotiateSocks5(*client, "::1", 5900));
}

TEST_F(Socks5, ConnectionRefused) {
  start(domainRequest(), {5, 5, 0, 1});
  EXPECT_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901),
               std::runtime_error);
}

TEST_F(Socks5, InvalidAddressType) {
  start(domainRequest(), {5, 0, 0, 99});
  EXPECT_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901),
               std::runtime_error);
}

TEST_F(Socks5, InvalidVersion) {
  start(domainRequest(), {4, 0, 0, 1});
  EXPECT_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901),
               std::runtime_error);
}

TEST_F(Socks5, RejectedMethod) {
  server = std::async(std::launch::async, [this] {
    expect({5, 1, 0});
    reply({5, 255});
  });
  EXPECT_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901),
               std::runtime_error);
}

TEST_F(Socks5, TruncatedReply) {
  server = std::async(std::launch::async, [this] {
    expect({5, 1, 0});
    reply({5});
    shutdown(peer, SHUT_WR);
  });
  EXPECT_THROW(network::negotiateSocks5(*client, "vnc.invalid", 5901),
               std::runtime_error);
}

TEST_F(Socks5, InvalidArguments) {
  EXPECT_THROW(network::negotiateSocks5(*client, "", 5900), std::invalid_argument);
  EXPECT_THROW(network::negotiateSocks5(*client, "host", 65536), std::invalid_argument);
  EXPECT_THROW(network::negotiateSocks5(*client, "host", 0), std::invalid_argument);
  EXPECT_THROW(network::negotiateSocks5(*client, "host", 5900, "user", ""),
               std::invalid_argument);
  std::string oversized(256, 'x');
  EXPECT_THROW(network::negotiateSocks5(*client, oversized.c_str(), 5900),
               std::invalid_argument);
  EXPECT_THROW(network::negotiateSocks5(*client, "host", 5900,
                                       oversized.c_str(), "pass"), std::invalid_argument);
  EXPECT_THROW(network::connectSocks5("localhost", "host", 5900), std::invalid_argument);
  EXPECT_THROW(network::connectSocks5("localhost:65536", "host", 5900), std::invalid_argument);
}
