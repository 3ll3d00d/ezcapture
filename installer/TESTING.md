# Installer test plan

Manual checks for `ezcapture-<version>-setup.exe`. Run them on the capture PC, plus Windows Sandbox for the
prerequisite tests (section 4), because the capture PC already has the prerequisites installed.

## Setup

1. Get two installers with different versions so the upgrade can be tested:
   * A: `gh run download 44049991572 -n installer` gives `ezcapture-0.0.0-ci.64-setup.exe`
   * B: start another build with `gh workflow run test.yml --ref feature/decklink-dv`, then
     `gh run download <run id> -n installer` once it finishes (`0.0.0-ci.<next number>`)
2. Copy `installer\check-install.ps1` next to them. It is read-only and reports the install folder, the
   registry entries, the filter registrations and the prerequisites. "Check" below means run it with
   `powershell -ExecutionPolicy Bypass -File check-install.ps1`.
3. Run Check before starting. If any filter is already registered from a zip, note its path for section 7.

Notes:

* The installer is unsigned, so expect a SmartScreen warning ("More info" > "Run anyway").
* CI test builds are not given a version number, so the property page footer shows `v0`. Release builds show the tag.
  The installer's version is `0.0.0-ci.N` regardless.
* Setup writes a log to `%TEMP%\Setup Log <date> #NNN.txt`. Look there first if something fails, e.g. search for
  `regsvr32`.

## 1. Fresh install through the wizard (A, both filters, with trace)

| Page                 | Expect                                                                                   |
|----------------------|------------------------------------------------------------------------------------------|
| Welcome              | "ezcapture 0.0.0-ci.64", no "currently installed" sentence                               |
| Destination          | `C:\Program Files\ezcapture`                                                             |
| Components           | Types: both / Magewell only / Blackmagic only / Custom. Magewell filter has two sub-items, Pro Capture (PCIe) and USB Capture. On a PC where Windows has seen a Magewell device only that family is ticked (type becomes Custom), with none seen both are ticked. Unticking both sub-items leaves the Magewell filter ticked. Unticking both filters and pressing Next gives "Select at least one filter" |
| Tasks                | Trace and firewall explanation (mentions Prometheus and ports 9474/9464/9475/9465) is fully readable, not clipped or overlapping the task boxes. "Install the trace builds" is unticked, "Allow the web API and Prometheus metrics through Windows Firewall" is ticked |
| Remote access        | Both boxes match the current property page settings. "Listen on" lists 0.0.0.0, this PC's LAN address(es) and 127.0.0.1, and shows the address in use (0.0.0.0 if none set). The token box shows the existing token with "setup keeps it", or a new 32 hex digit token. Unticking both greys out the address, unticking the web API greys out the token |
| Prerequisites        | A table with columns Dependency / Installed / To do: Visual C++ runtime, Magewell capture runtime, Magewell Pro Capture driver (the version Check shows, e.g. 1.3.0.4437, only if Pro Capture is ticked), Magewell USB Capture Utility (its version or "not found", only if USB Capture is ticked), Blackmagic Desktop Video, LAV Filters, each with its installed version. Current ones say "nothing". Rows line up and nothing is clipped. The box below lists Desktop Video and LAV Filters if missing, or "Nothing." |
| Ready                | Version, Build "AVX2 optimised" (or no-arch on an older CPU), both components, the trace and firewall tasks, the remote access choices, no prerequisites to install |
| Finish               | "Open the Magewell Pro Capture driver download page" (ticked, only if Pro Capture is ticked and the driver is missing), "Open the Magewell USB Capture download page" (ticked, only if USB Capture is ticked and the utility is not found), "Open the Blackmagic Desktop Video download page" (ticked, only if Desktop Video is missing), LAV Filters page (unticked, only if LAV is not registered), player setup guide (unticked). Ticked ones open in the browser |

Tick the trace task and install. Check:

- [ ] Installed apps entry `ezcapture 0.0.0-ci.64`, DisplayVersion `0.0.0-ci.64`
- [ ] `HKLM\Software\ezcapture`: Version, Build matching the CPU line, InstallDir
- [ ] Files: `mwcapture.ax`, `mwcapture-trace.ax`, `bmcapture.ax`, `bmcapture-trace.ax`, `COPYING`, `version.txt`,
      `licenses\MWCaptureRT-RedistLicense.rtf`, `unins000.exe/.dat`
- [ ] All four filters registered to the Program Files paths and listed as video capture sources
- [ ] Firewall rules `ezcapture Magewell web API` (port 9474), `ezcapture DeckLink web API` (port 9475),
      `ezcapture Magewell Prometheus metrics` (9464) and `ezcapture DeckLink Prometheus metrics` (9465), all
      enabled, action Allow, profile Private, remote LocalSubnet
- [ ] No "could not be registered" error box

Functional:

- [ ] JRiver: TV Options > Manage Devices lists Magewell Pro Capture, Magewell Pro Capture (Trace) and the Blackmagic equivalents
- [ ] Play a channel on the normal Magewell filter: video and audio work, the property page opens
- [ ] Switch to the trace filter: it plays, and a new `mwcapture_<epoch>.log` appears in `%TEMP%`
- [ ] With a DeckLink card and Desktop Video present, the same for the Blackmagic filters

## 2. Re-run the same version and change the selection

Run A again:

- [ ] Welcome says "ezcapture 0.0.0-ci.64 is already installed, you can use this to change which filters are installed"
- [ ] Components and the trace task are preselected as last time

Choose "Magewell only", untick trace, install. Check:

- [ ] `bmcapture.ax`, `bmcapture-trace.ax` and `mwcapture-trace.ax` deleted, and those three filters not registered
- [ ] Only the two Magewell firewall rules remain, and only one of each (not duplicates)
- [ ] `mwcapture.ax` still registered, and JRiver still plays the Magewell channel
- [ ] Still a single Installed apps entry

Run A again and choose both filters with trace, so the upgrade test starts from a full install.

## 3. Upgrade (A to B)

Run B:

- [ ] Welcome says "ezcapture 0.0.0-ci.64 is currently installed and will be replaced by 0.0.0-ci.<B>"
- [ ] Ready page shows "Version: 0.0.0-ci.<B> (replacing 0.0.0-ci.64)"
- [ ] After install: a single Installed apps entry showing B, `version.txt` and the registry show B, all four filters still registered

Upgrading while the filter is in use:

- [ ] Start playback in JRiver, then run B again. Setup lists JRiver as using files and offers to close it.
      Choose "Do not close": setup finishes asking for a restart. After the restart, Check shows everything registered and capture works
- [ ] Repeat, letting setup close JRiver: no restart needed. JRiver is not restarted automatically (by design)

## 4. Prerequisites (Windows Sandbox)

Windows Sandbox is a clean, disposable Windows with no VC++ runtime, Magewell runtime, Desktop Video or LAV. Enable
it in "Turn Windows features on or off" if needed, start it and copy the installer and `check-install.ps1` into it.
Each test needs a fresh sandbox: close it and start again.

4a. Accept the prerequisites:

- [ ] Prerequisites page lists "Microsoft Visual C++ runtime 14.x (not installed)" and "Magewell capture runtime
      3.x (not installed)", both ticked
- [ ] Ready page lists both under "Prerequisites to install"
- [ ] A "Installing prerequisites" progress page appears, then the install completes without error
- [ ] Check: the VC++ runtime matches the bundled version, `LibMWCapture.dll` matches `mwsdk\VERSION`, all filters registered

4b. Decline (fresh sandbox):

- [ ] Untick the Magewell runtime: the do-it-yourself box immediately says the runtime is not installed and the
      filter cannot be registered without it
- [ ] Untick the VC++ runtime as well: a bullet with the aka.ms download link appears
- [ ] Install: an error box lists the filters that "could not be registered" and names the missing prerequisites
- [ ] Run setup again and accept the prerequisites: everything registers

4c. Blackmagic only (fresh sandbox):

- [ ] The Magewell runtime is not offered (only VC++), and the Desktop Video bullet is shown

4d. Magewell only (fresh sandbox, no Pro Capture driver):

- [ ] The Pro Capture driver row says "not installed", "install yourself", the box below has the driver bullet with the
      magewell.com/downloads/pro-capture link, and Finish offers "Open the Magewell Pro Capture driver download page"
- [ ] Untick Pro Capture, leave USB Capture ticked: the driver row and bullet go, the USB Capture Utility row and the
      utility and firmware bullets appear, and Finish offers the USB Capture download page instead

4e. Magewell runtime in use (capture PC with an older MWCaptureRT, a Magewell app holding LibMWCapture.dll open):

- [ ] Setup installs the runtime and asks to restart. Before restarting, running setup again shows the runtime row as
      "restart Windows" with a restart bullet, not the install tick box
- [ ] After restarting the row shows the bundled version and "nothing"

## 5. Silent install (sandbox or capture PC)

From an admin prompt:

```
ezcapture-0.0.0-ci.64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /COMPONENTS="mw" /TASKS="" /LOG=%TEMP%\ez.log
```

- [ ] No UI. Check shows only `mwcapture.ax`, registered
- [ ] In a fresh sandbox the prerequisites are installed silently (ticked by default)
- [ ] `/COMPONENTS="mw,bm" /TASKS="trace"` installs all four

## 6. Uninstall

From Settings > Apps > Installed apps > ezcapture > Uninstall:

- [ ] Check: no Installed apps entry, no `HKLM\Software\ezcapture`, no install folder, none of the four filters registered,
      no ezcapture firewall rules
- [ ] JRiver no longer lists the filters
- [ ] VC++ and Magewell runtimes remain installed (expected, other software may need them)
- [ ] Filter settings under HKCU remain (expected)

Uninstall while JRiver is playing:

- [ ] Uninstall completes, saying some files will be removed on restart. After the restart the folder is gone

## 7. Previously installed from a zip

On a PC (or sandbox) where `mwcapture.ax` was registered from a zip folder with `regsvr32`:

- [ ] After installing, Check shows the Magewell filter registered to Program Files rather than the zip folder
- [ ] After uninstalling, the filter is no longer registered, even though the zip copy still exists (expected,
      re-run `regsvr32` on the zip copy if you want it back)

## 8. CPU without AVX2 (optional)

On a PC whose CPU lacks AVX2 (Check prints "no (NoArch)"):

- [ ] Ready page shows the no-arch build, and `HKLM\Software\ezcapture\Build` is `NoArch`
- [ ] Capture works (the AVX2 build would crash with an illegal instruction on such a CPU)

## 9. Web API, Prometheus and firewall rules (capture PC)

Run setup again with the firewall task unticked:

- [ ] Check shows all four firewall rules absent

Run it again with the firewall task ticked. On the Remote access page tick both boxes, choose `0.0.0.0` and note the
token, then finish:

- [ ] Check's remote access section shows web=1, prometheus=1, both addresses `0.0.0.0` and a 32 character token for
      each installed filter
- [ ] Run setup once more: Remote access shows both ticked, `0.0.0.0`, and the same token with "setup keeps it"
- [ ] Choose this PC's LAN address instead, finish: Check shows that address for both

Set it back to `0.0.0.0` and the Magewell port to 9474 on the property page if it was changed, then restart playback:

- [ ] The status line reads `Serving http://0.0.0.0:9474/ as magewell-<serial>`
- [ ] No Windows Firewall prompt appears. If one does, choose Allow, never Cancel: Cancel creates a block rule for the
      player that beats the installer's rule. Check lists any such block rules under "Player block rules"
- [ ] From another PC on the LAN, **Copy Link** opens the status page and it updates every second
- [ ] `curl http://<capture-pc>:9474/api/v1/filters/default/state` gives 401, and with
      `-H "Authorization: Bearer <token>"` gives the JSON
- [ ] Change the port to 9480, apply, restart playback: the status line adds "port 9480 is not covered by the
      installed firewall rule", and the other PC cannot connect
- [ ] Set the port back to 9474
- [ ] From another PC, `http://<capture-pc>:9464/metrics` returns the metrics
- [ ] On the property page, untick the web API and change its address without applying, then click **New Token** and
      confirm: the token and link change, the untick and the address edit are still there, and Apply saves them
- [ ] Delete the `webToken` value under `HKCU\Software\3ll3d00d\MWCapture`, reopen the property page and click
      **New Token**: it makes a token without asking to replace one

## 10. Release build

After tagging a release:

- [ ] The release has `ezcapture-<tag>-setup.exe` next to the zips
- [ ] Properties > Details of the setup exe shows the product version `<tag>`
- [ ] After install, the property page footer shows `v<tag>`, matching Installed apps
