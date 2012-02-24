#include <iostream>
#include "smtp_client.hpp"

int main(int argc, char ** argv)
{
   smtp::tui iface;
   try
   {
      iface.run();
   }
   catch(std::exception & e)
   {
      std::cerr << "Critical error: " << e.what() << std::endl;
   }
}
