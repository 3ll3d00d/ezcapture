# EDID for Dolby Vision capture

`vertex2-lg-dv-1080p-4000nit.bin` is a 256 byte EDID that makes an Apple TV 4K send the TV-led Dolby Vision RGB
tunnel (see [Dolby Vision](../../README.md#dolby-vision)) at up to 1080p, used with an HDFury Vertex2 between the
source and a Magewell Pro Capture card. It is not applied automatically.

SHA-256: `4776d83d20ced301307b68c2bcd50b76c5dd8c31c2e0f0eae7522032e37ed5c6`

## Why

A source only sends the tunnel when the sink's EDID has a Dolby Vision vendor specific video data block (VSVDB) that
supports *standard* (TV-led) Dolby Vision. With an Apple TV:

* the VSVDB the Vertex2 generates in automix mode is low latency only so the Apple TV sends LLDV
* a version 2 VSVDB offering standard + low latency still gets LLDV
* a version 1, standard only, VSVDB injected into the automix EDID removes Dolby Vision from the Apple TV format list
* a complete EDID with the standard only VSVDB works

## Provenance

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
