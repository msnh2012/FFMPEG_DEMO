﻿/**
 * 叶海辉
 * QQ群121376426
 * http://blog.yundiantech.com/
 */

#include "AppConfig.h"
#include "VideoEncoder.h"

//是否启用 ROI (region of interest) encoding 感兴趣区域视频编码技术
#define TEST_ROI_ENCODING

//#define ENCODE_H265

VideoEncoder::VideoEncoder()
{
    mBitRate = 600000;
    mIsStop = true;

    pCodecCtx = NULL;
    pCodec = NULL;

    picture_buf = NULL;
    picture = NULL;

    mCond = new Cond();
}

VideoEncoder::~VideoEncoder()
{

}

void VideoEncoder::setQuantity(int value)
{
    mBitRate = 450000 + (value - 5) * 50000;
}

void VideoEncoder::setWidth(int w, int h)
{
    mWidth = w;
    mHeight = h;
}

void VideoEncoder::inputYuvBuffer(uint8_t *buffer, int size, int64_t time)
{
    FrameDataNode node;
    node.size = size;
    node.buffer = buffer;
    node.time = time;

    mCond->Lock();
    mYuvBufferList.push_back(node);
    mCond->Unlock();
    mCond->Signal();
}

void VideoEncoder::startEncode()
{
    mIsStop = false;

    //启动新的线程
    std::thread([&](VideoEncoder *pointer)
    {
        pointer->run();

    }, this).detach();
}

void VideoEncoder::stopEncode()
{
    mIsStop = true;
}

void VideoEncoder::run()
{
    openEncoder();

    int y_size = pCodecCtx->width * pCodecCtx->height;

    AVPacket pkt;
    av_new_packet(&pkt, y_size*3);

#ifdef ENCODE_H265
    FILE *h264Fp = fopen("out.h265","wb");
#else
    FILE *h264Fp = fopen("out.h264","wb");
#endif

    while(1)
    {
        mCond->Lock();

        while(mYuvBufferList.empty())
        {
            mCond->Wait();
        }

        FrameDataNode node = mYuvBufferList.front(); //取出一帧yuv数据
        mYuvBufferList.pop_front();

        mCond->Unlock();

        if (node.buffer != NULL)
        {
//            picture->data[0] = node.buffer;     // 亮度Y
//            picture->data[1] = node.buffer + y_size;  // U
//            picture->data[2] = node.buffer + y_size*5/4; // V

            memcpy(picture_buf, node.buffer, node.size);

#ifdef TEST_ROI_ENCODING
            {
                // 申请side_data空间，假设有2个区域。
                // 不需要显式释放申请到的空间，
                // 在AVFrame的清理函数中会自动处理。
                AVFrameSideData *sd = av_frame_new_side_data(picture,
                             AV_FRAME_DATA_REGIONS_OF_INTEREST,
                             10*sizeof(AVRegionOfInterest));

                // 获取刚申请到的内存地址
                AVRegionOfInterest* roi = (AVRegionOfInterest*)sd->data;


                // 设置第一个区域
                roi[0].self_size = sizeof(*roi);
                roi[0].top       = 0;
                roi[0].bottom    = 600;
                roi[0].left      = 600;
                roi[0].right     = 1000;
                roi[0].qoffset   = AVRational{-1,1};

//                // 设置第二个区域
//                roi[1].self_size = sizeof(*roi);
//                roi[1].top       = ...;
//                roi[1].bottom    = ...;
//                roi[1].left      = ...;
//                roi[1].right     = ...;
//                roi[1].qoffset   = ...;

            }
#endif

            int ret = avcodec_send_frame(pCodecCtx, picture);
            if (ret != 0)
            {
                char buff[128]={0};
                av_strerror(ret, buff, 128);

                fprintf(stderr, "Error sending a frame for encoding! (%s)\n", buff);
                continue;
            }

            while (0 == avcodec_receive_packet(pCodecCtx, &pkt))
            {
                bool isKeyFrame = pkt.flags & AV_PKT_FLAG_KEY; //判断是否关键帧
                int w = fwrite(pkt.data, 1, pkt.size, h264Fp); //写入文件中 (h264的裸数据 直接写入文件 也可以播放  因为这里包含H264关键帧)

                fprintf(stderr, "%s encoded size=%d isKeyFrame=%d\n", __FUNCTION__, pkt.size, isKeyFrame);
            }
#ifdef TEST_ROI_ENCODING
            av_frame_remove_side_data(picture, AV_FRAME_DATA_REGIONS_OF_INTEREST);
#endif
            av_packet_unref(&pkt);
            av_free(node.buffer);
        }
    }

//    /*由于编码成H264的时候，一开始输入的几帧数据，不会立马输出
//     *因此停止输入的时候，编码器里面其实还是有数据的，下面的代码就是取出这个数据
//     *这个数据并不多 不处理也是可以的
//     */
//    while(1)
//    {
//         int got_picture=0;
//         int ret = avcodec_encode_video2(pCodecCtx, &pkt,NULL, &got_picture);

//         if (got_picture)
//         {
//               fwrite(pkt.data,1,pkt.size,h264Fp);
//         }
//         else
//         {
//             break;
//         }
//         av_free_packet(&pkt);
//    }

    fclose(h264Fp);

    closeEncoder();

}

#ifdef TEST_ROI_ENCODING
AVDictionary *VideoEncoder::setEncoderParam(const AVCodecID &codec_id)
{
    int in_w = mWidth;
    int in_h = mHeight;//宽高

    int mFrameRate = 15;
    int mQuality = 0;

    pCodecCtx->codec_id = codec_id;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecCtx->width = in_w;
    pCodecCtx->height = in_h;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = mFrameRate;//帧率(既一秒钟多少张图片)
    pCodecCtx->bit_rate = 100000; //比特率(调节这个大小可以改变编码后视频的质量)
    pCodecCtx->gop_size = 12;
    //H264 还可以设置很多参数 自行研究吧
    ////    pCodecCtx->me_range = 16;
    ////    pCodecCtx->max_qdiff = 4;
    ////    pCodecCtx->qcompress = 0.6;
    ////    pCodecCtx->qmin = 10;
    ////    pCodecCtx->qmax = 51;
    //    pCodecCtx->me_range = 16;
    //    pCodecCtx->max_qdiff = 1;
    //    pCodecCtx->qcompress = 0.6;
    //    pCodecCtx->qmin = 10;
    //    pCodecCtx->qmax = 51;
    //    //Optional Param
    //    pCodecCtx->max_b_frames=3;

    //    视频编码器常用的码率控制方式包括abr(平均码率)，crf(恒定码率)，cqp(恒定质量)，
    //    ffmpeg中AVCodecContext显示提供了码率大小的控制参数，但是并没有提供其他的控制方式。
    //    ffmpeg中码率控制方式分为以下几种情况：
    //    1.如果设置了AVCodecContext中bit_rate的大小，则采用abr的控制方式；
    //    2.如果没有设置AVCodecContext中的bit_rate，则默认按照crf方式编码，crf默认大小为23（此值类似于qp值，同样表示视频质量）；
    //    3.如果用户想自己设置，则需要借助av_opt_set函数设置AVCodecContext的priv_data参数。下面给出三种控制方式的实现代码：

#if 0
    ///平均码率
    //目标的码率，即采样的码率；显然，采样码率越大，视频大小越大
    pCodecCtx->bit_rate = mBitRate;

#elif 1
    ///恒定码率
//    量化比例的范围为0~51，其中0为无损模式，23为缺省值，51可能是最差的。该数字越小，图像质量越好。从主观上讲，18~28是一个合理的范围。18往往被认为从视觉上看是无损的，它的输出视频质量和输入视频一模一样或者说相差无几。但从技术的角度来讲，它依然是有损压缩。
//    若Crf值加6，输出码率大概减少一半；若Crf值减6，输出码率翻倍。通常是在保证可接受视频质量的前提下选择一个最大的Crf值，如果输出视频质量很好，那就尝试一个更大的值，如果看起来很糟，那就尝试一个小一点值。
//    av_opt_set(pCodecCtx->priv_data, "crf", "51.000", AV_OPT_SEARCH_CHILDREN);

#else
    ///qp的值和crf一样
//    av_opt_set(pCodecCtx->priv_data, "qp", "31.000",AV_OPT_SEARCH_CHILDREN);
#endif

#if TEST_OPT

    av_opt_set(pCodecCtx,"b","400000",0);		//bitrate
    //Another method
    //av_opt_set_int(pCodecCtx,"b",400000,0);	//bitrate

    av_opt_set(pCodecCtx,"time_base","1/25",0);	//time_base
    av_opt_set(pCodecCtx,"bf","5",0);			//max b frame
    av_opt_set(pCodecCtx,"g","25",0);			//gop
    av_opt_set(pCodecCtx,"qmin","10",0);		//qmin/qmax
    av_opt_set(pCodecCtx,"qmax","51",0);

#else

//    pCodecCtx->time_base.num = 1;
//    pCodecCtx->time_base.den = 25;
//    pCodecCtx->max_b_frames=5;
//    pCodecCtx->bit_rate = 400000;
//    pCodecCtx->gop_size=25;
//    pCodecCtx->qmin = 10;
//    pCodecCtx->qmax = 51;

    /* time base: this is the fundamental unit of time (in seconds) in terms
       of which frame timestamps are represented. for fixed-fps content,
       timebase should be 1/framerate and timestamp increments should be
       identically 1. */
//    ost->st->time_base = { 1, m_videoFrameRate };
//    pCodecCtx->time_base       = ost->st->time_base;
//    c->gop_size = 12; /* emit one intra frame every twelve frames at most */
//    pCodecCtx->gop_size = m_videoFrameRate * 2; ///I帧间隔

//    //固定允许的码率误差，数值越大，视频越小
//    c->bit_rate_tolerance = mBitRate;

//    //H264 还可以设置很多参数 自行研究吧
////    pCodecCtx->me_range = 16;
////    pCodecCtx->max_qdiff = 1;
//    c->qcompress = 0.85;
//    c->qmin = 18;

    pCodecCtx->qmin = 0;
    pCodecCtx->qmax = 51;

//    pCodecCtx->qmin = 16+(10-mQuality)*2;
//    pCodecCtx->qmax = 31+(10-mQuality)*2;

////    //采用（qmin/qmax的比值来控制码率，1表示局部采用此方法，0表示全局）
////    c->rc_qsquish = 0;

////    //因为我们的量化系数q是在qmin和qmax之间浮动的，
////    //qblur表示这种浮动变化的变化程度，取值范围0.0～1.0，取0表示不削减
////    c->qblur = 1.0;

//std::cout<<"mBitRate"<<mBitRate<<m_videoFrameRate<<std::endl;

////    ///b_frame_strategy
////    ///如果为true，则自动决定什么时候需要插入B帧，最高达到设置的最大B帧数。
////    ///如果设置为false,那么最大的B帧数被使用。
////    c->b_frame_strategy = 1;
////    c->max_b_frames = 5;

#endif

    // some formats want stream headers to be separate
    if (pCodecCtx->flags & AVFMT_GLOBALHEADER)
        pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    //编码器预设
    AVDictionary *param = 0;
    if(pCodecCtx->codec_id == AV_CODEC_ID_H264)
    {
//        //H.264
//        //av_dict_set(&param, "preset", "slow", 0);
//        av_dict_set(&param, "preset", "superfast", 0);
//        av_dict_set(&param, "tune", "zerolatency", 0);  //实现实时编码

        ///下面的可以启用硬件编码
        av_dict_set(&param, "preset", "medium", 0);
    //        av_dict_set(&param, "preset", "superfast", 0);
        av_dict_set(&param, "tune", "zerolatency", 0); //实现实时编码
        av_dict_set(&param, "profile", "main", 0);
    }
    else if(pCodecCtx->codec_id == AV_CODEC_ID_H265)
    {
        av_dict_set(&param, "preset", "ultrafast", 0);
        av_dict_set(&param, "tune", "zerolatency", 0);
        av_dict_set(&param, "profile", "main", 0);
    }

    return param;

}

#else

AVDictionary *VideoEncoder::setEncoderParam(const AVCodecID &codec_id)
{
    int in_w = mWidth;
    int in_h = mHeight;//宽高

    pCodecCtx->codec_id = codec_id;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecCtx->width = in_w;
    pCodecCtx->height = in_h;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = 15;//帧率(既一秒钟多少张图片)
    pCodecCtx->bit_rate = mBitRate; //比特率(调节这个大小可以改变编码后视频的质量)
    pCodecCtx->gop_size=12;
    //H264 还可以设置很多参数 自行研究吧
    ////    pCodecCtx->me_range = 16;
    ////    pCodecCtx->max_qdiff = 4;
    ////    pCodecCtx->qcompress = 0.6;
    ////    pCodecCtx->qmin = 10;
    ////    pCodecCtx->qmax = 51;
    //    pCodecCtx->me_range = 16;
    //    pCodecCtx->max_qdiff = 1;
    //    pCodecCtx->qcompress = 0.6;
    //    pCodecCtx->qmin = 10;
    //    pCodecCtx->qmax = 51;
    //    //Optional Param
    //    pCodecCtx->max_b_frames=3;


    // some formats want stream headers to be separate
    if (pCodecCtx->flags & AVFMT_GLOBALHEADER)
        pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    //编码器预设
    AVDictionary *param = 0;
    if(pCodecCtx->codec_id == AV_CODEC_ID_H264)
    {
        av_dict_set(&param, "preset", "medium", 0);
    //        av_dict_set(&param, "preset", "superfast", 0);
        av_dict_set(&param, "tune", "zerolatency", 0); //实现实时编码
        av_dict_set(&param, "profile", "main", 0);
    }
    else if(pCodecCtx->codec_id == AV_CODEC_ID_H265)
    {
        av_dict_set(&param, "preset", "ultrafast", 0);
        av_dict_set(&param, "tune", "zerolatency", 0);
        av_dict_set(&param, "profile", "main", 0);
    }

    return param;

}

#endif

///打开视频编码器
bool VideoEncoder::openVideoEncoder(const AVCodecID &codec_id)
{
    bool isSucceed = false;
    bool isHardWareEncoderOpened = false;

#ifdef TEST_ROI_ENCODING
#else
    bool mIsSupportHardEncoder = true;
    if (mIsSupportHardEncoder)
    {
        ///尝试打开cuvid编码器器
        isHardWareEncoderOpened = openHardEncoder_Cuvid(codec_id);

        ///cuvid打开失败了 继续尝试 qsv
        if (!isHardWareEncoderOpened)
        {
            isHardWareEncoderOpened = openHardEncoder_Qsv(codec_id);
        }
    }
#endif

    //尝试打开硬件解码器失败了 改用软解码
    if (!isHardWareEncoderOpened)
    {
        isSucceed = openSoftEncoder(codec_id);
    }
    else
    {
        isSucceed = true;
    }

    return isSucceed;
}

///打开硬件编码器（英伟达）
bool VideoEncoder::openHardEncoder_Cuvid(const AVCodecID &codec_id)
{
    bool isSucceed = false;

    fprintf(stderr,"open hardware encoder cuvid...\n");

    ///查找硬件解码器
    char hardWareDecoderName[32] = {0};

    if (AV_CODEC_ID_H264 == codec_id)
    {
        sprintf(hardWareDecoderName, "h264_nvenc");
    }
    else if (AV_CODEC_ID_HEVC == codec_id)
    {
        sprintf(hardWareDecoderName, "hevc_nvenc");
    }

    if (strlen(hardWareDecoderName) > 0)
    {
        pCodec = avcodec_find_encoder_by_name(hardWareDecoderName);

        if (pCodec != NULL)
        {
            pCodecCtx = avcodec_alloc_context3(pCodec);

            AVDictionary *param = setEncoderParam(codec_id);

            ///打开解码器
            if (avcodec_open2(pCodecCtx, pCodec, &param) < 0)
            {
                avcodec_close(pCodecCtx);
                avcodec_free_context(&pCodecCtx);
                pCodecCtx = nullptr;
                isSucceed = false;

                fprintf(stderr,"Could not open codec %s\n",hardWareDecoderName);
            }
            else
            {
                isSucceed = true;
                fprintf(stderr,"open codec %s succeed! %d %d\n",hardWareDecoderName,pCodec->id,pCodecCtx->codec_id);
            }
        }
        else
        {
            fprintf(stderr,"Codec %s not found.\n",hardWareDecoderName);
        }
    }

    return isSucceed;
}

///打开硬件编码器（intel）
bool VideoEncoder::openHardEncoder_Qsv(const AVCodecID &codec_id)
{
    bool isSucceed = false;

    fprintf(stderr,"open hardware encoder cuvid...\n");

    ///查找硬件解码器
    char hardWareDecoderName[32] = {0};

    if (AV_CODEC_ID_H264 == codec_id)
    {
        sprintf(hardWareDecoderName, "h264_qsv");
    }
    else if (AV_CODEC_ID_HEVC == codec_id)
    {
        sprintf(hardWareDecoderName, "hevc_qsv");
    }

    if (strlen(hardWareDecoderName) > 0)
    {
        pCodec = avcodec_find_encoder_by_name(hardWareDecoderName);

        if (pCodec != NULL)
        {
            pCodecCtx = avcodec_alloc_context3(pCodec);

            AVDictionary *param = setEncoderParam(codec_id);

            ///打开解码器
            if (avcodec_open2(pCodecCtx, pCodec, &param) < 0)
            {
                avcodec_close(pCodecCtx);
                avcodec_free_context(&pCodecCtx);
                pCodecCtx = nullptr;
                isSucceed = false;

                fprintf(stderr,"Could not open codec %s\n",hardWareDecoderName);
            }
            else
            {
                isSucceed = true;
                fprintf(stderr,"open codec %s succeed! %d %d\n",hardWareDecoderName,pCodec->id,pCodecCtx->codec_id);
            }
        }
        else
        {
            fprintf(stderr,"Codec %s not found.\n",hardWareDecoderName);
        }
    }

    return isSucceed;
}

///打开软编码器
bool VideoEncoder::openSoftEncoder(const AVCodecID &codec_id)
{
    bool isSucceed = false;

    fprintf(stderr,"open software encoder... \n");

    pCodec = avcodec_find_encoder(codec_id);

    if (pCodec == NULL)
    {
        fprintf(stderr, "Codec not found.\n");
        isSucceed = false;
    }
    else
    {
        pCodecCtx = avcodec_alloc_context3(pCodec);
        pCodecCtx->thread_count = 8;

        AVDictionary *param = setEncoderParam(codec_id);

        ///打开解码器
        if (avcodec_open2(pCodecCtx, pCodec, &param) < 0)
        {
            avcodec_close(pCodecCtx);
            avcodec_free_context(&pCodecCtx);
            pCodecCtx = nullptr;
            isSucceed = false;

            fprintf(stderr,"Could not open codec.\n");
        }
        else
        {
            isSucceed = true;
        }
    }

    return isSucceed;
}

bool VideoEncoder::openEncoder()
{

#ifdef ENCODE_H265
    AVCodecID codec_id = AV_CODEC_ID_H265;
#else
    AVCodecID codec_id = AV_CODEC_ID_H264;
#endif

    bool isSucceed = openVideoEncoder(codec_id);

    if (isSucceed)
    {
        picture = av_frame_alloc();

        picture->format = pCodecCtx->pix_fmt;
        picture->width  = pCodecCtx->width;
        picture->height = pCodecCtx->height;

        int size = avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height); //计算需要用到的数据大小
        picture_buf = (uint8_t *)av_malloc(size); //分配空间
        avpicture_fill((AVPicture *)picture, picture_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

        fprintf(stderr, " 编码器打开成功！pCodecCtx->pix_fmt=%d %d %d %d %d %d\n",
                pCodecCtx->pix_fmt, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, pCodecCtx->width, pCodecCtx->height, size);
    }

    return isSucceed;
}

bool VideoEncoder::closeEncoder()
{
    if (picture != NULL)
        av_free(picture);

    if (picture_buf != NULL)
        av_free(picture_buf);

    if (pCodecCtx != NULL)
        avcodec_close(pCodecCtx);

    pCodecCtx = NULL;
    pCodec = NULL;

    picture_buf = NULL;
    picture = NULL;

    return true;
}
