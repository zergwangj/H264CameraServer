/*
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <string>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "CameraDevice.hpp"
#include "Logger.hpp"

#define DEFAULT_RTSP_PORT   "554"

#define FRAME_WIDTH         640
#define FRAME_HEIGHT        480
#define FRAME_FPS           30


static CameraDevice g_cameraDevice;

typedef struct
{
    GstClockTime timestamp;
    GstBuffer *buffer;
} MyContext;

static void need_data(GstElement *appsrc, guint unused, MyContext *ctx)
{
    GstFlowReturn ret;

    AVFrame *capFrame = g_cameraDevice.Capture();
    if (capFrame != NULL) {
        gst_buffer_fill(ctx->buffer, 0, capFrame->data[0], capFrame->width * capFrame->height * 2);
        av_frame_unref(capFrame);
        av_frame_free(&capFrame);
    }

    GST_BUFFER_PTS(ctx->buffer) = ctx->timestamp;
    GST_BUFFER_DURATION(ctx->buffer) = gst_util_uint64_scale_int(1, GST_SECOND, FRAME_FPS);
    ctx->timestamp += GST_BUFFER_DURATION(ctx->buffer);

    g_signal_emit_by_name(appsrc, "push-buffer", ctx->buffer, &ret);
}

static void delete_context(MyContext *ctx)
{
    LOG(info) << "Delete context...";
    gst_buffer_remove_all_memory(ctx->buffer);
    delete ctx;
    g_cameraDevice.Close();
    LOG(info) << "Camera closed";
}

static void media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
    GstElement *element, *appsrc;
    MyContext *ctx;

    element = gst_rtsp_media_get_element(media);
    appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");
    gst_util_set_object_arg(G_OBJECT(appsrc), "format", "time");
    /* configure the caps of the video */
    g_object_set(G_OBJECT(appsrc), "caps", gst_caps_new_simple("video/x-raw",
                                       "format", G_TYPE_STRING, "YUY2",
                                       "width", G_TYPE_INT, FRAME_WIDTH,
                                       "height", G_TYPE_INT, FRAME_HEIGHT,
                                       "framerate", GST_TYPE_FRACTION, FRAME_FPS, 1, NULL), NULL);

    LOG(info) << "Media configure...";
    ctx = new MyContext();
    ctx->timestamp = 0;
    if (g_cameraDevice.Open("dshow", "video=USB Camera", FRAME_WIDTH, FRAME_HEIGHT, FRAME_FPS))
    {
        LOG(info) << "Camera opened";
    }
    ctx->buffer = gst_buffer_new_allocate(NULL, FRAME_WIDTH * FRAME_HEIGHT * 2, NULL);
    g_object_set_data_full(G_OBJECT(media), "my-extra-data", ctx, (GDestroyNotify)delete_context);

    g_signal_connect(appsrc, "need-data", (GCallback)need_data, ctx);
    gst_object_unref(appsrc);
    gst_object_unref(element);
}

int main(int argc, char *argv[])
{
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;

    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    server = gst_rtsp_server_new();
    g_object_set(server, "service", DEFAULT_RTSP_PORT, NULL);
    mounts = gst_rtsp_server_get_mount_points(server);

    factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, "( appsrc name=mysrc ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast ! rtph264pay name=pay0 pt=96 )");

    g_signal_connect(factory, "media-configure", (GCallback)media_configure, NULL);
    gst_rtsp_mount_points_add_factory(mounts, "/camera", factory);
    g_object_unref(mounts);
    gst_rtsp_server_attach(server, NULL);

    LOG(info) << "Camera streaming server runing...";
    g_main_loop_run(loop);

    return 0;
}