// MythTV
#include "osd.h"
#include "mythplayer.h"
#include "videodisplayprofile.h"
#include "decoderbase.h"
#include "mythcorecontext.h"
#include "mythlogging.h"
#include "mythmainwindow.h"
#include "mythuihelper.h"
#include "mythavutil.h"
#include "mthreadpool.h"
#include "mythcodeccontext.h"

#ifdef _WIN32
#include "videoout_d3d.h"
#endif

#ifdef USING_OPENGL
#include "opengl/mythvideooutopengl.h"
#endif

#ifdef USING_VULKAN
#include "vulkan/mythvideooutputvulkan.h"
#endif

#include "mythvideooutnull.h"
#include "mythvideoout.h"

// std
#include <cmath>
#include <cstdlib>

#define LOC QString("VideoOutput: ")

VideoFrameTypeVec MythVideoOutput::s_defaultFrameTypes = { FMT_YV12 };

void MythVideoOutput::GetRenderOptions(RenderOptions& Options)
{
    MythVideoOutputNull::GetRenderOptions(Options);

#ifdef _WIN32
    VideoOutputD3D::GetRenderOptions(Options);
#endif

#ifdef USING_OPENGL
    MythVideoOutputOpenGL::GetRenderOptions(Options);
#endif

#ifdef USING_VULKAN
    MythVideoOutputVulkan::GetRenderOptions(Options);
#endif
}

/**
 * \brief  Depending on compile-time configure settings and run-time
 *         renderer settings, create a relevant VideoOutput subclass.
 * \return instance of VideoOutput if successful, nullptr otherwise.
 */
MythVideoOutput *MythVideoOutput::Create(const QString& Decoder,    MythCodecID CodecID,
                                         PIPState PiPState,         const QSize& VideoDim,
                                         const QSize& VideoDispDim, float VideoAspect,
                                         QWidget *ParentWidget,     const QRect& EmbedRect,
                                         float FrameRate,           uint  PlayerFlags,
                                         const QString& Codec,      int ReferenceFrames)
{
    QStringList renderers;

    // select the best available output
    if (PlayerFlags & kVideoIsNull)
    {
        // plain null output
        renderers += "null";
    }
    else
    {
#ifdef _WIN32
        renderers += VideoOutputD3D::GetAllowedRenderers(CodecID, VideoDispDim);
#endif

#ifdef USING_OPENGL
       renderers += MythVideoOutputOpenGL::GetAllowedRenderers(CodecID, VideoDispDim);
#endif

#ifdef USING_VULKAN
       renderers += MythVideoOutputVulkan::GetAllowedRenderers(CodecID);
#endif
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Allowed renderers for %1 %2 (Decoder: %3): '%4'")
        .arg(get_encoding_type(CodecID)).arg(get_decoder_name(CodecID))
        .arg(Decoder).arg(renderers.join(",")));
    renderers = VideoDisplayProfile::GetFilteredRenderers(Decoder, renderers);
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Allowed renderers (filt: %1): %2")
        .arg(Decoder).arg(renderers.join(",")));

    QString renderer;

    auto *vprof = new VideoDisplayProfile();

    if (!renderers.empty())
    {
        vprof->SetInput(VideoDispDim, FrameRate, Codec);
        QString tmp = vprof->GetVideoRenderer();
        if (vprof->IsDecoderCompatible(Decoder) && renderers.contains(tmp))
        {
            renderer = tmp;
            LOG(VB_PLAYBACK, LOG_INFO, LOC + "Preferred renderer: " + renderer);
        }
        else
        {
            LOG(VB_PLAYBACK, LOG_INFO, LOC +
                QString("No preferred renderer for decoder '%1' - profile renderer: '%2'")
                .arg(Decoder).arg(tmp));
        }
    }

    if (renderer.isEmpty())
        renderer = VideoDisplayProfile::GetBestVideoRenderer(renderers);

    if (renderer.isEmpty() && !(PlayerFlags & kVideoIsNull))
    {
        QString fallback;
#ifdef USING_OPENGL
        if (MythRenderOpenGL::GetOpenGLRender())
            fallback = "opengl";
#endif
#ifdef USING_VULKAN
        if (MythRenderVulkan::GetVulkanRender())
            fallback = VULKAN_RENDERER;
#endif
        LOG(VB_GENERAL, LOG_WARNING, LOC + "No renderer found. This should not happen!.");
        LOG(VB_GENERAL, LOG_WARNING, LOC + QString("Falling back to '%1'").arg(fallback));
        renderer = fallback;
    }

    while (!renderers.empty())
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC +
            QString("Trying video renderer: '%1'").arg(renderer));
        int index = renderers.indexOf(renderer);
        if (index >= 0)
            renderers.removeAt(index);
        else
            break;

        MythVideoOutput* vo = nullptr;

        /* these cases are mutually exlusive */
        if (renderer == "null")
        {
            vo = new MythVideoOutputNull();
        }
#ifdef _WIN32
        else if (renderer == "direct3d")
        {
            vo = new VideoOutputD3D();
        }
#endif
#ifdef USING_OPENGL
        else if (renderer.contains("opengl"))
        {
            vo = new MythVideoOutputOpenGL(renderer);
        }
#endif
#ifdef USING_VULKAN
        else if (renderer.contains(VULKAN_RENDERER))
        {
            vo = new MythVideoOutputVulkan(renderer);
        }
#endif

        if (vo)
            vo->m_dbDisplayProfile = vprof;

        if (vo && !(PlayerFlags & kVideoIsNull))
        {
            // ensure we have a window to display into
            QWidget* widget = ParentWidget;
            MythMainWindow* window = GetMythMainWindow();
            if (!widget && window)
                widget = window->findChild<QWidget*>("video playback window");

            if (!widget)
            {
                LOG(VB_GENERAL, LOG_ERR, LOC + "No window for video output.");
                delete vo;
                vo = nullptr;
                return nullptr;
            }

            if (!widget->winId())
            {
                LOG(VB_GENERAL, LOG_ERR, LOC + "No window for video output.");
                delete vo;
                vo = nullptr;
                return nullptr;
            }

            // determine the display rectangle
            QRect display_rect = QRect(0, 0, widget->width(), widget->height());
            if (PiPState == kPIPStandAlone)
                display_rect = EmbedRect;

            vo->SetPIPState(PiPState);
            vo->SetVideoFrameRate(FrameRate);
            vo->SetReferenceFrames(ReferenceFrames);
            if (vo->Init(VideoDim, VideoDispDim, VideoAspect, display_rect, CodecID))
            {
                vo->SetVideoScalingAllowed(true);
                return vo;
            }

            vo->m_dbDisplayProfile = nullptr;
            delete vo;
            vo = nullptr;
        }
        else if (vo && (PlayerFlags & kVideoIsNull))
        {
            if (vo->Init(VideoDim, VideoDispDim, VideoAspect, QRect(), CodecID))
                return vo;

            vo->m_dbDisplayProfile = nullptr;
            delete vo;
            vo = nullptr;
        }

        renderer = VideoDisplayProfile::GetBestVideoRenderer(renderers);
    }

    LOG(VB_GENERAL, LOG_ERR, LOC +
        "Not compiled with any useable video output method.");
    delete vprof;
    return nullptr;
}

/**
 * \class VideoOutput
 * \brief This class serves as the base class for all video output methods.
 *
 * The basic use is:
 * \code
 * VideoOutputType type = kVideoOutput_Default;
 * vo = VideoOutput::InitVideoOut(type);
 * vo->Init(width, height, aspect ...);
 *
 * // Then create two threads.
 * // In the decoding thread
 * while (decoding)
 * {
 *     if (vo->WaitForAvailable(1000)
 *     {
 *         frame = vo->GetNextFreeFrame(); // remove frame from "available"
 *         av_lib_process(frame);   // do something to fill it.
 *         // call DrawSlice()      // if you need piecemeal processing
 *                                  // by VideoOutput use DrawSlice
 *         vo->ReleaseFrame(frame); // enqueues frame in "used" queue
 *     }
 * }
 *
 * // In the displaying thread
 * while (playing)
 * {
 *     if (vo->EnoughPrebufferedFrames())
 *     {
 *         // Sets "Last Shown Frame" to head of "used" queue
 *         vo->StartDisplayingFrame();
 *         // Get pointer to "Last Shown Frame"
 *         frame = vo->GetLastShownFrame();
 *         // add OSD, do any filtering, etc.
 *         vo->ProcessFrame(frame, osd, filters, pict-in-pict, scan);
 *         // tells show what frame to be show, do other last minute stuff
 *         vo->PrepareFrame(frame, scan);
 *         // here you wait until it's time to show the frame
 *         // Show blits the last prepared frame to the screen
 *         // as quickly as possible.
 *         vo->Show(scan);
 *         // remove frame from the head of "used",
 *         // vo must get it into "available" eventually.
 *         vo->DoneDisplayingFrame();
 *     }
 * }
 * delete vo;
 * \endcode
 *
 *  Note: Show() may be called multiple times between PrepareFrame() and
 *        DoneDisplayingFrame(). But if a frame is ever removed from
 *        available via GetNextFreeFrame(), you must either call
 *        DoneDisplayFrame() or call DiscardFrame(VideoFrame*) on it.
 *
 *  Note: ProcessFrame() may be called multiple times on a frame, to
 *        update an OSD for example.
 *
 *  The VideoBuffers class handles the buffer tracking,
 *  see it for more details on the states a buffer can
 *  take before it becomes available for reuse.
 *
 * \see VideoBuffers, MythPlayer
 */

/**
 * \fn VideoOutput::VideoOutput()
 * \brief This constructor for VideoOutput must be followed by an
 *        Init(int,int,float,WId,int,int,int,int,WId) call.
 */
MythVideoOutput::MythVideoOutput(bool CreateDisplay)
  : MythVideoBounds(CreateDisplay)
{
    m_dbLetterboxColour = static_cast<LetterBoxColour>(gCoreContext->GetNumSetting("LetterboxColour", 0));
}

/**
 * \fn VideoOutput::~VideoOutput()
 * \brief Shuts down video output.
 */
MythVideoOutput::~MythVideoOutput()
{
    delete m_dbDisplayProfile;
}

/**
 * \fn VideoOutput::Init(int,int,float,WId,int,int,int,int,WId)
 * \brief Performs most of the initialization for VideoOutput.
 * \return true if successful, false otherwise.
 */
bool MythVideoOutput::Init(const QSize& VideoDim, const QSize& VideoDispDim,
                           float VideoAspect, const QRect& WindowRect, MythCodecID CodecID)
{
    m_videoCodecID = CodecID;
    bool wasembedding = IsEmbedding();
    QRect oldrect;
    if (wasembedding)
    {
        oldrect = GetEmbeddingRect();
        StopEmbedding();
    }

    bool mainSuccess = InitBounds(VideoDim, VideoDispDim, VideoAspect, WindowRect);

    if (m_dbDisplayProfile)
        m_dbDisplayProfile->SetInput(GetVideoDispDim());

    if (wasembedding)
        EmbedInWidget(oldrect);

    VideoAspectRatioChanged(VideoAspect); // apply aspect ratio and letterbox mode

    return mainSuccess;
}

void MythVideoOutput::SetVideoFrameRate(float playback_fps)
{
    if (m_dbDisplayProfile)
        m_dbDisplayProfile->SetOutput(playback_fps);
}

void MythVideoOutput::SetReferenceFrames(int ReferenceFrames)
{
    m_maxReferenceFrames = ReferenceFrames;
}

MythDeintType MythVideoOutput::ParseDeinterlacer(const QString& Deinterlacer)
{
    MythDeintType result = DEINT_NONE;

    if (Deinterlacer.contains(DEINT_QUALITY_HIGH))
        result = DEINT_HIGH;
    else if (Deinterlacer.contains(DEINT_QUALITY_MEDIUM))
        result = DEINT_MEDIUM;
    else if (Deinterlacer.contains(DEINT_QUALITY_LOW))
        result = DEINT_BASIC;

    if (result != DEINT_NONE)
    {
        result = result | DEINT_CPU; // NB always assumed
        if (Deinterlacer.contains(DEINT_QUALITY_SHADER))
            result = result | DEINT_SHADER;
        if (Deinterlacer.contains(DEINT_QUALITY_DRIVER))
            result = result | DEINT_DRIVER;
    }

    return result;
}

void MythVideoOutput::SetDeinterlacing(bool Enable, bool DoubleRate, MythDeintType Force /*=DEINT_NONE*/)
{


    if (!Enable)
    {
        m_deinterlacing = false;
        m_deinterlacing2X = false;
        m_forcedDeinterlacer = DEINT_NONE;
        m_videoBuffers.SetDeinterlacing(DEINT_NONE, DEINT_NONE, m_videoCodecID);
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "Disabled all deinterlacing");
        return;
    }

    m_deinterlacing   = Enable;
    m_deinterlacing2X = DoubleRate;
    m_forcedDeinterlacer = Force;

    MythDeintType singlerate = DEINT_NONE;
    MythDeintType doublerate = DEINT_NONE;
    if (DEINT_NONE != Force)
    {
        singlerate = Force;
        if (DoubleRate)
            doublerate = Force;
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "Overriding deinterlacers");
    }
    else if (m_dbDisplayProfile)
    {
        singlerate = ParseDeinterlacer(m_dbDisplayProfile->GetSingleRatePreferences());
        if (DoubleRate)
            doublerate = ParseDeinterlacer(m_dbDisplayProfile->GetDoubleRatePreferences());
    }

    LOG(VB_GENERAL, LOG_INFO, LOC + QString("SetDeinterlacing (Doublerate %1): Single %2 Double %3")
        .arg(DoubleRate).arg(DeinterlacerPref(singlerate)).arg(DeinterlacerPref(doublerate)));
    m_videoBuffers.SetDeinterlacing(singlerate, doublerate, m_videoCodecID);
}

/**
 * \brief Tells video output to discard decoded frames and wait for new ones.
 * \bug We set the new width height and aspect ratio here, but we should
 *      do this based on the new video frames in Show().
 */
bool MythVideoOutput::InputChanged(const QSize& VideoDim, const QSize& VideoDispDim,
                                   float VideoAspect, MythCodecID  CodecID,
                                   bool& /*AspectOnly*/, int ReferenceFrames, bool /*ForceChange*/)
{
    SourceChanged(VideoDim, VideoDispDim, VideoAspect);
    m_maxReferenceFrames = ReferenceFrames;
    AVCodecID avCodecId = myth2av_codecid(CodecID);
    AVCodec* codec = avcodec_find_decoder(avCodecId);
    QString codecName;
    if (codec)
        codecName = codec->name;
    if (m_dbDisplayProfile)
        m_dbDisplayProfile->SetInput(GetVideoDispDim(), 0 ,codecName);
    m_videoCodecID = CodecID;
    DiscardFrames(true, true);
    // Update deinterlacers for any input change
    SetDeinterlacing(m_deinterlacing, m_deinterlacing2X, m_forcedDeinterlacer);
    return true;
}

void MythVideoOutput::GetOSDBounds(QRect& Total, QRect& Visible,
                                   float& VisibleAspect,
                                   float& FontScaling,
                                   float ThemeAspect) const
{
    Total = GetTotalOSDBounds();
    Visible = GetVisibleOSDBounds(VisibleAspect, FontScaling, ThemeAspect);
}

/**
 * \brief Returns visible portions of total OSD bounds
 * \param VisibleAspect physical aspect ratio of bounds returned
 * \param FontScaling   scaling to apply to fonts
 * \param ThemeAspect   aspect ration of the theme
 */
QRect MythVideoOutput::GetVisibleOSDBounds(float& VisibleAspect,
                                           float& FontScaling,
                                           float ThemeAspect) const
{
    QRect dvr = GetDisplayVisibleRect();
    float dispPixelAdj = 1.0F;
    if (dvr.height() && dvr.width())
        dispPixelAdj = (GetDisplayAspect() * dvr.height()) / dvr.width();

    float ova = GetOverridenVideoAspect();
    QRect vr = GetVideoRect();
    float vs = vr.height() ? static_cast<float>(vr.width()) / vr.height() : 1.0F;
    VisibleAspect = ThemeAspect * (ova > 0.0F ? vs / ova : 1.F) * dispPixelAdj;

    FontScaling = 1.0F;
    return { QPoint(0,0), dvr.size() };
}

/**
 * \fn VideoOutput::GetTotalOSDBounds() const
 * \brief Returns total OSD bounds
 */
QRect MythVideoOutput::GetTotalOSDBounds() const
{
    return GetDisplayVisibleRect();
}

PictureAttributeSupported MythVideoOutput::GetSupportedPictureAttributes()
{
    return m_videoColourSpace.SupportedAttributes();
}

int MythVideoOutput::ChangePictureAttribute(PictureAttribute AttributeType, bool Direction)
{
    return m_videoColourSpace.ChangePictureAttribute(AttributeType, Direction);
}

/**
 * \fn VideoOutput::SetPictureAttribute(PictureAttribute, int)
 * \brief Sets a specified picture attribute.
 * \param attribute Picture attribute to set.
 * \param newValue  Value to set attribute to.
 * \return Set value if it succeeds, -1 if it does not.
 */
int MythVideoOutput::SetPictureAttribute(PictureAttribute Attribute, int NewValue)
{
    return m_videoColourSpace.SetPictureAttribute(Attribute, NewValue);
}

int MythVideoOutput::GetPictureAttribute(PictureAttribute AttributeType)
{
    return m_videoColourSpace.GetPictureAttribute(AttributeType);
}

void MythVideoOutput::SetFramesPlayed(long long FramesPlayed)
{
    m_framesPlayed = FramesPlayed;
}

long long MythVideoOutput::GetFramesPlayed()
{
    return m_framesPlayed;
}

bool MythVideoOutput::IsErrored() const
{
    return m_errorState != kError_None;
}

VideoErrorState MythVideoOutput::GetError() const
{
    return m_errorState;
}

/// \brief Sets whether to use a normal number of buffers or fewer buffers.
void MythVideoOutput::SetPrebuffering(bool Normal)
{
    m_videoBuffers.SetPrebuffering(Normal);
}

/// \brief Tells video output to toss decoded buffers due to a seek
void MythVideoOutput::ClearAfterSeek()
{
    m_videoBuffers.ClearAfterSeek();
}

/// \brief Returns number of frames that are fully decoded.
int MythVideoOutput::ValidVideoFrames() const
{
    return static_cast<int>(m_videoBuffers.ValidVideoFrames());
}

/// \brief Returns number of frames available for decoding onto
int MythVideoOutput::FreeVideoFrames()
{
    return static_cast<int>(m_videoBuffers.FreeVideoFrames());
}

/// \brief Returns true iff enough frames are available to decode onto.
bool MythVideoOutput::EnoughFreeFrames()
{
    return m_videoBuffers.EnoughFreeFrames();
}

/// \brief Returns true iff there are plenty of decoded frames ready
///        for display.
bool MythVideoOutput::EnoughDecodedFrames()
{
    return m_videoBuffers.EnoughDecodedFrames();
}

/// \bug not implemented correctly. vpos is not updated.
VideoFrame* MythVideoOutput::GetLastDecodedFrame()
{
    return m_videoBuffers.GetLastDecodedFrame();
}

/// \brief Returns string with status of each frame for debugging.
QString MythVideoOutput::GetFrameStatus() const
{
    return m_videoBuffers.GetStatus();
}

/// \brief Returns frame from the head of the ready to be displayed queue,
///        if StartDisplayingFrame has been called.
VideoFrame* MythVideoOutput::GetLastShownFrame()
{
    return m_videoBuffers.GetLastShownFrame();
}

/**
 * \fn VideoOutput::CopyFrame(VideoFrame*, const VideoFrame*)
 * \brief Copies frame data from one VideoFrame to another.
 *
 *  Note: The frames must have the same width, height, and format.
 * \param To   The destination frame.
 * \param From The source frame
 */
void MythVideoOutput::CopyFrame(VideoFrame* To, const VideoFrame* From)
{
    if (To == nullptr || From == nullptr)
        return;

    To->frameNumber = From->frameNumber;
    To->disp_timecode = From->disp_timecode;
    To->frameCounter = From->frameCounter;

    copy(To, From);
}

/// \brief translates caption/dvd button rectangle into 'screen' space
QRect MythVideoOutput::GetImageRect(const QRect& Rect, QRect* DisplayRect)
{
    qreal hscale = 0.0;
    QSize video_size   = GetVideoDispDim();
    int image_height   = video_size.height();
    int image_width    = (image_height > 720) ? 1920 :
                         (image_height > 576) ? 1280 : 720;
    qreal image_aspect = static_cast<qreal>(image_width) / image_height;
    qreal pixel_aspect = static_cast<qreal>(video_size.width()) / video_size.height();

    QRect rect1 = Rect;
    if (DisplayRect && DisplayRect->isValid())
    {
        QTransform m0;
        m0.scale(static_cast<qreal>(image_width)  / DisplayRect->width(),
                 static_cast<qreal>(image_height) / DisplayRect->height());
        rect1 = m0.mapRect(rect1);
        rect1.translate(DisplayRect->left(), DisplayRect->top());
    }
    QRect result = rect1;

    QRect dvr_rec = GetDisplayVideoRect();
    QRect vid_rec = GetVideoRect();

    hscale = image_aspect / pixel_aspect;
    if (hscale < 0.99 || hscale > 1.01)
    {
        vid_rec.setLeft(static_cast<int>(lround(static_cast<qreal>(vid_rec.left()) * hscale)));
        vid_rec.setWidth(static_cast<int>(lround(static_cast<qreal>(vid_rec.width()) * hscale)));
    }

    qreal vscale = static_cast<qreal>(dvr_rec.width()) / image_width;
    hscale = static_cast<qreal>(dvr_rec.height()) / image_height;
    QTransform m1;
    m1.translate(dvr_rec.left(), dvr_rec.top());
    m1.scale(vscale, hscale);

    vscale = static_cast<qreal>(image_width) / vid_rec.width();
    hscale = static_cast<qreal>(image_height) / vid_rec.height();
    QTransform m2;
    m2.scale(vscale, hscale);
    m2.translate(-vid_rec.left(), -vid_rec.top());

    result = m2.mapRect(result);
    result = m1.mapRect(result);
    return result;
}

/**
 * \brief Returns a QRect describing an area of the screen on which it is
 *        'safe' to render the On Screen Display. For 'fullscreen' OSDs this
 *        will still translate to a subset of the video frame area to ensure
 *        consistency of presentation for subtitling etc.
 */
QRect MythVideoOutput::GetSafeRect()
{
    static constexpr float kSafeMargin = 0.05F;
    float dummy = NAN;
    QRect result = GetVisibleOSDBounds(dummy, dummy, 1.0F);
    int safex = static_cast<int>(static_cast<float>(result.width())  * kSafeMargin);
    int safey = static_cast<int>(static_cast<float>(result.height()) * kSafeMargin);
    return { result.left() + safex, result.top() + safey,
             result.width() - (2 * safex), result.height() - (2 * safey) };
}

const VideoFrameTypeVec *MythVideoOutput::DirectRenderFormats() const
{
    return m_renderFrameTypes;
}

/**
 * \brief Blocks until it is possible to return a frame for decoding onto.
 */
VideoFrame* MythVideoOutput::GetNextFreeFrame()
{
    return m_videoBuffers.GetNextFreeFrame();
}

/// \brief Releases a frame from the ready for decoding queue onto the
///        queue of frames ready for display.
void MythVideoOutput::ReleaseFrame(VideoFrame* Frame)
{
    m_videoBuffers.ReleaseFrame(Frame);
}

/// \brief Releases a frame for reuse if it is in limbo.
void MythVideoOutput::DeLimboFrame(VideoFrame* Frame)
{
    m_videoBuffers.DeLimboFrame(Frame);
}

/// \brief Tell GetLastShownFrame() to return the next frame from the head
///        of the queue of frames to display.
void MythVideoOutput::StartDisplayingFrame()
{
    m_videoBuffers.StartDisplayingFrame();
}

/// \brief Releases frame returned from GetLastShownFrame() onto the
///        queue of frames ready for decoding onto.
void MythVideoOutput::DoneDisplayingFrame(VideoFrame* Frame)
{
    m_videoBuffers.DoneDisplayingFrame(Frame);
}

/// \brief Releases frame from any queue onto the
///        queue of frames ready for decoding onto.
void MythVideoOutput::DiscardFrame(VideoFrame* Frame)
{
    m_videoBuffers.DiscardFrame(Frame);
}

/// \brief Releases all frames not being actively displayed from any queue
///        onto the queue of frames ready for decoding onto.
void MythVideoOutput::DiscardFrames(bool KeyFrame, bool /*Flushed*/)
{
    m_videoBuffers.DiscardFrames(KeyFrame);
}
