/*! \file    transcode.h
 * \author   Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief    Audio/Video transcoder (headers)
 * \details  Implementation of a simple transcoder utility that plugins
 * can make use of to transcode audio/video frames to a Janus file. This
 * file just saves RTP frames in a structured way, so that they can be
 * post-processed later on to get a valid container file (e.g., a .opus
 * file for Opus audio or a .webm file for VP8 video) and keep things
 * simpler on the plugin and core side. Check the \ref transcodeings
 * documentation for more details.
 * \note If you want to transcode both audio and video, you'll have to use
 * two different transcoders. Any muxing in the same container will have
 * to be done in the post-processing phase.
 *
 * \ingroup core
 * \ref core
 */

#ifndef JANUS_TRANSCODE_H
#define JANUS_TRANSCODE_H

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <opus/opus.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "mutex.h"
#include "refcount.h"

#define LIBAVCODEC_VER_AT_LEAST(major, minor) \
		(LIBAVCODEC_VERSION_MAJOR > major || \
	 	(LIBAVCODEC_VERSION_MAJOR == major && \
	  	LIBAVCODEC_VERSION_MINOR >= minor))

#if LIBAVCODEC_VER_AT_LEAST(51, 42)
	#define PIX_FMT_YUV420P AV_PIX_FMT_YUV420P
#endif

#if LIBAVCODEC_VER_AT_LEAST(56, 56)
	#ifndef CODEC_FLAG_GLOBAL_HEADER
	#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
	#endif
	#ifndef FF_INPUT_BUFFER_PADDING_SIZE
	#define FF_INPUT_BUFFER_PADDING_SIZE AV_INPUT_BUFFER_PADDING_SIZE
	#endif
#endif

#if LIBAVCODEC_VER_AT_LEAST(57, 14)
	#define USE_CODECPAR
#endif

#define AUDIO_MIN_SEQUENTIAL 		2
#define AUDIO_MAX_MISORDER			50
/* Mixer settings */
#define AUDIO_DEFAULT_PREBUFFERING	6
/* Opus settings */
#define	AUDIO_BUFFER_SAMPLES		8000
#define	AUDIO_OPUS_SAMPLES			960
#define AUDIO_DEFAULT_COMPLEXITY	4

#define JANUS_LIVE_BUFFER_MAX  2 * 1024 * 1024
#define htonll(x) ((1==htonl(1)) ? (x) : ((gint64)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((gint64)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

typedef struct janus_rtp_jb			janus_rtp_jb;
typedef struct janus_transcoder_pub 		janus_transcode_pub;
typedef struct janus_transcoder_el 		janus_transcode_el;

typedef struct janus_adecoder_opus {
	uint8_t 			channels;
	uint32_t 			sampling_rate;		/* Sampling rate (e.g., 16000 for wideband; can be 8, 12, 16, 24 or 48kHz) */
	OpusDecoder 		*decoder;			/* Opus decoder instance */

	gboolean 			fec;				/* Opus FEC status */
	uint16_t 			expected_seq;		/* Expected sequence number */
	uint16_t 			probation; 			/* Used to determine new ssrc validity */
	uint32_t 			last_timestamp;		/* Last in seq timestamp */
	
	janus_rtp_jb		*jb;
} janus_adecoder_opus;


typedef struct janus_aencoder_fdkaac {
	int 				sample_rate;
	int 				channels;
	int 				bitrate;
	AVFrame				*aframe;
	AVPacket			*apacket;
	AVCodec				*acodec;
    AVCodecContext		*actx;

	int					nb_samples;
	int					buflen;
	char				*buffer;
	janus_rtp_jb		*jb;
} janus_aencoder_fdkaac;


typedef struct janus_frame_packet {
	char 							*data;			/* data */
	uint16_t 						len;			/* Length of the data */
	uint16_t 						seq;			/* RTP Sequence number */
	uint64_t 						ts;				/* RTP Timestamp */
	
	gboolean 						video;
	uint32_t 						ssrc;
	gint64 							created;
	int 							keyFrame;

	int 							pt;				/* Payload type of the data */
	int 							skip;			/* Bytes to skip, besides the RTP header */
	int 							audiolevel;		/* Value of audio level in RTP extension, if parsed */
	int 							rotation;		/* Value of rotation in RTP extension, if parsed */
	uint8_t 						drop;			/* Whether this packet can be dropped (e.g., padding)*/
	struct janus_frame_packet 		*next;
	struct janus_frame_packet 		*prev;
} janus_frame_packet;


typedef struct janus_rtp_jb {
	uint32_t 						last_ts;
	uint32_t 						reset;
	uint32_t 						ssrc;
	uint16_t 						last_seq;
	uint16_t 						last_seq_out;
	int 							times_resetted;
	int			 					post_reset_pkts;

	uint32_t 						tb;
	uint64_t 						start_ts;
	gint64 							start_sys;

	gboolean 						keyframe_found;
	int 							keyFrame;
	int 							frameLen;
	int								buflen;
	uint8_t 						*received_frame;
	uint64_t 						ts;
	janus_transcoder_pub 					*pub;
	janus_adecoder_opus				*adecoder;
	janus_aencoder_fdkaac			*aencoder;
	uint32_t						lastts;
	uint32_t						offset;
	
	uint32_t 						size;
	janus_frame_packet 				*list;
	janus_frame_packet 				*last;
} janus_rtp_jb;


typedef struct janus_transcoder_pub {
	char 							*url;
	char 							*acodec;
	char 							*vcodec;
	gint64 							created;
	volatile gboolean				closed;

	janus_rtp_jb 					*audio_jb;
	janus_rtp_jb 					*video_jb;
    
	GSource 						*jb_src;
	GSource 						*pub_src;
	janus_transcoder_el 					*jb_loop;
	janus_transcoder_el 					*pub_loop;

	int 							audio_level_extmap_id;
	int 							video_orient_extmap_id;
	
	uint32_t 						size;
	janus_frame_packet 				*list;
	janus_frame_packet 				*last;
	uint32_t 						start_ts;
	gint64 							start_sys;

	int 							max_width;
	int 							max_height;
	gboolean 						init_flag;
	AVFormatContext 				*fctx;
	AVStream 						*vStream;
	AVStream 						*aStream;
#ifdef USE_CODECPAR
	AVCodecContext 					*vEncoder;
	AVCodecContext 					*aEncoder;
#endif
	AVBitStreamFilterContext		*aacbsf;
	uint32_t 						lastts;

	janus_mutex 					mutex;
	janus_mutex 					mutex_transcode;
	volatile gint 					destroyed;
	janus_refcount 					ref;
} janus_transcoder_pub;


typedef struct janus_transcoder_el {
	int 							id;
	char 							*name;
	GThread 						*thread;
	GMainLoop 						*mainloop;
	GMainContext 					*mainctx;
	janus_transcode_pub 					*pub;
} janus_transcoder_el;




/*! \brief Media types we can transcode */
typedef enum janus_transcoder_medium {
	JANUS_TRANSCODER_AUDIO,
	JANUS_TRANSCODER_VIDEO,
	JANUS_TRANSCODER_DATA
} janus_transcoder_medium;

/*! \brief Structure that represents a transcoder */
typedef struct janus_transcoder {
	/*! \brief Absolute path to the directory where the transcoder file is stored */
	char *dir;
	/*! \brief Filename of this transcoder file */
	char *filename;
	/*! \brief Transcodeing file */
	FILE *file;
	/*! \brief Codec the packets to transcode are encoded in ("vp8", "vp9", "h264", "opus", "pcma", "pcmu", "g722") */
	char *codec;
	/*! \brief When the transcodeing file has been created and started */
	gint64 created, started;
	/*! \brief Media this instance is transcodeing */
	janus_transcoder_medium type;
	/*! \brief Whether the info header for this transcoder instance has already been written or not */
	volatile int header;
	/*! \brief Whether this transcoder instance can be used for writing or not */
	volatile int writable;
	/*! \brief Mutex to lock/unlock this transcoder instance */
	janus_mutex mutex;
	/*! \brief Atomic flag to check if this instance has been destroyed */
	volatile gint destroyed;
	/*! \brief Reference counter for this instance */
	janus_refcount ref;
} janus_transcoder;

/*! \brief Initialize the transcoder code
 * @param[in] tempnames Whether the filenames should have a temporary extension, while saving, or not
 * @param[in] extension Extension to add in case tempnames is true */
void janus_transcoder_init(gboolean tempnames, const char *extension);
/*! \brief De-initialize the transcoder code */
void janus_transcoder_deinit(void);

/*! \brief Create a new transcoder
 * \note If no target directory is provided, the current directory will be used. If no filename
 * is passed, a random filename will be used.
 * @param[in] dir Path of the directory to save the transcodeing into (will try to create it if it doesn't exist)
 * @param[in] codec Codec the packets to transcode are encoded in ("vp8", "opus", "h264", "g711", "vp9")
 * @param[in] filename Filename to use for the transcodeing
 * @returns A valid janus_transcoder instance in case of success, NULL otherwise */

janus_transcoder_pub *janus_transcoder_pub_create(const char *url, const char *acodec, const char *vcodec);
int janus_transcoder_pub_save_frame(janus_transcoder_pub *pub, char *buffer, uint length, gboolean video, int slot);
int janus_transcoder_pub_close(janus_transcoder_pub *pub);
void janus_transcoder_pub_destroy(janus_transcoder_pub *pub);

janus_transcoder *janus_transcoder_create(const char *dir, const char *codec, const char *filename);
/*! \brief Save an RTP frame in the transcoder
 * @param[in] transcoder The janus_transcoder instance to save the frame to
 * @param[in] buffer The frame data to save
 * @param[in] length The frame data length
 * @returns 0 in case of success, a negative integer otherwise */
int janus_transcoder_save_frame(janus_transcoder *transcoder, char *buffer, uint length);
/*! \brief Close the transcoder
 * @param[in] transcoder The janus_transcoder instance to close
 * @returns 0 in case of success, a negative integer otherwise */
int janus_transcoder_close(janus_transcoder *transcoder);
/*! \brief Destroy the transcoder instance
 * @param[in] transcoder The janus_transcoder instance to destroy */
void janus_transcoder_destroy(janus_transcoder *transcoder);


#endif /* _JANUS_TRANSCODE_H */


