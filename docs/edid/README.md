# EDID library

EDIDs for capturing from an Apple TV 4K through an HDFury Vertex2 into a Magewell Pro Capture HDMI 4K Plus. None are
applied automatically, see [Apply on Vertex2](#apply-on-vertex2).

The status column records results on that chain, with Vertex2 firmware 0.62 for everything that needs 4K. On it the
card does not lock any signal above 340MHz from the Vertex2, on either output and even with the Vertex2 test pattern,
while the same card locks 4K60 RGB directly from a PC.

| File | 4K | Dolby Vision | HDR10 | Audio | Origin | Status |
|---|---|---|---|---|---|---|
| `vertex2-lg-dv30-4k-4000nit-no4k50-lpcm.bin` | up to 30Hz | standard, 4000 nit | yes | LPCM 7.1 only | derived from `no4k50` | **Recommended** when the link cannot lock above 340MHz. Apple TV sends 7.1 LPCM |
| `vertex2-lg-dv30-4k-4000nit-no4k50.bin` | up to 30Hz | standard, 4000 nit | yes | LPCM + bitstream | derived from videoprocessor | Works: 4K24 Dolby Vision decoded, but the Apple TV sends an Atmos bitstream |
| `vertex2-lg-dv-1080p-4000nit.bin` | none | standard, 4000 nit | yes | LPCM + bitstream | derived from videoprocessor | Works: Dolby Vision at up to 1080p, see below |
| `vertex2-lg-dv-1080p.bin` | none | standard, 350 nit | yes | LPCM + bitstream | derived from videoprocessor | Superseded by the 4000 nit version, the source tone maps to 350 nits |
| `vertex2-lg-dv30-4k-4000nit-297mhz.bin` | up to 60Hz, 300MHz limit | standard, 4000 nit | yes | LPCM + bitstream | derived from videoprocessor | Not sufficient: with 4K50 listed the Apple TV still sends 371MHz or 594MHz |
| `vertex2-lg-dv30-4k-4000nit.bin` | up to 60Hz | standard, 4000 nit | yes | LPCM + bitstream | derived from videoprocessor | Needs a link that locks above 340MHz, 4K50 HDR at 371MHz did not lock here |
| `vertex2-lg-dv60-4k-4000nit.bin` | up to 60Hz | standard incl. 4K60, 4000 nit | yes | LPCM + bitstream | derived from videoprocessor | Needs a link that locks above 340MHz, did not work here |
| `vertex2-full-no-dv-approx.bin` | up to 60Hz | none | yes | LPCM + bitstream | approximation of Vertex2 **Full no DV** | Unverified, FULL + LG DV with the Dolby Vision block removed |
| `magewell-vsdb340.bin` | up to 60Hz | none | no | LPCM + bitstream | derived from the default EDID of the card | Diagnostic, HDMI VSDB max TMDS 340MHz. It made no difference to the failure above 340MHz |

## SHA-256

| File | SHA-256 |
|---|---|
| `vertex2-lg-dv30-4k-4000nit-no4k50-lpcm.bin` | `cf1b35f6cba422aff59f316e47e8234974ddbdf2a9bfa53cf9568c8b2a1a5119` |
| `vertex2-lg-dv30-4k-4000nit-no4k50.bin` | `5cd7a02dae1df3fabdc8605cfee564cb62323d4369ba851015ae54b899bd0777` |
| `vertex2-lg-dv-1080p-4000nit.bin` | `4776d83d20ced301307b68c2bcd50b76c5dd8c31c2e0f0eae7522032e37ed5c6` |
| `vertex2-lg-dv-1080p.bin` | `e6163fbae4a6e32696859b10eabe071ff30235d30614bc3e87936cef7fd046f1` |
| `vertex2-lg-dv30-4k-4000nit-297mhz.bin` | `7ffa9335d2ea0c18ebec9983e444034b7a0e42159d5feebdb8d645a82eaa330f` |
| `vertex2-lg-dv30-4k-4000nit.bin` | `ad264f457802f0fc1ae02a8148fb976b39bc2d715c8ac889bd6f78056a8d0fd1` |
| `vertex2-lg-dv60-4k-4000nit.bin` | `0b2c664d624fa4630327da576941e75f429f0e834cce71baac43f3f81c42d15a` |
| `vertex2-full-no-dv-approx.bin` | `2cb63b5985c5fd5da02cb667b24a9e62361b9cea68c9709da45278d42fedc937` |
| `magewell-vsdb340.bin` | `4dbfe91324c3bee03c9470b001a1e0dd167561959bfa5bb2f118e93dfd3c2170` |

## Dolby Vision at up to 1080p

`vertex2-lg-dv-1080p-4000nit.bin` is a 256 byte EDID that makes an Apple TV 4K send the TV-led Dolby Vision RGB
tunnel (see [Dolby Vision](../../README.md#dolby-vision)) at up to 1080p.

### Why

A source only sends the tunnel when the sink's EDID has a Dolby Vision vendor specific video data block (VSVDB) that
supports *standard* (TV-led) Dolby Vision. With an Apple TV:

* the VSVDB the Vertex2 generates in automix mode is low latency only so the Apple TV sends LLDV
* a version 2 VSVDB offering standard + low latency still gets LLDV
* a version 1, standard only, VSVDB injected into the automix EDID removes Dolby Vision from the Apple TV format list
* a complete EDID with the standard only VSVDB works

### Provenance

It is derived from `vertex2-lg-dv30-hdr10bit.bin` from
[videoprocessor](https://github.com/billslack2/videoprocessor/tree/a8f62855c246019e3d9262f384e58c96d60f3147/docs/edid)
(SHA-256 `9a271cf7e071bf247a1804efda3a62fdc182e161951c1097c40484c210956b01`), which is in turn the Vertex2
**FULL + LG DV** custom default EDID (SHA-256 `5559f42d5a1cf43449584900d46d83a926c54eb07544bb9e244136141ccc86a9`)
with high rate 4K Dolby Vision and 12-bit deep colour withdrawn. The Dolby Vision VSVDB is a version 1, standard only,
block from a 2017 LG TV.

Changes from the videoprocessor file, to keep within the PCIe bandwidth of the capture machine and to stop the source
tone mapping to a 350 nit display:

| Where | Before | After |
|---|---|---|
| Preferred timing (base DTD1) | 3840x2160p60 | 1920x1080p60 |
| Base DTD2 | 1920x1080p60 | 1920x1080p50 |
| Range limits, max pixel clock | 600MHz | 170MHz |
| CTA video data block | 4K VICs 93-102 plus 1080p/i, 720p and SD | VICs 1-64 only, i.e. 1080p 24/25/30/50/60, 1080i, 720p and SD |
| Y420 capability map | present (4K VICs) | removed |
| HDMI VSDB | max TMDS 300MHz, HDMI_VICs 1-4 (4K) | max TMDS 200MHz, no HDMI_VICs |
| HDMI Forum VSDB | max 600Mcsc, 10-bit 4:2:0 | no rate above 340Mcsc, no 4:2:0 deep colour |
| Dolby VSVDB target max luminance | 350 nits (`0x0A`) | 4000 nits (`0x9C`) |

Audio, HDR static metadata (SDR/HDR/PQ/HLG), BT.2020 colorimetry and the rest of the Dolby VSVDB are unchanged and
both blocks have valid checksums.

The Apple TV maps its output to the target display described by the VSVDB, with the original 350 nit target the
tunnel's metadata reported a 0.001-350 nit source and scene peaks never exceeded 250 nits. With a 4000 nit target it
reports a 4000 nit source and the renderer does the tone mapping for the real display.

## Apply on Vertex2

Uploading to a custom slot overwrites what is in it, including the factory custom defaults.

1. Download a copy of the active EDID of the input the source is connected to (EDID tab, download active EDID).
2. Upload this file to a custom slot you have already changed (EDID tab, upload EDID tables).
3. Select that custom slot for the source's input.
4. Leave the Vertex2 Dolby Vision EDID flag off, the block is in the file and the flag replaces it.
5. Issue a hotplug (CONFIG tab) so the source reads the new EDID.

The Vertex2 rewrites the HDMI physical address per input so the EDID it presents differs from the file in that field
and the checksum.

Tested with Apple TV 4K -> Vertex2 -> Magewell Pro Capture HDMI 4K Plus -> JRiver (JRVR) at 1080p50.

## 4K up to 30Hz

`vertex2-lg-dv30-4k-4000nit-no4k50.bin` offers 4K Dolby Vision and HDR at up to 30Hz, for a chain that can't lock a
signal above 340MHz. A Vertex2 on firmware 0.62 driving a Magewell Pro Capture HDMI 4K Plus fails this way on both
outputs, even with the Vertex2's own test pattern, while the same card locks 4K60 RGB directly from a PC.

SHA-256: `5cd7a02dae1df3fabdc8605cfee564cb62323d4369ba851015ae54b899bd0777`

`vertex2-lg-dv30-4k-4000nit-no4k50-lpcm.bin` is the same with LPCM as the only audio format. With the bitstream
formats listed the Apple TV sends an Atmos bitstream, which can't be decoded here, so use this one for 7.1 LPCM.

Limiting the rate in the VSDBs is not enough. With 4K50/60 still listed, an Apple TV sends them at 371MHz or 594MHz
despite the EDID, so they are removed. Changes from the videoprocessor file:

| Where | Before | After |
|---|---|---|
| Preferred timing (base DTD1) | 3840x2160p60 | 3840x2160p30 |
| Range limits, max pixel clock | 600MHz | 300MHz |
| CTA video data block | includes VICs 96, 97, 101, 102 | those 4K50/60 VICs removed |
| Y420 capability map | present | removed |
| HDMI Forum VSDB | max 600Mcsc, 10-bit 4:2:0 | no rate above 340Mcsc, no 4:2:0 deep colour |
| Dolby VSVDB target max luminance | 350 nits (`0x0A`) | 4000 nits (`0x9C`) |

Tested with Apple TV 4K -> Vertex2 -> Magewell Pro Capture HDMI 4K Plus, 4K24 Dolby Vision at 297MHz. The tunnel was
decoded and the Apple TV switched into it from 4K24 SDR with Match Dynamic Range on. 24p HDR10 and 25Hz are untested.
