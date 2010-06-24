/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2010 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#  include "../../config.h"
#endif
#include <stdint.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <stdio.h>
#include <string.h>
/*
#if HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
*/
#include <stdlib.h>
#include <jack/jack.h>
#include "bio2jack.h"
#include "../../deadbeef.h"

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

static DB_output_t plugin;
DB_functions_t *deadbeef;

static intptr_t pjack_tid;
static int pjack_terminate;
unsigned int pjack_rate;
static int state;
int jack_device;
static int bits_per_sample = 16;
static int channels = 2;
static uintptr_t mutex;

#define jack_blocksize 1024

static void
pjack_thread (void *context);

static int
pjack_callback (char *stream, int len);

static int
pjack_init (void) {
    trace ("pjack_init\n");
    state = OUTPUT_STATE_STOPPED;
    pjack_terminate = 0;
    
    mutex = 0;
    JACK_Init();
    JACK_SetClientName("deadbeef");
    int retval = JACK_Open(&jack_device, bits_per_sample, &pjack_rate, channels);
    if (retval) {
	perror("failed to open jack with Jack_Open");
	plugin.free();
	return -1;
    } 
    fprintf(stderr, "jack_device: %d,pjack_rate: %d", jack_device, pjack_rate);
    /*    jack_options_t options = JackNoStartServer;
    jack_status_t status;
    jack_client_t *client = jack_client_open ("test", options, &status, NULL);
    if (client == NULL)
    {
        perror("jack_client_open() failed.");
        if (status & JackServerFailed)
        {
            perror("Unable to connect to JACK server.");
        }
        return -1;
	}
      pjack_rate = jack_get_sample_rate(client);
      jack_client_close (client);    */
              pjack_rate = 44100;
    // prepare jack for playback
    
    mutex = deadbeef->mutex_create ();

    pjack_tid = deadbeef->thread_start (pjack_thread, NULL);
    return 0;
}

static int
pjack_change_rate (int rate) {
  /*    if (!fd) {
        return pjack_rate;
	}*/

    if (rate == pjack_rate) {
        trace ("pjack_change_rate %d: ignored\n", rate);
        return rate;
    } else {
	perror("can't  change samplerate");
	return pjack_rate;
    }

/*    deadbeef->mutex_lock (mutex);
    if (ioctl (fd, SNDCTL_DSP_SPEED, &rate) == -1) {
        fprintf (stderr, "oss: can't switch to %d samplerate\n", rate);
        perror ("SNDCTL_DSP_CHANNELS");
        plugin.free ();
        return -1;
    }
    pjack_rate = rate;
    deadbeef->mutex_unlock (mutex);
  */
    
}

static int
pjack_free (void) {
    trace ("pjack_free\n");
    if (!pjack_terminate) {
        if (pjack_tid) {
            pjack_terminate = 1;
            deadbeef->thread_join (pjack_tid);
        }
        pjack_tid = 0;
        state = OUTPUT_STATE_STOPPED;
        pjack_terminate = 0;
        if (jack_device) {
            JACK_Close (jack_device);
        }
        if (mutex) {
            deadbeef->mutex_free (mutex);
            mutex = 0;
        }
    }
    return 0;
}

static int
pjack_play (void) {
    if (!pjack_tid) {
        if (pjack_init () < 0) {
            return -1;
        }
    }
    state = OUTPUT_STATE_PLAYING;
    return 0;
}

static int
pjack_stop (void) {
    state = OUTPUT_STATE_STOPPED;
    deadbeef->streamer_reset (1);
    return pjack_free();
}

static int
pjack_pause (void) {
    if (state == OUTPUT_STATE_STOPPED) {
        return -1;
    }
    // set pause state
    state = OUTPUT_STATE_PAUSED;
    return pjack_free();
}

static int
pjack_unpause (void) {
    // unset pause state
    if (state == OUTPUT_STATE_PAUSED) {

       if (!pjack_tid) {
          if(pjack_init() < 0) {
             return -1;
          }
       }

       state = OUTPUT_STATE_PLAYING;
    }
    return 0;
}

static int
pjack_get_rate (void) {
    return pjack_rate;
}

static int
pjack_get_bps (void) {
    return bits_per_sample;
}

static int
pjack_get_channels (void) {
    return channels;
}

static int
pjack_get_endianness (void) {
#if WORDS_BIGENDIAN
    return 1;
#else
    return 0;
#endif
}

static void
pjack_thread (void *context) {
#ifdef __linux__
    prctl (PR_SET_NAME, "deadbeef-jack", 0, 0, 0, 0);
#endif
    for (;;) {
        if (pjack_terminate) {
            break;
        }
        if (state != OUTPUT_STATE_PLAYING || !deadbeef->streamer_ok_to_read (-1)) {
            usleep (1000);
            continue;
        }

        int res = 0;
	//	perror("pjack_thread");
        char buf[jack_blocksize];
	int write_size = pjack_callback (buf, sizeof (buf));
	if ( write_size != jack_blocksize ) fprintf (stderr, "oops, size is %i", write_size);
	deadbeef->mutex_lock (mutex);
        if ( write_size > 0 )
	  res = JACK_Write (jack_device, (unsigned char*) buf, write_size);

	deadbeef->mutex_unlock (mutex);
        if (res != write_size) {
            fprintf (stderr, "jack: failed to write buffer %i - %i\n",res,write_size);
        } 
	//	usleep (5600);
	usleep (jack_blocksize * 1000000 / pjack_rate /8);
	//	usleep (2822);
    }
}

static int
pjack_callback (char *stream, int len) {
    int bytesread = deadbeef->streamer_read (stream, len);
    /*   int16_t ivolume = deadbeef->volume_get_amp () * 1000;
    for (int i = 0; i < bytesread/2; i++) {
        ((int16_t*)stream)[i] = (int16_t)(((int32_t)(((int16_t*)stream)[i])) * ivolume / 1000);
	}*/

    return bytesread;
}

static int
pjack_get_state (void) {
    return state;
}

static int
pjack_plugin_start (void) {
  /*   JACK_Init();
       JACK_SetClientName("deadbeef");*/
    return 0;
}

static int
pjack_plugin_stop (void) {
  /*    JACK_Close(jack_device);*/
    return 0;
}

DB_plugin_t *
jack_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

// define plugin interface
static DB_output_t plugin = {
    DB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.nostop = 0,
    .plugin.type = DB_PLUGIN_OUTPUT,
    .plugin.id = "jack",
    .plugin.name = "Jack output plugin",
    .plugin.descr = "plays sound via Jack API",
    .plugin.author = "Dmitry Klimov",
    .plugin.email = "lazyklimm@gmail.com",
    .plugin.website = "http://deadbeef.sf.net",
    .plugin.start = pjack_plugin_start,
    .plugin.stop = pjack_plugin_stop,
    .init = pjack_init,
    .free = pjack_free,
    .change_rate = pjack_change_rate,
    .play = pjack_play,
    .stop = pjack_stop,
    .pause = pjack_pause,
    .unpause = pjack_unpause,
    .state = pjack_get_state,
    .samplerate = pjack_get_rate,
    .bitspersample = pjack_get_bps,
    .channels = pjack_get_channels,
    .endianness = pjack_get_endianness,
};
