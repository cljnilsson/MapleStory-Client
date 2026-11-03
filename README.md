# HeavenClient

HeavenClient is a custom, made-from-scratch game client for Maplestory v83.

## Headless CLI login (Linux)

After building `build/HeavenClient` and starting the game server, launch the
offscreen client with `python ms_headless.py start`. Headless launches always
set `ALSOFT_DRIVERS=null` so OpenAL cannot use a host audio device. The script uses the project
directory containing `ms_headless.py` by default. It requires Xvfb, xauth, and
xdotool; `python ms_headless.py stop` closes the session.

To enter the game with the selected character:

```sh
python ms_headless.py play --username your-account
```

The command prompts for a password without echoing it. For scripted use, pipe
the password to `play --password-stdin`. Omit `--username` if the client saved
the account ID. `enter-game` is an alias for `play`. The command waits for
client-reported screen transitions. It exits nonzero on rejection or timeout
without using fixed login delays. It chooses
the first available world and its first channel, then uses the currently
selected character. Add `--character PortWarrior` to select an exact name through
the character-selection controller before entering. Selection failure stops the
command before Enter. At character selection, `state --field login` lists character
IDs/names, `selected_index`, `selected_name`, and `pic_mode`;
`act character-choose PortWarrior --wait-state login.selected_name==PortWarrior`
changes the selection without entering the game, including across pages.
Character creation, PIC prompts, and other notices still
need manual controls. Rebuild the client after updating it, because an older
binary does not emit the `HEADLESS_STATE` markers used by `play`.

The headless snapshot (`state.json`) exposes schema/build/asset identities,
session ID, snapshot sequence, main-loop progress, uptime, screen, map and
supported semantic actions. In-game snapshots also report whether the stats,
item inventory and equipment inventory windows are active (`ui.stats_open`,
`ui.item_inventory_open`, `ui.equip_inventory_open`), so `wait --state` or
`act --wait-state` can verify window actions. Use `act window stats open` (or
`items` / `equipment`) and `act window <name> close` to control these windows
without depending on key bindings. `status` reports
snapshot health. Event records
carry the session ID; malformed and unhandled server packets produce
`packet_error` events with opcode, packet length, parser offset and reason, but
never packet contents. Wait commands reject events from another session and
report a disconnected client, a stalled loop or a normal timeout separately.

## Supported versions

- The client is currently compatible with version 83 servers.
- The client has only been tested with [HeavenMS](https://github.com/ronancpl/HeavenMS).
- There is also a Switch version available here: [HeavenClientNX](https://github.com/lain3d/HeavenClientNX).

## Configuration

The build can be configured by editing the **MapleStory.h** file. The following options are available:

- **USE_ASIO**: Use Asio for networking (additional dependency)
- **USE_CRYPTO**: Use cryptography when communicating for the server

The default settings can be configured by editing the **Configuration.h** file. These are also generated after a game session in a file called **Settings**. These can be altered in the same way as **Configuration.h**, although, these do not persist if you delete the file, unlike **Configuration.h**.

## Building

*After cloning you need to check out the linux branch! To do so run ```git checkout linux``` in your HeavenClient directory.*

1. Run ```./build-deps.sh```. We try to build each dependency from source -- if any dependencies fail to build, you could try and find the corresponding package for your linux distro if it exists.
2. ```mkdir build```
3. ```cd build```
4. ```cmake ..```
5. ```make -j$CORES``` where $CORES is your number of CPU cores

This will always be the option with the most performance, but if you are using Mac / are having issues, try out the [Vagrant](#vagrant-setup) or [Docker](#docker-setup---web-vnc) setups.

## Required Files

*Always check **NxFiles.h** for an updated list of required nx files*

- [MapPretty.nx](https://drive.google.com/file/d/1d8HJkWY6ght5OAoJGtsAjNiG2BL1wcle/view?usp=sharing) (v167 GMS Map.wz)
- MapLatest.nx (Lastest GMS Map.wz)
- Map001.nx (Latest GMS Map001.wz)
- UI.nx (Latest GMS UI.wz)
- Everything else is from v83 GMS wz files

## Dependencies

- Nx library:
[NoLifeNX](https://github.com/ryantpayton/NoLifeNx)

- Graphics:
[GLFW3](http://www.glfw.org/download.html), [GLAD](https://github.com/Dav1dde/glad), [FreeType](http://www.freetype.org/)

- Audio:
[OpenAL-soft](https://github.com/kcat/openal-soft), [Alure](https://github.com/kcat/alure)

- Networking:
[Asio](http://think-async.com/)

---

## In-Game Issues

If you experience any kind of in-game glitches, UI rendering issues, or anything else that seems out of the ordinary that other developers are not experiences; Follow these steps in order to hopefully resolve aforementioned issues.

1. Clean Solution
2. Close Visual Studio
3. Delete the following files/folders: **.vs**, **x64**, **debug.log**, **MapleStory.aps**, **Settings**
4. Open Solution
5. Rebuild Solution

Note: These steps are not applicable to linux

---

## Vagrant setup

A [Vagrantfile](./Vagrantfile) has been included in the repo to simulate a complete HeaventClient setup on a linux ubuntu/bionic virtual machine (using virtualbox provider).

One can refer to the shell scripts written within the Vagrantfile to get an insight of the complete environmental setup and dependencies required to build and run the HeavenClient binary.

### Building/Running client via Vagrant

All ssh commands must be run on a shell within context of the HeavenClient directory (this project)

1. ```vagrant up```
2. Login to the desktop environment via the virtualbox window with default vagrant credentials (vagrant:vagrant) - once the bootstrap script has completed execution
   1. This is required to start up desktop environment (xfce)
3. ```vagrant ssh -- -R 8484:localhost:8484 -R 7575:localhost:7575 -R 7576:localhost:7576 -R 7577:localhost:7577```
   1. We SSH into the VM with reverse port-forwarding; this allows us to run/dev our [server](https://github.com/ronancpl/HeavenMS) on the host machine while allowing the client to connect to it and run on the VM
4. ```cd /home/vagrant/Desktop/HeavenClient/build/```
5. ```./HeavenClient```

Note: To run the HeavenClient from the build directory; all the [relevant **.nx** files](./Util/NxFiles.h) must be available within the linux build directory

Tip: Since the binary is built on the VM with a mounted/shared volume; the binary is also available/usable by a linux host

## Docker Setup - web-vnc

A [Dockerfile](./Dockerfile) has been included in the repo to simulate a complete HeaventClient setup on a linux ubuntu/bionic OS.

The Docker setup utilizes [fcwu/docker-ubuntu-vnc-desktop](https://github.com/fcwu/docker-ubuntu-vnc-desktop) **web-based lxde VNC** solution to output HeavenClient GUI on a web browser at port **6080**.

This allows the host to rely on minimal dependencies (other than nx files) for running HeavenClient.

### Docker Pre-requisites

- All [relevant](./Util/NxFiles.h) **.nx** files must be located in the **nx** directory
- A [Settings](./Settings) file must be present in project root - instructions to create this [below](#building--runnning-client)
  - This gives user ability to configure HeavenClient outside the container - on the host
- Relevant audio passthrough device must be installed on the host
  - Mac: [pulseaudio](https://www.freedesktop.org/wiki/Software/PulseAudio/)
  - Linux: [snd-aloop](./https://www.alsa-project.org/wiki/Matrix:Module-aloop)
- [HeavenMS](https://github.com/ronancpl/HeavenMS) server running at port 8484 on host machine

### Installing sound passthrough device

#### Mac

```sh
# Install pulseaudio
brew install pulseaudio

# Run pulseaudio daemon
pulseaudio --load=module-native-protocol-tcp --exit-idle-time=-1 --daemon
```

Note: To stop sound passthrough daemon: ```pulseaudio --kill```

#### Linux

```sh
# Insert kernel module snd-aloop and specify 2 as the index of sound loop device
sudo modprobe snd-aloop index=2
```

### Building & Runnning Client

1. Create Settings with server ip pointing to host
   1. ```echo "ServerIP = host.docker.internal" > Settings```
      1. This allows the HeavenClient application within the container to point to the running hosts IP (on which the server will be running)
2. Build & Run container for your OS
   1. Mac:
      1. ```docker-compose -f docker-compose.yml -f docker-compose.mac.yml up --build```
   2. Linux:
      1. ```docker-compose -f docker-compose.yml -f docker-compose.linux.yml up --build```
3. Run HeavenClient in the browser GUI at [localhost:6080](http://localhost:6080) by double clicking the HeavenClient icon

</br>
Note: If the client fails to startup, then it is very likely something went wrong with the setup. The link doesn't show errors hence you will have to use the **LXTerminal** within the web-based GUI to run the client manually.

- The HeaventClient binary is located within the **/root** folder of the container
- You may need to restart your browser before trying to run the game after applying any fixes
- You need to ```docker-compose down``` if you move your **.nx** files into the **nx/** difectory post container start-up (since symlinks are generated only when starting up fresh container)

---

## Donations

If you feel obligated to donate, to further help and support all parties involved in the development of the HeavenClient project, you can donate using [this](https://paypal.me/pools/c/8frYNoobcY) link.

Please remember this is ONLY for the HeavenClient development and will only be used in the support of helping further develop the client. *Also please remember to support Nexon as this is not meant to replace anything Nexon offers*

Another important note to remember is that HeavenClient is a free open-sourced client developed for personal use. Do NOT pay for any services requested by anyone in regards to this client. It will always remain open and free of charge. There is no intent to publish this code with any payment in mind. If that ever changes, donations and disclaimers for donations will be removed.

M161 world/channel selection (build and offline verification only):
```sh
python ms_headless.py state --field login
python ms_headless.py act world-choose 0 --wait-state login.world_id==0
python ms_headless.py act channel-choose 2 --wait-state login.channel==2
python ms_headless.py play --world 0 --channel 2 --character PortWarrior
```
World IDs come from `login.worlds`; channel numbers are 1-based. World selection
opens the channel screen. Channel selection alone does not enter it; `play`
confirms the selection and presses Enter. Only worlds supported by the existing
UI (IDs 0–4 with available button art) can be selected. Snapshots expose all
advertised world names, IDs and channel counts. Live verification remains open.

`play --world --channel` also applies both choices when resuming on the channel
screen; a rejected selection stops before Enter.

Login modal observations are available on every screen through `state --field
login_dialog`: `pic_keyboard` and `notice` are booleans; `message_id` is the
`UILoginNotice::Message` numeric value, or null when no notice is active.
For example, `wait --state login_dialog.pic_keyboard==true` waits for the PIC
keyboard, and `wait --state login_dialog.notice==true` waits for a login notice.
PIC contents are never included. World/channel/character selection actions reject
while either modal is active; dismiss the dialog through its existing UI first.
These observations are build-verified only; live PIC acceptance remains open.

`python ms_headless.py pic` prompts for the PIC without echo; `pic --stdin`
reads it from stdin for automation. This command requires an already open PIC
keyboard on character selection and accepts 6–16 ASCII digits, matching the
numeric UI. It submits through the keyboard's existing OK callback, including
registration/confirmation callbacks when those dialogs are open. It does not
open the dialog or automatically register a PIC.

The PIC is never a CLI argument or included in snapshot/action events. The
request uses an owner-only temporary mailbox file in the private session directory;
the client removes it before dispatch, and the CLI cleans up on timeout.
`pic --wait-state screen==in_game` can observe entry after submission; dispatch
success alone does not confirm server acceptance. Wrong-PIC and first-registration
flows still require live verification. No PIC live test has been run for this slice.

`act login-notice confirm` acknowledges the active login notice through its UI
callback (including the PIC registration prompt). `act login-notice cancel`
chooses No only when that notice offers a cancel button; otherwise it rejects.
Snapshots expose `login_dialog.can_cancel`. Use
`--wait-state login_dialog.pic_keyboard==true` when confirming registration,
or `--wait-state login_dialog.notice==false` when dismissing an error.
These actions require an active login notice outside the field and reject other
dialog classes sharing its UI slot. Dispatch success does not prove server
acceptance; registration and wrong-PIC flows remain unverified live.

`play` uses the guarded `character-enter` action for no-PIC entry and waits for
the in-game screen event. Rejected entry stops immediately without sending Return.

`act character-enter` activates the selected character through the same callback
as the Select button. It requires an active character-selection screen, an
available character, no active PIC/login notice, and no arguments. For PIC mode
1, follow with `pic` after `--wait-state login_dialog.pic_keyboard==true`.
For registration (mode 0), confirm the notice first with `act login-notice confirm`;
the existing registration/confirmation callbacks handle subsequent submissions.
For mode 2, use `--wait-state screen==in_game` to observe entry. Dispatch success
only means the callback ran; server acceptance remains a separate observation.

`act character-back --wait-state screen==world_select` returns from character
selection through the Back button callback, including when the character list is
empty. It accepts no arguments and rejects while a login/PIC dialog owns input.
The screen event follows the restored world controller (world or channel selection);
use `play --world <id> --channel <number>` to select and enter again.

`act pic-cancel` uses the active PIC keyboard cancellation callback. It rejects
keyboards without one; `login_dialog.pic_can_cancel` reports availability. This
action accepts no arguments and can be used in a batch before selecting again.

`login_dialog.modal` reports any active dialog in the shared login-notice slot,
including quit, key-selection and class-confirmation dialogs, and the separate
key-confirmation slot. Selection, entry
and PIC actions reject while this flag is true; `login-notice` still responds
only to an ordinary login notice. This prevents actions behind other dialogs.

The in-game Change Channel window now sends the v83 channel request and logs the
character into the redirected channel. It uses the last advertised channel count
(up to the window's 20-channel layout) and prevents selecting the current channel.
This path still needs live transfer verification; a dedicated headless channel
change action and outcome observation are not yet available.

M161 channel-change automation: `act channel-change 2` (also accepted in a batch)
requests a 1-based advertised channel through the same controller as the channel
window. It rejects the current/unavailable channel, active movement, NPC/login
dialogs and Cash Shop. Dispatch acknowledgement is not transfer completion.
In-game `navigation` exposes `world_id`, `configured_channel`, `pending_channel`
(0 means no outstanding target), and `field_entry_seq`, incremented after each
completed field transition. Configured channel changes on the redirect response;
only a later field-entry sequence can establish that a field loaded. An ignored
request can leave the pending target visible; a retry replaces it.
Known blocker: full character-data field entry currently requires UICharSelect,
which is absent during an in-game transfer. Transfer reinitialization must be
fixed before claiming live channel-change support.
