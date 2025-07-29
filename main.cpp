#include <cstdint>
#include <fstream>
#include <iostream>

#include <stdexcept>

extern "C" {
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

// void decode(AVFormatContext* format_context, AVFrame* frame, AVPacket* packet) {

// }



void convert_to_raw_from_non_raw(AVFormatContext* format_context, AVCodecContext* context, int audio_stream_index)  {
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

    outfile.flush();
    outfile.close();
    av_packet_free(&packet);
    av_frame_free(&frame);
}

void convert_to_wav_from_non_raw(AVFormatContext* format_context, AVCodecContext* context, int audio_stream_index, AVCodecParameters* input_audio_params) {
    const AVCodec* wav_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    AVCodecContext* wav_context = avcodec_alloc_context3(wav_codec);

    wav_context->bit_rate = input_audio_params->bit_rate;
    wav_context->ch_layout = input_audio_params->ch_layout;
    // wav_context->sample_fmt = static_cast<AVSampleFormat>(input_audio_params->format);
    wav_context->sample_fmt = AV_SAMPLE_FMT_S16;
    wav_context->sample_rate = input_audio_params->sample_rate;

    if (avcodec_open2(wav_context, wav_codec, nullptr) < 0) {
        throw std::runtime_error("Could not open wav_context");
    } 

    std::ofstream file("out.wav");
    // Write wav header somehow
    if (!file.is_open()) {
        throw std::runtime_error("What the fyuck file aint open");
    }

    AVFrame* wav_frame = av_frame_alloc();
    AVPacket* wav_packet = av_packet_alloc();
    wav_frame->nb_samples = input_audio_params->frame_size;
    wav_frame->format = AV_SAMPLE_FMT_S16;
    av_channel_layout_copy(&wav_frame->ch_layout, &input_audio_params->ch_layout);
    av_frame_make_writable(wav_frame);
    if (av_frame_get_buffer(wav_frame, 0) < 0) {
        throw std::runtime_error("Could not get buffer for frame");
    }

    AVFrame* input_frame = av_frame_alloc();
    AVPacket* input_packet = av_packet_alloc();

    while (av_read_frame(format_context, input_packet) >= 0) {
        if (input_packet->stream_index != audio_stream_index) {
            continue;
        }

        int input_response = avcodec_send_packet(context, input_packet);
        if (input_response < 0) {
            throw std::runtime_error{"Error sending packet"};
        }

        while (input_response >= 0) {
            input_response = avcodec_receive_frame(context, input_frame);
            if (input_response == AVERROR(EAGAIN) || input_response == AVERROR_EOF) {
                break;
            }          
            else if (input_response < 0) {
                throw std::runtime_error{"Error recv frame"};
            }

            // Since its planar we need to use frame->data[0..channels]
            if (input_frame->format == AV_SAMPLE_FMT_FLTP) {
                // wav_frame->nb_samples = input_frame->nb_samples; // Синхронизация количества семплов
                // wav_frame->format = AV_SAMPLE_FMT_S16;
                // av_channel_layout_copy(&wav_frame->ch_layout, &input_audio_params->ch_layout);
                // if (av_frame_get_buffer(wav_frame, 0) < 0) {
                //     throw std::runtime_error("Could not get buffer for frame");
                // }


                float** input_samples = reinterpret_cast<float**>(input_frame->data);

                // TODO: WRITE IT SO IT IS NOT PLANAR
                int16_t* wav_samples = reinterpret_cast<int16_t*>(wav_frame->data[0]);
                int idx = 0;
                // std::println("Samples: {}", wav_frame->nb_samples);
                for (auto i = 0; i < input_frame->nb_samples; ++i) {
                    for (auto j = 0; j < input_frame->ch_layout.nb_channels; ++j) {
                        wav_samples[idx++] = input_samples[j][i] * 32767;
                    }
                }

                int wav_response = avcodec_send_frame(wav_context, wav_frame);
                if (wav_response < 0) {
                    throw std::runtime_error("Could not send a wav frame");
                }

                while (wav_response >= 0) {
                    wav_response = avcodec_receive_packet(wav_context, wav_packet);
                    if (wav_response == AVERROR(EAGAIN) || wav_response == AVERROR_EOF) {
                        break;
                    }
                    else if (wav_response < 0) {
                        throw std::runtime_error("Error receiving packet encoding");
                    }

                    file.write(reinterpret_cast<const char*>(&wav_packet->data), wav_packet->size);
                    av_packet_unref(input_packet);
                }
            }
        }
        av_packet_unref(input_packet);
    }
    avcodec_send_frame(wav_context, nullptr); // flush

    file.flush();
    file.close();
    av_frame_free(&input_frame);
    av_packet_free(&input_packet);
    av_frame_free(&wav_frame);
    av_packet_free(&wav_packet);
    avcodec_free_context(&wav_context);
}

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
            codec_params = tempCodecParams;
            codec = tempCodec;
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

    
    // convert_to_raw_from_non_raw(format_context, context, audio_stream_index);
    convert_to_wav_from_non_raw(format_context, context, audio_stream_index, codec_params);

    avcodec_free_context(&context);

    avformat_close_input(&format_context);
    avformat_free_context(format_context);
}
