#ifndef LMP_H
#define LMP_H

#include <inttypes.h>

/******************************************************************************/
/* Internal types/structs/functions: these may change */

typedef struct {
	int8_t *sample;
	uint16_t len;			// In halfwords
	uint16_t repeat_pos;
	uint16_t repeat_len;		// In halfwords; 1=no repeat
	uint8_t default_volume;
} mpsamp_t;

typedef struct {
	uint8_t on; 			// Instrument or -1
	uint8_t vol;
	uint8_t inst;
	uint8_t looping;		// 0 no loop, 1 loop, 2 looping
	uint16_t pitch;			// From original period in song note
	uint8_t effect;			// 0xff = no effect
	uint8_t effect_param;
	int8_t* sample;
	uint32_t pos;			// phase accumulator in fixed-point
	uint32_t phaseinc;		// increment/pitch
	uint32_t len;			// Fixed-point
	uint32_t repeat_pos;		// Fixed-point
	uint32_t repeat_end;		// Fixed-point
} mpschan_t;

typedef struct {
	uint8_t *mod_data;

	uint8_t *instruments;
	uint8_t *sequence;
	uint8_t length;
	uint32_t *patterns;

	mpsamp_t inst[31];

	// State
	unsigned int pos;		// 0 to length-1
	unsigned int pos_pattern; 	// 0 to 63
	unsigned int speed;
	unsigned int tick_counter;
	unsigned int tempo;

	// Format/config
	uint8_t thirtyone;
	uint8_t song_loop;
	/* A couple of modules do not use Fxx commands quite the same as others,
	 * for example F20 and F30 (which set a very low tempo, unexpected for
	 * those modules).  Option to ignore those commands:
	 */
	uint8_t support_tempo;
	uint8_t stereo;			// 0: mono mix, 1 stereo

	// Channel state
	mpschan_t cs[4];

	unsigned int sample_counter;
	unsigned int samples_per_tick;
} mps_t;

int	lmp_fill_buffer_mono(mps_t *mps, int16_t *samples, unsigned int sample_buffer_size);
int	lmp_fill_buffer_stereo_hard(mps_t *mps, int16_t *samples, unsigned int sample_buffer_size);
int	lmp_fill_buffer_stereo_soft(mps_t *mps, int16_t *samples, unsigned int sample_buffer_size);

/******************************************************************************/
/* External API */

typedef enum { LMP_MONO, LMP_STEREO_SOFT, LMP_STEREO_HARD } lmp_mix_t;

int 		lmp_init(mps_t *mps, uint8_t *mod_base);

unsigned int 	lmp_get_length(mps_t *mps);

void		lmp_set_pos(mps_t *mps, unsigned int pos);

#define LMP_OPT_LOOP		0	/* Default: yes */
#define LMP_OPT_SUPPORT_TEMPO	1	/* Default: yes */
void		lmp_set_option(mps_t *mps, unsigned int option, unsigned int val);

/* The main generation routine is selected by stereo/mono mix type (hopefully statically): */
static inline int	lmp_fill_buffer(mps_t *mps, int16_t *samples,
					unsigned int sample_buffer_size,
					lmp_mix_t mix_type)
{
	if (mix_type == LMP_MONO)
		return lmp_fill_buffer_mono(mps, samples, sample_buffer_size);
	else if (mix_type == LMP_STEREO_HARD)
		return lmp_fill_buffer_stereo_hard(mps, samples, sample_buffer_size);
	else if (mix_type == LMP_STEREO_SOFT)
		return lmp_fill_buffer_stereo_soft(mps, samples, sample_buffer_size);
	else
		return -1;
}



#endif
