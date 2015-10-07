/*
 *   gegl-video <video-file|frame-folder|frames|edl>
 *
 *      --output-frames path/image- --frame-extension png
 *      --output-video  path/video.avi
 *      --play (default)
 *      -- gegl:threshold value=0.3
 *
 * the edl,. should be a list of frames.. and video with frame start->end bits
 * 0001.png
 * 0002.png
 * 0003.png
 * 0004.png
 * foo.avi 3-210
 * clip2.avi
 *
 *   image folders are folder containing image files to be played in sequence
 *   each image file contains its own embedded PCM data, facilitating easy re-assembly.
 *
 */

#include <gegl.h>
#include <gegl-audio.h>
#include <glib/gprintf.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef enum NeglRunMode {
  NEGL_NO_UI = 0,
  NEGL_VIDEO,
  NEGL_TERRAIN,
  NEGL_UI,
} NeglRunmode;

int frame_start = 0;
int frame_end   = 0;

char *frame_extension = ".png";
char *frame_path = NULL;
char *video_path = NULL;
int output_frame_no = -1;
int run_mode = NEGL_NO_UI;
int show_progress = 0;

long babl_ticks (void);

void usage(void);
void usage(void)
{
      printf ("usage: negl [options] <video> [thumb]\n"
" -p, --progress   - display info about current frame/progress in terminal\n"
"\n"
"\n"
"debug level control over run-mode options:\n"
" --video   - show video frames as they are decoded (for debug)\n"
" -s <frame>, --start-frame <frame>\n"
"           - first frame to extract analysis from (default 0)\n"
" -e <frame>, --end-frame <frame>\n"
"           - last frame to extract analysis from (default is 0 which means auto end)\n"
"\n"
"Options can also follow then video (and thumb) arguments.\n"
"\n");
  exit (0);
}

static void parse_args (int argc, char **argv)
{
  int i;
  int stage = 0;
  for (i = 1; i < argc; i++)
  {
    if (g_str_equal (argv[i], "-p") ||
        g_str_equal (argv[i], "--progress"))
    {
      show_progress = 1;
    }
    else if (g_str_equal (argv[i], "-s") ||
             g_str_equal (argv[i], "--start-frame"))
    {
      frame_start = g_strtod (argv[i+1], NULL);
      i++;
    } 
    else if (g_str_equal (argv[i], "-c") ||
             g_str_equal (argv[i], "--frame-count"))
    {
      frame_end = frame_start + g_strtod (argv[i+1], NULL) - 1;
      i++;
    } 
    else if (g_str_equal (argv[i], "--output-frames") ||
             g_str_equal (argv[i], "-of"))
    {
      frame_path = g_strdup (argv[i+1]);
      i++;
    } 
    else if (g_str_equal (argv[i], "--output-frame-start") ||
             g_str_equal (argv[i], "-ofs`"))
    {
      output_frame_no = g_strtod (argv[i+1], NULL);
      i++;
    } 
    else if (g_str_equal (argv[i], "--frame-extension") ||
             g_str_equal (argv[i], "-fe"))
    {
      frame_extension = g_strdup (argv[i+1]);
      i++;
    } 
    else if (g_str_equal (argv[i], "-e") ||
             g_str_equal (argv[i], "--end-frame"))
    {
      frame_end = g_strtod (argv[i+1], NULL);
      i++;
    } 
    else if (g_str_equal (argv[i], "--ui"))
    {
      run_mode = NEGL_VIDEO;
    }
    else if (g_str_equal (argv[i], "--no-ui"))
    {
      run_mode = NEGL_NO_UI;
    }
    else if (g_str_equal (argv[i], "-h") ||
             g_str_equal (argv[i], "--help"))
    {
      usage();
    }
    else if (stage == 0)
    {
      video_path = g_strdup (argv[i]);
      stage = 1;
    } 
  }

}

GeglNode   *gegl_decode  = NULL;
GeglNode   *gegl_display = NULL;
GeglNode   *display      = NULL;

GeglBuffer *previous_video_frame  = NULL;
GeglBuffer *video_frame  = NULL;

GeglNode *store, *load;

static void decode_frame_no (int frame)
{
  if (video_frame)
  {
    g_object_unref (video_frame);
  }
  video_frame = NULL;
  gegl_node_set (load, "frame", frame, NULL);
  gegl_node_process (store);
}

GeglNode *translate = NULL;

gint
main (gint    argc,
      gchar **argv)
{
  GeglNode *display = NULL;
  GeglNode *readbuf = NULL;

  if (argc < 2)
    usage();

  gegl_init (&argc, &argv);
  parse_args (argc, argv);

  gegl_decode = gegl_node_new ();
  gegl_display = gegl_node_new ();

  store = gegl_node_new_child (gegl_decode,
                               "operation", "gegl:buffer-sink",
                               "buffer", &video_frame, NULL);
  load = gegl_node_new_child (gegl_decode,
                              "operation", "gegl:ff-load",
                              "frame", 0,
                              "path", video_path,
                              NULL);
  gegl_node_link_many (load, store, NULL);

  decode_frame_no (0);

  {
    int frames = 0; 
    double frame_rate = 0;
    gegl_node_get (load, "frames", &frames, NULL);
    gegl_node_get (load, "frame-rate", &frame_rate, NULL);
    
    if (frame_end == 0)
      frame_end = frames;
  }

  if (output_frame_no == -1)
    output_frame_no = frame_start;

  printf ("frames: %i - %i\n", frame_start, frame_end);
  printf ("video: %s\n", video_path);

  switch(run_mode)
  {
    case NEGL_NO_UI:
      break;
    case NEGL_VIDEO:
      readbuf = gegl_node_new_child (gegl_display,
                                     "operation", "gegl:buffer-source",
                                     NULL);
      display = gegl_node_create_child (gegl_display, "gegl:display");
      gegl_node_link_many (readbuf, display, NULL);
      break;
  }

  {
    gint frame;
    for (frame = frame_start; frame <= frame_end; frame++)
      {
        if (show_progress)
        {
          fprintf (stdout, "\r%2.1f%% %i/%i (%i)", 
                   (frame-frame_start) * 100.0 / (frame_end-frame_start),
                   frame-frame_start,
                   frame_end-frame_start,
                   frame);
          fflush (stdout);
        }

	decode_frame_no (frame);

        switch(run_mode)
        {
          case NEGL_NO_UI:
            break;
          case NEGL_VIDEO:
	    gegl_node_set (readbuf, "buffer", video_frame, NULL);
	    gegl_node_process (display);
            break;
        }

	if (frame_path)
	{
          char *path = g_strdup_printf ("%s%07i%s", frame_path, output_frame_no++, frame_extension);
	  GeglNode *save_graph = gegl_node_new ();
	  GeglNode *readbuf, *save;
	  readbuf = gegl_node_new_child (save_graph, "operation", "gegl:buffer-source", "buffer", video_frame, NULL);
	  save = gegl_node_new_child (save_graph, "operation", "gegl:png-save", "bitdepth", 8, "compression", 2,
	    "path", path, NULL);
	    gegl_node_link_many (readbuf, save, NULL);
	  gegl_node_process (save);
	  g_object_unref (save_graph);
          g_free (path);
	}

      }
      if (show_progress)
        fprintf (stdout, "\n");
  }

  if (video_frame)
    g_object_unref (video_frame);
  video_frame = NULL;
  g_object_unref (gegl_decode);
  g_object_unref (gegl_display);

  gegl_exit ();
  return 0;
}