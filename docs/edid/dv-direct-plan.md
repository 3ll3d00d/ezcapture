# Plan: Dolby Vision from an Apple TV connected directly to a Magewell Pro

Status: in progress. Testing has not started.

## Goal

Get the Dolby Vision RGB tunnel, including at 4K50, from an Apple TV through the EZ-MX42HAS-ARC switch into the card,
without the Vertex2. Find out what makes the Apple TV send LLDV instead, then pick an EDID that gets the tunnel, or
handle LLDV in the filter. The filter side is a spike, see [LLDV handling](#lldv-handling).

The chain is Apple TV -> EZ-MX42HAS-ARC switch -> card. The switch has to stay in it (HDCP), and the Vertex2 is out
of scope because it cannot lock above 340 MHz.

## What is known

* The card has `mw-current.bin` loaded. It is the card's default EDID with the TrueHD/MAT flag set (byte 175
  `0x00` to `0x01`) and the checksum updated. It is the same file as `magewell-current-atmos.bin`.
* The Apple TV reports Dolby Vision, but the card receives **12-bit YCbCr 4:2:2, BT.2020, with no HDR InfoFrame**,
  at 4K50, 1080p50 and 4K23.976 (Finch). That is LLDV. The tunnel, 8-bit RGB 4:4:4 with no HDR InfoFrame, never
  arrives.
* beta.2 tags LLDV as SDR BT.2020 (tf 4), because only an HDR InfoFrame tells the filter the signal is PQ.
* Audio was 2-channel PCM throughout. MAT/Atmos was not sent either.
* The Vertex2 EDIDs got the tunnel at 4K24 and 1080p. Their Dolby Vision block is the same as the card's,
  `24/26 9c 09 00 ad 52 40 ad 23 0d`. The only difference is the 2160p60 bit (`26` on the card).

## EDID differences

The card's EDID compared with `vertex2-lg-dv-1080p-4000nit.bin`, which got the tunnel at 1080p:

| Field | `mw-current.bin` | Vertex2 1080p |
|---|---|---|
| HDMI 1.4 VSDB deep colour (byte 188) | `b8`: 30 and 36-bit, Y444 | `98`: 30-bit, Y444 |
| HDMI 1.4 VSDB max TMDS | 600 MHz | 200 MHz |
| HDMI Forum VSDB | `01 78 80 03`: 600 MHz, SCDC, 4:2:0 10/12-bit | `01 00 80 00`: no rate above 340 MHz, SCDC |
| Video capability block (byte 181) | `0f` | `cf` (QY/QS selectable) |
| YCbCr 4:2:0 capability map | present | absent |
| Dolby Vision block, 2160p60 (byte 222) | set (`26`) | clear (`24`) |
| 4K VICs | 4K50/60 listed | none |
| Audio and speaker blocks | card defaults | Vertex2 LG |

## Variants

Each variant is `mw-current.bin` (MAT flag still set) with only the listed bytes changed and the CTA checksum
(byte 255) updated. The files are in `W:\junk\edid-bisect\`, and the script that made them is `edid_bisect.py`
in the session scratchpad.

| File | Change | SHA-256 |
|---|---|---|
| `mw-all.bin` | all four changes below | `6653829c816c9678a13d76a8f12f31bc29d3804af50b40af82e4080234ceb630` |
| `mw-dc30.bin` | no 12-bit deep colour: byte 188 `b8`→`98`, byte 204 `03`→`00` | `44e8358fcdb8047fcd3425291001ad1f0fbc1a121dedbe6a98bf3ffd958b016c` |
| `mw-hf340.bin` | Forum VSDB max TMDS rate: byte 202 `78`→`00` | `3bc44b0019129b63f53a2940282f6ced1ed0f41d5d722e3ab4d4b274c266f5a6` |
| `mw-vcdb.bin` | video capability block: byte 181 `0f`→`cf` | `273bf2a1811a33b077dd6ce0e45e04adef1c1da15cf64cee65fd5fcd66750ccc` |
| `mw-dv30.bin` | Dolby Vision 2160p60 bit cleared: byte 222 `26`→`24` | `82f6b6a77083bd2c4f1b44f84c7d26fa7706a1ac8a7d7de993934dd557c04942` |

`mw-current.bin` is `69a44e5a47739cbaee774f4c68dc86c49b905694c0d0b3ed685bb02e0ff7556c`.

`mw-all.bin` and `mw-hf340.bin` advertise no TMDS rate above 340 MHz. With them, 4K50/60 cannot be sent, so test
at 1080p or 4K24.

## Procedure

**On hold.** The switch's stock firmware replaces every Dolby Vision block with a low-latency-only one, so the
tunnel can't be tested through it, see [Switch firmware](#switch-firmware-ez-mx42has-arc-v104). This procedure
applies once the switch presents standard DV.

The chain is Apple TV -> EZ-MX42HAS-ARC switch -> card. The Apple TV reads the EDID the switch presents, so the
switch must pass the card's EDID through. Set the switch's EDID management to copy (passthrough) from the output
that feeds the card. In a fixed or built-in EDID mode the switch never reads the card's EDID, and a power cycle does
not change that.

Use the trace build of the LLDV spike (`W:\junk\trace\mwcapture-trace.ax`). It logs the vendor specific InfoFrame
and shows LLDV on the property page.

For each EDID:

1. Load it onto the card.
2. Power cycle the switch (fully off, then on) so it reads the card's EDID again.
3. Restart the Apple TV (Settings > System > Restart) so it reads the EDID the switch now presents.
4. Record the Apple TV's Format, Chroma, Match Frame Rate and Match Dynamic Range settings.
5. Start a trace capture. Note the menu signal, then play **Finch** on the Apple TV app for about 30 seconds, then
   stop.
6. Check that the EDID was active: the filter's startup line `Card EDID Dolby Vision block ...` shows the expected
   payload (`249c...` with `2160p60: no` for `mw-all.bin` and `mw-dv30.bin`, `269c...` otherwise).
7. In the trace, look at the Finch segment (4K23.976, or 1080p with the 340 MHz files):
   * tunnel: `8 bits (BGR RGB ...)` followed by `DV tunnel detected`
   * LLDV: `Vendor specific InfoFrame [dolby: true, dv: true, low latency: true ...]` and a format line naming
     `LLDV` with `tf: 15`. The property page HDR box reads `HDR (Dolby Vision LLDV)`.
   * HDR10: `HDR Infoframe is present tf: ... to 2`

### Check the switch first

Run `vertex2-lg-dv-1080p-4000nit.bin` first. It has no 4K modes and a 200 MHz limit, so a source reading it can only
send 1080p:

* The menu drops to 1080p: the switch presents the EDID under test. Carry on with the order below.
* The menu stays at 4K: the switch is presenting its own EDID. Change its EDID mode, or upload the EDID to the
  switch, before testing anything else. The Apple TV cannot be connected directly (HDCP).

### Order

1. `vertex2-lg-dv-1080p-4000nit.bin` at 1080p, with Finch. It got the tunnel through the Vertex2, so LLDV here means
   the chain, not the EDID, decides.
2. `mw-current.bin` at 4K50 and with Finch at 4K23.976, the baseline.
3. The 4K50-capable single changes: `mw-dc30.bin`, `mw-vcdb.bin`, `mw-dv30.bin` (expect no Dolby Vision at 4K50
   with `mw-dv30.bin`, only at 30Hz and below).
4. `mw-all.bin` and `mw-hf340.bin` only if the above don't find it. Test them at 1080p or 4K24, since their 340 MHz
   limit rules out 4K50.

## Results

| EDID | Apple TV settings | Finch arrives as | Trace |
|---|---|---|---|
| `mw-current.bin` | | LLDV (12-bit 4:2:2, no HDR InfoFrame) | `MagewellTrace_1795213058.log` |
| `mw-all.bin` | | 4K23.976 4:2:2 BT.2020, no HDR InfoFrame (LLDV); menu 4K50 4:2:2 | `edid-bisect/mw-all.log` |
| `mw-dc30.bin` | | 4K23.976 4:2:2 BT.2020, no HDR InfoFrame (LLDV); menu 4K50 4:2:2 | `edid-bisect/mw-dc30.log` |
| `mw-hf340.bin` | | | |
| `mw-vcdb.bin` | | | |
| `mw-dv30.bin` | | menu 4K50 Dolby Vision (Apple TV report) | `edid-bisect/mw-dv30.log` |
| `mw-dv30.bin`, spike build | | menu 4K50 LLDV, VSIF `low latency: true`, no backlight metadata | `edid-bisect/spike-test.log` |
| `vertex2-lg-dv-1080p-4000nit.bin` | | menu 4K50 LLDV, so the card's EDID is not reaching the Apple TV | `edid-bisect/latest-test.log` |
| `vertex2-lg-dv-1080p-4000nit.bin`, ARC patched to skip the DV rewrite | | **1080p50 DV tunnel**: 8-bit RGB, `DV tunnel detected` at frame 1, 6000 frames with no rejects or CRC failures. VSIF is the HDMI LLC one (`03 0c 00`, all zero), no Dolby VSIF | `edid-bisect/fw-test.log` |
| `mw-dv30.bin`, patched ARC | | no DV offered; 4K50 HDR10 | `edid-bisect/dv30.log` |
| `mw-all.bin`, patched ARC | | 4K24 DV tunnel; no DV at 50/60 (2160p60 clear, 340 MHz) | `edid-bisect/mw-all.log` |
| `mw-vcdb.bin`, patched ARC | | **4K50 and 4K59.94 DV tunnel**, 0 rejected / CRC failures, but only ~37 fps captured (unpack takes ~12 ms a frame) | `edid-bisect/mw-vcdb.log` |

**Answer: the video capability block.** The card's default `0f` declares the RGB quantisation range as not selectable
(QS=0). With `cf` (QS=1) the Apple TV offers standard DV at up to 4K60, and the tunnel arrives full range RGB. No
other change is needed, so `mw-vcdb.bin` keeps 2160p60, 600 MHz, 12-bit deep colour and MAT.

The card's EDID was active in each run (the filter logged the expected Dolby Vision block, `2160p60: no` for
`mw-all.bin` and `mw-dv30.bin`). The Apple TV did not act on it, though:

* `mw-all.bin` advertises no TMDS rate above 340 MHz, yet the Apple TV menu stayed at 4K50 4:2:2, which needs
  594 MHz.
* `mw-dv30.bin` clears 2160p60 Dolby Vision, yet the Apple TV still reported 4K50 Dolby Vision.

A control run settles it. `vertex2-lg-dv-1080p-4000nit.bin` has no 4K modes and a 200 MHz limit, yet with it on the
card the Apple TV still sent 4K50 LLDV (`edid-bisect/latest-test.log`). **The Apple TV is reading the switch's EDID,
not the card's**, so none of the results above test the card's EDID.

The link is not the problem. 4K50 12-bit 4:2:2 LLDV and the 4K50 8-bit RGB tunnel both run at 594 MHz, and the card
locks 4K50 LLDV through the switch. Tunnel or LLDV is decided only by the EDID the Apple TV reads.

## Switch firmware (EZ-MX42HAS-ARC V1.04)

From the disassembly in `ezcoo-fw-tools` (`docs/ARC-V1.04.md`):

* **Every Dolby Vision block is rewritten.** Every EDID the switch presents goes through the same code, whether
  built-in or copied from output 1. That code replaces any DV VSVDB with the firmware's own block,
  `eb 01 46 d0 00 45 0b 90 86 60 76 8f` (v2, **low latency only**, PQ 20-3225, about 1390 nits). The Apple TV
  therefore never sees standard (TV-led) DV, and can only send LLDV. The block is stored at `0x08017B18`.
* So **the tunnel cannot arrive through this switch** with its stock firmware, at any resolution, whatever EDID the
  card has. EDID changes outside the DV block do pass through, which makes the bisect pointless for tunnel or LLDV.
* No EDID upload or dump command. The only EDID choices are built-in index 0-20 (serial `EZS INx EDID y`) or index
  21, copy from output 1 (DIP switches or front panel only).
* Copy mode only refreshes when output 1 hot-plugs with a different EDID. After any serial `EZS INx EDID`, a
  changed EDID with the same physical address is stored but not applied. If the copy buffer is invalid, the switch
  silently falls back to index 20 (`MXX-O-3-HVA`: 4K60, DV LL-only, Atmos). This probably explains why
  `vertex2-lg-dv-1080p-4000nit.bin` changed nothing. To force a refresh, move the DIP EDID switches off the copy
  position and back, replug output 1, then restart the Apple TV.

### Options for the 4K50 tunnel

1. **Patch the stored DV block.** Replace the 12 bytes at `0x08017B18` with a standard DV block, for example a v1
   12-byte block with the low latency bit clear, and reflash. Every presented EDID then advertises standard DV. A full
   flash backup exists, and the MCU's ROM bootloader allows recovery. It is still a firmware write, and nobody has
   checked yet whether the firmware verifies its own integrity.
2. **The `RED`/`WRT` debug commands.** These may write the HDMI chip's EDID RAM directly. The change would not
   persist, and the commands are untested.
3. **A different switch.** The EZ-MX42PRO-HAS accepts uploaded EDIDs. No DV rewrite is documented for it, but that
   needs checking, and it gave no picture with the Magewell EDID uploaded unchanged.

Until one of these is in place, LLDV is the only Dolby Vision this chain can carry, and the LLDV handling is the
path that works today.

## LLDV handling

This is a spike on Magewell Pro only, not committed yet.

* Each signal check reads the HDMI vendor specific InfoFrame (`MWCAP_HDMI_INFOFRAME_ID_VS`). It is logged at INFO
  whenever it changes: the raw bytes, plus the decoded Dolby fields (Dolby Vision signal, low latency, backlight
  metadata, effective max PQ). The decoding is `dv_tunnel::parse_dolby_vsif`.
* A locked signal with a Dolby VSIF that has Dolby Vision and low latency set, and no HDR InfoFrame, is treated as
  LLDV. The colour format becomes BT.2020 (named `LLDV`), the transfer function becomes PQ (15), and HDR metadata is
  synthesised:
  * BT.2020 primaries and D65 white point
  * min mastering luminance 0.005 nits
  * max mastering luminance and MaxCLL from the VSIF's effective max PQ when backlight metadata is present,
    otherwise 1000 nits
  * MaxFALL set to MaxCLL
* The picture is captured as it arrives. 12-bit 4:2:2 is captured as P210, or P010 at 4K60 when the PCIe bandwidth
  is short.

Not done yet:

* The VSIF field layout follows edid-decode. The Apple TV's menu VSIF (`81 01 1b`, OUI `46 d0 00`, PB4 `03`, the
  rest zero) matches it for the Dolby Vision and low latency flags. The backlight and effective max fields have not
  been seen set yet, check them with Finch.
* The fallback peak should come from the target luminance in the card's EDID Dolby Vision block.
* The property page shows LLDV in the HDR box (`HDR (Dolby Vision LLDV)`), with the range, the signal
  (bit depth, subsampling, PQ BT.2020), the peak and whether it came from the VSIF, the VSIF backlight and aux
  fields, and what was sent to the renderer. The metrics do not report LLDV yet.
* Magewell USB and DeckLink are not covered.
