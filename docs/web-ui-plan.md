# Plan: web UI and HTTP API for the capture filters

Status: proposed. No code yet.

## Goal

An optional local HTTP server per filter DLL that offers:

* a JSON API for reading status and changing settings, designed to be used straight from Home Assistant's built-in
  REST integrations with no custom component, and
* a web page built on that same API that shows what the property page shows, plus data the property page has no room
  for.

It has to cost nothing when it is off, and close to nothing when it is on and nobody is polling it.

## Constraints

* The filter runs inside the player process (MPC-BE, JRiver, ...). A crash or hang in the web code takes playback
  down with it, so the web code never runs on a capture thread and never holds a filter lock while doing I/O.
* Both build systems must change together (see `AGENTS.md`).
* Off by default, like Prometheus metrics.
* No new third-party dependency if it can be avoided.

## Decision: CivetWeb, already linked

`prometheus-cpp[pull]` already links CivetWeb 1.16 statically into both filter DLLs. `civetweb.lib`,
`civetweb-cpp.lib`, `civetweb.h` and `CivetServer.h` are already in `vcpkg_installed`. Using it adds no binary size
and no new code to vet.

Prometheus uses the same library, but its `CivetServer` belongs to `prometheus::Exposer`, which is private to
`quill::PrometheusSink`. The web UI therefore runs its own `CivetServer` on its own port. Serving both from one port
would mean replacing the exposer with a server we own, which is a later option, not part of this plan.

Rejected:

| Option | Why not |
|---|---|
| http.sys (`HttpApi.dll`) | A process that is not elevated needs a `netsh http add urlacl` reservation. That is friction for anyone not using the installer. |
| Hand-written Winsock | The lightest option, but we would own HTTP parsing and its edge cases for no real saving over an idle CivetWeb. |
| Mongoose, cpp-httplib | A second HTTP stack next to the CivetWeb we already ship. |
| Separate exe over IPC | Best isolation, but a second process to install, start and keep in step with the filter. Revisit if in-process proves fragile. |
| MQTT with HA discovery | Entities would appear automatically, but it needs an MQTT client library, a broker, and an outbound connection kept open the whole time. The REST API plus a webhook covers the same automations. |

## How it fits together

```
capture threads ──► capture_filter status members ──► callbacks (fan-out)
                                                        ├─► CSignalInfoProp (property page, UI thread)
                                                        └─► status_snapshot (copy under its own mutex)
                                                                 ▲            │ on signal change
web_ui server (CivetWeb worker thread) ── GET  state ────────────┘            ▼
                                       ── PUT  controls ──► ISignalInfo   notifier ──► HA webhook (optional)
                                                            setters
```

### 1. More than one callback

Today `capture_filter` holds one `ISignalInfoCB* mInfoCallback`, which belongs to the property page. Change it to a
small list (at most two entries in practice) guarded by the existing `mInfoCallbackLock`. Every place that does
`if (mInfoCallback != nullptr) mInfoCallback->X(...)` calls each entry instead. Add one helper so that pattern is
written once.

`SetCallback(cb)` keeps its meaning for the page: it sets or clears the page's entry. The web UI registers through a
new internal `AddCallback` / `RemoveCallback` that is not part of `ISignalInfo`, so the COM interface does not
change.

### 2. `status_snapshot` (new, `common/status_snapshot.h/.cpp`)

Implements `ISignalInfoCB`. Each callback copies the payload into its own member under a `std::mutex` and bumps a
version counter. That is the same copy the property page already makes in `PostToUiThread`, and it happens on the
same thread straight after the filter writes the status, so it adds no new race with the filter's own writes.

It holds the latest of: device, display, video input/output, HDR, DV, audio input/output, the five latency stats,
measured fps, headroom, the control values and the metrics status. It also works out the
[summary](#summary-block) and remembers the previous one, so it can tell the notifier when the summary has changed.

`to_json()` copies everything under the lock, releases it, then formats JSON with `std::format`. There is no JSON
library. The output is fixed in shape, and strings are escaped by one small helper.

The cost when the server is on and nobody is connected: one struct copy per callback, mostly once per metrics
window, which happens about once a second. JSON is only built when a request arrives.

### 3. `web_ui` server (new, `common/web_ui.h/.cpp`)

One server per DLL per bind address, shared by every filter instance in that DLL and reference counted the same way
as `create_or_get_sink` for metrics.

CivetWeb options:

| Option | Value | Why |
|---|---|---|
| `listening_ports` | `127.0.0.1:<port>` by default | local only unless changed |
| `num_threads` | `2` | one request at a time is plenty, the second keeps a slow client from blocking everything |
| `document_root` | not set | no files are served from disk |
| `request_timeout_ms` | `5000` | |
| `enable_keep_alive` | `yes` | |

Idle cost: the CivetWeb master thread blocked in `select` and the two workers waiting for a connection. CivetWeb
1.16 has no `prespawn_threads` option, so the workers start with the server. No timers and no work.

### 4. The page

One HTML file with inline JS and CSS and no framework, gzipped and compiled into `common` as a byte array in a
generated header (`common/web_page.h`), so both DLLs get it with no `.rc` changes. It is served with `Content-Encoding: gzip` and a strong ETag. The target is under 15 KB
compressed. It uses only the public API below, so anything it can do, Home Assistant can do too.

It polls the state endpoint once a second and sends `If-None-Match`. When nothing has changed, the server answers
`304` without building JSON. Polling was chosen over SSE or WebSockets because, in CivetWeb, a held-open connection
keeps a worker thread busy for as long as the client stays connected.

Layout: the same groups as the property page (device, video in/out, HDR/DV, audio in/out, latency, controls), with
a filter picker when the server has more than one filter.

## HTTP API (v1)

### Design rules (most of them for Home Assistant)

* **One request returns everything.** HA's `rest:` integration fetches one resource and pulls any number of sensors
  and binary sensors out of it with `value_template`. The state endpoint is that resource, so 30 entities cost one
  request per `scan_interval`.
* **The schema is fixed.** Every key is always present, whether or not there is a signal. A missing value is `null`,
  never `-1`, `0` or `""`. A missing key makes an HA template fail, while `null` lets it show `unknown`.
* **The right JSON types.** Numbers are numbers, flags are `true`/`false`, and nothing comes back as a string with a
  unit in it (`"23.976 Hz"`). The unit goes in the key name instead: `fps`, `_ms`, `_nits`, `_c`, `_hz`,
  `_bytes_per_sec`.
* **Enums are stable, lowercase snake_case strings** (`"locked"`, `"no_metadata"`), suitable for `device_class: enum`
  and automation triggers. Display text such as the Magewell signal status is kept separately in a `*_text` field.
  Automations should not use the `*_text` fields.
* **Keys are snake_case**, so templates read `value_json.video.input.fps`.
* **Each control is its own resource.** HA's RESTful switch uses one URL for both reading and setting (`GET` to read,
  `POST`/`PUT` with `body_on`/`body_off` to set). Each control URL works that way.
* **Filter ids do not change.** They come from the device, not from load order, so an HA config stays valid after a
  reboot or when another filter loads first.
* **Versioned paths** (`/api/v1/...`). Fields may be added within v1. Renaming or removing one means `/api/v2`.

### Filter ids

`<vendor>-<stable device id>`, lowercase. For Magewell that is the board serial plus the channel (`magewell-a3f21c-0`).
For DeckLink it is `BMDDeckLinkPersistentID`, or the device index if that is missing (`decklink-0`). `default` is an
alias for the first filter registered on the server. Most setups have one capture device per player, so HA configs
can use `default` and never need to know the id.

### Endpoints

| Method | Path | Returns |
|---|---|---|
| GET | `/` | the page |
| GET | `/api/v1/info` | `{"api_version":1,"product":"ezcapture","version":"1.4.0","filters":[{"id":...,"name":...,"device":...}]}` |
| GET | `/api/v1/filters/<id>/state` | everything, see [State document](#state-document) |
| GET | `/api/v1/filters/<id>/controls` | every control with its value, type and range |
| GET | `/api/v1/filters/<id>/controls/<name>` | `{"value":true,"pending_reload":false}` |
| POST, PUT | `/api/v1/filters/<id>/controls/<name>` | body `{"value":...}`, returns the same as GET |
| GET | `/api/v1/filters/<id>/history/<kind>` | phase 4, see [Extra data](#extra-data-phase-4) |

Responses are `application/json; charset=utf-8` with `Cache-Control: no-store`, except the page. Errors are
`{"error":"code","message":"..."}` with `400` (bad value or unknown control), `401` (no token or a wrong one), `404`
(unknown filter or path) or `413` (body over 4 KB).

### State document

```json
{
  "filter": {"id": "magewell-a3f21c-0", "name": "Magewell Pro Capture", "state": "running"},
  "summary": {
    "signal": true,
    "video_mode": "3840x2160p23.976",
    "dynamic_range": "hdr10",
    "audio": "truehd",
    "audio_channels": 8
  },
  "device": {"description": "...", "temperature_c": 51.2, "fan_rpm": null, "link_speed": 3,
             "link_width": 4, "bandwidth_bytes_per_sec": 3.9e9, "bandwidth_used_bytes_per_sec": 1.2e9},
  "display": {"refresh_hz": 24, "status_text": "..."},
  "video": {
    "input":  {"valid": true, "status_text": "Locked", "width": 3840, "height": 2160, "fps": 23.976,
               "bit_depth": 12, "colour_format": "yuv", "pixel_layout": "422", "quantisation": "limited",
               "saturation": "limited", "pixel_clock_mhz": 297.0, "total_width": 5500, "total_height": 2250},
    "output": {"width": 3840, "height": 2160, "fps": 23.976, "bit_depth": 10, "pixel_format": "p210",
               "colour_format": "yuv", "subsampling": "422", "transfer_function": "pq",
               "quantisation": "limited"},
    "measured_fps": 23.977,
    "headroom": {"interval_ms": 41.7, "busy_mean_ms": 6.1, "busy_max_ms": 9.8}
  },
  "hdr": {"active": true, "max_cll_nits": 1000, "max_fall_nits": 400, "max_dml_nits": 1000,
          "min_dml_nits": 0.005, "primaries": {"rx": 0.708, "ry": 0.292, "gx": 0.17, "gy": 0.797,
          "bx": 0.131, "by": 0.046, "wpx": 0.3127, "wpy": 0.329}},
  "dv": {"state": "inactive", "metadata_valid": false, "frames": 0, "rejected_frames": 0, "...": "..."},
  "audio": {
    "input":  {"present": true, "pcm": false, "sample_rate_hz": 192000, "bit_depth": 24, "channel_pairs": 4},
    "output": {"codec": "truehd", "sample_rate_hz": 192000, "bit_depth": 24, "channels": 8,
               "channel_layout": "7.1"}
  },
  "latency": {
    "video": [{"stage": "capture", "min_ms": 1.2, "mean_ms": 1.4, "max_ms": 2.0}],
    "audio": [{"stage": "capture", "min_ms": 3.0, "mean_ms": 3.2, "max_ms": 4.1}]
  },
  "controls": {"refresh_rate_switch": true, "hdr_profile_switch": false, "hdr_profile": 3,
               "sdr_profile": 1, "high_thread_priority": true, "audio_capture": true,
               "metrics_enabled": false, "pending_reload": false},
  "updated": "2026-11-21T20:14:03.512Z"
}
```

The field mappings from the `domain.h` structs are worked out during phase 1 and recorded in a table in
`docs/homeassistant.md`. The enum values (`colour_format`, `transfer_function`, `dynamic_range`, `dv.state`, `audio`)
are listed there too, since automations depend on them.

#### Summary block

The handful of values automations nearly always want, already worked out, so HA templates stay simple:

| Field | Values |
|---|---|
| `signal` | `true` when the video input is valid |
| `video_mode` | `"<w>x<h>p<fps>"` or `null` |
| `dynamic_range` | `sdr`, `hdr10`, `hlg`, `dv` (tunnel locked), `lldv`, or `null` with no signal |
| `audio` | `none`, `pcm`, `ac3`, `eac3`, `truehd`, `dts`, `dts_hd`, `unknown`. These are exactly the values of `enum codec` in `common/domain.h`: `none` is `PAUSE_OR_NULL` or no audio input, and `unknown` is `BITSTREAM` (a burst type the filter does not recognise). |
| `audio_channels` | integer or `null` |

The webhook notifier sends this block when it changes.

The filter cannot detect Atmos (inside E-AC-3 or MAT), DTS:X, or tell DTS-HD MA apart from HRA, because that needs
the bitstream payload to be parsed. If `docs/atmos-decode-plan.md` adds that parsing, it becomes a separate
`audio_objects` field (`atmos`, `dts_x`, `null`). `audio` stays as it is, so the change is an addition within v1
and no existing automation breaks.

### Controls

| Name | Type | Range | When it applies |
|---|---|---|---|
| `refresh_rate_switch` | bool | | now |
| `hdr_profile_switch` | bool | | now |
| `hdr_profile` | int | 0–999 (JRiver JRVR profile id) | now |
| `sdr_profile` | int | 0–999 | now |
| `high_thread_priority` | bool | | next load |
| `audio_capture` | bool | | next load |
| `metrics_enabled`, `metrics_port`, `metrics_bind_address` | bool, int, string | | next load |
| `web_*` settings | | | read-only over HTTP, so the API cannot lock itself out or open itself to the network |

`pending_reload` is `true` when a saved value is not yet in effect. HA can show it as a binary sensor. The `When it
applies` column needs checking against each setter during phase 3.

Each control calls the existing `ISignalInfo` setter, so registry persistence and when it applies stay exactly as
they are. After a change the filter calls `ReloadControls` on every callback, which keeps an open property page in
step. The HTTP thread never touches device state directly. A future control that needs the device (for example a
mode switch) will post to the filter instead of running on the HTTP thread.

Value parsing is lenient so HA's `body_on`/`body_off` stay simple: `{"value":true}`, `{"value":"on"}` and
`{"value":1}` all mean on.

## Home Assistant

HA nearly always runs on another machine, so using it means setting the bind address to `0.0.0.0` or the capture
host's LAN address, and making a token.

### Using the API without a custom integration

```yaml
# secrets.yaml
ezcapture_auth: "Bearer 3f9c...e1"

# configuration.yaml
rest:
  - resource: http://capture-host:9474/api/v1/filters/default/state
    headers:
      Authorization: !secret ezcapture_auth
    scan_interval: 10
    sensor:
      - name: Capture video mode
        value_template: "{{ value_json.summary.video_mode }}"
      - name: Capture dynamic range
        device_class: enum
        value_template: "{{ value_json.summary.dynamic_range }}"
      - name: Capture audio
        value_template: "{{ value_json.summary.audio }}"
      - name: Capture card temperature
        device_class: temperature
        unit_of_measurement: "°C"
        value_template: "{{ value_json.device.temperature_c }}"
      - name: Capture measured fps
        unit_of_measurement: "fps"
        value_template: "{{ value_json.video.measured_fps }}"
    binary_sensor:
      - name: Capture signal
        value_template: "{{ value_json.summary.signal }}"

switch:
  - platform: rest
    name: Capture refresh rate switching
    resource: http://capture-host:9474/api/v1/filters/default/controls/refresh_rate_switch
    method: put
    headers:
      Authorization: !secret ezcapture_auth
      Content-Type: application/json
    body_on: '{"value": true}'
    body_off: '{"value": false}'
    is_on_template: "{{ value_json.value }}"

rest_command:
  ezcapture_set:
    url: "http://capture-host:9474/api/v1/filters/default/controls/{{ name }}"
    method: put
    headers:
      Authorization: !secret ezcapture_auth
    content_type: application/json
    payload: '{"value": {{ value | tojson }}}'
```

HA has no RESTful number or select platform. Integer controls such as `hdr_profile` use a template `number` whose
`set_value` calls `rest_command.ezcapture_set`. `docs/homeassistant.md` gives a full worked example.

### Availability

The server only exists while a graph has the filter loaded. When the player closes, HA marks the entities
`unavailable`. That is accurate ("capture is not running"), and HA automations can trigger on it. It is documented
rather than worked around. `filter.state` (`stopped`, `paused`, `running`) covers a graph that is loaded but not
playing.

### Instant changes: webhook push (optional)

At a 10 s `scan_interval`, a change to HDR or DV can take up to 10 s to reach an automation. To make it instant, an
optional `webNotifyUrl` setting holds an HA webhook URL (`http://ha:8123/api/webhook/<id>`). When the summary block
changes, the filter POSTs `{"filter": "<id>", "summary": {...}}` to it.

* WinHTTP, which is built into Windows, so no new dependency.
* Sent from a thread-pool work item (`TrySubmitThreadpoolCallback`), never from the callback thread. The
  `status_snapshot` callback only records that the summary changed.
* Changes are debounced by 500 ms, so a signal that drops for a moment sends one update, not a burst. Only the
  latest summary is sent. Timeouts are short (2 s connect, 2 s send), there are no retries, and a failure is
  logged once and shown as the notifier status on the property page.
* Off when no URL is set, so it costs nothing.

```yaml
template:
  - trigger:
      - trigger: webhook
        webhook_id: ezcapture-lounge
        local_only: true
    sensor:
      - name: Capture dynamic range (push)
        state: "{{ trigger.json.summary.dynamic_range }}"
```

### Later: a custom integration

The API is designed so a HACS integration can be added later without changing it: `/api/v1/info` identifies the
server and lists filters for a config flow, the controls endpoint gives types and ranges for entity creation, and
filter ids are stable unique ids. Zeroconf discovery could follow by advertising `_ezcapture._tcp` with
`DnsServiceRegister`, which is built into Windows 10+. Neither is part of this plan.

## Security

Any web page the user visits can send requests to the capture host, and DNS rebinding gets around the browser's
same-origin rule. Opening the server to the LAN for HA makes this matter more.

* **Token.** A random 128-bit token is made on first enable and stored in the registry. It is sent as
  `Authorization: Bearer <token>`, the standard header that HA's `headers:` and `rest_command` support.
* **Writes always need the token.** A browser cannot add an `Authorization` header to a cross-origin request
  without a CORS preflight. The server never answers a preflight with CORS headers, so a malicious page cannot send
  a write even if it guesses the URL.
* **Reads:**
  * Bound to loopback: reads work without the token, but only when the `Host` header is `127.0.0.1`, `localhost` or
    `[::1]` with the right port. This stops DNS rebinding.
  * Bound to anything else: reads need the token too.
  * A request with a valid token skips the `Host` check, because HA may use any name or IP for the capture host.
* **The page** reads the token from the URL fragment (`/#t=...`), which is never sent to the server or written to
  logs, and sends it as the Bearer header. The property page shows the full URL and the bare token, each with a copy
  button, plus a "new token" button.
* Token comparison takes constant time. Bodies over 4 KB are rejected. Unknown control names and out-of-range values
  return `400`.
* There is no TLS. Plain HTTP on a home LAN is what HA's REST integrations normally use. Documented as such.
* Binding to a non-loopback address needs an inbound firewall rule. See [Firewall](#firewall).

### Firewall

The installer adds an inbound rule by default. It is a new `[Tasks]` entry, `firewall`, "Allow the web API through
Windows Firewall (private networks)". It is checked by default, and unchecking it skips the rule.

* There is one rule per installed component: `ezcapture Magewell web API` for TCP 9474 and
  `ezcapture DeckLink web API` for TCP 9475. Each is scoped to the private profile with remote addresses
  `LocalSubnet`, so it never opens the port on public networks or beyond the LAN.
* The rules are port rules, not program rules, because the listening process is whichever player hosts the filter.
* They are added with `netsh advfirewall firewall add rule` from `[Run]` (`runhidden`). They are deleted by name in
  `[UninstallRun]`, and any rule with the same name is deleted first so a reinstall does not create duplicates.
* The rule allows nothing while the server is off, because nothing is listening on the port.
* Windows can still show its own prompt the first time a player listens on a LAN address. Dismissing that prompt
  creates a block rule for the player exe, and block rules beat the port rule. `installer/TESTING.md` and
  `docs/homeassistant.md` explain this. `check-install.ps1` reports whether the ezcapture rules are present and lists
  any block rules for known player exes (MPC-BE, MPC-HC, JRiver Media Center).
* If the port is changed in the property page, the installer's rule no longer covers it. The status line shows
  "port N is not covered by the installed firewall rule" when the port differs from the default and the server is
  bound to a non-loopback address.

## Settings

New registry values under each filter's existing key, next to the `prometheus*` values:

| Value | Default |
|---|---|
| `webEnabled` | `0` |
| `webPort` | `9474` (Magewell), `9475` (DeckLink), next to the metrics ports 9464/9465. Confirmed. |
| `webBindAddress` | `127.0.0.1` |
| `webToken` | made on first enable |
| `webNotifyUrl` | empty (off) |

Like metrics, these apply the next time the filter is loaded. The property page gets an enable checkbox, port,
address and notify URL fields, a status line (`Serving http://127.0.0.1:9474/` or the error), the notifier status,
and the token and URL copy buttons. These go through a new `ReloadWebControls` on `ISignalInfoCB`.

## Build changes (both systems)

* `common`: add `status_snapshot.cpp`, `web_ui.cpp` and `web_notifier.cpp` to `common.vcxproj`,
  `common.vcxproj.filters` and `common/CMakeLists.txt`.
* Link `civetweb` / `civetweb-cpp` explicitly in each filter project. Today they only come in through prometheus.
  In CMake: `find_package(civetweb CONFIG REQUIRED)` and `civetweb::civetweb-cpp`. In MSBuild: vcpkg's automatic
  linking already pulls them in, so check that it does not need adding to `AdditionalDependencies`.
* Link `winhttp.lib` (MSBuild `AdditionalDependencies`, CMake `target_link_libraries`) and `bcrypt.lib` for
  `BCryptGenRandom` (the token).
* Page: `tools/gen_web_page.py` gzips `common/web/index.html` into `common/web_page.h`, a byte array plus the
  SHA-256 of the source. The header is checked in, so neither build system needs a generation step, and CI runs
  `gen_web_page.py --check` to fail if the header's hash does not match `index.html`.
* No change to compiler flags.

## Extra data (phase 4)

Things the property page cannot show that are cheap to keep. Each has its own endpoint, so the state poll stays
small and the page or HA asks for history only when it needs it.

* `history/latency`: the last 300 metrics windows (5 minutes) of min/mean/max per stage, as a ring buffer in
  `status_snapshot`, drawn as sparklines. About 300 × 5 × 24 bytes ≈ 36 KB per filter, filled from the
  window callback that already runs.
* `history/signal`: the last 50 changes to the summary block with timestamps, so a dropout overnight can be seen the
  next morning.
* `history/modeswitch`: mode switch results (`OnModeUpdated`) with timing.
* Counters for frame drops and headroom, added to the state document. Totals, not rates, so HA's
  `state_class: total_increasing` and long-term statistics work.
* The full HDR and DV metadata, which the page has to cut short.
* `log`: the tail of the log through a fixed-size quill sink. Optional, and only filled while the server is on.

## Phases

1. **Plumbing.** Callback fan-out, `status_snapshot` with its JSON (fixed schema, summary block), unit tests in
   `mwcapture-test`, and the field mapping table in `docs/homeassistant.md`.
2. **Read-only server.** `web_ui`, settings, token, Host check, property page enable/status, `GET` endpoints, the
   page, the installer firewall task and the `check-install.ps1` firewall check. Measure idle and polled CPU and
   threads on the capture host, and test the `rest:` example against a real HA instance.
3. **Controls.** `PUT`/`POST` control endpoints, `pending_reload`, the RESTful switch, `rest_command` and template
   number examples tested in HA.
4. **Webhook notifier.**
5. **Extra data.** History endpoints, counters.

## Testing

* Unit tests:
  * The state JSON has every key present with no signal and with each signal type, and nulls where values are
    missing.
  * Enum values match `docs/homeassistant.md`.
  * String escaping.
  * Token and Host header rules.
  * Control value parsing (`true`, `"on"`, `1`).
  * The summary derivation for SDR, HDR10, HLG, DV tunnel and LLDV.
* Home Assistant: load the YAML from `docs/homeassistant.md` into a test instance and check:
  * Every entity has a value.
  * The switch works in both directions, and a change made on the property page shows up in HA.
  * Entities go `unavailable` when the player closes and come back when it reopens.
  * The webhook fires once per real change.
* On the capture host:
  * Threads and CPU in Process Explorer with the server off, on and idle, and polled by HA every 10 s with 4K
    capture running.
  * `ezcapture_frame_latency_seconds` with the server on and off.
  * The webhook pointed at an unreachable host: capture must not be affected.
* Installer: with the firewall task checked, the rules exist with the private profile and local subnet scope. A
  reinstall leaves one rule each, not two. Uninstall removes them. With the task unchecked, no rules are added.
* Two filter instances in one player, and Magewell and DeckLink DLLs loaded together (separate servers and ports,
  stable ids).
* Stop the graph and destroy the filter while a request or webhook send is in progress, and check there is no
  use-after-free. `RemoveCallback`, unregistering from `web_ui` and cancelling the notifier all happen before the
  status members are freed.

## Decisions

* Ports are 9474 (Magewell) and 9475 (DeckLink).
* `summary.audio` is limited to what `enum codec` can tell apart. Object audio comes later as a separate field.
* The installer adds the firewall rules by default (private profile, local subnet), and the user can uncheck the task.
