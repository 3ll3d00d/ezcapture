# Plan: decode Atmos from Magewell Pro bitstream to PCM

Status: not started. Phase 0 (what the Apple TV sends) blocks everything else.

The goal is for an Apple TV Atmos signal captured on a Magewell Pro card to leave the filter as multichannel PCM that
LAV Audio accepts, with the Atmos objects placed onto a speaker layout such as 7.1.4. Today the same signal leaves as a
TrueHD bitstream, which LAV can only decode to the 7.1 bed or pass through to an AVR.

The decoding is done by Omniphony's renderer library (`liborender`, shipped as `orender.dll`), which loads
[harletty-bridge](https://github.com/harletty/harletty-bridge)'s Dolby plugin to decode the stream. The filter only feeds
it packets and copies out the PCM.

This is Magewell Pro only. It is the only filter that parses IEC 61937. Magewell USB is fixed at 48 kHz, and DeckLink
has no bitstream detection.

## Evidence so far

* The Apple TV signal is reported as "HBR Bitstream". HBR is the HDMI transport for 8 channels at 192 kHz, which on an
  Apple TV means Dolby MAT 2.0. A MAT frame carries either a TrueHD stream or PCM, each with Atmos metadata. Which one
  the Apple TV sends is unknown.
* `mw_audio_capture_pin.cpp` already finds IEC 61937 bursts. Burst type `0x16` (TrueHD/MAT) maps to `TRUEHD` and each
  complete burst is delivered downstream as one `MEDIASUBTYPE_DOLBY_TRUEHD` sample. Whether LAV decodes the MAT
  framing correctly today has not been checked.
* harletty-bridge's Dolby plugin lists "TrueHD (raw and MAT)" and E-AC-3 with Atmos objects (JOC), for burst types
  `0x01`, `0x15` and `0x16`. MAT carrying PCM is not mentioned.
* The bridge interface is Rust-only, with no C header, so the filter cannot load a bridge directly. `liborender` has a
  stable C interface (`orender.h`, version 0.12). It loads the bridges itself, and returns rendered speaker audio:
  * `orender_create(OrenderConfig*)`: the config holds the sample rate, a speaker layout file (7.1.4 if none is
    given), the bridge path and the codec.
  * `orender_process(r, pkt, len, pts_us, out, cap, &frames, &channels, &out_pts_us)` takes one encoded packet and
    writes interleaved float PCM into a buffer the caller supplies.
  * `orender_drain`, `orender_reset`, `orender_output_latency_samples`, `orender_has_objects`, `orender_channel_layout`.
* Licences: ezcapture, the Omniphony renderer and the harletty bridges are all GPL-3.0, so they are compatible. The filter also
  links Magewell's closed SDK. Loading `orender.dll` at runtime, and shipping it as a separate download, keeps the GPL
  code out of the filter binary.

## Design

```
Magewell Pro audio frame
  -> existing IEC 61937 parser (burst complete, codec TRUEHD or EAC3)
  -> decode on?   no  -> as today: deliver the burst as a bitstream sample
                  yes -> orender_process(burst payload) -> float PCM, N channels
                         -> convert to 32-bit int PCM
                         -> deliver as WAVE_FORMAT_EXTENSIBLE PCM with a channel mask that includes top speakers
```

* **The decoding stage.** A small class, `atmos_renderer` in `mwcapture/`, wraps `orender.dll`. It loads the DLL with
  `LoadLibrary`, checks the ABI major and minor versions, and owns the `orender_create` / `orender_process` / `orender_reset` calls. The
  header is copied into the repo from Omniphony. It is the only new compile dependency and is not a link dependency,
  so the vcxproj and CMake changes are just one include path. Both must be updated together, as `AGENTS.md`
  requires.
* **What is passed in.** Each complete burst payload, the bytes after the 8-byte preamble that the parser already
  collects into `mDataBurstBuffer`. Phase 1 settles whether `orender_process` wants this as-is, needs the MAT framing
  removed, or wants the whole IEC 61937 burst (the bridge says it accepts "raw or IEC61937-wrapped access units").
* **Output format.** Everything stays on a path LAV already accepts from this filter, WAVEFORMATEXTENSIBLE PCM, only
  wider:
  * Convert the float output to 32-bit int (`MEDIASUBTYPE_PCM_IN32`), the format the filter already sends for 32-bit
    PCM. LAV also accepts `MEDIASUBTYPE_IEEE_FLOAT`, so switch only if conversion costs too much.
  * The channel count is set by the speaker layout file, and 12 (7.1.4) is only the renderer's default. Examples:
    5.1.2 is 8 channels, 7.1.2 is 10, 9.1.6 is 16.
  * Set the channel mask from `orender_channel_layout`. For 7.1.4 that is `KSAUDIO_SPEAKER_7POINT1_SURROUND` plus
    `SPEAKER_TOP_FRONT_LEFT|RIGHT` and `SPEAKER_TOP_BACK_LEFT|RIGHT`. The channel order must match the mask order,
    and `orender_set_channel_mapping` can be used to force that order.
  * The mask has no wides and no top middle pair. 9.1.6 cannot be described with it. 5.1.2 and 7.1.2 put their heights
    on the top front pair.
  * HDMI carries at most 8 PCM channels. If the playback PC feeds an AVR over HDMI, the layout can be no larger than
    5.1.2. In that case sending the bitstream to an AVR that decodes Atmos is usually better.
  * Sample rate 48 kHz. The media type changes when decoding starts or stops, through the existing
    `ShouldChangeMediaType` / `DoChangeMediaType` path.
* **Timing.**
  * One burst does not produce one output frame. `orender_process` can return zero or more frames, and its output
    trails its input by `orender_output_latency_samples`.
  * Timestamps on delivered samples come from `out_pts_us`, measured from the capture time of the burst that went in.
  * Report the decoder latency through the existing latency metrics so it can be offset against video.
* **Format changes.** Call `orender_reset` whenever the codec changes, a PAUSE/NULL gap appears, or the stream is
  discontinuous (`mSinceCodecChange < 2`). When `orender_has_objects` is 0, fall back to passthrough so LAV decodes the
  stream as it does now.
* **Settings**, each a registry value shown on the property page:
  * Decode Atmos: off, or on.
  * Speaker layout file: empty means 7.1.4.
  * Path to `orender.dll`: empty means next to the filter.

  Off is the default, so anyone who sends the bitstream on to an AVR sees no change. If the DLL is missing or the wrong
  version, log it and pass the bitstream through.

## Phase 0: what the Apple TV sends

The 8-channel bitstream path had never run before this phase. Every source so far, DD+ included, arrived as 2
channels. `CopyToBitstreamBuffer` placed channel pair N at `N * inputChannelCount` instead of `N * 2`, which scrambled
any 8-channel bitstream. That is now fixed, but still untested on hardware.

1. Install the Debug build of mwcapture. It defines `RECORD_RAW`, which writes the untouched capture buffer to
   `%TEMP%\audio_cap-*.raw`, about 6 MB a second at 192 kHz. The raw file does not depend on the bitstream code.
2. Play an Atmos title on the Apple TV, then a 5.1 Dolby Digital Plus title. Record about 10 seconds of each, and keep
   the filter log.
3. Run `python -I tools/iec61937_inspect.py <file>.raw --dump <dir>`. It rebuilds the bitstream as both 2 and 8
   channels. The correct count is the one with consistent bursts. It reports burst types, sizes and spacing. A MAT
   burst is 61424 bytes.
4. The script then looks for the TrueHD major sync `F8 72 6F BA` after the MAT start code:
   * If it is there, MAT carries TrueHD, which harletty decodes.
   * If not, MAT carries PCM. Find where the PCM sits between the MAT start, middle and end codes, split it into
     channels, and check what they are:
     * Speaker channels: with a 5.1 title the back pair is silent and dialogue stays in the centre. Extracting them
       gives 7.1 PCM directly, with no renderer, the same as the Apple TV's own LPCM output.
     * Object channels: sound moves between channels, or there are more than 8. These need the metadata and a
       renderer, so the plan stops here until harletty supports MAT PCM. Raise an issue upstream with the capture
       attached.
5. Run the capture through the `harletty` CLI or `orender` to confirm objects come out.
6. Check what LAV does with today's TrueHD output from the same capture. That is the baseline.

## Phase 1: offline harness

1. Add `atmos_renderer` and a test in `mwcapture-test` that feeds a Phase 0 recording through it and writes a WAV
   file.
2. Settle the input framing, the channel order against the mask, and the latency. Check the WAV in Audacity or a
   similar tool, with a known object test clip if one is available.

## Phase 2: filter integration

1. Add the decode branch in `FillBuffer` after a burst completes, along with the media type for N-channel PCM, the
   timestamps and the reset rules above.
2. Add the property page settings and the registry values.
3. Add metrics: decoder latency, frames in and out, objects active, and resets.

## Phase 3: docs

1. In the README, explain where to get `orender.dll` and the harletty Dolby bridge, which versions are supported,
   where to put them, and the licence position.
2. State what was tested: Apple TV, the layout or layouts used, and the downstream chain.

## Risks

* **The Apple TV may send PCM inside MAT.** Phase 0 settles this, and nothing else is built until it does.
* **The chain beyond LAV and more than 8 channels.** LAV, the audio renderer (MPC Audio Renderer, sanear, the default
  DirectSound renderer) and the Windows audio device all have to accept the layout's channel count with top speakers. Test with
  LAV's mixer on and off. The fallback is to render to 7.1 and lose the heights. That is still better than the bed
  alone, because objects are folded down rather than dropped.
* **ABI churn.** `liborender` is at ABI 0.12 and the bridge interface at 0.6, and both are pre-1.0. Pin the versions
  and refuse to load any others.
* **CPU and latency on the capture thread.** If `orender_process` is too slow to run inline, move it to its own
  thread with a small queue.
* **Burst buffer sizes.** The burst sizes are `uint16_t`. A 61424-byte MAT burst fits, but check that nothing adds the
  8-byte preamble to it.
