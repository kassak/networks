#include "common/udp.hpp"
#include <iostream>

#include "client.hpp"

in_addr resolve(std::vector<in_addr> const & ips)
{
   std::cout << "Multiple interfaces found. Select right." << std::endl;
   for(size_t i = 0; i < ips.size(); ++i)
   {
      std::cout << i << " " << inet_ntoa(ips[i]) << std::endl;
   }
   std::cout << "Number: ";
   std::cout.flush();
   size_t idx;
   std::cin >> idx;
   return ips[idx];
}

int main(int argc, char** argv)
{
   s2m::client_t client("239.1.1.1", &resolve);
   client.run();
   return 0;
   try
   {
      udp::socket_t sock;
      sock.connect("239.1.1.1", 12321);
      sock.bind();
      sock.set_echo(true);
      sock.join_group(true);
      std::string test("bbbbb");
      sock.send(test.c_str(), test.size());
      char c[200];
      size_t s = sock.recv(c, 3);
      std::cout << "recv: " << std::string(c, c+s) << "|" << std::endl;
   }
   catch(std::runtime_error&e)
   {
      std::cout << "err: " << e.what() << std::endl;
   }
   return 0;
}
