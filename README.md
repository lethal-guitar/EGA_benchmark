# EGA full-screen drawing benchmark

This is a little DOS program for measuring the performance
of drawing a full-screen image in EGA mode `0xD`, i.e. 320x200 with 16 colors.

It measures three things:

* Drawing to the entire screen in one go, plane by plane, from main memory to video ram
* Drawing in a grid of 8x8 pixel tiles, from video ram to video ram using latch copies
* Drawing in a grid of 8x8 pixel tiles, from main memory to video ram

The first method serves as a baseline, since it should be (to the best of my knowledge)
the fastest possible way to set the entire screen's content.
The 2nd method is the way Duke Nukem, Duke Nukem II, and Cosmo's Cosmic Adventure are rendering their map and background graphics.
The 3rd method is for comparison, to show the speedup those games achieved by using the latch copy technique.

## Building and running

To compile the benchmark, I've used Borland C++ 3.1. Other early Borland compilers will probably work as well,
but the resulting benchmark might produce slightly different results.
Although all the performance-critical code is written in Assembly, so it shouldn't make a huge difference.

To build, simply invoke the provided `build.bat`. `bcc` must be in the path.

To run the benchmark, two data files are needed:

* `BONUSSCN.MNI` - raw full-screen image in planar layout
* `DROP12.MNI` - raw tileset image in the format used by Duke Nukem II/Cosmo

These files are part of Duke Nukem II's game data,
but the precise content actually doesn't matter, just that they are 32000 bytes in size.
The content of the files has no influence on the benchmark, just on the visuals
that appear on screen.
So you can also just create two empty files of the appropriate size, and you should get valid timings.

When run, the benchmark briefly displays the images on screen, followed by a black screen
while the actual measurement is running. At the end, the number of milliseconds needed per
iteration for each drawing method is printed.

## Command-line arguments

There are two optional command line arguments:

```
egabench <num_iterations> <timer_rate>
```

The first argument controls the number of iterations each drawing method should be run for. By default, this is 1000.

The second argument sets the timer frequency, in Hz. By default, this is 1000, in order to get
millisecond accuracy. On slower machines, the overhead of having the timer interrupt fire
frequently could potentially make the drawing itself slower, so it's worth experimenting with
different frequencies.
