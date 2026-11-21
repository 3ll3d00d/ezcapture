# Home Assistant and the web API

Each filter can run a small HTTP server with a JSON API and a status page. The API is designed for Home Assistant's
built-in REST integrations, so no custom component is needed. This page is the reference for what the API returns.
[web-ui-plan.md](web-ui-plan.md) explains the design.

## Setting it up

1. Open the filter's property page. In the **Web API** row, tick **Enable?**, then set the address and port.
   * `127.0.0.1` (the default) only allows this PC. For Home Assistant on another machine, use `0.0.0.0` (all
     networks) or this PC's LAN address.
   * The default ports are 9474 for Magewell and 9475 for DeckLink. The installer's firewall rule opens these ports
     on private networks. If you change the port, add your own firewall rule.
2. Click **Apply**, then stop and start playback so the filter is loaded again. The status line then shows where it
   is serving, e.g. `Serving http://0.0.0.0:9474/ as magewell-a3f21c`.
3. **Copy Link** copies the page address with the access token, e.g. `http://capture-pc:9474/#t=3f9c...`. Open it in
   a browser to see the status page.
4. **Copy Token** copies only the token, for Home Assistant. **New Token** replaces it. Anything using the old token
   stops working once the filter is next loaded.

The server only runs while a player has the filter loaded. When the player closes, Home Assistant marks the
entities `unavailable`.

When the server is on, it uses three threads that sleep until a request arrives. With it off, nothing runs.

## Access

Send the token as a header: `Authorization: Bearer <token>`.

* Bound to `127.0.0.1`, reads work without the token as long as the request uses `127.0.0.1`, `localhost` or `[::1]`
  as the host name. This stops other websites from reading it through DNS rebinding.
* Bound to any other address, every request needs the token.
* The status page itself (`/`) never needs the token. It holds no data, and it reads the token from the link.

## Endpoints

| Method | Path | Returns |
|---|---|---|
| GET | `/` | the status page |
| GET | `/api/v1/info` | `{"api_version": 1, "product": "ezcapture", "version": "...", "filters": [{"id": "...", "name": "..."}]}` |
| GET | `/api/v1/filters/<id>/state` | the [state document](#state-document) |

`<id>` can be `default`, which is the first filter loaded, so most setups never need the real id.

Responses send `Cache-Control: no-store`. The state document has an `ETag`, so send `If-None-Match` to get a `304`
with no body when nothing has changed. Errors are `{"error": "<code>", "message": "..."}` with status `401` (missing
or wrong token), `404` (unknown filter or path), `405` (wrong method) or `413` (body over 4 KB).

## Home Assistant sensors

```yaml
# secrets.yaml
ezcapture_auth: "Bearer 3f9c...e1"

# configuration.yaml
rest:
  - resource: http://capture-pc:9474/api/v1/filters/default/state
    headers:
      Authorization: !secret ezcapture_auth
    scan_interval: 10
    sensor:
      - name: Capture video mode
        unique_id: ezcapture_video_mode
        value_template: "{{ value_json.summary.video_mode }}"
      - name: Capture dynamic range
        unique_id: ezcapture_dynamic_range
        device_class: enum
        value_template: "{{ value_json.summary.dynamic_range }}"
      - name: Capture audio
        unique_id: ezcapture_audio
        device_class: enum
        value_template: "{{ value_json.summary.audio }}"
      - name: Capture audio channels
        unique_id: ezcapture_audio_channels
        value_template: "{{ value_json.summary.audio_channels }}"
      - name: Capture filter state
        unique_id: ezcapture_filter_state
        device_class: enum
        value_template: "{{ value_json.filter.state }}"
      - name: Capture card temperature
        unique_id: ezcapture_temperature
        device_class: temperature
        unit_of_measurement: "°C"
        state_class: measurement
        value_template: "{{ value_json.device.temperature_c }}"
      - name: Capture measured fps
        unique_id: ezcapture_measured_fps
        unit_of_measurement: "fps"
        state_class: measurement
        value_template: "{{ value_json.video.measured_fps }}"
      - name: Capture MaxCLL
        unique_id: ezcapture_max_cll
        unit_of_measurement: "nits"
        value_template: "{{ value_json.hdr.max_cll_nits }}"
    binary_sensor:
      - name: Capture signal
        unique_id: ezcapture_signal
        value_template: "{{ value_json.summary.signal }}"
      - name: Capture HDR
        unique_id: ezcapture_hdr
        value_template: "{{ value_json.summary.dynamic_range not in [none, 'sdr'] }}"
```

An automation that reacts to the dynamic range, e.g. to change a projector's picture mode:

```yaml
automation:
  - alias: Projector follows HDR
    trigger:
      - trigger: state
        entity_id: sensor.capture_dynamic_range
    action:
      - choose:
          - conditions: "{{ trigger.to_state.state in ['hdr10', 'dv', 'lldv'] }}"
            sequence:
              - action: script.projector_hdr_mode
          - conditions: "{{ trigger.to_state.state == 'sdr' }}"
            sequence:
              - action: script.projector_sdr_mode
```

## State document

`GET /api/v1/filters/<id>/state` returns one JSON document with everything. Its shape never changes:

* Every key is always present. A value that is not known is `null`, never `-1`, `0` or `""`.
* Numbers are JSON numbers, with the unit in the key name (`_ms`, `_hz`, `_c`, `_nits`, `_bytes_per_sec`).
* Values marked *id* are stable lowercase strings for automations. Values marked *text* are display text, can
  change between releases, and should not be used in automations.

### `filter`

| Key | Type | Values |
|---|---|---|
| `id` | string | the id used in API paths |
| `name` | string | |
| `state` | id | `stopped`, `paused`, `running` |

### `summary`

What most automations need, worked out from the rest of the document.

| Key | Type | Values |
|---|---|---|
| `signal` | bool | `true` when the video input is valid |
| `video_mode` | string or null | `<width>x<height>p<fps>`, e.g. `3840x2160p23.976`, `1920x1080p50`. `null` with no signal. |
| `dynamic_range` | id or null | `sdr`, `hdr10`, `hlg`, `dv`, `lldv`. `null` with no signal. |
| `audio` | id | `none`, `pcm`, `ac3`, `eac3`, `truehd`, `dts`, `dts_hd`, `unknown` |
| `audio_channels` | int or null | `null` when `audio` is `none` |

`dynamic_range` rules, first match wins:

1. `dv` when the Dolby Vision tunnel is locked (`dv.state` is `locked` or `no_metadata`).
2. `lldv` when `dv.state` is `lldv`.
3. `hlg` when the output transfer function is HLG.
4. `hdr10` when there is an HDR InfoFrame or the output transfer function is PQ.
5. `sdr` otherwise.

`audio` is the codec the filter detected. It is `none` when there is no audio input, and `unknown` for a bitstream
the filter does not recognise. The filter cannot detect Atmos, DTS:X or DTS-HD MA vs HRA.

### `device`

| Key | Type | Notes |
|---|---|---|
| `description` | text | model and serial or API version |
| `protocol` | id | `pcie`, `usb` |
| `temperature_c` | number or null | |
| `fan_rpm` | int or null | USB Pro only |
| `link_speed` | int or null | PCIe generation, or the Magewell USB speed code |
| `link_width` | int or null | PCIe lanes |
| `max_payload_size`, `max_read_request_size` | int or null | PCIe bytes, Magewell Pro only |
| `bandwidth_bytes_per_sec` | number or null | estimated usable capture bandwidth of the link |
| `bandwidth_used_bytes_per_sec` | number or null | what the current capture format needs |

### `display`

| Key | Type | Notes |
|---|---|---|
| `refresh_hz` | int or null | refresh rate after the last switch |
| `status_text` | text | result of the last refresh rate switch |

### `video.input`

| Key | Type | Values |
|---|---|---|
| `valid` | bool | |
| `status` | id or null | `locked`, `locking`, `no_signal`, `unsupported_signal` |
| `status_text` | text | |
| `width`, `height` | int or null | |
| `aspect_x`, `aspect_y` | int or null | |
| `fps` | number or null | |
| `frame_duration` | int or null | 100 ns units |
| `bit_depth` | int or null | |
| `colour_format` | id or null | `rgb`, `rec601`, `rec709`, `bt2020`, `bt2020c`, or the DeckLink layout (`yuv_422`, `rgb_444`) |
| `pixel_layout` | id or null | `yuv420`, `yuv422`, `yuv444`, `rgb444`, or a DeckLink pixel format slug |
| `quantisation` | id or null | `limited`, `full` |
| `saturation` | id or null | `limited`, `full`, `extended` |
| `pixel_clock_mhz` | number or null | Magewell only |
| `total_width`, `total_height` | int or null | including blanking, Magewell only |

### `video.output`

| Key | Type | Values |
|---|---|---|
| `width`, `height`, `aspect_x`, `aspect_y` | int or null | |
| `fps` | number or null | |
| `bit_depth` | int or null | |
| `pixel_format` | id or null | the DirectShow subtype, e.g. `p210`, `nv12`, `v210`, `rgb48` |
| `colour_format` | id or null | `rgb`, `rec601`, `rec709`, `bt2020`, `bt2020c`, `p3d65` |
| `subsampling` | id or null | `yuv420`, `yuv422`, `yuv444`, `rgb444` |
| `transfer_function` | id or null | `bt709`, `pq`, `hlg` |
| `quantisation` | id or null | `limited`, `full` |
| `saturation` | id or null | `limited`, `full`, `extended` |

### `video` (other)

| Key | Type | Notes |
|---|---|---|
| `measured_fps` | number or null | frames delivered per second over the last metrics window |
| `headroom.interval_ms` | number or null | frame interval |
| `headroom.busy_mean_ms`, `headroom.busy_max_ms` | number or null | time spent on each frame. Frames drop when this reaches the interval. |

### `hdr`

| Key | Type | Notes |
|---|---|---|
| `active` | bool | an HDR InfoFrame is present |
| `max_cll_nits`, `max_fall_nits` | number or null | |
| `max_dml_nits`, `min_dml_nits` | number or null | mastering display luminance |
| `primaries` | object or null | `rx`, `ry`, `gx`, `gy`, `bx`, `by`, `wpx`, `wpy` |

### `dv`

| Key | Type | Values |
|---|---|---|
| `state` | id | `inactive`, `searching`, `locked`, `no_metadata`, `rejected`, `lldv` |
| `metadata_valid`, `full_range` | bool | |
| `colour_space`, `source_min_pq`, `source_max_pq` | int | |
| `l1` | object or null | `min_pq`, `avg_pq`, `max_pq` |
| `metadata_bytes`, `packets`, `frames`, `rejected_frames`, `crc_failed_copies`, `recovered_packets` | int | counters |
| `lldv` | object or null | only when `state` is `lldv`: `bit_depth`, `pixel_encoding` (`rgb444`, `yuv422`, `yuv444`, `yuv420`), `backlight_metadata`, `effective_max_pq`, `peak_nits`, `aux_run_mode`, `aux_version`, `aux_debug` |

### `audio.input`

| Key | Type | Notes |
|---|---|---|
| `present` | bool | |
| `pcm` | bool or null | `null` when not present |
| `bit_depth` | int or null | |
| `sample_rate_hz` | int or null | |
| `channel_pairs` | int | bitmask of valid HDMI channel pairs |
| `channel_allocation` | int | CEA-861 channel allocation |
| `lfe_level` | int | |

### `audio.output`

| Key | Type | Values |
|---|---|---|
| `codec` | id | as `summary.audio`, but not forced to `none` when the input is gone |
| `sample_rate_hz`, `bit_depth`, `channels` | int or null | |
| `channel_layout` | text | |
| `lfe_offset_db` | int | `0` or `-10` |
| `lfe_channel` | int or null | |
| `burst_size` | int or null | IEC 61937 data burst size for bitstreams |

### `latency`

`video` and `audio` are arrays of `{"stage": id, "min_ms", "mean_ms", "max_ms"}`, one per measured stage, over the
last metrics window. The stages depend on the card and the pixel format, so look them up by `stage`, not by position.

### `controls` and `metrics`

`controls` holds the property page settings: `refresh_rate_switch`, `hdr_profile_switch`, `hdr_profile`,
`sdr_profile`, `high_thread_priority`, `audio_capture`, `metrics_enabled`.

`metrics` holds the Prometheus settings: `enabled`, `bind_address`, `port`, `status_text`.

### `updated`

The time of the last change as an ISO 8601 UTC string, or `null` if nothing has been received yet.
