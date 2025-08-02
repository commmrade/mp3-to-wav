#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

extern "C" {
#include <libavformat/avio.h>
#include <libavcodec/codec_id.h>
#include <libavutil/channel_layout.h>
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

struct InputAudio {
    AVFormatContext* fmt_ctx{nullptr};

    AVCodecParameters* codec_params{nullptr};
    AVCodecContext* c_context{nullptr};
    const AVCodec* codec{nullptr};

    int audio_stream_index{-1};

    ~InputAudio() {
        avcodec_free_context(&c_context);
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
    }
};
void load_audio(InputAudio& audio_info, const char* filename) {
    audio_info.fmt_ctx = avformat_alloc_context();
    if (avformat_open_input(&audio_info.fmt_ctx, filename, nullptr, nullptr) < 0) {
        throw std::runtime_error("Could not open input");
    }
    avformat_find_stream_info(audio_info.fmt_ctx, nullptr); // For example, videos may contain 2 streams (audio + video)

    for (auto i = 0; i < audio_info.fmt_ctx->nb_streams; ++i) {
        auto* tempCodecParams = audio_info.fmt_ctx->streams[i]->codecpar;
        const auto* tempCodec = avcodec_find_decoder(tempCodecParams->codec_id);

        if (tempCodecParams->codec_type == AVMEDIA_TYPE_AUDIO) { // Find audio stream and get its codec params and codec
            audio_info.codec_params = tempCodecParams;
            audio_info.codec = tempCodec;
            audio_info.audio_stream_index = i;
        }
    }
    if (!audio_info.codec) {
        throw std::runtime_error("Could not find CODEC for audio");
    }

    audio_info.c_context = avcodec_alloc_context3(audio_info.codec);
    if (!audio_info.c_context) {
        throw std::runtime_error("Could not alloc context");
    }
    if (avcodec_parameters_to_context(audio_info.c_context, audio_info.codec_params) < 0) {
        throw std::runtime_error("Could not add parameters to context");
    }
    if (avcodec_open2(audio_info.c_context, audio_info.codec, nullptr) < 0) {
        throw std::runtime_error("Could not open codec");
    }
}


void decode_to_file(InputAudio& i_audio, AVPacket* packet, AVFrame* frame, const char* filename) {
    std::ofstream outfile("out.raw", std::ios_base::binary);
    if (!outfile.is_open()) {
        exit(1);
    }

    while (av_read_frame(i_audio.fmt_ctx, packet) >= 0) {
        int response = avcodec_send_packet(i_audio.c_context, packet);
        if (response < 0) {
            throw std::runtime_error{"Error sending packet"};
        }

        while (response >= 0) {
            response = avcodec_receive_frame(i_audio.c_context, frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                break;
            }          
            else if (response < 0) {
                throw std::runtime_error{"Error recv frame"};
            }

            // Since its planar we need to use frame->data[0..channels]
            if (frame->format == AV_SAMPLE_FMT_FLTP) {
                float** samples = reinterpret_cast<float**>(frame->data);
                for (auto i = 0; i < frame->nb_samples; ++i) {
                    for (auto j = 0; j < frame->ch_layout.nb_channels; ++j) {
                        outfile.write(reinterpret_cast<const char*>(&samples[j][i]), sizeof(float));
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    outfile.flush();
    outfile.close();
}

void convert_to_raw_from_non_raw(InputAudio& i_audio)  {
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    decode_to_file(i_audio, packet, frame, "out.raw");
    
    av_packet_free(&packet);
    av_frame_free(&frame);
}

void convert_to_wav_from_non_raw(AVFormatContext* input_fmt_ctx, AVCodecContext* input_ctx, int audio_stream_index) {
    const char* filename = "out.wav";
    // Follow mux.c on github example ffmpeg
}

int main(int, char**){

    // TODO разбить код на функции структуры и тд
    InputAudio audio_info{};
    load_audio(audio_info, "song2.mp3"); 
    
    convert_to_raw_from_non_raw(audio_info);
    // convert_to_wav_from_non_raw(format_context, context, audio_stream_index);
}
