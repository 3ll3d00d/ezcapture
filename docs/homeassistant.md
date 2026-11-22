# Home Assistant and the web API

Each filter can run a small HTTP server with a JSON API and a status page. The API is designed for Home Assistant's
built-in REST integrations, so no custom component is needed. This page is the reference for what the API returns.
[web-ui-plan.md](web-ui-plan.md) explains the design.

## Setting it up

The installer's **Remote access** page can do step 1 for you: it turns the web API on, sets the address and shows the
token to enter in Home Assistant. Then go straight to step 2.

1. Open the filter's property page. In the **Web API** row, tick **Enable?**, then set the address and port.
   * `127.0.0.1` (the default) only allows this PC. For Home Assistant on another machine, use `0.0.0.0` (all
     networks) or this PC's LAN address.
   * The default ports are 9474 for Magewell and 9475 for DeckLink. The installer's firewall rule opens these ports
     on private networks. If you change the port, add your own firewall rule.
2. Click **Apply**, then stop and start playback so the filter is loaded again. The status line then shows where it
   is serving, e.g. `Serving http://0.0.0.0:9474/ as magewell-a3f21c`.
3. **Copy Link** copies the page address with the access token, e.g. `http://capture-pc:9474/#t=3f9c...`. Open it in
   a browser to see the status page.
4. **Copy Token** copies only the token. **New Token** replaces it. Anything using the old token stops working once
   the filter is next loaded.
5. In Home Assistant's `secrets.yaml`, add the token with `Bearer ` in front of it:

   ```yaml
   ezcapture_auth: "Bearer 3f9c0a...e1"
   ```

   The secret has to hold the whole `Authorization` header value, not just the token. A `!secret` can only replace
   a whole YAML value, templates cannot read secrets, and HA's REST integrations have no bearer token option, so
   there is no way to add `Bearer ` in `configuration.yaml`. There is a space after `Bearer`, not a colon. With the
   bare token the filter answers every request with `401` and the log shows `Got non-ok response from resource:
   401`.

The server only runs while a player has the filter loaded. When the player closes, Home Assistant marks the
entities `unavailable`.

The RESTful switches read their URL once at startup. If that fails, e.g. because the filter is not loaded, HA logs
`Platform rest not ready yet` and retries every 30 seconds, and the switches do not exist until it succeeds. After
fixing a wrong token, restart Home Assistant so they are set up again.

When the server is on, it uses three threads that sleep until a request arrives. With it off, nothing runs.

## Access

Send the token as a header: `Authorization: Bearer <token>`.

* Bound to `127.0.0.1`, reads work without the token as long as the request uses `127.0.0.1`, `localhost` or `[::1]`
  as the host name. This stops other websites from reading it through DNS rebinding.
* Bound to any other address, every request needs the token.
* Changing a setting always needs the token, whatever the address.
* The status page itself (`/`) never needs the token. It holds no data, and it reads the token from the link.

## Endpoints

| Method | Path | Returns |
|---|---|---|
| GET | `/` | the status page |
| GET | `/api/v1/info` | `{"api_version": 1, "product": "ezcapture", "version": "...", "filters": [{"id": "...", "name": "..."}]}` |
| GET | `/api/v1/filters/<id>/state` | the [state document](#state-document) |
| GET | `/api/v1/filters/<id>/controls` | every [control](#controls) with its value, type and range |
| GET | `/api/v1/filters/<id>/controls/<name>` | one control |
| PUT or POST | `/api/v1/filters/<id>/controls/<name>` | sets it from a body like `{"value": true}`, returns it |
| GET | `/api/v1/filters/<id>/history/latency` | the last 5 minutes of metrics windows, see [History](#history) |
| GET | `/api/v1/filters/<id>/history/signal` | the last 50 changes to the summary or filter state |
| GET | `/api/v1/filters/<id>/history/display` | the last 20 refresh rate switches |

`<id>` can be `default`, which is the first filter loaded, so most setups never need the real id.

Responses send `Cache-Control: no-store`. The state document has an `ETag`, so send `If-None-Match` to get a `304`
with no body when nothing has changed. Errors are `{"error": "<code>", "message": "..."}` with status `400` (bad body or
value), `401` (missing or wrong token), `404` (unknown filter, control or path), `405` (wrong method), `413` (body
over 4 KB) or `500` (the setting could not be saved).

## Controls

| Name | Type | Range | Applies |
|---|---|---|---|
| `refresh_rate_switch` | boolean | | now |
| `hdr_profile_switch` | boolean | | now |
| `hdr_profile` | integer | 0 to 1000, the JRiver JRVR profile id | now |
| `sdr_profile` | integer | 0 to 1000 | now |
| `high_thread_priority` | boolean | | when playback next starts |
| `audio_capture` | boolean | | when the filter is next loaded |
| `metrics_enabled` | boolean | | when the filter is next loaded |
| `metrics_port` | integer | 1 to 65535 | when the filter is next loaded |
| `metrics_bind_address` | string | an IPv4 or IPv6 address, up to 63 characters | when the filter is next loaded |

Each control reads and returns:

```json
{"name": "hdr_profile", "type": "integer", "value": 3, "min": 0, "max": 1000, "applies": "now", "pending_reload": false}
```

`applies` is `now`, `next_start` or `next_load`. `pending_reload` is `true` when a `next_load` control has been
changed since the filter was loaded. `GET .../controls` also returns `pending_reload` for all of them together,
and the state document has it as `controls.pending_reload`.

Values are read leniently so Home Assistant bodies stay simple:

* booleans: `true`/`false`, `1`/`0`, or the strings `"on"`/`"off"`, `"true"`/`"false"`, `"yes"`/`"no"`, `"1"`/`"0"`
* integers: a whole number, or a string holding one

A change made through the API updates an open property page, and the property page's changes show up in the API.
The web API's own settings (enabled, address, port, token) can only be changed on the property page, so the API
cannot lock itself out or open itself to the network.

## Home Assistant switches and numbers

A boolean control maps straight onto HA's RESTful switch, which reads and sets one URL:

```yaml
switch:
  - platform: rest
    name: Capture refresh rate switching
    unique_id: ezcapture_refresh_rate_switch
    resource: http://capture-pc:9474/api/v1/filters/default/controls/refresh_rate_switch
    method: put
    headers:
      Authorization: !secret ezcapture_auth
      Content-Type: application/json
    body_on: '{"value": true}'
    body_off: '{"value": false}'
    is_on_template: "{{ value_json.value }}"
```

HA has no RESTful number, so integer controls use a `rest_command` and a template number:

```yaml
rest_command:
  ezcapture_set:
    url: "http://capture-pc:9474/api/v1/filters/default/controls/{{ name }}"
    method: put
    headers:
      Authorization: !secret ezcapture_auth
    content_type: application/json
    payload: '{"value": {{ value | tojson }}}'

template:
  - number:
      - name: Capture HDR profile
        unique_id: ezcapture_hdr_profile
        min: 0
        max: 1000
        step: 1
        state: "{{ state_attr('sensor.capture_controls', 'hdr_profile') }}"
        set_value:
          - action: rest_command.ezcapture_set
            data:
              name: hdr_profile
              value: "{{ value | int }}"
          - action: homeassistant.update_entity
            target:
              entity_id: sensor.capture_controls
```

`sdr_profile` works the same way, with `name: sdr_profile`.

`sensor.capture_controls` holds the current values as attributes. Add it to the `rest:` block from
[Home Assistant sensors](#home-assistant-sensors):

```yaml
      - name: Capture controls
        unique_id: ezcapture_controls
        value_template: "{{ value_json.controls.pending_reload }}"
        json_attributes_path: "$.controls"
        json_attributes:
          - refresh_rate_switch
          - hdr_profile_switch
          - hdr_profile
          - sdr_profile
          - high_thread_priority
          - audio_capture
```

`rest_command` can also be called straight from a script or automation, e.g. to turn off refresh rate switching
while a game console is on:

```yaml
action: rest_command.ezcapture_set
data:
  name: refresh_rate_switch
  value: false
```

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
      - name: Capture refresh rate
        unique_id: ezcapture_refresh_rate
        unit_of_measurement: "Hz"
        value_template: "{{ value_json.display.refresh_hz }}"
      - name: Capture overrun windows
        unique_id: ezcapture_overrun_windows
        state_class: total_increasing
        value_template: "{{ value_json.counters.overrun_windows_total }}"
    binary_sensor:
      - name: Capture signal
        unique_id: ezcapture_signal
        value_template: "{{ value_json.summary.signal }}"
      - name: Capture HDR
        unique_id: ezcapture_hdr
        value_template: "{{ value_json.summary.dynamic_range not in [none, 'sdr'] }}"
```

HA makes the entity ids from the names, so `Capture MaxCLL` becomes `sensor.capture_maxcll`, not
`sensor.capture_max_cll`. It is `unknown` for SDR sources, which have no MaxCLL.

To change a projector's picture mode or lens from the dynamic range, see
[Example: projector modes with JRiver](#example-projector-modes-with-jriver).

## Instant updates with a webhook

Polling every 10 seconds means an automation can react up to 10 seconds late, e.g. to a change from SDR to HDR. For
an instant reaction, put a Home Assistant webhook URL in the **Webhook** box on the property page, e.g.
`http://homeassistant.local:8123/api/webhook/ezcapture-lounge`. Apply, then restart playback.

The filter then POSTs this whenever the summary or the filter state changes, and once when it is loaded:

```json
{"filter": "magewell-a3f21c", "name": "Magewell Pro Capture", "state": "running",
 "summary": {"signal": true, "video_mode": "3840x2160p23.976", "dynamic_range": "hdr10", "audio": "truehd", "audio_channels": 8}}
```

* It waits 500 ms after a change for any more, so a signal that drops for a moment sends one update, and only the
  latest summary is sent.
* An update identical to the last one sent is skipped.
* It gives up after 2 seconds and does not retry. The result is shown after "webhook:" in the property page status
  line.
* It is sent from a Windows thread pool thread, so a slow or missing Home Assistant never holds up capture.
* No update is sent when the filter unloads. The REST sensors going `unavailable` show that.

Use it as a trigger for template sensors:

```yaml
template:
  - trigger:
      - trigger: webhook
        webhook_id: ezcapture-lounge
        local_only: true
        allowed_methods: [POST]
    sensor:
      - name: Capture dynamic range (instant)
        unique_id: ezcapture_dynamic_range_push
        device_class: enum
        state: "{{ trigger.json.summary.dynamic_range or 'none' }}"
      - name: Capture video mode (instant)
        unique_id: ezcapture_video_mode_push
        state: "{{ trigger.json.summary.video_mode or 'none' }}"
```

or directly in an automation (`trigger: webhook` with the same `webhook_id`).

## Example: projector modes with JRiver

A working setup where JRiver plays both files and the capture card (a playlist or zone item that shows the
filter), and Home Assistant switches the projector's picture mode and lens to suit. It uses:

* the [JRiver integration](https://github.com/3ll3d00d/jriver_homeassistant) for `media_player.jriver`, with the
  `HDR Format` and `UseLens` library fields added under **Extra fields** in its options
* the REST sensors and the webhook sensors from the sections above
* `script.projector_hdr_mode`, `script.projector_sdr_mode`, `script.projector_scope` and `script.projector_16_9`,
  whatever your projector needs (e.g. `rest_command`s to [cmdserver](https://github.com/3ll3d00d/cmdserver) for a
  JVC)

Replace `Capture` with the title JRiver shows while it is playing the capture card.

### One dynamic range for files and the capture card

JRiver's `HDR Format` describes the file being played, so while it shows the capture card it says nothing about the
signal. This sensor uses the filter for the capture card and the file otherwise, with the same ids as
`summary.dynamic_range`:

```yaml
template:
  - sensor:
      # sdr/hdr10/hlg/dv/lldv for whatever JRiver is showing; unknown = the capture card but the filter isn't
      # answering; none = JRiver off
      - name: JRiver dynamic range
        unique_id: jriver_dynamic_range
        device_class: enum
        state: >-
          {%- set title = state_attr('media_player.jriver', 'media_title') or '' -%}
          {%- set f = (state_attr('media_player.jriver', 'HDR Format') or '') | lower -%}
          {%- if states('media_player.jriver') in ['off', 'unavailable', 'unknown'] -%}
            {%- set r = 'none' -%}
          {%- elif title == 'Capture' -%}
            {%- set r = states('sensor.capture_dynamic_range_instant') if has_value('sensor.capture_dynamic_range') else 'unknown' -%}
          {%- else -%}
            {%- set r = 'sdr' if f == '' else 'dv' if 'dolby' in f else 'hlg' if 'hlg' in f else 'hdr10' -%}
          {%- endif -%}
          {{ r }}
        attributes:
          source: >-
            {{ 'capture' if (state_attr('media_player.jriver', 'media_title') or '') == 'Capture' else 'file' }}
```

* For the capture card it takes the webhook sensor, so it changes as soon as the signal does, but only while the
  polled sensor is available, so a value left over from when the filter was last loaded is never used.
* JRiver reports `HDR Format` values such as `HDR10`, `HDR10+` and `DolbyVision Profile 7 FEL, HDR10-compatible`,
  and an empty value for SDR.

### Picture mode: playback switches quickly, the signal is quick into HDR and slow out of it

Starting playback or a new title is a deliberate choice, so apply that title's mode straight away, whichever way it
goes. Within a title, SDR is the resting state: menus, the gap between items and a capture card source sitting on
a menu are SDR, and HDR means real content has started. So a signal change switches to HDR quickly and back to SDR
slowly. The slow return also stops the projector flapping between modes:

```yaml
automation:
  - alias: Projector picture mode follows dynamic range
    mode: restart
    triggers:
      - trigger: state
        entity_id: sensor.jriver_dynamic_range
        to: [hdr10, hlg, dv, lldv]
        for: {seconds: 5}
        id: hdr
      - trigger: state
        entity_id: sensor.jriver_dynamic_range
        to: sdr
        for: {seconds: 60}
        id: sdr
      - trigger: state
        entity_id: sensor.projector
        to: "on"
        for: {seconds: 10}
        id: projector_on
      - trigger: state
        entity_id: media_player.jriver
        to: playing
        for: {seconds: 5}
        id: playing
      - trigger: state
        entity_id: media_player.jriver
        attribute: media_title
        not_to: [Media Center]
        for: {seconds: 5}
        id: new_title
    conditions:
      - condition: state
        entity_id: sensor.projector
        state: "on"
      - condition: state
        entity_id: media_player.jriver
        state: playing
      - alias: not JRiver's transient title
        condition: not
        conditions:
          - condition: state
            entity_id: media_player.jriver
            attribute: media_title
            state: Media Center
      - alias: a signal change, the projector coming on, or a file being chosen
        condition: or
        conditions:
          - condition: trigger
            id: [hdr, sdr, projector_on]
          - condition: state
            entity_id: sensor.jriver_dynamic_range
            attribute: source
            state: file
    actions:
      - choose:
          - conditions:
              - condition: trigger
                id: [hdr, projector_on, playing, new_title]
              - condition: state
                entity_id: sensor.jriver_dynamic_range
                state: [hdr10, hlg, dv, lldv]
              - condition: not
                conditions:
                  - condition: state
                    entity_id: sensor.projector
                    attribute: picture_mode
                    state: HDR
            sequence:
              - action: script.projector_hdr_mode
          - conditions:
              - condition: trigger
                id: [sdr, projector_on, playing, new_title]
              - condition: state
                entity_id: sensor.jriver_dynamic_range
                state: sdr
              - condition: not
                conditions:
                  - condition: state
                    entity_id: sensor.projector
                    attribute: picture_mode
                    state: SDR
            sequence:
              - action: script.projector_sdr_mode
```

* `unknown` and `none` match neither branch, so the projector is left alone when the mode can't be known.
* The 5 second hold on a new title lets JRiver's fields settle; they go blank for a moment between items, which
  reads as `sdr`. The 60 second hold covers the same gap within a playlist.
* As a file starts JRiver shows the title `Media Center` for a few seconds, with blank fields that read as `sdr`.
  Ignore it or the projector drops to SDR just before an HDR film starts.
* The capture card keeps one title whatever the source is showing, and JRiver can drop to idle and back on it by
  itself, so for the capture card only the signal holds decide.
* The projector-on trigger sets the mode when the projector is turned on part way through.
* It only sends a command when the projector is in the wrong mode. Replace `sensor.projector` and its
  `picture_mode` attribute with whatever reports your projector's power and picture mode.

### Lens: from the file, playback switches quickly, quick into scope and slow out of it

The filter sees a 16:9 signal whatever the content's aspect ratio, so scope can't be detected for the capture card.
For files, a `UseLens` library field (`1` for scope, `0` for 16:9) can drive the lens. As for the picture mode,
starting playback or a new title applies that title's lens straight away, and otherwise 16:9 is the resting state:

```yaml
automation:
  - alias: Projector lens follows JRiver file
    mode: restart
    triggers:
      - trigger: state
        entity_id: media_player.jriver
        attribute: UseLens
        to: "1"
        for: {seconds: 5}
        id: scope
      - trigger: state
        entity_id: media_player.jriver
        attribute: UseLens
        to: "0"
        for: {seconds: 60}
        id: flat
      - trigger: state
        entity_id: sensor.projector
        to: "on"
        for: {seconds: 10}
        id: projector_on
      - trigger: state
        entity_id: media_player.jriver
        to: playing
        for: {seconds: 5}
        id: playing
      - trigger: state
        entity_id: media_player.jriver
        attribute: media_title
        not_to: [Media Center]
        for: {seconds: 5}
        id: new_title
    conditions:
      - condition: state
        entity_id: sensor.projector
        state: "on"
      - condition: state
        entity_id: media_player.jriver
        state: playing
      - alias: not JRiver's transient title
        condition: not
        conditions:
          - condition: state
            entity_id: media_player.jriver
            attribute: media_title
            state: Media Center
      - alias: a file, not the capture card
        condition: state
        entity_id: sensor.jriver_dynamic_range
        attribute: source
        state: file
    actions:
      - choose:
          - conditions:
              - condition: trigger
                id: [scope, projector_on, playing, new_title]
              - condition: state
                entity_id: media_player.jriver
                attribute: UseLens
                state: "1"
            sequence:
              - action: script.projector_scope
          - conditions:
              - condition: trigger
                id: [flat, projector_on, playing, new_title]
              - condition: state
                entity_id: media_player.jriver
                attribute: UseLens
                state: "0"
            sequence:
              - action: script.projector_16_9
```

* The holds are on the triggers, not the conditions. A `for:` on an attribute condition is measured from the
  entity's last update, and a playing media player updates every few seconds, so it would never be met.
* Add a check that the lens isn't already in position, as for the picture mode, so the scripts only run when
  needed.

### Things that help

* **Reset before turning the projector off.** Many projectors, including JVCs, ignore commands once they start
  cooling down, so put the picture mode and lens back to their resting state in a script that then sends standby,
  and use that script for the power off button. Do the same when the projector next turns on with nothing playing,
  to cover it being turned off with the remote.
* **Reset when playback stops.** When JRiver has been stopped (`idle`, `off` or `unavailable`, not `paused`) for
  30 seconds, put the picture mode and lens back to their resting state. 30 seconds rides over the gap between
  playlist items and picking the next title.
* **One command at a time.** The picture mode and lens automations can fire together, e.g. when playback starts. A
  projector usually takes one command at a time and refuses commands while it is still changing mode, so whatever
  sends them should queue them and retry a refused one. cmdserver does both.

## History

The filter keeps a little history in memory so the status page can show what happened recently, e.g. a dropout
overnight. It is lost when the filter unloads. Times (`t`) are milliseconds since the Unix epoch.

`history/latency` has one sample per metrics window (about every 1.5 seconds), up to 300 for video and 300 for
audio:

```json
{"video": {"stages": ["capture", "conversion"],
           "samples": [{"t": 1795301590176, "fps": 23.976, "busy_mean_ms": 5.1, "busy_max_ms": 9.2,
                        "stages": [[1.0, 1.25, 2.0], [2.0, 2.5, 3.0]]}]},
 "audio": {"stages": ["capture"], "samples": [{"t": 1795301590180, "stages": [[3.0, 3.5, 4.0]]}]}}
```

Each entry in a sample's `stages` is `[min_ms, mean_ms, max_ms]` for the stage at the same position in `stages`.

`history/signal` has `{"changes": [{"t": ..., "state": "running", "summary": {...}}]}`, oldest first, with the same
`summary` as the state document.

`history/display` has `{"changes": [{"t": ..., "refresh_hz": 24, "status_text": "..."}]}`.

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
`sdr_profile`, `high_thread_priority`, `audio_capture`, `metrics_enabled`, and `pending_reload` (see
[Controls](#controls)).

`metrics` holds the Prometheus settings: `enabled`, `bind_address`, `port`, `status_text`.

### `counters`

Totals since the filter was loaded, for `state_class: total_increasing` sensors:

| Key | Notes |
|---|---|
| `signal_changes_total` | changes to the summary or filter state |
| `overrun_windows_total` | metrics windows in which the busiest frame took longer than the frame interval, so frames were probably dropped |

### `updated`

The time of the last change as an ISO 8601 UTC string, or `null` if nothing has been received yet.
