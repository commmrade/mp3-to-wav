#include <fstream>
#include <iostream>
#include <stdexcept>

extern "C" {
#include <libavutil/samplefmt.h>
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_par.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <print>


int main(int, char**){
    AVFormatContext* format_context = avformat_alloc_context();
    if (avformat_open_input(&format_context, "song.mp3", nullptr, nullptr) < 0) {
        throw std::runtime_error("Could not open input");
    }
    std::println("Song info: long name: {}", format_context->iformat->long_name);
    avformat_find_stream_info(format_context, nullptr); // For example, videos may contain 2 streams (audio + video)
    
    AVCodecParameters* codec_params = nullptr;
    const AVCodec* codec = nullptr;
    int audio_stream_index = 0;

    for (auto i = 0; i < format_context->nb_streams; ++i) {
        auto* tempCodecParams = format_context->streams[i]->codecpar;
        const auto* tempCodec = avcodec_find_decoder(tempCodecParams->codec_id);

        if (tempCodecParams->codec_type == AVMEDIA_TYPE_AUDIO) { // Find audio stream and get its codec params and codec
            codec_params = format_context->streams[i]->codecpar;
            codec = avcodec_find_decoder(codec_params->codec_id);
            audio_stream_index = i;
            std::println("Audio stream: sample rate: {}, channels: {}, audio format: {}", codec_params->sample_rate, codec_params->ch_layout.nb_channels, av_get_sample_fmt_name((AVSampleFormat)codec_params->format));
        }
    }
    if (!codec) {
        throw std::runtime_error("Could not find CODEC for audio");
    }

    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (!context) {
        throw std::runtime_error("Could not alloc context");
    }
    if (avcodec_parameters_to_context(context, codec_params) < 0) {
        throw std::runtime_error("Could not add parameters to context");
    }

    if (avcodec_open2(context, codec, nullptr) < 0) {
        throw std::runtime_error("Could not open codec");
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    std::ofstream outfile("out.raw", std::ios_base::binary);


    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index != audio_stream_index) {
            continue;
        }

        int response = avcodec_send_packet(context, packet);
        if (response < 0) {
            throw std::runtime_error{"Error sending packet"};
        }

        while (response >= 0) {
            response = avcodec_receive_frame(context, frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                break;
            }          
            else if (response < 0) {
                throw std::runtime_error{"Error recv frame"};
            }

            // Since its planar we need to use frame->data[0..channels]
            if (frame->format == AV_SAMPLE_FMT_FLTP) {
                float** samples = reinterpret_cast<float**>(frame->data);

                // av_get_bytes_per_sample((AVSampleFormat)frame->format);
                for (auto i = 0; i < frame->nb_samples; ++i) {
                    for (auto j = 0; j < frame->ch_layout.nb_channels; ++j) {
                        outfile.write(reinterpret_cast<const char*>(&samples[j][i]), sizeof(float));
                    }
                }
            }
        }
        av_packet_unref(packet);
    }


    avformat_close_input(&format_context);
    avformat_free_context(format_context);
}
