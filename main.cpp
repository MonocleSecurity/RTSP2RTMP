///// Includes /////

#include "main.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
}

///// Globals /////

AVFormatContext* rtspformatcontext = nullptr;
AVFormatContext* rtmpcontext = nullptr;
AVPacket* packet = nullptr;

///// Functions /////

int Run(char** argv)
{
  // Create RTSP feed
  std::cout << "Opening RTSP stream" << std::endl;
  const std::string rtspuri(argv[1]);
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "rtsp_transport", "tcp", 0); // Usually more reliable to use TCP interleaved
  if (avformat_open_input(&rtspformatcontext, rtspuri.c_str(), nullptr, &opts) != 0)
  {
    std::cout << "Failed to create RTSP stream" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "OpeningRetrieving RTSP streams" << std::endl;
  if (avformat_find_stream_info(rtspformatcontext, nullptr) < 0)
  {
    std::cout << "Failed to open RTSP stream" << std::endl;
    return EXIT_FAILURE;
  }
  // Find the H264 video stream
  std::cout << "Parsing RTSP streams" << std::endl;
  std::optional<unsigned int> audio_stream;
  std::optional<unsigned int> video_stream;
  for (unsigned int i = 0; i < rtspformatcontext->nb_streams; ++i)
  {
    if ((rtspformatcontext->streams[i] == nullptr) || (rtspformatcontext->streams[i]->codecpar == nullptr))
    {
      // Just ignore
      return EXIT_FAILURE;
    }
    if (!audio_stream.has_value() && (rtspformatcontext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))
    {
      audio_stream = i;
    }
    if (!video_stream.has_value() && (rtspformatcontext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
    {
      video_stream = i;
    }
  }
  if (!video_stream.has_value())
  {
    std::cout << "Failed to find RTSP video stream" << std::endl;
    return EXIT_FAILURE;
  }
  // Start collecting packets
  packet = av_packet_alloc();
  if (packet == nullptr)
  {
    std::cout << "Failed to allocate packet" << std::endl;
    return EXIT_FAILURE;
  }
  packet->dts = 0;
  packet->pts = 0;
  // Setup RTMP stream
  const std::string rtmpurl(argv[2]);
  std::cout << "Creating RTMP context" << std::endl;
  if (avformat_alloc_output_context2(&rtmpcontext, nullptr, "flv", rtmpurl.c_str()) < 0)
  {
    std::cout << "Failed to create RTMP context" << std::endl;
    return EXIT_FAILURE;
  }
  // Initialise video codec
  const AVCodec* codec = avcodec_find_decoder(rtspformatcontext->streams[*video_stream]->codecpar->codec_id);
  if (codec == nullptr)
  {
    std::cout << "Failed to find video decoder" << std::endl;
    return EXIT_FAILURE;
  }
  AVStream* videooutstream = avformat_new_stream(rtmpcontext, codec);
  if (videooutstream == nullptr)
  {
    std::cout << "Failed to create RTMP video stream" << std::endl;
    return EXIT_FAILURE;
  }
  if (avcodec_parameters_copy(videooutstream->codecpar, rtspformatcontext->streams[*video_stream]->codecpar) < 0)
  {
    std::cout << "Failed to copy video stream parameters" << std::endl;
    return EXIT_FAILURE;
  }
  // Initialise audio codec
  AVStream* audiooutstream = nullptr;
  if (audio_stream.has_value())
  {
    const AVCodec* avcodecaudio = avcodec_find_decoder(rtspformatcontext->streams[*audio_stream]->codecpar->codec_id);
    if (avcodecaudio == nullptr)
    {
      std::cout << "Failed to find audio decoder" << std::endl;
      return EXIT_FAILURE;
    }
    audiooutstream = avformat_new_stream(rtmpcontext, avcodecaudio);
    if (audiooutstream == nullptr)
    {
      std::cout << "Failed to create RTMP audio stream" << std::endl;
      return EXIT_FAILURE;
    }
    if (avcodec_parameters_copy(audiooutstream->codecpar, rtspformatcontext->streams[*audio_stream]->codecpar) < 0)
    {
      std::cout << "Failed to copy audio stream parameters" << std::endl;
      return EXIT_FAILURE;
    }
  }
  // Set
  rtspformatcontext->streams[*video_stream]->time_base.num = 1;
  rtspformatcontext->streams[*video_stream]->time_base.den = 1000;
  if (audio_stream.has_value())
  {
    rtspformatcontext->streams[*audio_stream]->time_base.num = 1;
    rtspformatcontext->streams[*audio_stream]->time_base.den = 1000;
  }
  // Open RTMP output IO
  std::cout << "Opening RTMP stream" << std::endl;
  if (avio_open(&rtmpcontext->pb, rtmpurl.c_str(), AVIO_FLAG_WRITE))
  {
    std::cout << "Failed to open RTMP stream" << std::endl;
    return EXIT_FAILURE;
  }
  if (avformat_write_header(rtmpcontext, nullptr) < 0)
  {
    std::cout << "Failed to write RTMP stream header" << std::endl;
    return EXIT_FAILURE;
  }
  // Stream
  std::cout << "Starting streaming" << std::endl;
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  while (true)
  {
    if (av_read_frame(rtspformatcontext, packet) < 0)
    {
      std::cout << "Failed to read RTSP frame" << std::endl;
      return EXIT_FAILURE;
    }
    const int64_t time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    packet->pts = time;
    packet->dts = time;
    if ((packet->stream_index == *video_stream))
    {
      packet->stream_index = videooutstream->index;
    }
    else if (audiooutstream && audio_stream.has_value() && (packet->stream_index == *audio_stream))
    {
      packet->stream_index = audiooutstream->index;
    }
    else
    {
      // Ignore
      continue;
    }
    if (av_interleaved_write_frame(rtmpcontext, packet))
    {
      std::cout << "Failed to write RTMP frame" << std::endl;
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
  // Param check
  if (argc < 3)
  {
    std::cout << "./rtsp2rtmp rtsp://admin:password@192.168.1.2/stream rtmp://a.rtmp.youtube.com/live2/<key>" << std::endl;
    return EXIT_FAILURE;
  }
  // Run
  const int ret = Run(argv);
  // Cleanup
  if (rtspformatcontext)
  {
    avformat_free_context(rtspformatcontext);
    rtspformatcontext = nullptr;
  }
  if (rtmpcontext)
  {
    avformat_free_context(rtmpcontext);
    rtmpcontext = nullptr;
  }
  if (packet)
  {
    av_packet_free(&packet);
    packet = nullptr;
  }
  return ret;
}
