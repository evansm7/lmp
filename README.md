# Little Module Player (LMP)

v0.1 6 March 2021

Here lies a quite simple and quite small SoundTracker playroutine.  Built just for fun, powered by Sunday afternoon vibes and ideal for embedded hacks.

I feel the need to make excuses for this poor little routine:  it doesn't support all of the exotic and non-standard effects that make ST/PT songs sound so good and so different depending on who plays them.  It's Good Enough (TM) to play simple songs Completely Properly (TM), and seems to manage most of the MODs I've collected over the years.

This routine converts an in-memory MOD file into (buffers of) signed 16-bit PCM samples upon request.

It's ideal for embedding into, er, embedded things: it's about 3KB of code in a Cortex-M4 project, for instance.

Add a sound track (hahaha SWIDT) for your demos, games, etc. on your ARM Cortex-M board in one easy step!


## Usage

Include `littlemodplayer.h`, and build `littlemodplayer.c` into your project.

The overall paradigm is:

 * Initialise LMP with a pointer to an in-memory MOD file
 * Set playback optional options
 * When your PCM playback demands, call LMP to render a buffer of PCM samples.

Rough example:

~~~
mps_t	mpstate;	// My state

lmp_init(&mpstate, pointer_to_modfile);
lmp_set_option(&mpstate, ...); // Optional, configure looping, stereo

...
#define BUF_SIZE 2048 	// In samples, not bytes

int16_t pb_buffer[BUF_SIZE];

while (playing_pcm) {
	if (buffer_needs_refilling)
		lmp_fill_buffer(&mpstate, pb_buffer, BUF_SIZE, LMP_STEREO_SOFT);
}
~~~


## Wait, back up, WTF is SoundTracker/ProTracker?

ORLY.  [Check out the WP!](https://en.wikipedia.org/wiki/ProTracker)  They're music sequencer programs, from the 1980s/1990s, and looads of music was written with the various trackers for games, demos, etc.

LMP plays the songs created by these programs.

### Is it like an mp3 from the past?

More like a player piano.  Don't ask.  It sequences a bunch of samples at different notes/pitches/volumes, lets you make loops etc.


## Music from where?

### The internet

[ModArchive](https://modarchive.org/) is a good place to start.  Look for `.MOD` 4-channel SoundTracker/ProTracker files.  This won't play XM etc.

### Write your own!

You could go use a period-correct ancient computer, or a modern derivative like [MilkyTracker](https://milkytracker.org) on your Modern Computer.

But don't use portamento effects (exotic!) if you want it to sound the same through LMP ;-)

* * *

(c) 2021 Matt Evans
