#pragma once
#include "common/udp.hpp"
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

   streamer_t(std::string const & host, uint16_t port/*, in_addr const & local_address*/)
   {
      data_source_.connect(host, port);
      data_source_.join_group(true);
   }

   ~streamer_t()
   {
      if(rtaudio_)
         try
         {
            rtaudio_->closeStream();
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
      opts.flags = 0;//RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
      opts.numberOfBuffers = 3;

      try
      {
         rtaudio_->openStream(&outparams, &inparams, format, SAMPLE_RATE, &nframes, &streamer_t::callback, this, &opts);
         rtaudio_->startStream();
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
      return rtaudio_->getCurrentApi();
   }

   std::unordered_map<size_t, std::string> devices()
   {
      std::unordered_map<size_t, std::string> devs;
      //std::vector<RtAudio::DeviceInfo> dev(rtaudio_->getDeviceCount());
      logger::trace() << "Devices: " << rtaudio_->getDeviceCount();
      for(size_t i = 0; i < rtaudio_->getDeviceCount(); ++i)
         devs[i] = rtaudio_->getDeviceInfo(i).name;
      return devs;
   }


   struct frame_t
   {
      enum ftype
      {
         SOUND,
      };
      enum {DATA_SIZE = 10240};

      in_addr source;
      char data[DATA_SIZE];
   };

   int ready(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
   {
      char* input  = reinterpret_cast<char*>(inputBuffer);
      char* output = reinterpret_cast<char*>(outputBuffer);
      double energy = 0;
      for(size_t i = 0; i < nFrames; ++i)
      {
         if(output) output[i] = 127*std::sin((i/double(nFrames))*440);
         if(input) energy += util::sqr(input[i]/127.0), input[i] = 0;
      }
      energy /= nFrames;
      if(status == RTAUDIO_INPUT_OVERFLOW)
         logger::warning() << "RTAUDIO_INPUT_OVERFLOW";
      if(status == RTAUDIO_OUTPUT_UNDERFLOW)
         logger::warning() << "RTAUDIO_OUTPUT_UNDERFLOW";
      logger::trace() << "streamer::ready " << streamTime << " " << nFrames << " energy: " << std::string(int(energy*40), '*');

      return 0;
   }
private:
   static int callback(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime,
      RtAudioStreamStatus status, void *streamer)
   {
      return reinterpret_cast<streamer_t*>(streamer)->ready(outputBuffer, inputBuffer, nFrames, streamTime, status);
   }

private:
   udp::socket_t data_source_;
   in_addr local_address_;
   boost::optional<RtAudio> rtaudio_;
};

