# pj_media demos

All demos require Qt 6.8+ and are built only when `pj_media_qt` is available.
Video demos additionally require FFmpeg (libavcodec, libavformat).

## Building

```bash
./build.sh            # RelWithDebInfo
# or
./build.sh --debug    # Debug + ASAN (Qt 6.8 required in debug Qt path)
```

## Running

### Image viewer (MCAP)

Browse JPEG images from an MCAP file with play/scrub controls.

```bash
./build/pj_media/demos/mcap_image_viewer pj_media/testdata/test_images.mcap
```

### Multi-channel viewer

Display multiple image topics side-by-side (e.g. RGB + depth).

```bash
./build/pj_media/demos/multi_channel_viewer pj_media/testdata/test_images.mcap
```

### Simulated stream

Live 30 Hz synthetic JPEG stream with 3-second retention buffer.
Toggle between live mode and scrub mode.

```bash
./build/pj_media/demos/simulated_stream
```

### MP4 video player

File-based video playback with slider scrub via FileVideoSource.

```bash
./build/pj_media/demos/mp4_video_viewer pj_media/testdata/test_480p.mp4
./build/pj_media/demos/mp4_video_viewer pj_media/testdata/test_1080p.mp4
```

### Video stream demo

Simulates live H.264 streaming by demuxing an MP4 and pushing
packets into ObjectStore at real-time rate. Live/scrub toggle.

```bash
./build/pj_media/demos/video_stream_demo pj_media/testdata/test_480p.mp4
```

### Extract frame (CLI)

Extract a single frame from an MCAP file and save as PPM.

```bash
./build/pj_media/demos/extract_frame pj_media/testdata/test_images.mcap [frame_index]
```
