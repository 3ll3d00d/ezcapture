# Magewell video capture: VideoProcessor vs ezcapture

Compared against billslack2/videoprocessor tag `1.4-rc6-magewell-20261029` (commit `5b8132b`).
Scope: **video only**. Based on reading the code. VP figures are VP's own measurements from its docs; nothing was built or run for this comparison.

- VP Magewell code: `src/VideoProcessor-Lib/magewell/`
- VP DV decoder: `src/VideoProcessor-Lib/video_frame_formatter/CRGBDolbyVisionTunnelFormatter*.cpp`
- Ours: `mwcapture/mw_video_capture_pin.cpp`, `mwcapture/mw_domain.h`, `common/video_capture_pin.*`

---

## 1. What each one is

- **VideoProcessor (VP)** is a standalone app. Magewell frames go to VP's own libplacebo renderer, and the Dolby Vision decode lives in that renderer. It supports Pro Capture cards only.
- **ezcapture** is a DirectShow source filter that captures as-is and passes frames to JRVR, MPC-VR or madVR. It supports Pro and USB cards.

## 2. Features

| Area | VP | ezcapture |
|---|---|---|
| Dolby Vision RGB tunnel | **Yes.** Checks every frame's metadata (CRC), unpacks to 12-bit 4:2:2, passes Dolby colour metadata + L1 scene brightness to libplacebo | **No.** Tunnel reaches the renderer as plain BGR24, so the picture is wrong |
| USB Capture cards | Excluded | Plus and Pro |
| Checks which output formats the card supports | Yes, at capture start | Pro uses hard-coded `proPixelFormats`. VP found the 4K+ returns BGRA for r210 requests and never completes v210 |
| Bandwidth-based format choice | P010 when 4:2:2 would exceed 1.6 GB/s (UHD 50/60) | P210 for all 10/12-bit 4:2:2 |
| 10/12-bit YCbCr 4:4:4 | P210 (keeps 10 bits, drops chroma) | **AYUV (8-bit)** plus CPU byteswap. Precision lost |
| 12-bit RGB | Converted to P210 (lossy) | BGR10 (10-bit 4:4:4), RGB48 fallback |
| RGB range | Read from AVI InfoFrame; card input range forced to full for DV (verified, restored) | SDK-reported range used for both input and output |
| Input selection | Configured input, auto-scan off | Whatever the card has selected |
| Interlaced input | Weave + field order passed to renderer | Card blend deinterlace |
| HDR metadata | Assumes primaries in R,G,B order (VP's own comment says unverified) | LAV HDR side data (≤1/s); R/G/B inferred from coordinates (more robust) |
| Display integration | None | Refresh-rate switching, JRVR HDR profile switching, property pages |
| Frame timing | Hardware timestamps, missed frames counted from timestamp gaps | Hardware reference clock, discontinuity flag on ≥2-interval gaps, latency CSV |
| Robustness | 2 s no-frame watchdog, SDK failure counters, 1 GiB buffer budget, runtime SDK loading | Simpler. Hot spin: failed `MWGetVideoBufferInfo` in `grab()` retries with no backoff (`mw_video_capture_pin.cpp:125-134`) |

## 3. Performance

### Where ezcapture is ahead
- **Latency.** We copy the frame that is still arriving (`iNewestBuffering`). VP waits for a complete frame (`NEWEST_BUFFERED`), which by design adds about one frame. Not measured on either side.
- **No intermediate copy.** On the straight-through path the card writes straight into the pinned DirectShow sample.
- **8-bit RGB.** Always BGR24 (3 B/px). Below its threshold VP uses BGRA (4 B/px, 33% more PCIe traffic). Above it VP captures BGR24 and then spends CPU expanding to BGRA, only because its decoder expects 4 B/px.
- **Serial grab is fine.** Because our copy runs while the frame is arriving, little copy time is left at completion. A conversion only has to fit in about one frame interval. VP's copy/convert overlap fixes a problem caused by its whole-frame wait: about 15 ms copy + about 2 ms expansion at 4K60 > 16.7 ms.

### Where VP is ahead
- **4K60 4:2:2 throughput.** VP measured P210 at UHD59.94 at about 52 fps with 76 skipped frames, against P010 at 59.94 fps with none. P210 at 4K60 is about 1.99 GB/s, which is what we request. **Our latency CSV should confirm whether we drop frames here.**
- **Pinning.** We pin/unpin the sample on every frame; pinning each buffer once is cheaper. (VP doesn't pin.)

## 4. Pros and cons

**VP pros:** working DV tunnel; card capability check; careful range and input handling; bandwidth-aware format choice; weave + field order; strong failure handling; thorough synthetic tests.

**VP cons:** about one frame more latency; more PCIe traffic for RGB8; no USB cards; lossy 12-bit RGB; complex and tied to its own renderer (the DV path can't be reused as-is); DV is opt-in, needs a restart, and parts are still marked unqualified on Magewell hardware; only L1 applied (no L2/L8 trims).

**ezcapture pros:** lower latency; no copy on the main path; leaner RGB8; USB support; 10-bit 4:4:4 for deep RGB; more robust HDR primaries; refresh-rate and profile switching; colour processing left to the user's renderer.

**ezcapture cons:** no DV; probable 4K60 4:2:2 HDR frame drops; 8-bit loss for 10/12-bit 4:4:4 YCbCr; no card capability check; no range override or AVI-based range; card-side blend deinterlace; per-frame pin/unpin; hot spin in `grab()`.

---

## 5. Tunnel format implied by VP's implementation

From `CRGBDolbyVisionTunnelFormatter.cpp`, which VP says is the inverse of the CroqueMr `libreelec-x86-DV` packer.

1. **Picture.** The HDMI signal is RGB 4:4:4 8-bit with no HDR InfoFrame. Per pixel: I = `G<<4 | (B & 0xF)`, C = `R<<4 | (B>>4)`, with Cb/Cr alternating between pixels. That gives 12-bit 4:2:2, which maps directly onto a P216/P210 layout (U on even pixels, V on odd).
2. **Packets.** Raster order from pixel 0, row padding ignored. One pixel = one bit: the parity of its 24 RGB bits, MSB first. A packet is 1024 bits (128 bytes), sent 3 times in a row (3072 pixels). A copy is valid when CRC-32/MPEG-2 over the 128 bytes gives 0.
3. **Header.**
   - `b0[7:6]` packet type: 0 single, 1 first, 2 middle, 3 last. `b0[5:1]` = 0. `b0[0]` = no_md.
   - `b1`: high nibble affected id, low nibble current id. Affected must be current or current + 1.
   - `b2[0]` end-of-stream; other bits 0.
   - First packet: bytes 3–4 metadata length (big-endian, max 0x2F00), body from byte 5 (119 bytes). Continuation packets: 121 body bytes from byte 3, same `b0` flags/`b1`/`b2`.
4. **Metadata body** (big-endian).
   - YCbCr→RGB matrix: 9 × int16 at 2–19, ÷ 8192
   - Offsets: 3 × uint32 at 20–31, ÷ 2^28
   - RGB→LMS matrix: 9 × int16 at 32–49, ÷ 16384
   - Byte 60 signal bit depth (must be 12), 61 colour space (0 or 2), 63 full-range flag
   - Source min/max PQ at 64 and 66
   - Byte 70 extension block count; blocks from 71 as `u32 length, u8 level, payload`. L1 = min/max/avg PQ.
5. **Timing.** affected = current → apply now. affected = current + 1 → hold for the next frame, or until a later header names that id. no_md keeps the current metadata. End-of-stream drops metadata and unlocks after this frame.
6. **State machine.**
   - Unlocked: plain SDR RGB allowed.
   - First valid header: lock.
   - Locked frame without a valid header: show **black**, never raw RGB.
   - 24 frames in a row without usable metadata: discard metadata and stay black until fresh metadata arrives.
   - Only end-of-stream or a capture format reset allows SDR again.
7. **Capture preconditions.** Bytes must arrive exactly as sent: Magewell input/output range forced to full, no scaling or colour conversion, top-down, known byte order. The source only sends DV if the card's EDID advertises Dolby Vision (VP uses a Vertex2 with a DV EDID).

---

## 6. Plan: DV tunnelling in ezcapture

**Design.** Decode the transport in the filter and output **P216** (12-bit in 16-bit samples; P210 is the same bytes, used as fallback). Attach LAV's **`IID_MediaSideDataDOVIMetadataV2`** on every frame, with identity reshaping plus the tunnel's matrices and extension blocks. The renderer does IPT→RGB and tone mapping, as it does for LAV-decoded DV.

- Keeps our "capture as-is, renderer does colour" model.
- Reuses the existing reconnect, side-data and frame-writer machinery.
- Passes L2/L5/L6/L8–L11 extension blocks through, which VP drops.
- LAV's header is GPL like ours; we already keep a partial copy in `common/lavfilters_side_data.h`.

### Why this should work with JRVR
JRVR is a libplacebo renderer that is normally fed by LAV Video. For DV, LAV attaches `IID_MediaSideDataDOVIMetadataV2` (plus the legacy struct) and the renderer hands that to libplacebo, which builds a `pl_dovi_metadata` from it (reshaping curves + the IPT-PQ matrices) and renders with `PL_COLOR_SYSTEM_DOLBYVISION`. VP does the same thing directly: a `pl_dovi_metadata` with the tunnel's matrices and **no reshaping**. Sending LAV's struct with identity reshaping should therefore land in the same libplacebo code path VP uses. The JRVR questions in section 7 are now a fallback if the spike fails, not a precondition.

### Phase 0: minimal spike (implemented on `feature/dv-tunnel`, needs a hardware test)
Scope, deliberately small:
- Magewell Pro only, always on (no setting): detection only ever locks on a CRC-valid DV header so ordinary RGB is unaffected.
- On start the card EDID is read and logged: whether it has a Dolby Vision block, its version, and whether it supports standard (TV-led) DV, which the tunnel needs, or low latency only, which gets LLDV instead. This is the card's own EDID; with an HDFury (e.g. Vertex2) presenting a custom EDID upstream, the source sees that one instead, so the log line only matters for a direct connection.
- Eligible when the signal is locked 8-bit RGB 4:4:4 with no HDR InfoFrame.
- **Unlocked:** BGR24 straight through as now; packet 0 is checked in place on every frame (3072 pixels + CRC, cheap). On a CRC-valid header the frame is blanked and the pin reconnects to P210.
- **Locked:** the card captures BGR24 (RGB in = RGB out, signal range, so no card-side conversion) into a scratch buffer; `common/dv_tunnel.h` decodes it (VP's state machine: 3 copies, multi-packet, pending/no_md/EOS, black without usable metadata, 24 frame expiry) and unpacks to **P210** (12-bit codes MSB-aligned in 16 bits, i.e. P216 bytes; P210 because every renderer accepts it). Media type BT.2020 / PQ (15), range from the metadata's full-range flag.
- Every rendered frame carries `IID_MediaSideDataDOVIMetadataV2` + legacy, from `common/dv_tunnel_lav.h`: identity reshaping (2 pivots 0..4095, linear poly, denom 23), matrices/offsets/signal fields from the tunnel, L1 only.
- EOS, or the signal stops qualifying, reconnects back to BGR24. If the renderer rejects P210 the decode is disabled until restart rather than looping.
- Scalar unpack on the capture thread, no AVX2 or row workers yet.
- Logging (trace builds): lock/unlock and a stats line every 600 frames (rejected frames, CRC failures, recovered copies, metadata size/packets, range, source PQ, L1).
- Tests: `mwcapture-test/dv_tunnel_test.cpp`, an independent sender covering all 4096 codes, single/multi packet, CRC recovery, pending, no_md, black + expiry, EOS and the LAV mapping.

What the hardware test answers:
1. Does the card deliver the tunnel bytes exactly without a range override? (the log shows CRC failures/recovered copies; `rejected` close to `frames` means no)
2. Does JRVR accept P210 + DOVI side data from us and render DV (correct colours, not the green/purple tunnel)?
3. Does the mid-stream BGR24 -> P210 switch and back work in JRVR, and does the HDR profile switch fire?
4. Does the scalar unpack keep up at 4K60 (latency CSV / dropped frames)?

**Results (2026-11-07, build 0.7.1-dv.3, Apple TV 4K -> Vertex2 -> Magewell Pro -> JRVR, 1080p50):**
1. Yes. Locked on the first frame, 0 rejected frames and 0 CRC failures (not even a first-copy failure) without any range override.
2. Yes. JRVR accepts P210 + `IID_MediaSideDataDOVIMetadataV2` with identity reshaping and renders normal colours, i.e. the LAV side data path reaches libplacebo's DV decoder as hoped.
3. The BGR24 -> P210 switch works (JRVR asked for a 2048 pixel stride, honoured via the sample media type).
4. Not tested yet at 4K.

Source setup that was needed:
- The Apple TV only sends the TV-led tunnel when the EDID offers standard DV *without* low latency. The Vertex2's generated DV block is LLDV only; a v2 block with Standard + LL still got LLDV.
- A standard-only v1 block (2017 LG) injected into the automix EDID was not enough, DV disappeared from the Apple TV format list. A complete EDID was needed: videoprocessor's `docs/edid` file, derived from the Vertex2 "FULL + LG DV" preset, worked. To stay within the PCIe bandwidth it was cut down to 1080p max (`out/test-builds/edid/vertex2-lg-dv-1080p.bin`).
- The DM source range reported by the tunnel (0.001 - 350 nits, L1 max 250) is the target display from the EDID's DV block, i.e. the Apple TV maps to the advertised display. Raising the v1 block's target max to 4000 nits (byte `0x0A` -> `0x9C`, `out/test-builds/edid/vertex2-lg-dv-1080p-4000nit.bin`) makes the tunnel report a 4000 nit source, so that EDID is the one to use; the renderer then tone maps to the real display.

If 1 fails: add the range override (Phase 2). If 2 fails: ask the section 7 questions, try P216, or go to the Phase 5 CPU fallback.

### Phase 1: decoder core (`common/`, header-only, no DirectShow)
- `common/dv_tunnel.h`:
  - Packet reader: parity, three-copy CRC, reassembly with VP's bounds checks.
  - Metadata parser covering all extension levels.
  - The state machine from section 5.
  - Layouts: `BGR24` (read directly, no BGRA repack), `BGRA`, `ARGB`, `R210` (both alignments, for DeckLink later).
- `conversion_kernels.h`: `rgb8_tunnel_to_p216<Layout>`, scalar and AVX2 picked at compile time (`EZ_ENABLE_AVX2`), with padding. Black = I 0 + neutral chroma, not all zeros.
- `common/lavfilters_side_data.h`: add the DOVI V2 and legacy GUIDs and struct, plus a `to_lav_dovi()` mapper:
  - Header: identity.
  - Mapping: 2 pivots, linear polynomial `[0, 1<<denom]`.
  - ColorMetadata from the tunnel metadata.
  - Extensions passed through.

### Phase 2: Magewell integration (Pro only at first)
- **Opt-in setting.** Registry flag + property-page checkbox, "Decode Dolby Vision RGB tunnel".
- **Eligibility.** Pro card, 8-bit RGB signal, no HDR InfoFrame.
- **Range override.** Save the card's input range, set full, read it back, and always restore on stop, format change, signal loss and destruction. On the SDR path take the range from the AVI InfoFrame we already read.
- **VSIF.** Register `MWCAP_NOTIFY_HDMI_INFOFRAME_VS`; log the vendor-specific InfoFrame's OUI/length as a hint and for status. CRC stays the deciding test.
- **Unlocked.** Keep BGR24 straight-through and check the first 3072 pixels in place (cheap). On a valid header, drop that frame and renegotiate the media type.
- **Locked.** Copy into a pinned scratch buffer; a new `dv_tunnel` frame writer (strategy `RGB_DV_P216`) unpacks it into the sample.
  - Media type: P216, BT.2020 primaries and matrix, PQ (15), range from the metadata's full-range flag. P210 fallback.
  - DOVI side data on every frame (not the once-per-second HDR throttle).
- **Media type changes.** Add a DV flag to `video_format` / `ShouldChangeMediaType` so lock and unlock reuse `OnVideoSignal` / `RenegotiateMediaType`. The JRVR HDR profile switch fires because the transfer function changes to PQ.
- **Status.** Property page shows unlocked/locked/lost, metadata id, packet count, CRC failures and L1 brightness; quill logging as elsewhere.

### Phase 3: performance
1. Add a `dvtunnel` mode to `kernelbench` and measure at 4K60. A 4K frame reads about 24 MB of BGR24 and writes about 33 MB of P216. BGR24 capture is about 1.49 GB/s, under VP's 1.6 GB/s budget; VP measured UHD60 RGB24 with no skips.
2. If copy + unpack still leaves headroom within a frame interval, keep the serial grab like every other conversion.
3. Otherwise split rows across workers within the frame (as VP's formatter does). That cuts per-frame time without adding latency, unlike overlapping frames.
4. Separately, pin each allocator/scratch buffer once instead of every frame. This helps all formats.

### Phase 4: tests (`mwcapture-test`, run in both the AVX2 and noarch builds)
- Independent sender fixture (inverse packing).
- All 4096 I and C codes round-trip exactly.
- CRC, and recovery from the second or third copy.
- Multi-packet reassembly, including L1 split across packets.
- Pending (current + 1) and no_md handling.
- End-of-stream unlock.
- Black after lock; 24-frame expiry.
- Padded strides, every layout, scalar vs AVX2.
- Side-data mapping.

### Phase 5: optional follow-ups
- **CPU fallback** for madVR or renderers that ignore DOVI: IPT-PQ → BT.2020 PQ P210 + HDR10 side data. CPU-heavy (PQ lookup tables + 4:2:2 chroma); opt-in only.
- **DeckLink.** Reuse the core for r210/ARGB, including the alignment probe.
- **Our own gaps from section 4:**
  - Check the card's supported formats (`MWGetVideoCaptureSupportColorFormat`).
  - Measure 4K60 P210; add a P010 bandwidth policy if we're dropping frames.
  - Avoid 8-bit AYUV for 10/12-bit 4:4:4.
  - Add a backoff to the `grab()` spin.

### Build and docs
- New `common/` files go into both `common.vcxproj`/filters and CMake; same for kernelbench and tests (per `AGENTS.md`). No compiler flag changes.
- README: a DV-capable EDID is required; the filter won't write EDIDs automatically.

### Biggest risks
- Whether JRVR and MPC-VR accept DOVI side data on P216 from our filter (Phase 0).
- Whether our card/driver delivers the tunnel bytes exactly without the range override.

---

## 7. Questions for the JRVR author

Self-contained; can be sent as is.

> **Feeding Dolby Vision from a capture filter into JRVR: does this work, and how should we do it?**
>
> ezcapture is a DirectShow source filter for Magewell (and Blackmagic) HDMI capture cards. It passes frames uncompressed to the renderer and already attaches LAV's `IMediaSideData` HDR10 side data (`IID_MediaSideDataHDR`, `IID_MediaSideDataHDRContentLightLevel`) to samples.
>
> We want to support the HDMI **Dolby Vision RGB tunnel** (the standard TV-led DV format, not LLDV, which a source sends when the sink's EDID advertises DV). On the wire it is 8-bit RGB 4:4:4 with no HDR InfoFrame. The real signal is 12-bit IPT-PQ 4:2:2 packed into the RGB bits, and the DV display-management metadata is carried in the pixel LSBs at the top of each frame (CRC-protected, ≤1 packet set per frame).
>
> **Our plan:** decode the tunnel in the filter and deliver to the renderer:
> - **Picture:** P216 (12-bit values in 16-bit samples; P210 as fallback), 4:2:2, the IPT-PQ samples unchanged (no reshaping or colour conversion by us).
> - **Metadata:** `IID_MediaSideDataDOVIMetadataV2` on every sample, built as if LAV had decoded a single-layer stream:
>   - Header: 12-bit BL/VDR, no enhancement layer, residual disabled.
>   - Mapping: identity reshaping (2 pivots, linear polynomial mapping each component onto itself), since the tunnel has already been reshaped by the source.
>   - ColorMetadata: the YCbCr→RGB and RGB→LMS matrices, offsets, signal bit depth/colour space/range and source min/max PQ, taken directly from the tunnel metadata.
>   - Extension blocks (L1, L2, L5, L6, L8–L11) copied through unchanged.
> - **Media type:** `VIDEOINFOHEADER2` with BT.2020 primaries/matrix and PQ transfer, range from the metadata's full-range flag.
>
> The renderer would then do IPT→RGB and tone mapping exactly as it does for LAV-decoded DV.
>
> **Questions**
> 1. Does JRVR use DOVI side data from any upstream filter that implements `IMediaSideData` on its samples, or only when LAV Video is the decoder (or through some other interface)?
> 2. Is `IID_MediaSideDataDOVIMetadataV2` the right GUID, or do you need the legacy one as well? Must it be on every sample, or does the last one persist until replaced?
> 3. Which pixel formats does the DV path accept? Is 4:2:2 P216/P210 fine, or do you need 4:2:0 (P010/P016)? What bit alignment do you expect for 12-bit data in a 16-bit container (MSB-aligned like P010, or LSB)?
> 4. Will identity reshaping with these header values work, or does your DV path assume a profile-5/8 bitstream? Is there a better way to say "already reshaped, apply display management only"? Which header/profile fields do you actually look at?
> 5. The tunnel's `signal_color_space` is 0 or 2. Do you handle both? Do you respect the metadata's full-range flag, or the media type's range?
> 6. What should the media type colorimetry say when DOVI side data is present? Does JRVR ignore it, use it as a fallback, or need particular values?
> 7. Which extension levels do you use, and do you need them in the V2 struct exactly as LAV fills them (raw level payloads, as in the bitstream)?
> 8. **Format changes mid-stream.** Before the source enters DV we send BGR24 SDR; when the tunnel locks we switch to P216 + DOVI via a dynamic media-type change (`QueryAccept`/`ReceiveConnection` on the allocator path). On unlock we go back. Does JRVR handle that cleanly, and does its DV state reset when side data stops arriving or the type changes? Does JRVR's HDR/DV display switching react to this?
> 9. When the tunnel loses metadata we output black. Should those frames carry the last metadata, no side data, or something else?
> 10. Timing: we resolve the tunnel's "apply on next frame" semantics ourselves, so each sample's side data is meant for that sample. Is that what you assume?
> 11. Would you rather receive something else, e.g. the raw tunnel metadata bytes, or the undecoded BGR24 frame with a flag so JRVR unpacks it itself?
> 12. Is there a JRVR version from which this is supported, and a way for a filter to tell whether the connected JRVR supports it so we can fall back?
>
> We can provide raw captured frames from a DV source and a small test filter that sends P216 + DOVI side data if that helps.
