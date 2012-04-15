#pragma once
#include "common/udp.hpp"
#include "common/net_stuff.hpp"
#include <queue>
#include <stk/RtAudio.h>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

struct streamer_t
{
   struct error : std::runtime_error
   {
      error(std::string const & what)
         : std::runtime_error(what)
      {
      }
   };

   static const size_t SAMPLE_RATE = 44100;
   static const size_t MAX_QUEUE = 5;
   static const size_t ACCEPTABLE_SYN_DESYNC = 10;
   static const size_t DOWN_SAMPLE = 7;

   streamer_t(std::string const & host, uint16_t port/*, in_addr const & local_address*/)
      : syn_(0)
      , internal_offset_(0)
   {
      data_source_.connect(host, port);
      data_source_.join_group(true);
      data_source_.set_echo(true);
      data_source_.bind();

      input_frame_.offset = 0;
      output_frame_.offset = frame_t::DATA_SIZE;
      //input_frame_.source = local_address_;
   }

   ~streamer_t()
   {
      if(rtaudio_)
         try
         {
            if(rtaudio_->in.isStreamRunning())
               rtaudio_->in.stopStream();
            if(rtaudio_->out.isStreamRunning())
               rtaudio_->out.stopStream();
            if(rtaudio_->in.isStreamOpen())
               rtaudio_->in.closeStream();
            if(rtaudio_->out.isStreamOpen())
               rtaudio_->out.closeStream();
         }
         catch(RtError &e)
         {
            throw error(e.getMessage());
         }
   }

   void init(size_t api = RtAudio::UNSPECIFIED)
   {
      if(rtaudio_)
         throw std::logic_error("Already running");
      rtaudio_ = boost::in_place((RtAudio::Api)api);
   }


   void run(size_t input_device_id, size_t output_device_id)
   {
      RtAudio::StreamParameters inparams;
      inparams.deviceId = input_device_id;
      inparams.nChannels = 1;

      RtAudio::StreamParameters outparams;
      outparams.deviceId = output_device_id;
      outparams.nChannels = 1;

      RtAudioFormat format = RTAUDIO_SINT8;
      size_t nframes = frame_t::DATA_SIZE;
      RtAudio::StreamOptions opts;
      opts.flags = RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
      opts.numberOfBuffers = 3;

      try
      {
         rtaudio_->in.openStream(NULL, &inparams, format, SAMPLE_RATE, &nframes, &streamer_t::callback_in, this, &opts);
         rtaudio_->in.startStream();
         rtaudio_->out.openStream(&outparams, NULL, format, SAMPLE_RATE, &nframes, &streamer_t::callback_out, this, &opts);
         rtaudio_->out.startStream();
      }
      catch(RtError & e)
      {
         throw error(e.getMessage());
      }

   }

   static const char * apistr(RtAudio::Api api)
   {
      switch(api)
      {
         case RtAudio::UNSPECIFIED:   return "Auto";
         case RtAudio::LINUX_ALSA:    return "The Advanced Linux Sound Architecture API.";
         case RtAudio::LINUX_OSS:     return "The Linux Open Sound System API.";
         case RtAudio::UNIX_JACK:     return "The Jack Low-Latency Audio Server API.";
         case RtAudio::MACOSX_CORE:   return "Macintosh OS-X Core Audio API.";
         case RtAudio::WINDOWS_ASIO:  return "The Steinberg Audio Stream I/O API.";
         case RtAudio::WINDOWS_DS:    return "The Microsoft Direct Sound API.";
         case RtAudio::RTAUDIO_DUMMY: return "A compilable but non-functional API.";
         default:                     return "Fucking unknown";
      }
   }

   std::unordered_map<size_t, std::string> apis() const
   {
      std::unordered_map<size_t, std::string> apis;
      std::vector<RtAudio::Api> api;
      RtAudio::getCompiledApi(api);
      for(auto a : api)
         apis.insert(std::make_pair(a, apistr(a)));
      return apis;
   }

   size_t api()
   {
      return rtaudio_->in.getCurrentApi();
   }

   std::unordered_map<size_t, std::string> devices()
   {
      std::unordered_map<size_t, std::string> devs;
      //std::vector<RtAudio::DeviceInfo> dev(rtaudio_->getDeviceCount());
      logger::trace() << "Devices: " << rtaudio_->in.getDeviceCount();
      for(size_t i = 0; i < rtaudio_->in.getDeviceCount(); ++i)
         devs[i] = rtaudio_->in.getDeviceInfo(i).name;
      return devs;
   }


   struct frame_t
   {
      bool operator < (frame_t const & other) const
      {
         return syn < other.syn;
      }

      enum ftype
      {
         SOUND,
      };
      enum {DATA_SIZE = 1024};

      ftype type;
      size_t syn;
      in_addr source;
      char data[DATA_SIZE];
   };

   struct partial_frame_t
   {
      frame_t frame;
      size_t offset;
      size_t internal_offset;
   };

   bool send_frame(partial_frame_t & frame)
   {
      pollfd pfd;
      pfd.fd = *data_source_;
      pfd.events = POLLOUT;
      if(util::poll<error>(&pfd, 1, 0)) // TODO: use offset and partial writing. assert for now?
      {
         size_t cnt = data_source_.send(&frame.frame, 1);
         assert(cnt == sizeof(frame_t));
         frame.offset += cnt;
         logger::trace() << "streamer::send_frame";
         return true;
      }
      return false;
   }

   void send_frames()
   {
      while(!send_queue_.empty())
      {
         if(!send_frame_)
         {
            send_frame_ = boost::in_place();
            send_frame_->offset = 0;
            send_frame_->frame = send_queue_.front();
            send_queue_.pop_front();
         }

         if(send_frame(*send_frame_))
         {
            send_frame_.reset();
            continue;
         }
         else
            break;
      }
   }

   void playback(frame_t const & frame)
   {
      if(syn_ > ACCEPTABLE_SYN_DESYNC && frame.syn < syn_ - ACCEPTABLE_SYN_DESYNC) // too late
      {
         logger::warning() << "streamer::playback: dropping frame";
         return;
      }
      if(frame.syn > syn_ + ACCEPTABLE_SYN_DESYNC) // i am slowpoke
      {
         logger::warning() << "streamer::playback: resynchronization";
         syn_ = frame.syn;
      }
      frame_queue_t::iterator it = std::lower_bound(playback_queue_.begin(), playback_queue_.end(), frame);
      if(it == playback_queue_.end() || it->syn != frame.syn)
      {
         playback_queue_.insert(it, frame);
      }
      else
      {
         for(size_t i = 0; i < frame_t::DATA_SIZE; ++i)
            it->data[i] = util::bound((int)it->data[i] + (int)frame.data[i], -127, 127);
      }
      if(syn_ > ACCEPTABLE_SYN_DESYNC)
         playback_queue_.erase(
            std::remove_if(playback_queue_.begin(), playback_queue_.end(), [syn_, ACCEPTABLE_SYN_DESYNC](frame_t const & fr)->bool{return fr.syn < syn_ - ACCEPTABLE_SYN_DESYNC;}),
            playback_queue_.end()
            );
   }

   bool recv_frame(partial_frame_t & frame)
   {
      pollfd pfd;
      pfd.fd = *data_source_;
      pfd.events = POLLIN;
      if(util::poll<error>(&pfd, 1, 0)) // TODO: use offset and partial reading. assert for now?
      {
         size_t cnt = data_source_.recv(&frame.frame, 1);
         assert(cnt == sizeof(frame_t));
         frame.offset += cnt;
         logger::trace() << "streamer::recv_frame";
         return true;
      }
      return false;
   }

   void recv_frames()
   {
      while(true)
      {
         if(!recv_frame_)
         {
            recv_frame_ = boost::in_place();
            recv_frame_->offset = 0;
         }

         if(recv_frame(*recv_frame_))
         {
            playback(recv_frame_->frame);
            recv_frame_.reset();
            continue;
         }
         else
            break;
      }
   }

   int in_ready(void *in_buf, size_t nframes, double stream_time, RtAudioStreamStatus status)
   {
      char* input  = reinterpret_cast<char*>(in_buf);
      double energy = util::energy(input, nframes);
      if(status == RTAUDIO_INPUT_OVERFLOW)
         logger::warning() << "RTAUDIO_INPUT_OVERFLOW";
      logger::trace() << "streamer::in_ready " << stream_time << " " << nframes << " energy: " << energy << std::string(int(energy*40), '*');

      size_t offset = 0;
      while(offset + DOWN_SAMPLE <= nframes)
      {
//         size_t cnt = util::min(frame_t::DATA_SIZE - input_frame_.offset, nframes - offset);
//         std::copy(input + offset, input + offset + cnt, input_frame_.frame.data + input_frame_.offset);
//         input_frame_.offset += cnt;
         size_t cnt = util::min((frame_t::DATA_SIZE - input_frame_.offset), (nframes - offset)/DOWN_SAMPLE);
         for(size_t i = 0; i < cnt; ++i)
            input_frame_.frame.data[input_frame_.offset + i] = input[offset + DOWN_SAMPLE*i];
         input_frame_.offset += cnt;
         offset += DOWN_SAMPLE*cnt;
         if(input_frame_.offset == frame_t::DATA_SIZE)
         {
            send_queue_.push_back(input_frame_.frame);
            input_frame_.offset = 0;
            input_frame_.frame.type = frame_t::SOUND;
            input_frame_.frame.syn = syn_;
            syn_ += 1;
            if(send_queue_.size() > MAX_QUEUE)
               while(send_queue_.size() > MAX_QUEUE/2)
                  send_queue_.pop_front();
         }
      }

      logger::trace() << "streamer::in_ready: queue size: " << send_queue_.size();

      send_frames();

      return 0;
   }

   void interpolate(char * out, char from, char to, size_t offs, size_t cnt)
   {
      for(size_t i = offs; i < offs + cnt; ++i)
         out[i] = from + (to - from)/(double)DOWN_SAMPLE + 0.5;
   }

   int out_ready(void *out_buf, size_t nframes, double stream_time, RtAudioStreamStatus status)
   {
      char* output = reinterpret_cast<char*>(out_buf);
      if(status == RTAUDIO_OUTPUT_UNDERFLOW)
         logger::warning() << "streamer::out_ready RTAUDIO_OUTPUT_UNDERFLOW";
      logger::trace() << "streamer::out_ready " << stream_time << " " << nframes << "sps: " << rtaudio_->out.getStreamSampleRate();

      recv_frames();
      size_t offset = 0;
      while(offset < nframes)
      {
         if(output_frame_.offset == frame_t::DATA_SIZE)
         {
            if(playback_queue_.empty())
            {
               logger::warning() << "streamer::out_ready no frames";
               return 0;
            }
            output_frame_.frame = playback_queue_.front();
            logger::trace() << "streamer::out_ready frame energy: " << util::energy(output_frame_.frame.data, frame_t::DATA_SIZE);
            playback_queue_.pop_front();
            output_frame_.offset = 0;
         }
//         size_t cnt = util::min(frame_t::DATA_SIZE - output_frame_.offset, nframes - offset);
//         std::copy(output_frame_.frame.data + output_frame_.offset, output_frame_.frame.data + output_frame_.offset + cnt, output + offset);
//         output_frame_.offset += cnt;
         size_t cnt = util::min((frame_t::DATA_SIZE - output_frame_.offset), (nframes - offset + internal_offset_)/DOWN_SAMPLE);
         if(cnt == 0)
         {
            interpolate(&output[offset],
                        played_,
                        output_frame_.frame.data[output_frame_.offset],
                        internal_offset_, nframes - offset - internal_offset_
                        );
            offset = nframes;
            internal_offset_ = nframes - offset;
         }
         else
         {
            interpolate(&output[offset],
                        played_,
                        output_frame_.frame.data[output_frame_.offset],
                        internal_offset_, DOWN_SAMPLE - internal_offset_
                        );
            played_ = output_frame_.frame.data[output_frame_.offset];
            for(size_t i = 1; i < cnt; ++i)
            {
               interpolate(&output[offset - internal_offset_ + i*DOWN_SAMPLE],
                           output_frame_.frame.data[output_frame_.offset + i - 1],
                           output_frame_.frame.data[output_frame_.offset + i],
                           0, DOWN_SAMPLE
                           );
               played_ = output_frame_.frame.data[output_frame_.offset + i];
            }
         }
         output_frame_.offset += cnt;
         offset += cnt*DOWN_SAMPLE - internal_offset_;
         internal_offset_ = 0;
      }
//      for(size_t i = 0; i < nframes; ++i)
//         output[i] = rand();

      return 0;
   }
private:
   static int callback_in(void *out_buf, void *in_buf, unsigned int nframes, double stream_time,
      RtAudioStreamStatus status, void *streamer)
   {
//      return 0;
      assert(out_buf == NULL);
      return reinterpret_cast<streamer_t*>(streamer)->in_ready(in_buf, nframes, stream_time, status);
   }

   static int callback_out(void *out_buf, void *in_buf, unsigned int nframes, double stream_time,
      RtAudioStreamStatus status, void *streamer)
   {
//      return 0;
      assert(in_buf == NULL);
      return reinterpret_cast<streamer_t*>(streamer)->out_ready(out_buf, nframes, stream_time, status);
   }

   struct io_control
   {
      io_control(RtAudio::Api api)
         : in(api)
         , out(api)
      {
      }

      RtAudio in;
      RtAudio out;
   };

   typedef
      std::list<frame_t> frame_queue_t;
private:
   udp::socket_t data_source_;
   in_addr local_address_;
   boost::optional<io_control> rtaudio_;

   frame_queue_t send_queue_;
   frame_queue_t playback_queue_;
   partial_frame_t input_frame_;
   partial_frame_t output_frame_;
   boost::optional<partial_frame_t> send_frame_;
   boost::optional<partial_frame_t> recv_frame_;
   size_t syn_;
   char played_;
   size_t internal_offset_;
};
