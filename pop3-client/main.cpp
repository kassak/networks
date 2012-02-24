#include <iostream>
#include "pop3_client.hpp"
//#include <readline/readline.h>
//#include <readline/history.h>

int main(int argc, char ** argv)
{
   /*
   char * tmp = readline("username: ");
   std::string username(tmp);
   free(tmp);

   add_history(username.c_str());

   tmp = getpass("password: ");
   std::string pass(tmp);
   */
   pop3::tui iface;
   try
   {
      iface.run();
   }
   catch(std::exception & e)
   {
      std::cerr << "Critical error: " << e.what() << std::endl;
   }
}
