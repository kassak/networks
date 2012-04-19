#include "common/udp.hpp"
#include <iostream>
#include <fstream>

#include "client.hpp"
#include "streamer.hpp"
#include "tui.hpp"

size_t get_stuff(std::string const & msg, std::unordered_map<size_t, std::string> const & stuff)
{
   std::cout << msg << std::endl;
   for(auto s : stuff)
   {
      std::cout << s.first << " " << s.second << std::endl;
   }
   std::cout << "Number: ";
   std::cout.flush();
   size_t idx;
   std::cin >> idx;
   return idx;
}

int main(int argc, char** argv)
{
   std::ofstream logf("log.txt");
   logger::set_logger(logger::ERROR,   logger::holder_by_ref(logger::details::level_printer(logger::ERROR),   logf));
   logger::set_logger(logger::WARNING, logger::holder_by_ref(logger::details::level_printer(logger::WARNING), logf));
   logger::set_logger(logger::DEBUG,   logger::holder_by_ref(logger::details::level_printer(logger::DEBUG),   logf));
   logger::set_logger(logger::TRACE,   logger::holder_by_ref(logger::details::level_printer(logger::TRACE),   logf));
//   logger::set_logger(logger::TRACE, logger::null_holder());
   tui ui;
   ui.run();
   return 0;
/*   streamer_t ss("239.1.1.1", 11111);
   ss.init(get_stuff("Select api.", ss.apis()));
   ss.run(get_stuff("Input device.", ss.devices()), get_stuff("Output device.", ss.devices()));
   size_t aaa;
   std::cin >> aaa;
   return 0;*/
/*   s2m::client_t client("239.1.1.1", &resolve);
   client.run();
   return 0;*/
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
