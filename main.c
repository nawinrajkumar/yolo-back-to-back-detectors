/*******************************************************************************
 * Author: Nawin Raj Kumar
 *******************************************************************************/

#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <cuda_runtime_api.h>
#include "gstnvdsmeta.h"
#include "MQTTClient.h"


/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 1920
#define MUXER_OUTPUT_HEIGHT 1080

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 40000
#define MAX_DISPLAY_LEN 64

#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "ExampleClientPub"
#define TOPIC       "MQTT Examples"
#define QOS         1
#define TIMEOUT     10000L

gint frame_number = 0;


static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  MQTTClient client;
  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;

  int rc;

  MQTTClient_create(&client, ADDRESS, CLIENTID,
      MQTTCLIENT_PERSISTENCE_NONE, NULL);
  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;

  if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
  {
      g_print("Failed to connect, return code %d\n", rc);
      exit(-1);
  }
  GstBuffer *buf = (GstBuffer *) info->data;
  guint num_rects = 0;
  NvDsObjectMeta *obj_meta = NULL;
  NvDsMetaList * l_frame = NULL;
  NvDsMetaList * l_obj = NULL;
  NvDsDisplayMeta *display_meta = NULL;
  NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);
  for(l_frame = batch_meta->frame_meta_list; l_frame=NULL; l_frame = l_frame->next){
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);
    int offset = 0;
    display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
    NvOSD_TextParams *txt_params  = &display_meta->text_params[0];
    display_meta->num_labels = 1;
    offset = snprintf(txt_params->display_text, MAX_DISPLAY_LEN, "Person");
    offset = snprintf(txt_params->display_text + offset , MAX_DISPLAY_LEN, "Vehicle");
    txt_params->x_offset = 10;
    txt_params->y_offset = 12;

    /* Font , font-color and font-size */
    txt_params->font_params.font_name = "Serif";
    txt_params->font_params.font_size = 10;
    txt_params->font_params.font_color.red = 1.0;
    txt_params->font_params.font_color.green = 1.0;
    txt_params->font_params.font_color.blue = 1.0;
    txt_params->font_params.font_color.alpha = 1.0;

    /* Text background color */
    txt_params->set_bg_clr = 1;
    txt_params->text_bg_clr.red = 0.0;
    txt_params->text_bg_clr.green = 0.0;
    txt_params->text_bg_clr.blue = 0.0;
    txt_params->text_bg_clr.alpha = 1.0;

    nvds_add_display_meta_to_frame(frame_meta, display_meta); 
    // pubmsg.payload = frame_meta;
    // pubmsg.payloadlen = strlen(frame_meta);
    // pubmsg.qos = QOS;
    // pubmsg.retained = 0;
    // MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
    // g_print("Waiting for up to %d seconds for publication of %s\n"
    //         "on topic %s for client with ClientID: %s\n",
    //         (int)(TIMEOUT/1000), frame_meta, TOPIC, CLIENTID);
    // rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    // g_print("Message with delivery token %d delivered\n", token);
    // g_print ("Frame Number  Number of objects "
    //         "Vehicle Count  Person Count ");
    }

    frame_number++;

    return GST_PAD_PROBE_OK;
}

static gboolean bus_call (GstBus * bus, GstMessage * msg, gpointer data){
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      if (debug)
        g_printerr ("Error details: %s\n", debug);
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

void cb_new_pad (GstElement *qtdemux, GstPad* pad, gpointer data) {
  GstElement* h264parser = (GstElement*) data;
  gchar *name = gst_pad_get_name (pad);
  if (strcmp (name, "video_0") == 0 && 
      !gst_element_link_pads(qtdemux, name, h264parser, "sink")){
    g_printerr ("Could not link %s pad of qtdemux to sink pad of h264parser", name);
  }
}

int main (int argc, char *argv[]){
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *source = NULL, *h264parser = NULL, *nvv4l2h264enc = NULL, *qtdemux = NULL,
             *nvv4l2decoder = NULL, *streammux = NULL, *sink = NULL, *nvvidconv = NULL, *qtmux = NULL,
             *pgie = NULL, *sgie = NULL, *numberplate = NULL, *ocr = NULL,  *tracker = NULL, *nvvidconv2 = NULL, *nvosd = NULL, *h264parser2 = NULL;

  GstElement *transform = NULL;
  GstBus *bus = NULL;
  guint bus_watch_id;
  GstPad *osd_sink_pad = NULL;


  /* Check input arguments */
  if (argc != 2) {
    g_printerr ("Usage: %s </path/to/input/video.mp4>\n", argv[0]);
    return -1;
  }

  /* Standard GStreamer initialization */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  /* Create Pipeline element that will form a connection of other elements */
  pipeline = gst_pipeline_new ("deepstream_tutorial_app1");

  /* Input File source element */
  source = gst_element_factory_make ("filesrc", "file-source");

  /* QTDemux for demuxing different type of input streams */
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");

  /* Since the data format in the input file is elementary h264 stream,
   * we need a h264parser */
  h264parser = gst_element_factory_make ("h264parse", "h264-parser");

  /* Use nvdec_h264 for hardware accelerated decode on GPU */
  nvv4l2decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

  /* Create nvstreammux instance to form batches from one or more sources. */
  streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

  /* Use nvinfer to run inferencing on decoder's output,
   * behaviour of inferencing is set through config file */
  pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");

  sgie = gst_element_factory_make("nvinfer", "secondary-nvinference-engine");

  numberplate = gst_element_factory_make ("nvinfer", "numberplate-nvinference-engine");

  ocr = gst_element_factory_make ("nvinfer", "ocr-nvinference-engine");

  /* Assigns track ids to detected bounding boxes*/
  tracker = gst_element_factory_make ("nvtracker", "tracker");

  /* Use convertor to convert from NV12 to RGBA as required by nvosd */
  nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

  /* Create OSD to draw on the converted RGBA buffer */
  nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

  /* Use convertor to convert from NV12 to RGBA as required by nvosd */
  nvvidconv2 = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter2");

  /* Use convertor to convert from NV12 to H264 as required */
  nvv4l2h264enc = gst_element_factory_make ("nvv4l2h264enc", "nvv4l2h264enc");

  /* Since the data format for the output file is elementary h264 stream,
   * we need a h264parser */
  h264parser2 = gst_element_factory_make ("h264parse", "h264parser2");

  qtmux = gst_element_factory_make ("qtmux", "qtmux");

  //sink = gst_element_factory_make ("filesink", "filesink");
  sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");


  if (!pipeline || !source || !h264parser || !qtdemux ||
      !nvv4l2decoder || !streammux || !pgie || !sgie || !numberplate || !ocr || !tracker || 
      !nvvidconv || !nvosd || !nvvidconv2 || !nvv4l2h264enc || 
      !h264parser2 || !qtmux || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }


  /* we set the input filename to the source element */
  g_object_set (
    G_OBJECT (source), 
    "location", 
    argv[1], 
    NULL
  );

  g_object_set (
    G_OBJECT (streammux), 
    "batch-size", 
    1, 
    "width", 
    MUXER_OUTPUT_WIDTH, 
    "height",
    MUXER_OUTPUT_HEIGHT,
    "batched-push-timeout", 
    MUXER_BATCH_TIMEOUT_USEC, NULL
  );

  /* Set all the necessary properties of the nvinfer element,
  * the necessary ones are : */
  g_object_set (
      G_OBJECT (pgie),
      "config-file-path", 
      "configs/vehicle/yolov5_vehicle.txt", 
      NULL
    );

  g_object_set (
    G_OBJECT (sgie),
    "config-file-path",
    "configs/vehicle/secondary_config.txt",
    NULL
  );

  g_object_set (
    G_OBJECT (numberplate),
    "config-file-path",
    "configs/numberplate/numberplate.txt",
    NULL
  );

  g_object_set (
    G_OBJECT (ocr),
    "config-file-path",
    "configs/ocr/ocr_config.txt",
    NULL
  );

  /* Set all the necessary properties of the nvtracker element,
  * the necessary ones are : */
  g_object_set (
      G_OBJECT (tracker),
      "ll-lib-file", 
      "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so", 
      "ll-config-file",
      "/opt/nvidia/deepstream/deepstream/sources/apps/sample_apps/deepstream-test1/dstest1_tracker_config.txt",
      NULL
    );


  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /*
  * Link "video_0" pad of qtdemux to sink pad of h264Parse
  * "video_0" pad of qtdemux is created only when 
  * a valid video stream is found in the input
  * in that case only the pipeline will be linked
  */
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (cb_new_pad), h264parser);

  /* Set up the pipeline */
  /* we add all elements into the pipeline */
  gst_bin_add_many (
    GST_BIN (pipeline),
    source,
    qtdemux,
    h264parser,
    nvv4l2decoder,
    streammux, 
    pgie,
    sgie,
    numberplate,
    //ocr,
    tracker,
    nvvidconv, 
    nvosd,
    sink, 
    NULL
  );
  
  /* 
  * Dynamic linking
  * sink_0 pad of nvstreammux is only created on request
  * and hence cannot be linked automatically
  * Need to request it to create it and then link it 
  * to the upstream element in the pipeline
  */
  GstPad *sinkpad, *srcpad;
  gchar pad_name_sink[16] = "sink_0";
  gchar pad_name_src[16] = "src";

  /* Dynamically created pad */
  sinkpad = gst_element_get_request_pad (streammux, pad_name_sink);
  if (!sinkpad) {
    g_printerr ("Streammux request sink pad failed. Exiting.\n");
    return -1;
  }

  /* Statically created pad */
  srcpad = gst_element_get_static_pad (nvv4l2decoder, pad_name_src);
  if (!srcpad) {
    g_printerr ("Decoder request src pad failed. Exiting.\n");
    return -1;
  }

  /* Linking the pads */
  if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
      g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
      return -1;
  }

  /* Unreference the object */
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* 
   * we link the elements together
   * file-source -> qtdemux -> h264-parser -> nvh264-decoder ->
   * nvinfer -> tracker -> nvvidconv -> nvosd -> nvvidconv2 -> 
   * nvh264-encoder -> qtmux -> filesink 
  */
  if (!gst_element_link_many (source, qtdemux, NULL)) {
    g_printerr ("Source and QTDemux could not be linked: 1. Exiting.\n");
    return -1;
  }

  if (!gst_element_link_many (h264parser, nvv4l2decoder, NULL)) {
    g_printerr ("H264Parse and NvV4l2-Decoder could not be linked: 2. Exiting.\n");
    return -1;
  }

  if (!gst_element_link_many (streammux, pgie, sgie, numberplate, tracker, nvvidconv, nvosd, sink, NULL)) {
  g_printerr ("Rest of the pipeline elements could not be linked: 3. Exiting.\n");
  return -1;
  }

  osd_sink_pad = gst_element_get_static_pad (nvosd, "sink");
  if (!osd_sink_pad)
    g_print ("Unable to get sink pad\n");
  else
  gst_pad_add_probe (osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
        osd_sink_pad_buffer_probe, NULL, NULL);
  gst_object_unref (osd_sink_pad);


  /* Set the pipeline to "playing" state */
  g_print ("Using file: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait till pipeline encounters an error or EOS */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  return 0;
}