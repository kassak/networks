#pragma once

#include <stdexcept>

#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace rline
{
#include <readline/readline.h>
#include <readline/history.h>
}
#include "common/tcp.hpp"
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>

namespace smtp
{
   struct reset_error : std::runtime_error
   {
      reset_error(std::string const & what)
         : std::runtime_error(what)
      {
      }
   };

   struct smtp_error : std::runtime_error
   {
      smtp_error(std::string const & what)
         : std::runtime_error(what)
      {
      }
   };

   struct client
   {
      client(std::string const & host, uint16_t port)
      {
         sock_.connect(host, port);
         std::cout << "Connected to " << host << " at " << port << std::endl;
         std::string msg;
         size_t code;
         code = handle_response(msg);
         if(code != 220)
            throw smtp_error("Connect failed: " + msg);
      }

      void login(std::string const & chost, std::string const & user, std::string const & pass)
      {
         sock_ << "HELO " << chost << "\n";
         std::string msg;
         size_t code;
         code = handle_response(msg);
         if(code != 250)
            throw smtp_error("Login failed: " + msg);

         sock_ << "AUTH LOGIN\n";
         code = handle_response(msg);
         if(code != 334)
            throw smtp_error("Login failed: " + msg);

         using namespace boost::archive::iterators;

         typedef
             base64_from_binary<
                 transform_width<std::string::const_iterator, 6, 8>
         > base64_t;

         typedef
             transform_width<
                 binary_from_base64<std::string::const_iterator>, 8, 6
         > binary_t;

         std::string
            b64_user(base64_t(user.begin()), base64_t(user.end())),
            b64_pass(base64_t(pass.begin()), base64_t(pass.end()));

         std::cout << "!" << b64_user << std::endl;

         sock_ << b64_user << "\n";
         code = handle_response(msg);
         if(code != 334)
            throw smtp_error("Login failed: " + msg);
         sock_ << b64_pass << "\n";
         code = handle_response(msg);
         if(code != 235)
            throw smtp_error("Login failed: " + msg);
      }

      size_t handle_response(std::string& other)
      {
         std::string resp = sock_.getline();
         std::cout << "**" <<resp <<std::endl;
         size_t res;
         std::stringstream ss(resp);
         ss >> res;
         std::getline(ss, other);
         return res;
      }

      void quit()
      {
         sock_ << "QUIT\n";
         std::string tmp;
         handle_response(tmp);
      }

      void send(std::string const & from, std::vector<std::string> const & to, std::string const & subj, std::vector<std::string> const & body)
      {
         std::string msg;

         sock_ << "MAIL FROM:<" << from << ">\n";
         if(handle_response(msg) != 250)
            throw smtp_error("Error while MAIL FROM: " + msg);
         for(size_t i = 0; i < to.size(); ++i)
         {
            sock_ << "RCPT TO:<" << to[i] << ">\n";
            if(handle_response(msg) != 250)
               throw smtp_error("Error while RCPT TO: " + msg);
         }
         sock_ << "DATA\n";
         if(handle_response(msg) != 354)
            throw smtp_error("Error while DATA: " + msg);

         sock_ << "From: <" << from << ">\n";
         sock_ << "To: ";
         for(size_t i = 0; i < to.size(); ++i)
            sock_ << (i==0?"<":", <") << to[i] << ">";
         sock_ << "\n";
         sock_ << "Subject: " << subj << "\n";
         sock_ << "\n";

         for(size_t i = 0; i < body.size(); ++i)
            sock_ << body[i] << "\n";
         sock_ << ".\n";

         if(handle_response(msg) != 250)
            throw smtp_error("Error while DATA body: " + msg);
      }

   private:
      tcp::socket_t sock_;
   };

   struct tui
   {
      tui()
         : state_(ST_RESET)
      {
         rline::read_history(".srv_hist");
      }

      ~tui()
      {
         rline::write_history(".srv_hist");
      }

      enum state_t
      {
         ST_SERVER,
         ST_LOGIN,
         ST_MENU,
         ST_EXIT,

         ST_RESET = ST_SERVER,
      };

      void handle_mail()
      {
         char * tmp = rline::readline("mail from: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string ml_from(tmp);
         free(tmp);
         rline::add_history(ml_from.c_str());

         std::vector<std::string> rcpt_to;
         while(true)
         {
            tmp = rline::readline("mail to(empty to continue): ");
            if(tmp == NULL)
               throw std::runtime_error("EOF recieved");
            if(tmp[0] == '\0')
               break;
            rcpt_to.push_back(tmp);
            free(tmp);
            rline::add_history(ml_from.c_str());
         }
         if(rcpt_to.empty())
            throw smtp_error("Not recipients");

         tmp = rline::readline("Subject: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string subject(tmp);
         free(tmp);

         std::vector<std::string> body;
         bool first = true;
         while(true)
         {
            tmp = rline::readline(first ? "Message body(period to end):\n" : NULL);
            first = false;
            if(tmp == NULL)
               throw std::runtime_error("EOF recieved");
            if(tmp[0] == '.' && tmp[1] == '\0')
               break;
            body.push_back(tmp);
            free(tmp);
         }
         client_->send(ml_from, rcpt_to, subject, body);

         client_->quit();
      }

      void handle_server()
      {
         char * tmp = rline::readline("smtp server: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string srv(tmp);
         free(tmp);
         rline::add_history(srv.c_str());

         tmp = rline::readline("port: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string port_str(tmp);
         free(tmp);

         uint16_t port;
         try
         {
            port = boost::lexical_cast<uint16_t>(port_str);
         }
         catch(boost::bad_lexical_cast &)
         {
            throw reset_error("Port mustbe 0..65535");
         }

         client_ = boost::in_place(srv, port);
      }

      void handle_login()
      {
         char * tmp = rline::readline("FQDN: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string fqdn(tmp);
         free(tmp);
         rline::add_history(fqdn.c_str());

         tmp = rline::readline("login: ");
         if(tmp == NULL)
            throw std::runtime_error("EOF recieved");
         std::string login(tmp);
         free(tmp);
         rline::add_history(login.c_str());

         tmp = getpass("password: ");

         client_->login(fqdn, login, tmp);
      }

      void run()
      {
         while(state_ != ST_EXIT)
         {
            try
            {
               switch(state_)
               {
               case ST_SERVER:
                  handle_server();
                  state_ = ST_LOGIN;
                 break;
               case ST_LOGIN:
                  handle_login();
                  state_ = ST_MENU;
                  break;
               case ST_MENU:
                  handle_mail();
                  state_ = ST_RESET;
                  break;
               default:
                  state_ = ST_EXIT;
               }
            }
            catch(reset_error & e)
            {
               std::cerr << e.what() << std::endl;
               state_ = ST_RESET;
            }
            catch(smtp_error & e)
            {
               std::cerr << e.what() << std::endl;
               state_ = ST_RESET;
            }
         }
      }
   private:
      boost::optional<client> client_;
      state_t state_;
   };
}
