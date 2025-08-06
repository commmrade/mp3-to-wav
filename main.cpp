#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <libavutil/rational.h>
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

struct OutputAudio {
    AVFormatContext* fmt_ctx{nullptr};
    AVStream* st{nullptr};
    AVCodecContext* codec_ctx{nullptr};
    const AVCodec* codec{nullptr};


    ~OutputAudio() {
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


void add_stream(OutputAudio& o_audio, const AVCodecParameters* input_params) {
    o_audio.codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!o_audio.codec) {
        throw std::runtime_error("Codec wasn't found");
    }

    o_audio.st = avformat_new_stream(o_audio.fmt_ctx, o_audio.codec);
    if (!o_audio.st) {
        throw std::runtime_error("Could not make a new stream");
    }

    o_audio.codec_ctx = avcodec_alloc_context3(o_audio.codec);
    if (!o_audio.codec_ctx) {
        throw std::runtime_error("Could not alloc codec context");
    }

    // Match output properties to input
    o_audio.codec_ctx->sample_rate = input_params->sample_rate;
    av_channel_layout_copy(&o_audio.codec_ctx->ch_layout, &input_params->ch_layout);
    o_audio.codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16; // PCM_S16LE uses signed 16-bit samples

    // Set time base based on sample rate
    o_audio.st->time_base = (AVRational){1, o_audio.codec_ctx->sample_rate};

    // Open the codec after setting properties
    if (avcodec_open2(o_audio.codec_ctx, o_audio.codec, nullptr) < 0) {
        throw std::runtime_error("Could not open encoder");
    }

    // Copy codec parameters to stream
    if (avcodec_parameters_from_context(o_audio.st->codecpar, o_audio.codec_ctx) < 0) {
        throw std::runtime_error("Could not copy codec parameters to stream");
    }
}

void convert_to_wav_from_non_raw(InputAudio& i_audio) {
    const char* filename = "out.wav";

    OutputAudio o_audio{};

    // Allocate output (muxing) context
    o_audio.fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&o_audio.fmt_ctx, nullptr, "wav", filename) < 0) {
        throw std::runtime_error("Could not alloc output wav context");
    }

    // Set up format
    const AVOutputFormat* fmt = nullptr;
    fmt = o_audio.fmt_ctx->oformat;

    // Setup codec
    add_stream(o_audio, i_audio.codec_params);

    av_dump_format(o_audio.fmt_ctx, 0, filename, 1);

    // Open output stream
    if (avio_open(&o_audio.fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
        throw std::runtime_error("Could not open avio stream");
    }
    if (avformat_write_header(o_audio.fmt_ctx, nullptr) < 0) {
        throw std::runtime_error("Could not write header");
    }

    // Write audio
    {
        AVFrame* ifr = av_frame_alloc();
        AVPacket* ipkt = av_packet_alloc();
        AVFrame* ofr = av_frame_alloc();

        AVPacket* opkt = av_packet_alloc();

        int64_t next_pts = 0;
        while (av_read_frame(i_audio.fmt_ctx, ipkt) >= 0) {

            int resp = avcodec_send_packet(i_audio.c_context, ipkt);
            if (resp < 0) {
                throw std::runtime_error("Error sending packet to decoder");
            }

            while (resp >= 0) {
                resp = avcodec_receive_frame(i_audio.c_context, ifr);
                if (resp == AVERROR(EAGAIN) || resp == AVERROR_EOF) {
                    break;
                } else if (resp < 0) {
                    throw std::runtime_error("Error receiving frame from decoder");
                }

                if (ifr->format == AV_SAMPLE_FMT_FLTP) {
                    // Set output frame properties
                    ofr->nb_samples = ifr->nb_samples;
                    ofr->format = AV_SAMPLE_FMT_S16;
                    ofr->sample_rate = ifr->sample_rate;
                    av_channel_layout_copy(&ofr->ch_layout, &ifr->ch_layout);
                    ofr->pts = next_pts;

                    // Allocate buffer
                    if (av_frame_get_buffer(ofr, 0) < 0) {
                        throw std::runtime_error("Failed to allocate output frame buffer");
                    }

                    // Ensure frame is writable
                    if (av_frame_make_writable(ofr) < 0) {
                        throw std::runtime_error("Failed to make output frame writable");
                    }

                    // Verify channel consistency
                    if (ifr->ch_layout.nb_channels != ofr->ch_layout.nb_channels) {
                        throw std::runtime_error("Channel layout mismatch");
                    }

                    // Convert samples
                    int16_t* dst = (int16_t*)ofr->data[0];
                    int nb_channels = ofr->ch_layout.nb_channels;
                    for (int i = 0; i < ofr->nb_samples; ++i) {
                        for (int ch = 0; ch < nb_channels; ++ch) {
                            float sample = ((float*)ifr->data[ch])[i];
                            sample = std::max(-1.0f, std::min(1.0f, sample)); // Clamp
                            dst[i * nb_channels + ch] = (int16_t)(sample * 32767.0f);
                        }
                    }

                    // Send frame to encoder
                    if (avcodec_send_frame(o_audio.codec_ctx, ofr) < 0) {
                        throw std::runtime_error("Error sending frame to encoder");
                    }

                    // Write packets
                    while (avcodec_receive_packet(o_audio.codec_ctx, opkt) >= 0) {
                        opkt->stream_index = o_audio.st->index; // Ensure correct stream index
                        if (av_interleaved_write_frame(o_audio.fmt_ctx, opkt) < 0) {
                            throw std::runtime_error("Error writing packet");
                        }
                        av_packet_unref(opkt);
                    }

                    next_pts += ofr->nb_samples;
                }

                av_frame_unref(ifr);
                av_frame_unref(ofr);
            }
            av_packet_unref(ipkt);
        }

        // Flush decoder
        avcodec_send_packet(i_audio.c_context, nullptr);
        while (avcodec_receive_frame(i_audio.c_context, ifr) >= 0) {
            // Process remaining frames as above (repeat the conversion and writing logic)
            av_frame_unref(ifr);
        }

        // Flush encoder
        avcodec_send_frame(o_audio.codec_ctx, nullptr);
        while (avcodec_receive_packet(o_audio.codec_ctx, opkt) >= 0) {
            opkt->stream_index = o_audio.st->index;
            av_interleaved_write_frame(o_audio.fmt_ctx, opkt);
            av_packet_unref(opkt);
        }

        av_frame_free(&ifr);
        av_packet_free(&ipkt);
        av_frame_free(&ofr);
        av_packet_free(&opkt);
    }

    av_write_trailer(o_audio.fmt_ctx);

}

int main(int, char**){

    // TODO разбить код на функции структуры и тд
    InputAudio audio_info{};
    load_audio(audio_info, "../song2.mp3");
    convert_to_wav_from_non_raw(audio_info);
}
