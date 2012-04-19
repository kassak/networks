#pragma once

#include "client.hpp"
#include <ncurses.h>

struct tui
{
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

      void update()
      {
         wclear(wnd_);

         std::vector<s2m::client_t::user_t> users;
         client_->get_users(users);
         if(pos_ + height_ - 1 > (int)users.size())
            pos_ = users.size() - height_;
         if(pos_ < 0)
            pos_ = 0;
         mvprintw(0, 0, "users:");
         for(int i = 0; i < height_; ++i)
         {
            if(i+pos_ >= (int)users.size())
               break;
            mvprintw(i+1, 0, format_user_info(users[i+pos_]).c_str());
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
      ::initscr();

      ulist_ = boost::in_place(boost::ref(client_));
   }

   ~tui()
   {
      ::endwin();
   }

   void run()
   {
      while(true)
      {
         client_->do_stuff();
         ulist_->update();
         ::refresh();
         sleep(s2m::PROCESS_PERIOD);
      }
   }

private:
   boost::optional<user_list> ulist_;
   std::shared_ptr<s2m::client_t> client_;
};
