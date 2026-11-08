# Plan: Dolby Vision tunnel on DeckLink and Magewell USB

Status: planned. Dolby Vision capture currently works on Magewell Pro cards only.

The plan is to establish what each card delivers, then move the Magewell Dolby Vision logic into `common/` so all
filters share it. Only frame acquisition stays device-specific.

Testing is limited to 4K30. Nothing in the code will restrict higher rates, the docs will state what was tested.

## Evidence so far

* [videoprocessor](https://github.com/billslack2/videoprocessor) documents a live Vertex2 and DeckLink 4K23.976 Dolby
  Vision tunnel, delivered in DeckLink's 10-bit `r210` format, in either of two bit placements of the 8-bit RGB. Its
  ARGB and BGRA paths were tested synthetically.
* videoprocessor excludes Magewell USB cards, so there is no evidence either way for those.
* ezcapture's DeckLink filter selects `bmdFormat8BitARGB` for 8-bit RGB and `bmdFormat10BitRGB` for 10-bit RGB, depending
  on what the card reports.

## Shared code

| Shared in `common/` | Device-specific |
|---|---|
| Decoding, CRC and metadata state (`dv_tunnel::decoder`) | Magewell Pro: BGR24 capture buffer, buffer pinning, EDID check |
| Pixel layouts: BGR24, ARGB, BGRA, `r210` in both placements | DeckLink: frames from the SDK callback |
| Candidate rule: locked, RGB, no HDR metadata, even width | Magewell USB: frames from the USB capture API |
| Locked and rejected state, switching the format to P210, BT.2020, PQ | How each filter triggers a format reload |
| Writing the output frame: unpack or black, padding, sample size check | |
| LAV side data, Dolby Vision status, stats logging | |

The candidate rule accepts 8-bit RGB, and also 10-bit RGB on DeckLink. The metadata CRC decides what is really Dolby
Vision, so ordinary RGB is never affected.

## Phase 1: probe build

1. Add a pixel layout to the bit reader and unpacker in `dv_tunnel.h`. Parity ignores any alpha byte. For `r210`, try
   both placements and use the one that passes the CRC.
2. Extend `dv_tunnel_test.cpp` so the decoder and unpack tests run for every layout.
3. Run the decoder on candidate frames in `bmcapture` and on Magewell USB, logging only: CRC lock, layout, failed copies,
   packet counts and what the card reports. The picture is not changed.

**Gate:** run stage B of the test protocol below on each card. DeckLink and USB pass or fail separately.

## Phase 2: shared refactor, no behaviour change

4. Add a header-only session class holding the shared state, and move the Magewell code into it.
5. Add unit tests for the candidate rule and state transitions.
6. Re-test Magewell Pro 4K24 Dolby Vision.

## Phase 3: card integration

For each card that passed the gate:

7. Run the decoder on the thread that sees every frame. On DeckLink this is the SDK callback thread rather than the pin
   thread, because metadata can apply to the next picture and the pin thread can skip frames. Each `video_frame` then
   carries its own render flag and metadata.
8. Add a Dolby Vision frame writer alongside `any_rgb` and `straight_through`, with a `DV_TUNNEL` strategy.
9. When the tunnel locks, reload the format to P210, handling a renderer rejection as the Magewell Pro path does.
10. Correct the bandwidth estimate, which assumes BGR24 for the tunnel (`capture_filter.cpp`).
11. Publish status to the existing Dolby Vision fields on the property page.

## Phase 4: docs

12. Update the README's Dolby Vision section and record what was tested on each card.

## Risks

* **Pixel layout:** the probe establishes which layout each card delivers.
* **ARGB byte order:** parity doesn't depend on it, so the probe won't catch a mistake. A wrong order shows as wrong
  colours in phase 3.
* **EDID:** the DeckLink filter only sets the dynamic range field and no way to write a full EDID has been found, so the
  Vertex2 supplies it.
* **Not reviewed yet:** `processVideoFrame`, `MediaSampleBackedDecklinkBuffer` and the Magewell USB capture path.
* **Build files:** any new file or test has to be added to both the MSBuild and CMake builds.

## Test protocol

How to find out what a capture card does with the Dolby Vision RGB tunnel from an Apple TV 4K, using a known good
source and EDID. Replace the Magewell Pro Capture in the reference chain with the card under test and compare.

Tested limit: up to 4K30. A DeckLink or USB card has to preserve the pixels exactly, one scaling or colour conversion
step anywhere in the path makes the tunnel unreadable.

### Reference chain

```
Apple TV 4K -> Vertex2 INPUT 1 -> Vertex2 TX0 -> card under test -> ezcapture filter -> JRiver (JRVR)
```

#### Vertex2 (firmware 0.62)

| Setting | Value |
|---|---|
| EDID | `edid/vertex2-lg-dv30-4k-4000nit-no4k50-lpcm.bin` uploaded to Custom 1, mode Custom |
| INPUT 0 EDID table | Custom 1 |
| INPUT 1 EDID table | Follow INPUT 0 |
| TX0 input | 1 - INPUT 1 |
| Scaling, HDR and LLDV options | off, nothing may alter the picture |
| Dolby Vision EDID flag | off, the block is in the file |

Issue a hotplug from the CONFIG tab after any EDID change, then restart the Apple TV (Settings > System > Restart) so it
reads the new EDID. The card's own EDID does not matter here, the Apple TV reads the one presented by the Vertex2.

#### Apple TV

| Setting | Value |
|---|---|
| Format | 4K Dolby Vision 24 Hz, except where a test says otherwise |
| HDMI Output | YCbCr |
| Match Frame Rate | on |
| Match Dynamic Range | on |

Record the Chroma setting on the results sheet.

Test content: **Finch** on the Apple TV app. It switches into Dolby Vision on this chain. Don't use a Disney+ title, one
listed as Dolby Vision did not stay in Dolby Vision here.

#### Reference signals

These were measured at the Vertex2 input on the reference chain. `http://<vertex-ip>/ssi/infopage.ssi` has `RX1` and
`TX0`, `http://<vertex-ip>/ssi/hdrpage.ssi` has the AVI and HDR infoframes.

| State | Vertex2 `RX1` | AVI infoframe | HDR infoframe |
|---|---|---|---|
| Dolby Vision tunnel | `4K23.974 RGB 8b DV 297MHz 2.2` | `82:02:0d:90:12:28:08:5d:40:...` (RGB) | empty |
| 4K24 SDR | `4K23.974 444 BT709 8b SDR 297MHz 2.2` | `82:02:0d:18:52:a8:00:5d:...` (YCbCr 4:4:4) | no HDR, EOTF 0 |

If `RX1` doesn't show the expected state, the problem is upstream of the card and the test result is not valid.

### Before you start

1. Record the card model, driver version and firmware on the results sheet.
2. Install the `mwcapture` or `bmcapture` build under test, and run `regsvr32` on it last, from an admin prompt with
   JRiver closed. All variants of a filter share the property page, an older registered build can crash it.
3. Check nothing in the card's own settings scales, converts colour or changes range.
4. **Magewell USB:** select the card's RGB output mode and record what it is called.
5. Connect the card to Vertex2 TX0.

### Stage A: signal tests

These use any current ezcapture build and need no new code. Run in order and record each.

| # | Apple TV | Content | Expected at Vertex2 `RX1` | Clock |
|---|---|---|---|---|
| A1 | Format 4K SDR 24 Hz | home screen | `4K23.974 444 BT709 8b SDR` | 297MHz |
| A2 | Format 4K Dolby Vision 24 Hz | home screen | `4K23.974 RGB 8b DV` | 297MHz |
| A3 | Format 4K Dolby Vision 24 Hz | Finch | `4K23.974 RGB 8b DV` | 297MHz |
| A4 | Format 4K Dolby Vision 30 Hz, if listed | home screen | `4K30 RGB 8b DV` | 297MHz |
| A5 | Format 4K HDR 24 Hz | home screen or 24p HDR10 title | `4K23.974` HDR, expected 4:2:2 12-bit | 297MHz |

For each test record, from the ezcapture property page:

* signal status: Locked, Unsupported Signal or No Signal
* Dimensions, Frame Rate and Pixel Layout of the input
* Audio input format, expected LPCM 7.1 during Finch

Also record whether the picture looks like normal video. An undecoded tunnel looks visibly wrong, with garbled colours.
A2 to A4 are expected to look wrong on any build without Dolby Vision support for that card, that is still a useful
result if the signal locks.

| Stage A result | Meaning |
|---|---|
| Locks with RGB 8-bit or 10-bit RGB input | Continue to stage B |
| Locks but reports a YCbCr format during A2 or A3 | The card or its settings are converting, the tunnel can't survive |
| Unsupported Signal or No Signal at 297MHz | The card can't receive this signal in this setup, stop |

### Stage B: tunnel integrity

**Needs the probe build, which doesn't exist yet.** The probe runs the Dolby Vision decoder on incoming frames without
changing the picture. It reports whether the metadata CRC locks and which pixel layout it was found in. The exact log
lines will be added here when it is built.

| # | Apple TV | Content | Capture for at least |
|---|---|---|---|
| B1 | Format 4K Dolby Vision 24 Hz | home screen | 30 seconds |
| B2 | Format 4K Dolby Vision 24 Hz | Finch, include a scene change | 2 minutes |
| B3 | Format 4K Dolby Vision 30 Hz, if listed | home screen | 30 seconds |
| B4 | Format 4K SDR 24 Hz | home screen, as a negative control | 30 seconds |

Record the CRC lock state, pixel layout (BGR24, BGRA, ARGB or `r210` and its alignment), frames seen, rejected frames and
failed CRC copies.

| Stage B result | Meaning |
|---|---|
| B1 to B3 lock with few or no rejected frames, B4 never locks | The card preserves the tunnel |
| Lock with continuing rejected frames | The tunnel arrives damaged, record which layout and how often |
| Never locks on B1 to B3 while stage A locked | The card alters the pixels, or the layout isn't handled yet |
| B4 locks | Fault in the probe, its result can't be trusted |

### Results sheet

Copy one per card.

```
Card model:
Driver / firmware:
Filter build:
USB RGB mode, if any:
Apple TV Chroma setting:
EDID file:

Test  RX1 at Vertex2                        Card status    Input format        Audio      Picture normal?
A1
A2
A3
A4
A5

Test  CRC locked?  Layout / alignment   Frames   Rejected   Failed copies
B1
B2
B3
B4

Notes:
```
