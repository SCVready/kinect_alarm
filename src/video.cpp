/**
 * @author Alejandro Solozabal
 *
 * @file video.cpp
 *
 */

#include "video.hpp"

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                FILE *outfile)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3" PRId64 "\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0)
        {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %3" PRId64 " (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int encode_video_from_frames(char *filename,uint16_t **video_frames,int num_frames,int height,int width,int fps)
{
    const char *codec_name;
    const AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y;
    FILE *f;
    AVFrame *frame;
    AVPacket *pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    printf("encode_video_from_frames\n");

    codec_name = "libx264";//libx264//h264_omx

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec)
    {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c)
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    /* put sample parameters */
    c->bit_rate = 4000000;
    /* resolution must be a multiple of two */
    c->width = width;
    c->height = height;
    /* frames per second */
    c->time_base = (AVRational){1, fps};
    c->framerate = (AVRational){fps, 1};

    /* emit one intra frame every ten frames
    * check frame pict_type before passing frame
    * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
    * then gop_size is ignored and the output of encoder
    * will always be I frame irrespective to gop_size
    */
    c->gop_size = 1;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

/*
    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);
*/
    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0)
    {
        //fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    f = fopen(filename, "wb");
    if (!f)
    {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    uint16_t *current_frame;
    /* encode 1 second of video */
    for (i = 0; i < num_frames; i++)
    {
        fflush(stdout);

        current_frame = video_frames[i];

        /* make sure the frame data is writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* prepare a dummy image */
        /* Y */
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = (current_frame[x+y*width] < 256) ? current_frame[x+y*width] : 255;
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128;
                frame->data[2][y * frame->linesize[2] + x] = 128;
            }
        }

        frame->pts = i;

        /* encode the image */
        encode(c, frame, pkt, f);
    }

    /* flush the encoder */
    encode(c, NULL, pkt, f);

    /* add sequence end code to have a real MPEG file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}
