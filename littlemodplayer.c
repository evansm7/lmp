/*
 * Little (SoundTracker/ProTracker) Module Player
 *
 * World's simplest ProTracker module player.  Supports 4 channel 15- or
 * 31-instrument SoundTracker/ProTracker modules.  Doesn't support exotic
 * effects.  We may disagree on what constitutes "exotic".  :-)
 *
 * Outputs stereo or mono s16 at a configurable rate (default 44.1KHz).
 * Usage:
 *
 * Initialise player/state:
 *
 *	mps_t	mpstate;
 * 	lmp_init(&mpstate, pointer_to_modfile);
 *      lmp_set_option(&mpstate, ...); // Optional, configure looping
 *
 * Generate samples repeatedly, to fill audio buffer:
 *
 *	lmp_fill_buffer(&mpstate, sample_buffer, OUTPUT_BUFFERSIZE, LMP_MONO);
 *
 * 6 March 2020 (c) 2021 Matt Evans
 */

#include <inttypes.h>
#include <string.h>

#include "littlemodplayer.h"

////////////////////////////////////////////////////////////////////////////////
/* Tunables, define by your project: */

#ifndef LMP_SAMPLERATE
#define LMP_SAMPLERATE		44100
#endif

#ifndef LMP_DEBUG_LEVEL
#define LMP_DEBUG_LEVEL 	-1
#endif

////////////////////////////////////////////////////////////////////////////////

#define SAMPLES_PER_TICK	(LMP_SAMPLERATE/50)
#define SAMP_FP_SPLIT		12	/* 20:12 FP */

#define BS16(x)			( (((x) >> 8) & 0xff) | (((x) << 8) & 0xff00) )

#ifdef BIG_ENDIAN
#define BE_to_host16(x)		( (x) )
#define host_to_LE16(x)		( BS16(x) )
#define host_to_LE32(x)		( BS32(x) )
#else /* LE, default */
#define BE_to_host16(x)		( BS16(x) )
#define host_to_LE16(x)		( (x) )
#define host_to_LE32(x)		( (x) )
#endif

#if LMP_DEBUG_LEVEL >= 0
#include <stdio.h>
#define MPDBG(l, x...)	       	do { if (LMP_DEBUG_LEVEL >= (l)) printf(x); } while(0)
#else
/* Avoid printf, stdio.h unless necessary. */
#define MPDBG(l, x...)	       	do { } while(0)
#endif


////////////////////////////////////////////////////////////////////////////////
/* Utilities */

static unsigned int	lmp_samps_from_tempo(unsigned int tempo)
{
	/* 125 = 50Hz = LMP_SAMPLERATE/50. */
	return (125*LMP_SAMPLERATE/50)/tempo;
}

static uint32_t 	lmp_pi_from_pitch(uint16_t pitch)
{
	/* Note pitch is in units of 3.579545MHz ticks between samples.
	 * I.e. higher values are a lower output sample rate Range is 0x71 to
	 * 0x358, roughly 4181.7Hz to 31677.4Hz on a 14KHz sample?
	 *
	 * C-1 to B-1: 856,808,762,720,678,640,604,570,538,508,480,453
	 * C-2 to B-2: 428,404,381,360,339,320,302,285,269,254*,240,226
	 * C-3 to B-3: 214,202,190,180,170,160,151,143,135,127,120,113
	 *
	 * *=A-2 (This note is 1:1 at 14KHz, I think, w/ PAL freq.,
	 * 3.579545/0.014MHz = 255.68)
	 *
	 * Assuming the source sample is 14KHz, it means the output is a ratio
	 * of LMP_SAMPLERATE/14000, e.g. output one instrument sample 3.15
	 * times to output.
	 *
	 * IOW, instrument sample is output freq*LMP_SAMPLERATE/(14000*254)
	 * times, so phase inc is 1/that (in fixed-point format).
	 *
	 * FIXME: a LUT is faster than a reciprocal!!!!!!!!!111
	 */
	return ((1UL<<SAMP_FP_SPLIT)*254*14000ULL/LMP_SAMPLERATE)/(unsigned int)pitch;
}

////////////////////////////////////////////////////////////////////////////////
/* Public functions */

int 	lmp_init(mps_t *mps, uint8_t *mod_base)
{
	int tss = 0;
	/* FIXME: detect 15 vs 31-sample format */
	mps->mod_data = mod_base;

	mps->thirtyone = !strncmp("M.K.", (char *)(mod_base + 0x438), 4);

	uint8_t *instruments = mod_base + 0x14;
	mps->instruments = instruments;

	/* Unpack module header into more convenient format: */
	if (mps->thirtyone) {
		mps->length = mod_base[0x3b6];
		mps->sequence = mod_base + 0x3b8;
		mps->patterns = (uint32_t *)(mod_base + 0x43c);
	} else {
		mps->length = mod_base[0x1d6];
		mps->sequence = mod_base + 0x1d8;
		mps->patterns = (uint32_t *)(mod_base + 0x258);
	}

	/* Scan sequence for max pattern number: */
	unsigned int max_patt = 0;
	for (int i = 0; i < 128; i++) {
		MPDBG(1, "%03d: %03d\n", i, mps->sequence[i]);
		if (mps->sequence[i] > max_patt)
			max_patt = mps->sequence[i];
	}

	MPDBG(0, "Module name:   %s\n"
	      " length:       %d\n"
	      " max pattern:  %d\n"
	      , (char *)(mod_base + 0), mps->length, max_patt);

	int8_t *samples_at = (int8_t *)&mps->patterns[256*(max_patt + 1)];
	int8_t *last_sample = samples_at;
	for (int i = 0; i < (mps->thirtyone ? 31 : 15); i++) {
		uint16_t *instr = (uint16_t *)(instruments + (i*30));
		mps->inst[i].sample = last_sample;
		mps->inst[i].len = BE_to_host16(instr[10+1])*2;	 		/* Length in halfwords */
		mps->inst[i].default_volume = 0x7f & BE_to_host16(instr[10+2]);	/* 0-64 inclusive */
		mps->inst[i].repeat_pos = BE_to_host16(instr[10+3])*2;
		mps->inst[i].repeat_len = BE_to_host16(instr[10+4])*2;		/* 1 = no repeat (hmm) */
		last_sample = last_sample + mps->inst[i].len;

		MPDBG(0, "Instrument %2d at %p (+0x%08lx): len %4x vol %2d repeat pos %4x rlen %4x %s\n",
		      i, mps->inst[i].sample, (uint8_t *)mps->inst[i].sample-mod_base, mps->inst[i].len,
		      mps->inst[i].default_volume, mps->inst[i].repeat_pos, mps->inst[i].repeat_len,
		      (char *)instr);
	}

	for (int i = 0; i < 4; i++) {
		mps->cs[i].on = 0;
		mps->cs[i].inst = 0;
		mps->cs[i].vol = 0x40;
		mps->cs[i].effect = 0xff;
	}

	mps->speed = 6;
	mps->tick_counter = 0;
	mps->pos = 0;
	mps->pos_pattern = 0;

	mps->tempo = 125;
	mps->samples_per_tick = lmp_samps_from_tempo(mps->tempo);
	mps->sample_counter = mps->samples_per_tick;

	/* Defaults to looping */
	mps->song_loop = 1;
	mps->support_tempo = 1;

	return 0;
}

void	lmp_set_option(mps_t *mps, unsigned int option, unsigned int val)
{
	switch (option) {
		case LMP_OPT_LOOP:
			mps->song_loop = !!val;
			break;

		case LMP_OPT_SUPPORT_TEMPO:
			mps->support_tempo = !!val;
			break;

		default:
			break;
	}
}

/* Total number of patterns in song sequence */
unsigned int 	lmp_get_length(mps_t *mps)
{
	return mps->length;
}

/* Set position in song sequence (0..(length-1)) */
void	lmp_set_pos(mps_t *mps, unsigned int pos)
{
	if (pos < mps->length) {
		mps->pos = pos;
		mps->pos_pattern = 0;
	}
}

static void lmp_process_command(mps_t *mps, int chan, uint8_t command, uint8_t val)
{
	switch (command) {
		case 0: { 	/* Arpeggio */
			if (val != 0) {
				MPDBG(1, "Unsupported effect: Arpeggio %02x\n", val);
			}
		} break;

		case 1:
		case 2: { 	/* Portamento up/down: */
			mps->cs[chan].effect = command;
			mps->cs[chan].effect_param = val;
			MPDBG(2, "Portamento: %d %02x\n", command, val);
		} break;

		case 10: { 	/* Volume slide */
			/* Signed parameter?
			 * Clamp volume to 0-64
			 */
			int8_t v = (int8_t)val;
			int vol = mps->cs[chan].vol;
			vol += v;
			if (vol > 0x40)
				vol = 0x40;
			if (vol < 0)
				vol = 0;
			mps->cs[chan].vol = vol;
		} break;

		case 11: { 	/* Position jump (sequence) */
			mps->pos_pattern = 0;
			mps->pos = val;
			/* If out of range, check against len will catch it. */
			if (val == 0) {
				/* Detect crude loop to zero; loop code below catches this. */
				mps->pos = ~0;
			}
		} break;

		case 12: { 	/* Volume */
			if (val > 0x40)
				val = 0x40;
			mps->cs[chan].vol = val;
		} break;

		case 13: { 	/* Pattern break to row XY in next pattern */
			/* Note DECIMAL parameter! */
			int pos = (((val & 0xf0) >> 4) * 10) + (val & 0xf);
			if (pos > 63) {
				/* Ignore it? */
				MPDBG(1, "Pattern break to strange position %02x\n", val);
			} else {
				mps->pos_pattern = pos;
				mps->pos++;
			}
		} break;

		case 15: { 	/* Set speed */
			if (val > 0 && val < 0x1f) {
				mps->speed = val;
				mps->tick_counter = mps->speed;
				MPDBG(2, "Set speed %02x\n", val);
			}
			if (val >= 0x20) {
				if (mps->support_tempo) {
					mps->tempo = val;
					mps->samples_per_tick = lmp_samps_from_tempo(mps->tempo);
					MPDBG(2, "Set tempo %02x\n", val);
				} else {
					MPDBG(1, "Unsupported effect: Set tempo %02x\n", val);
				}
			}
		} break;

		case 14: { 	/* Filter/extended */
			MPDBG(1, "Unsupported effect: Filter cmd %02x\n", val);
		} break;

		default:
			MPDBG(1, "Unsupported effect: %x:%02x\n", command, val);
			break; 	/* Do nothing */
	}
}

/* Returns "done" */
static int lmp_tick(mps_t *mps)
{
	/* This is a tick event.  1 in N (tempo) ticks leads to moving
	 * the pattern onto the next row.  Initially, a tick == 50Hz (for tempo 125).
	 */
	if (mps->tick_counter > 1) { /* Can speed be 0? */
		/* Process "inter-note" effects, like portamento
		 * which are applied on a non-note intermediate tick:
		 */
		for (int chan = 0; chan < 4; chan++) {
			switch (mps->cs[chan].effect) {
				case 1: /* Fall through */
				case 2:
					if (mps->cs[chan].effect == 1) {
						mps->cs[chan].pitch -= mps->cs[chan].effect_param;
						if (mps->cs[chan].pitch < 113)
							mps->cs[chan].pitch = 113;
					} else {
						mps->cs[chan].pitch += mps->cs[chan].effect_param;
						if (mps->cs[chan].pitch > 856)
							mps->cs[chan].pitch = 856;
					}
					mps->cs[chan].phaseinc = lmp_pi_from_pitch(mps->cs[chan].pitch);
					break;

				case 0xff:
				default:
					break;
			}
		}

		mps->tick_counter--;
		return 0;
	}

	mps->tick_counter = mps->speed;

	uint8_t current_pattern = mps->sequence[mps->pos];
	uint32_t *current_frame = &mps->patterns[256*current_pattern + 4*mps->pos_pattern];

	/* OK, process the event at current_pattern[pos_pattern].  */

	MPDBG(2, "%02d(%02d):%02d ", mps->pos, current_pattern, mps->pos_pattern);

	mps->pos_pattern++;

	for (int chan = 0; chan < 4; chan++) {
		/* Format of the pattern's word:
		 *
		 * Frequency = b0[3:0],b1[7:0]
		 * Instrument = b0[4],b2[7:4]
		 * Command = b2[3:0]
		 * Value = b3[7:0]
		 */

		/* FIXME: This masking is a bit gross, as it was written for LE
		 * then bodged for BE...  Could be cleaned up!
		 */
                uint32_t note = host_to_LE32(current_frame[chan]);
		uint8_t val = note >> 24;
		uint8_t inst = ((note >> 20) & 0xf) | (note & 0x10);
		uint16_t freq = ((note << 8) & 0xf00) | ((note >> 8) & 0xff);
		uint8_t command = (note >> 16) & 0xf;

		MPDBG(3, "  %04d %02d %x%02x", freq, inst, command, val);

		/* Reset inter-note effects: */
		mps->cs[chan].effect = 0xff;

		/* Play a note? */
		if (freq &&
		    (inst <= (mps->thirtyone ? 31 : 15))) {
			mps->cs[chan].on = 1;

			/* inst = 0 means last, and *can* play a note.
			 * Also uses current volume of channel.
			 */
			if (inst) {
				mps->cs[chan].inst = inst-1;
				mps->cs[chan].vol = mps->inst[inst-1].default_volume;
			}
			/* Else, if inst=0 it stays at current channel volume and instrument. */
			inst = mps->cs[chan].inst;

			mps->cs[chan].sample = mps->inst[inst].sample;
			mps->cs[chan].pos = 0;
			/* Note len, repeat_pos and repeat_end are fixed-point/avec fraction */
			mps->cs[chan].len = mps->inst[inst].len << SAMP_FP_SPLIT;
			if (mps->inst[inst].repeat_len != 1*2) {
				mps->cs[chan].looping = 1;
				mps->cs[chan].repeat_pos = mps->inst[inst].repeat_pos << SAMP_FP_SPLIT;
				mps->cs[chan].repeat_end = (mps->inst[inst].repeat_pos +
							    mps->inst[inst].repeat_len) << SAMP_FP_SPLIT;
			} else {
				mps->cs[chan].looping = 0;
			}

			mps->cs[chan].phaseinc = lmp_pi_from_pitch(freq);
			mps->cs[chan].pitch = freq;
		}

		lmp_process_command(mps, chan, command, val);
	}
	MPDBG(2, "\n");

	/* Move onto new frame in the pattern */
	if (mps->pos_pattern > 63) {
		/* Finished last pattern.  Find new one: */
		mps->pos++;
		mps->pos_pattern = 0;
		MPDBG(1, "Pos %d\n", mps->pos);
	}

	if (mps->pos >= mps->length) {
		MPDBG(1, "LOOPED\n");
		mps->pos = 0;

		/* Done (FIXME: a little early, gotta play the last note...)
		 * Loop detection is crude: go off end of sequence, or B to 0.
		 */
		if (!mps->song_loop)
			return 1;
	}

	return 0;
}

static void	lmp_render_samples(mps_t *mps, int16_t csamp[4])
{
	for (int chan = 0; chan < 4; chan++) {
		int32_t c1, c2;
		int32_t c = 0;;
		if (mps->cs[chan].on) {
			/* Linear interpolation between samples based on
			 * fractional part of 'pos' (that is, a sample
			 * is taken somewhere between two coarser
			 * instrument sample points, and blended with
			 * components of each weighted by distance). */
			int frac = mps->cs[chan].pos & ((1 << SAMP_FP_SPLIT)-1);
			int nfrac = (1 << SAMP_FP_SPLIT) -
				(mps->cs[chan].pos & ((1 << SAMP_FP_SPLIT)-1));

			c1 = (int)mps->cs[chan].sample[mps->cs[chan].pos >> SAMP_FP_SPLIT] * 0x100;
			/* This might go off the end of the sample.
			 *
			 * Though that generally sounds fine, it's messy to access
			 * off the end of the file given to us!
			 */
			if ((mps->cs[chan].pos >> SAMP_FP_SPLIT) <
			    (mps->cs[chan].len >> SAMP_FP_SPLIT))
				c2 = (int)mps->cs[chan].sample[(mps->cs[chan].pos >>
								SAMP_FP_SPLIT) + 1] * 0x100;
			else
				c2 = c1;

			/* Linear interpolate between c1 and c2: */
			c = ((c1 * nfrac) + (c2 * frac)) >> SAMP_FP_SPLIT;

			MPDBG(5, "C%d: %08x %04x len %08x rpt_pos %08x rpt_end %08x "
			      "(c1 %08x c2 %08x nf %08x f %08x)\n",
			      chan,
			      mps->cs[chan].pos,
			      c & 0xffff, mps->cs[chan].len,
			      mps->cs[chan].repeat_pos,
			      mps->cs[chan].repeat_end,
			      c1, c2, nfrac, frac);

			/* Scale volume: */
			c = (c * mps->cs[chan].vol) / 64;

			mps->cs[chan].pos += mps->cs[chan].phaseinc;

			if ((mps->cs[chan].looping < 2) &&
			    (mps->cs[chan].pos > mps->cs[chan].len)) {
				/* Reached very end. */
				if (mps->cs[chan].looping == 0) {
					/* No repeat, finish: */
					mps->cs[chan].on = 0;
				} else /* looping = 1 */ {
					mps->cs[chan].looping = 2;
				}
			}

			if ((mps->cs[chan].looping == 2) &&
			    (mps->cs[chan].pos > mps->cs[chan].repeat_end)) {
				mps->cs[chan].pos = mps->cs[chan].repeat_pos;
			}
		}
		csamp[chan] = (int16_t)c;
	}
}

static int lmp_check_for_tick(mps_t *mps)
{
	if (--mps->sample_counter == 0) {
		mps->sample_counter = mps->samples_per_tick;
		return lmp_tick(mps);
	}
	return 0;
}

/* These functions render a buffer of samples from the song.
 * Return 1 if song ongoing, 0 if song ended (song is set to not loop).
 *
 * They are split out to compile-time select mono or stereo variants.
 *
 * Samples are s16; stereo outputs left-right (2 samples), mono 1 sample.
 * sample_buffer_size is in units of number of samples.
 */

/* Mono */
int 	lmp_fill_buffer_mono(mps_t *mps, int16_t *samples, unsigned int sample_buffer_size)
{
	int done = 0;
	/* Mix active samples remaining for this frame: */
	for (unsigned int i = 0; i < sample_buffer_size; i++) {
		int32_t sum = 0;
		int16_t csamp[4];

		lmp_render_samples(mps, csamp);

		/* Mono, average channels: */
		for (int chan = 0; chan < 4; chan++) {
			sum += csamp[chan];
		}
		samples[i] = host_to_LE16(sum/4);

		done |= lmp_check_for_tick(mps);
	}

	/* Returns true if we should keep being called... */
	return !done;
}

/* Hard stereo */
int 	lmp_fill_buffer_stereo_hard(mps_t *mps, int16_t *samples, unsigned int sample_buffer_size)
{
	int done = 0;
	/* Mix active samples remaining for this frame: */
	for (unsigned int i = 0; i < sample_buffer_size; i++) {
		int16_t csamp[4];

		lmp_render_samples(mps, csamp);

		/* Stereo (hard panning):
		 *
		 * LRRL separation.  "Sounds rough, but it's cheap", as
		 * they say.
		 */
		samples[i] = host_to_LE16((csamp[0] + csamp[3])/2);
		i++;
		samples[i] = host_to_LE16((csamp[1] + csamp[2])/2);

		done |= lmp_check_for_tick(mps);
	}

	return !done;
}

/* Soft stereo */
int 	lmp_fill_buffer_stereo_soft(mps_t *mps, int16_t *samples, unsigned int sample_buffer_size)
{
	int done = 0;
	/* Mix active samples remaining for this frame: */
	for (unsigned int i = 0; i < sample_buffer_size; i++) {
		int16_t csamp[4];

		lmp_render_samples(mps, csamp);

		/* Stereo:
		 *
		 * Rather than "hard" Amiga LRRL separation, blend as:
		 * L = ((c0 + c3)*(3/4) + (c1 + c2)*(1/4))/2
		 * R = ((c1 + c2)*(3/4) + (c0 + c3)*(1/4))/2
		 */
		samples[i] = host_to_LE16((((csamp[0] + csamp[3])*3) + (csamp[1] + csamp[2]))/(4*2));
		i++;
		samples[i] = host_to_LE16((((csamp[1] + csamp[2])*3) + (csamp[0] + csamp[3]))/(4*2));

		done |= lmp_check_for_tick(mps);
	}

	return !done;
}


////////////////////////////////////////////////////////////////////////////////

#ifdef LMP_TEST_MAIN

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#define OUTPUT_BUFFERSIZE	1024	/* In samples */

/* Simple test, renders a MOD into raw samples.
 *
 * >  lmp my_amazing_song.mod output.raw
 *
 * Convert the output with something like:
 *	sox -t s16 -r 44100 -c 2 --endian little output.raw output.wav
 * (Or, play -t s16 -r 44100 -c 2 --endian little output.raw)
 */
int 	main(int argc, char *argv[])
{
	int ifd, ofd;
	char *ifile, *ofile;
	struct stat sb;
	uint8_t *modfile;
	unsigned int modlen;
	int r;

	if (argc != 3) {
		fprintf(stderr, "Syntax: %s  <infile.mod> <outfile.raw>\n",
			argv[0]);
		return 1;
	}
	ifile = argv[1];
	ofile = argv[2];

	/* Load input file (module) fully: */
	ifd = open(ifile, O_RDONLY);
	if (ifd < 0) {
		perror("Can't open input");
		return 1;
	}
	fstat(ifd, &sb);

	modlen = sb.st_size;
	printf("Allocating %d bytes for '%s'\n", modlen, ifile);
	modfile = malloc(modlen);
	if (!modfile) {
		fprintf(stderr, "Can't alloc %d!\n", modlen);
		return 1;
	}
	r = read(ifd, modfile, modlen);
	if (r != modlen) {
		/* Lazy */
		fprintf(stderr, "Short read (%d)\n", r);
		return 1;
	}
	close(ifd);

	ofd = open(ofile, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (ofd < 0) {
		perror("Can't open output");
		return 1;
	}

	/* Now do something! */
	mps_t		mpstate;
	int16_t		sample_buffer[OUTPUT_BUFFERSIZE];

	lmp_init(&mpstate, modfile);
	lmp_set_option(&mpstate, LMP_OPT_LOOP, 0);

	/* Run till done */
	unsigned int len = 0;
	/* If loop detection didn't work, cap at 5 minutes: */
	unsigned int max_len = (60*5) * (2*LMP_SAMPLERATE/OUTPUT_BUFFERSIZE);
	int more;
	do {
		more = lmp_fill_buffer(&mpstate, sample_buffer, OUTPUT_BUFFERSIZE, LMP_STEREO_SOFT);
		write(ofd, sample_buffer, OUTPUT_BUFFERSIZE*sizeof(int16_t));
		len++;
	} while(more && (len < max_len));

	close(ofd);

	return 0;
}

#endif
