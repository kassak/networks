#pragma once

#include "client.hpp"
#include <ncurses.h>

struct tui
{
   struct help_box
   {
      help_box()
         : wnd_(NULL)
      {
         resize();
      }

      void resize()
      {
         if(wnd_ != NULL)
            delwin(wnd_);
         int rows, cols;
         getmaxyx(stdscr, rows, cols);
         int size = 20;
         wnd_ = newwin(rows-2, size, 0, cols - size);
      }

      void update()
      {
         wclear(wnd_);

         mvwprintw(wnd_, 0, 0, "Help box:\n");
         wprintw(wnd_, "c - connect to chat room\n");
         wprintw(wnd_, "n - set nick\n");

         wrefresh(wnd_);
      }

      ~help_box()
      {
         if(wnd_ != NULL)
            delwin(wnd_);
      }

   private:
      WINDOW *wnd_;
   };

   struct user_list
   {
      user_list(std::shared_ptr<s2m::client_t> & client)
         : wnd_(NULL)
         , pos_(0)
         , client_(client)
      {
         resize();
      }

      std::string format_user_info(s2m::client_t::user_t const & user)
      {
         std::stringstream ss;
         ss << user.nick << " [" <<  inet_ntoa(user.ip) << "]";
         if(user.room_port != 0)
            ss << " -> " << inet_ntoa(user.room_address) << ":" << user.room_port;
         return ss.str();
      }

      void resize()
      {
         if(wnd_ != NULL)
            delwin(wnd_);
         int rows, cols;
         getmaxyx(stdscr, rows, cols);
         height_ = rows - 2;
         wnd_ = newwin(height_, cols, 0, 0);
      }

      void chpos(int delta)
      {
         pos_ += delta;
      }

      void update()
      {
         wclear(wnd_);

         std::vector<s2m::client_t::user_t> users;
         client_->get_users(users);
         if(pos_ + height_ - 1 > (int)users.size())
            pos_ = users.size() - height_;
         if(pos_ < 0)
            pos_ = 0;
         mvwprintw(wnd_, 0, 0, "users:");
         for(int i = 0; i < height_; ++i)
         {
            if(i+pos_ >= (int)users.size())
               break;
            mvwprintw(wnd_, i+1, 0, format_user_info(users[i+pos_]).c_str());
         }

         wrefresh(wnd_);
      }

      ~user_list()
      {
         if(wnd_ != NULL)
            delwin(wnd_);
      }

   private:
      WINDOW *wnd_;
      int pos_;
      std::shared_ptr<s2m::client_t> client_;
      int height_;
   };



   tui()
      : client_(new s2m::client_t("239.1.1.1", util::get_local_ip(util::resolve)))
   {
      wnd_ = ::initscr();
      ::cbreak();

      ulist_ = boost::in_place(boost::ref(client_));
      hbox_ = boost::in_place();
   }

   ~tui()
   {
      ::endwin();
   }

   void update()
   {
      ulist_->update();
      hbox_->update();
      ::refresh();
   }

   void print_err(std::string const & err)
   {
      int rows, cols;
      getmaxyx(stdscr, rows, cols);
      ::move(rows-1, 0);
      ::clrtoeol();
      ::printw(err.c_str());
   }

   bool query(std::string const & pref, std::string & res)
   {
      int rows, cols;
      getmaxyx(stdscr, rows, cols);
      ::move(rows - 1, 0);
      ::printw(pref.c_str());
      ::refresh();
      char nick[127];
      ::nocbreak();
      int r = ::getnstr(nick, 127);
      ::cbreak();
      if(r == OK)
      {
         res = std::string(nick);
         ::move(rows-1, 0);
         ::clrtoeol();
         ::refresh();
      }
      else
      {
         print_err("Error: too long");
      }
      return r == OK;
   }

   struct options_box
   {
      options_box(std::unordered_map<size_t, std::string> const & stuff)
         : wnd_(NULL)
         , stuff_(stuff)
      {
         resize();
      }

      void resize()
      {
         if(wnd_ != NULL)
            delwin(wnd_);
         int cols = getmaxx(stdscr);
         wnd_ = newwin(stuff_.size(), cols, 0, 0);
         wbkgdset(wnd_, ' '|A_REVERSE);
      }

      void update()
      {
         wclear(wnd_);

         int i = 0;
         for(auto const & t : stuff_)
         {
            mvwprintw(wnd_, i, 0, "%i - %s\n", t.first, t.second.c_str());
            ++i;
         }

         wrefresh(wnd_);
      }

      ~options_box()
      {
         if(wnd_ != NULL)
            delwin(wnd_);
      }

   private:
      WINDOW *wnd_;
      std::unordered_map<size_t, std::string> const & stuff_;
   };

   size_t select_option(std::string const & msg, std::unordered_map<size_t, std::string> const & stuff)
   {
      options_box ob(stuff);
      //ob.update();
      //::refresh();
      std::string prefix;
      std::string res;
      while(true)
      {
         update();
         ob.update();
         if(query(prefix + msg, res))
         {
            std::stringstream ss(res);
            size_t num;
            if(ss >> num)
            {
               if(stuff.find(num) != stuff.end())
                  return num;
               else
                  prefix = "(Error. Not in options) ";
            }
            else
               prefix = "(Error. Not a number) ";
         }
         else
            prefix = "(Error. Too long) ";
      }
   }

   void configure_api()
   {
      streamer_t ss;//temporary
      api_ = select_option("Select api: ", ss.apis());
      ss.init(api_);
      inp_dev_ = select_option("Select input: ", ss.devices());
      outp_dev_ = select_option("Select output: ", ss.devices());

      client_->set_devices(api_, inp_dev_, outp_dev_);
   }

   void change_nick()
   {
      std::string nick;
      if(query("New nick: ", nick))
         client_->set_nick(nick);
   }

   void connect()
   {
      std::string ip, sport;
      if(!query("Chat room IP: ", ip))
         return;
      in_addr addr;
      if(inet_aton(ip.c_str(), &addr) == 0)
      {
         print_err("Error: invalid IP");
         return;
      }
      if(!query("Chat room port: ", sport))
         return;
      std::stringstream ss(sport);
      uint16_t port;
      if(!(ss >> port))
      {
         print_err("Error: invalid port");
         return;
      }
      client_->set_room(addr, port);
   }

   void run()
   {
      configure_api();

      update();
      while(true)
      {
         client_->do_stuff();
         update();
         sleep(s2m::PROCESS_PERIOD);

         ::nodelay(wnd_, true);
         int ch = ::getch();
         ::nodelay(wnd_, false);
         switch(ch)
         {
         case KEY_UP:
            ulist_->chpos(-1);
            break;
         case KEY_DOWN:
            ulist_->chpos(1);
            break;
         case 'n':
            change_nick();
            break;
         case 'c':
            connect();
            break;
         }
      }
   }

private:
   boost::optional<user_list> ulist_;
   boost::optional<help_box> hbox_;
   std::shared_ptr<s2m::client_t> client_;
   WINDOW * wnd_;
   int api_, inp_dev_, outp_dev_;
};
