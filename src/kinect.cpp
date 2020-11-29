/**
 * @author Alejandro Solozabal
 *
 * @file cKinect.cpp
 *
 */

/*******************************************************************
 * Includes
 *******************************************************************/
#include <unistd.h>//todo remove
#include <chrono>

#include "log.hpp"
#include "kinect.hpp"

/*******************************************************************
 * Static variables
 *******************************************************************/
std::unique_ptr<KinectFrame> Kinect::m_depth_frame;
std::unique_ptr<KinectFrame> Kinect::m_video_frame;

std::mutex Kinect::m_depth_mutex, Kinect::m_video_mutex;
std::condition_variable Kinect::m_depth_cv, Kinect::m_video_cv;

/*******************************************************************
 * Class definition
 *******************************************************************/
Kinect::Kinect() : CyclicTask("Kinect", 0)
{
    /* Members initialization */
    m_is_kinect_initialized = false;
    m_kinect_ctx            = NULL;
    m_kinect_dev            = NULL;

    m_depth_frame = std::make_unique<KinectFrame>(DEPTH_WIDTH, DEPTH_HEIGHT);
    m_video_frame = std::make_unique<KinectFrame>(VIDEO_WIDTH, VIDEO_HEIGHT);
}

Kinect::~Kinect()
{
}

int Kinect::Init()
{
    int retval = -1;

    /* Check if it's already initialize */
    if(m_is_kinect_initialized)
    {
        retval = 0;
    }
    else
    {
        /* Library freenect init */
        if (0 != freenect_init(&m_kinect_ctx, NULL))
        {
            LOG(LOG_ERR,"freenect_init() failed\n");
        }
        else
        {
            /* Set log level */
            freenect_set_log_level(m_kinect_ctx, FREENECT_LOG_FATAL);

            /* Select subdevices */
            freenect_select_subdevices(m_kinect_ctx, (freenect_device_flags) (FREENECT_DEVICE_MOTOR | FREENECT_DEVICE_CAMERA));

            /* Find out how many devices are connected */
            int num_devices = 0;
            if (0 > (num_devices = freenect_num_devices(m_kinect_ctx)))
            {
                LOG(LOG_ERR,"freenect_num_devices() failed\n");
            }
            else if(num_devices == 0)
            {
                LOG(LOG_ERR,"No Kinect device found\n");
            }
            else
            {
                LOG(LOG_INFO,"Kinect device found\n");

                /* Open the first device */
                if (0 != freenect_open_device(m_kinect_ctx, &m_kinect_dev, 0))
                {
                    LOG(LOG_ERR,"freenect_open_device() failed\n");
                }
                /* Configure depth and video mode */
                else if (0 != freenect_set_depth_mode(m_kinect_dev, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT)))
                {
                    LOG(LOG_ERR,"freenect_set_depth_mode() failed\n");
                }
                else if (0 != freenect_set_video_mode(m_kinect_dev, freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_IR_10BIT)))
                {
                    LOG(LOG_ERR,"freenect_set_video_mode() failed\n");
                }
                else
                {
                    /* Set frame callbacks */
                    freenect_set_depth_callback(m_kinect_dev, DepthCallback);
                    freenect_set_video_callback(m_kinect_dev, VideoCallback);

                    /* Set kinect init flag to true */
                    m_is_kinect_initialized = true;
                    retval = 0;
                }
            }
        }
    }
    return retval;
}

int Kinect::Term()
{
    LOG(LOG_INFO,"Shutting down kinect\n");

    /* Stop everything and shutdown */
    if(m_kinect_dev)
    {
        freenect_close_device(m_kinect_dev);
        freenect_shutdown(m_kinect_ctx);
    }

    /* Initialize flag to false */
    m_is_kinect_initialized = false;

    return 0;
}

int Kinect::Start()
{
    /* Initialize frame time-stamps */
    m_depth_frame->m_timestamp = 0;
    m_video_frame->m_timestamp = 0;

    freenect_start_video(m_kinect_dev);
    freenect_start_depth(m_kinect_dev);

    /* Call parent Start to start the execution thread*/
    CyclicTask::Start();

    return 0;
}

int Kinect::Stop()
{
    /* Call parent Stop to start the execution thread*/
    CyclicTask::Stop();

    if(m_kinect_dev)
    {
        freenect_stop_depth(m_kinect_dev);
        freenect_stop_video(m_kinect_dev);
    }

    return 0;
}

void Kinect::ExecutionCycle()
{
    freenect_process_events(m_kinect_ctx);
}

void Kinect::GetDepthFrame(std::shared_ptr<KinectFrame> frame)
{
    std::unique_lock<std::mutex> ulock(m_depth_mutex);

    /* Compare the given timestamp with the current, if it's the same must wait to the next frame */
    if(frame->m_timestamp == m_depth_frame->m_timestamp)
    {
        if(m_depth_cv.wait_for(ulock, std::chrono::seconds(1)) == std::cv_status::timeout)
        {
            LOG(LOG_WARNING,"GetDepthFrame() failed to acquire a frame in 1 second\n");
        }
    }

    *frame = *m_depth_frame;
}

void Kinect::GetVideoFrame(std::shared_ptr<KinectFrame> frame)
{
    std::unique_lock<std::mutex> ulock(m_video_mutex);

    /*  Compare the given timestamp with the current, if it's the same must wait to the next frame */
    if(frame->m_timestamp == m_video_frame->m_timestamp)
    {
        if(m_video_cv.wait_for(ulock, std::chrono::seconds(1)) == std::cv_status::timeout)
        {
            LOG(LOG_WARNING,"GetVideoFrame() failed to acquire a frame in 1 second\n");
        }
    }

    *frame = *m_video_frame;
}

void Kinect::DepthCallback(freenect_device* dev, void* data, uint32_t timestamp)
{
    std::unique_lock<std::mutex> ulock(m_depth_mutex);
    m_depth_frame->Fill(static_cast<uint16_t*>(data));
    m_depth_frame->m_timestamp = timestamp;
    m_depth_cv.notify_all();
}

void Kinect::VideoCallback(freenect_device* dev, void* data, uint32_t timestamp)
{
    std::unique_lock<std::mutex> ulock(m_video_mutex);
    m_video_frame->Fill(static_cast<uint16_t*>(data));
    m_video_frame->m_timestamp = timestamp;
    m_video_cv.notify_all();
}

bool Kinect::ChangeTilt(double tilt_angle)
{
    int retval = 0; 

    if(0 != freenect_set_tilt_degs(m_kinect_dev, tilt_angle))
    {
        LOG(LOG_WARNING,"freenect_set_tilt_degs() failed\n");
        retval = -1;
    }
    return retval;
}

int Kinect::ChangeLedColor(freenect_led_options color)
{
    int retval = 0; 
    if(0 != freenect_set_led(m_kinect_dev,color))
    {
        LOG(LOG_WARNING,"freenect_set_led() failed\n");
        retval = -1;
    }
    return retval;
}
