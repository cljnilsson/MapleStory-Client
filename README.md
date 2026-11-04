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

M161 world/channel selection:
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
advertised world names, IDs and channel counts. Isolated live checks covered world 0 and channels 1/2.

`play --world --channel` also applies both choices when resuming on the channel
screen; a rejected selection stops before Enter.

Login modal observations are available on every screen through `state --field
login_dialog`: `pic_keyboard` and `notice` are booleans; `message_id` is the
`UILoginNotice::Message` numeric value, or null when no notice is active.
For example, `wait --state login_dialog.pic_keyboard==true` waits for the PIC
keyboard, and `wait --state login_dialog.notice==true` waits for a login notice.
PIC contents are never included. World/channel/character selection actions reject
while either modal is active; dismiss the dialog through its existing UI first.
Wrong-PIC recovery and first registration passed isolated muted live checks on 2026-11-03.

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
success alone does not confirm server acceptance. Wrong-PIC recovery and first
registration were observed with the fan client and an isolated demo database.

`act login-notice confirm` acknowledges the active login notice through its UI
callback (including the PIC registration prompt). `act login-notice cancel`
chooses No only when that notice offers a cancel button; otherwise it rejects.
Snapshots expose `login_dialog.can_cancel`. Use
`--wait-state login_dialog.pic_keyboard==true` when confirming registration,
or `--wait-state login_dialog.notice==false` when dismissing an error.
These actions require an active login notice outside the field and reject other
dialog classes sharing its UI slot. Dispatch success does not prove server
acceptance; observe the resulting notice or field entry.

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
A muted isolated live check transferred from channel 1 to 2 with a new field-entry sequence.

M161 channel-change automation: `act channel-change 2` (also accepted in a batch)
requests a 1-based advertised channel through the same controller as the channel
window. It rejects the current/unavailable channel, active movement, NPC/login
dialogs and Cash Shop. Dispatch acknowledgement is not transfer completion.
In-game `navigation` exposes `world_id`, `configured_channel`, `pending_channel`
(0 means no outstanding target), and `field_entry_seq`, incremented after each
completed field transition. Configured channel changes on the redirect response;
only a later field-entry sequence can establish that a field loaded. An ignored
request can leave the pending target visible; a retry replaces it.
Full character-data field entry can reuse the current character when the character
selector is absent, requiring a matching character ID. Both entry paths replace
stats, inventories, skills, quests, travel/book data and cooldowns from the packet.
The server field header supplies the actual channel, including random View All selection.

`act character-delete <exact-selected-name>` invokes the selected character's
Delete button and rejects a missing or mismatched name, unavailable selection,
or active login/PIC modal. This is destructive: mode 2 sends the deletion request
immediately; mode 1 retains the existing confirmation and protected `pic` flow;
mode 0 displays the existing no-PIC refusal. Dispatch is not proof of deletion.
The `character_delete_result` event reports server `character_id`, raw `status`
and `ok` (status 0); successful responses also remove the character from the
character-list snapshot. Isolated live checks removed a disposable non-leader and
refused a synthetic guild leader (status 22).

`act logout open --wait-state logout_dialog==true` opens the existing Quit
controller; `act logout cancel --wait-state logout_dialog==false` dismisses it.
`act logout confirm --wait-state screen==login` uses the existing fade/field-clear/
login-reconnect flow. Confirmation requires an active Quit dialog. Opening rejects
active movement, NPC/login dialogs and Cash Shop; other semantic field actions
reject while the Quit dialog owns input. Confirmation marks `logout_pending`
until the login UI is created. Observing `login` confirms the screen transition,
not successful server authentication or relog. Isolated live checks separately
confirmed a fresh authentication after logout.

Returning to the login screen clears headless NPC-dialog, party/invite and
map/object observations and emits `observations_reset` with reason `login`.
A later entry into the same map reports its objects afresh. Process event and
field-entry sequences remain monotonic. This reset does not prove reconnect or
authentication success and does not clear server-side party membership.


`act view-all --wait-state login.view_all==true` requests every account character
from world/channel selection or ordinary character selection. The latter also
has a “View all characters” click target at (521,300), size 180×24. The aggregate
list exposes IDs, names and `world_id`; choose with `character-choose`, then
`character-enter`. PIC entry/registration uses the same protected keyboard flow.
The server chooses the channel; observe `navigation.configured_channel` after
field entry. Create/delete are disabled in this list; use `character-back` and
ordinary selection for those operations. Server padding duplicates are removed;
more than 127 raw entries is an explicit unsupported-count error. The original
world-panel “View All” button continues to control recommended worlds.

M161's live exit checks passed with muted clients and a temporary demo database,
including View All entry with PIC enabled and disabled. These are fan-client
observations (`X?`), not original compatible-client acceptance.

## Field and presentation diagnostics (M162)

Use `act observe on` before a short test, then `act observe off`. Tracing is
opt-in and stops recording presentation/motion observations after 4,096 records
per enable window (`observation.count`, `dropped`, `limit`). Existing event logs
rotate at 1 MiB. `observation.render_frame` identifies a field draw invocation;
it does not establish that a sprite was visible or that its pixels were correct.

`presentation` events distinguish `packet`, `model`, `render` and `end`
boundaries. Remote attacks report their actor and skill at packet receipt and
model application. Character actions report model changes and first draw after
a change. Effects and damage numbers carry an `observation_id` from construction
to first draw (and effect end); damage creation records its numeric value and
target actor. Generic effects may have an unknown source/actor (zero). Local
buff records expose stat, source, duration and estimated expiry in process uptime;
buff icons have independent render/end observations. Skill support remains the
client's existing support; an observation does not implement a missing skill.

Snapshots include local/remote action, stance, velocity, foothold and climbing,
mob HP percentage (null before an update), stance/death state, spawn temporary
status masks/records, drop item/meso/owner/pickup state, and reactor state. Spawn
status records are packet observations, not a maintained active-status model;
subsequent unsupported status packets remain visible as packet errors. Ground
item packets omit stack quantity, so `quantity` is null. Remote `grounded` is
also null: this fan client's remote physics flag can remain true in flight.
`physics_grounded` exposes that flag explicitly; `movement` exposes the last
received/applied movement's stance byte, foothold, coordinates and duration.
Use those values with `motion` transition events to inspect jumps, landings and
ladder/rope entry/exit, without taking screenshots every frame.

Each object array is capped at 512 with `<kind>_truncated`; retained mob packet
metadata is capped at 2,048 and motion actors at 512. Existing inventory contents
and UI window flags remain available; `ui.chat_input_open` lets native chat
scripts wait for input ownership instead of assuming a fixed delay.

`act observe pause` pauses periodic snapshots (one final paused marker is
written); actions and event recording continue. `status` reports `paused` and
snapshot predicates reject stale values until `act observe resume`. These
controls are for bounded diagnostics, not a replacement for normal health
checks. `act observe stats` emits progress/frame/export counters and total/max
snapshot export microseconds, including serialization and atomic file writing.
Enable/disable tracing resets these measurement counters. Compare quiet paused,
export-on and trace-on windows; render throughput is sensitive to host load.
All headless launches force `ALSOFT_DRIVERS=null`.

## Loot, chairs and inventory diagnostics (M163)

All actions below dispatch ordinary gameplay packets; snapshots change only when
normal client gameplay or server replies update the model. They do not grant,
remove or edit inventory items through the automation interface.

- `act pickup` uses the same settled-drop selection and centred 32 × 24 pixel
  half-range as the native loot action, and sends the player's actual position.
  It rejects when no settled drop is in range. Ownership, inventory space and
  server timing can still reject a dispatched request; wait for the inventory
  change and field removal separately. Use the character's actual key binding
  for native input (the demo fixture binds pickup to X).
- `act sit` uses the existing nearby map-seat path. `act chair <setup-slot>` and
  native SETUP double-click use the held chair item; `act stand` or native
  Up/Left/Right/Jump requests cancellation. Local/remote snapshots expose `chair`
  and `chair_asset`; chair events distinguish model state and actual asset draw
  invocation. Portable-chair use has no item-specific server acknowledgement;
  its local model is predictive, while a peer's SHOW_CHAIR confirms the server
  broadcast. Spawn packets retain chairs for players who arrive later. Draw
  events do not prove pixel correctness or original-client compatibility.
- `act item-move <equip|use|setup|etc|cash> <source> <destination> <whole-count>`
  requests a whole-stack move, swap or merge within the tab.
  `act item-drop <tab> <slot> <quantity>` requests a quantity drop. Invalid slots,
  quantities and cash/static-untradable drops reject; protected drops requiring
  confirmation can still use the normal inventory UI. Server validation remains
  authoritative. `act item-split ...` explicitly rejects: Java's v83 move handler
  ignores quantity for moves and cannot split into another slot. No local split
  or invented packet extension is used.
- `act scripted-item <use-slot>` sends SCRIPTED_ITEM for the 243xxxx family;
  `act use-item <use-slot>` rejects that family. Native USE double-click and item
  bindings choose the matching packet through the same player method. The
  server resolves the WZ script name and executes the copied script. The two
  copied item scripts perform effects and dispose without opening dialogue;
  dispatch alone does not establish a script effect or dialogue.

Inventory entries now include `tooltip` name/description/valid/cash/untradable,
`flags`, `owner` and `expiry_raw` (the unconverted 64-bit wire time, including
sentinels). Equipment also includes every received stat, upgrade slots/level and
hammer count. Pet metadata unavailable through these models is explicitly null.
These are model data and shared tooltip inputs, not an assertion that a tooltip
was rendered. Ground-drop quantity stays null because its packet omits count.

State predicates support both dotted and bracket indices, for example
`inventory.use[0].count==20`, and JSON collection equality such as `drops==[]`.
Malformed expressions reject; collection equality does not attempt ordering.


### Skill controls (M164, live acceptance pending)

- `act skill-learn <id>` requests one SP through the normal SpendSp packet;
  job, SP, maximum level and prerequisites are checked. Wait for `skills` and
  `player.sp` to change; dispatch is not a learned-level confirmation.
- `act skill-assign <Maple-keycode> <id>` binds a learned active skill through
  the native keyboard map and ChangeKeyMap packet. Keycodes are Maple codes,
  not X key names. `act skill-use <id>` uses the existing combat path and rejects
  unlearned/passive skills, unavailable player states, cooldowns and insufficient
  known resources. The server remains authoritative for MP and ammunition.
- `act skill-down <id>` / `act skill-up <id>` share native skill-key handling.
  Hurricane, Wind Archer Hurricane and Rapid Fire repeat through the existing
  attack-animation cadence while held. Piercing Arrow and the three Big Bang
  variants release an attack on key-up. The local hold ends after 30 seconds,
  on death or field reset; `act skill-cancel` ends it without a release attack.
  WZ prepare/keydown assets are drawn when available. This does not establish
  original-client timing or visual compatibility.
- `act buff-cancel <id>` requests cancellation of a currently active buff;
  snapshots/icons clear on the server response, not dispatch.

`skills` exports received levels, master levels and raw expiry (512 entries,
with `skills_truncated`); `player` exports AP/SP. `charge` exports the held skill,
elapsed simulation milliseconds and asset availability; remote characters expose
`charge_skill`/`charge_asset`. `summons` is capped at128 records and exports owner,
object/skill/level, position, movement mode and animation/asset state. Spawn,
remove and attack packets are decoded. Automatic attacks currently cover only
first-job Cygnus summons with a WZ radius and attack animation; follow movement is
not implemented (`follow_supported:false`). Other summons have no automated
attack support. Generic foreign buff mask/value updates and cancellations are
exported separately; specialized foreign layouts emit an unsupported observation.
No new compatible-client acceptance is implied by these capabilities.

### Cash Shop controls (M165)

Use `act cash-enter` and `act cash-exit`; exit uses the native button's fade and
channel reconnect path. Wait for `screen==cash_shop` / `screen==in_game` and a
new `navigation.field_entry_seq` to establish the return, rather than dispatch.

- `act cash-buy <currency> <commodity-serial>` buys a single product; currency
  `1` is NX Credit, `2` Maple Points, `4` NX Prepaid. Use a real server commodity
  serial. The sample product list was removed; catalogue browsing/package buying
  is not implemented by these controls.
- `act cash-retrieve <cash-id>` retrieves an entry from the received cash
  inventory. The normal character inventory changes only on the acknowledgement.
- `act cash-gift <YYYYMMDD> <commodity-serial> <character> <message>` sends a
  gift through the server's birthday/recipient/funds checks.
- `act cash-coupon <code>` redeems a code. Codes are ordinary action arguments;
  use disposable codes for automated tests and diagnostic bundles.
- `act cash-name-rules <YYYYMMDD>` and `act cash-world-rules <YYYYMMDD>` request
  eligibility. A readable panel displays the received result/code. Actual
  name registration and world transfer are outside these controls.

`cash_shop` contains received balances with `balances_known`, cash inventory
with `inventory_known`, gifts, `pending`, `result`, `message`, `rules`,
`rules_error`, and a revision counter. Cash IDs and raw expiry are strings to
avoid JSON integer precision loss. Inventory/gift lists are bounded to512 entries;
exceeding the bound fails explicitly. Gift-summary packets do not include
quantity/serial/expiry; their zero defaults are unavailable values, not receipts
for a particular quantity. Gift messages are not exported. `server_reenabled`
means the server sent enable-actions without a reason; it does not diagnose
insufficient funds by itself. Unsupported operation codes emit `cash_unsupported`.
Balances and transaction state reset on login and Cash Shop entry.

Muted disposable live checks confirmed two entries/returns, a real purchase and
retrieval, unchanged state on insufficient funds, one-use coupon refusal on
reuse, gift acknowledgement and name/world result panels. These are fan-client
observations (`X?`). The skin layout still mixes asset generations, and this
panel is not an original-client rules dialog. All headless invocations continue
to force `ALSOFT_DRIVERS=null`; missing/short audio payloads are rejected before
entering the decoder.

### M166 reset actions (partial)

`act ap-reset <cash-slot> <destination> <source>` accepts `str`, `dex`, `int`,
`luk`, `hp`, or `mp`, with an AP Reset (5050000) in that received CASH slot.
`act sp-reset <cash-slot> <destination-skill-id> <source-skill-id>` requires an
SP Reset (505 family other than 5050000). Both reject identical targets, dead
players, movement holds and active dialogs/Cash Shop. The server validates
stat minima, skill eligibility and consumption. The acknowledgement means
packet dispatch, not a successful reset. Watch received inventory and player
stats/skills for the outcome. No local stat/item mutation is performed.
Snapshots now include base `player.str`, `dex`, `int`, and `luk`.

Muted fan observations verified STR→DEX and Power Strike→Slash Blast with
one item consumed each. The dialog continuation below adds native and MP→HP
coverage; the completion entry below records the remaining M166 families.


M166 reset dialog continuation: double-click an AP/SP reset in the Cash inventory,
or run `act reset-open <cash-slot>`. Enter destination then source (`dex str` for
AP; `1001005 1001004` for SP), then Confirm/Return; Cancel/Escape sends nothing.
A readable panel is provided even when Notice6 assets are absent. Invalid input
stays open with an error. The dialog and `ap-reset`/`sp-reset` actions share
validation, including received slot identity, live character, different targets,
and a learned SP source plus available destination metadata. No stats or items
change optimistically. `reset_dialog` exports slot/item/input/error while open,
otherwise null. The server may refuse a dispatched request, including its
three-second cash-use cooldown; observe the notice and received state.
Muted checks observed button-based AP and SP changes/consumption, MP→HP pool
changes, cancellation, local validation and explicit minimum-INT/cooldown refusal.
These are fan-only X? observations. See the completion entry below.


M166 equipment controls: double-click a supported Cash item, or use
`act cash-item-open <cash-slot>`. Enter `equip <positive-bag-slot>` or
`equipped <negative-wire-slot>`, then Confirm/Return; Cancel/Escape sends nothing.
`act cash-item-use <cash-slot> equip|equipped <target-slot>` uses the same validator.
Supported: Item Tag5060000, locks5060001/5061000–5061003, Karma552 and Hammer557.
Tag requires equipped equipment; Hammer requires bag equipment below two uses.
Resealing and repeated Karma are refused locally. Server eligibility and its
three-second cooldown still apply. No target or stack is changed optimistically.
The existing `reset_dialog` snapshot key also describes these shared cash dialogs.
Tooltips display received owner, seal, Karma, expiry and Hammer/upgrade data;
missing tooltip skins use readable fallback panels. Slot/metadata caching refreshes
labels after server updates. Muted fan evidence covers tag/seal/Karma/Hammer,
consumption and refusal (`X?`); Vega/Owl/Maple Life are covered below.


M166 complete (fan evidence only). The shared Cash double-click/`cash-item-open`
panel and `cash-item-use` support these additional inputs:

- Vega5610000/5610001: `<equip-slot> <use-scroll-slot>` (both1–127). The scroll
  must match the equipment category and the spell's10%/60% variant. Example:
  `act cash-item-use 26 1 11`. Immediate consumption and delayed equipment updates
  remain separate. `vega` snapshots and `vega_packet/model/draw` events record the
  stages. An asset-backed progress/result panel waits for the matching equipment
  packet before presenting success/failure. Close hides the panel and does not
  cancel the server operation. An unanswered request blocks another cash use for
  ten seconds; timeout alone never marks a result successful.
- Owl5230000: `<searched-item-id>`, e.g. `act cash-item-use 27 2000000`.
  Received rows expose owner, description, map/channel, bundles, quantity and
  price in `owl`. Native results page four rows at a time (Next/Previous, Return
  advances, Escape closes). Equipment payloads decode into temporary inventories.
  Empty searches leave the Owl unconsumed. Shop warp is later milestone scope.
- Maple Life5431000/5432000: `<name> <face> <hair> <hair-color> <skin> <gender>
  <job-choice> <improve-SP>`, e.g.
  `act cash-item-use 28 LifeTest 20000 30000 0 0 0 0 0`.
  Job choices0–4 are warrior/magician/bowman/thief/pirate; genders0/1. The server
  owns creation, slot limits and consumption. Native response panels and
  `maple_life` snapshots report acknowledgement/refusal. Reconnect to observe the
  actual new character on the list. Card5432000's additional-slot side effect is
  server-owned; the live gate used5431000.

Muted fan checks passed Vega's delayed model/result animation, wrong-scroll
refusal, Owl open/closed/empty/equipment results plus consumption, and Maple Life
name refusal/creation/consumption/list/level30 field entry. These are `X?`, not
original-client compatibility claims. M166's detailed evidence is in porting history.

M167 broadcasts and timed effects use the shared Cash double-click or
`act cash-item-open <cash-slot>` panel. `act cash-item-use <cash-slot> <input>`
uses exactly the same validation and packet builder. Inputs:

- Ordinary megaphone5071, kite508 and weather512: the message.
- Super megaphone5072: `<ear> <message>` (ear0/1 disables/enables the whisper ear).
- Item megaphone5076: `<ear> <inventory-type> <slot> <message>`. Types1–5 are
  Equip/Use/Setup/Etc/Cash; `0 0` means no linked item. Bag slots are1–127.
- Triple megaphone5077: `<ear> <line1>|<line2>|<line3>` (one to three lines).
- Avatar539: `<ear> <line1>|<line2>|<line3>|<line4>`.
- Maple TV5075000–5075005: `<ear> <partner-name-or-> <five lines separated by |>`.
  Some source variants ignore the partner/ear; the packet body follows the
  original handler for each variant. Example: `0 - Hello|Maple|TV|from|me`.

Use1–80 characters per line. The server owns level restrictions, cooldowns,
consumption, TV exclusivity and removal. Dispatch alone is not success.
Broadcast panels show received text, channel/linked item and character portraits;
these are readable fan presentations, not reproductions of the original skins.
Kites are anchored to received x/foothold and use the available item animation.
Weather resolves the Cash item's Map asset path and displays moving particles
plus its received message; absent assets retain the readable message panel.

`broadcasts` snapshots contain up to20 notices and100 kite records, plus avatar,
TV and weather state: owner/message/identity, channel/ear, position/foothold,
portrait and asset presence/path, actual panel draw calls, age and removal time.
TV/avatar/weather/kite visibility follows server removal packets; age alone
never claims their expiry. Ordinary notice overlays last10 seconds locally and
also enter the chat log. Field changes clear map-specific weather/kites; logout
clears all broadcast state. Cached text rows keep layout work out of each frame.


M168 social controls: native chat accepts `/whisper NAME TEXT`, `/find NAME`,
`/buddy NAME TEXT`, `/party TEXT`, `/guild TEXT`, `/alliance TEXT`, and `/spouse
TEXT`. Plain text and GM commands retain general chat. `/helpchat` (or the help
button) lists controls. Bodies are limited to127 characters; the input scrolls
horizontally and rejects oversized requests locally.

Open the social window with `/social` or the chat target button. Its Buddies,
Messenger and Notes tabs have native buttons, recipient and message fields.
- Buddies: add by name, accept/deny invitations, click a received contact,
  send to that contact, or remove it. Previous/Next pages the contact list.
- Messenger: Create, enter a name and Invite; recipients Accept or Deny.
  Received members/portraits and text appear in the window. Send or press Enter
  in the message field; Leave exits the room. Closing the window hides it without
  leaving. `/messenger` reopens it. Offline and denial replies are received state.
- Notes: `/notes` opens received notes and composition. Previous/Next selects an
  individual received note, even when senders repeat. Discard sends that note's
  identity and hides it locally. Sending needs a ticket from receiving a cash
  gift. The server sends no note-delivery acknowledgement; the panel says
  requested, and the recipient receives stored notes on login. Escape closes
  the panel, including when a text field has focus.

`act chat-send '<text or command>'` uses the native chat parser.
`act social-open 'buddy|messenger|notes'` opens the actual social window.
`act social-action '<action> <name-or-> <text>'` uses that window's submission
path. Actions: buddy-add/accept/deny/remove/send, messenger-create/invite/accept/
deny/send/leave, note-send/discard. Use `-` where a name is unnecessary. Discard
uses the currently displayed received note, never a supplied raw ID.

The bounded `social` snapshot exports received buddies, members, notes, latest64
Messenger lines, pending invitation names, request/result status, active tab and
actual draw count. `chat`/`social` events expose received text/outcomes. A dispatch
result confirms a request, not delivery. Logout clears social state.

M168's muted fan checks cover cross-channel whisper/find and all five private
chat families, buddy native add/deny/accept/select/send, Messenger native create/
invite/deny/accept/chat/leave/offline, and gift-ticket note send/login receipt/
read/discard/paging. An unrelated active observer received subsequent public text
without private messages. These are fan observations (X?), not original-client
compatibility or original skin fidelity. See the root plan/history for evidence.


M169 adds `/partyui` (or `act social-open party`). Click a received roster name
before **Kick** or **Leader**. **Accept/Deny** answers the current received
invitation, including Party Search invitations. Enter `1 31` in the Message field
and click **Search** to search levels 1–31 across all jobs; **Stop search** sends
the stop request. The range must include your level and span at most 30 levels.
Search has no server acknowledgement, so the status explicitly says requested.

Semantic controls use the same request path: `party-deny`, `party-kick NAME`,
`party-leader NAME`, `party-search-start '1 31'`, `party-search-stop` (and the
existing create/leave/invite/accept). `social-action 'party-search-start - 1 31'`
requires the actual social window. The social window snapshot includes button
labels/positions, selected target and input options, alongside received party and
invitation state. Leadership transfer currently requires an online member.
M169 Family, guide and quest-incubator controls are described below.

### Family, guide and quest incubators (M169)

`/family` opens the Family tab; Refresh requests current reputation and daily
privilege use. Pedigree requests the named character's received family tree
(blank target means yourself). Click a received member name before Summon or
Reunite. Family invitation and summon packets open this tab with Accept/Deny or
Summon yes/no controls. Summon and Reunite use the server's reputation costs;
other Family privilege effects remain unimplemented on the server.

`/guide` or `/incubator` opens the Guide tab. Guide dialog requests the ordinary
server-selected NPC script. Choose item cycles quest incubators in the received
ETC inventory; Choose food cycles compatible inventory stacks using Item.nx
metadata. Feed one sends the M114 quest-feed request. The panel displays actual
received quest progress and inventory counts; it does not grant rewards locally.
This is separate from the Cash item called Incubator.

The existing `social-open` action accepts `family` and `guide`. With the real
window open, `social-action` supports `family-open`, `family-pedigree NAME`,
`family-invite NAME`, `family-accept`, `family-deny`, `family-summon NAME`,
`family-reunite NAME`, `family-summon-accept`, `family-summon-deny`, `guide-open`,
`incubator-next`, `incubator-food`, and `incubator-feed`. For actions without a
name use `-`, for example `social-action 'family-open -'`. Snapshots export
received Family/privilege/pedigree/invitation state, incubator selection and
progress, and actual window rows, named choices and button coordinates.

### Player shops and hired merchants

`/market` (or `act market-open`) opens the market window. Fill its argument field,
then click an action. Successful native requests clear the field. The directory,
listings, visitors, chat, sale results and wallet come from server packets.
The window exports its actual text and button positions under `market`.

- Create: `permitID description`, using a received Cash store permit or merchant.
- Stock: `inventory slot bundles quantity price` (inventory 1=Equip, 2=Use,
  3=Setup, 4=ETC). Open lists the stock; Take uses a listing index before opening.
- Visit: an owner ID from the received directory. Buy: `listingIndex bundles`.
  Chat takes a message; Ban takes a received visitor name (player-shop owner).
- Owl: an index from received Owl search results; sends the ordinary shop warp.
- Leave exits the room. Close shuts the owner's store; Withdraw, Organize and
  Resume are hired-merchant owner controls. These actions take no arguments.
- Previous/Next page received rows; Hide closes only the panel.

The same controls are available as `act market-action 'action arguments'`.
Requests are reported as dispatched, not confirmed transactions. Server inventory,
meso, room and box responses establish outcomes. Leaving locally closes the room
view because the protocol does not acknowledge every voluntary exit. Login and
field transitions clear market state. Field shops use readable description boxes;
the fan UI does not claim the original client's shop artwork.

M171 service controls (fan presentation): `/newyear` opens the social card panel;
enter a recipient/message and Send using a received Use card2160101. Previous/Next
select server records or delivery notices; Receive claims only the selected
unreceived card. Login records are retained; no card receipt is predicted.
`act social-open newyear` and `act social-action 'newyear-send Name message'` /
`'newyear-receive -'` use the same request path. Card messages are limited to127
characters and login records to1024.

`/mts` or `act mts-enter` requests real MTS entry (server must enable USE_MTS,
level/map/event restrictions apply). The received window supports Browse, Cart,
clicking a received listing, Add/Remove, server page navigation and Exit/Escape
through the actual return-to-field request. `act mts-action 'add 0'` uses a
zero-based received row index; `browse`, `cart`, `remove 0`, `next`, `previous`,
`exit` share native handlers. Snapshot `mts` exports received balances/listings,
actual button positions and listing selectors. Native cart display currently
shows the first16 rows; selling/purchasing/transfer controls are unsupported.
This is a functional browse/cart interface, not the original MTS artwork.

Wedding gift results open the social registry from the server's NPC/script path.
Previous/Next selects a received gift; Claim gift requests its authoritative
registry index and waits for inventory/list refresh. Close requests wedding
window closure. `act social-action 'wedding-claim -'` shares that path. Wedding
controls are available through `/wedding` or `act social-open marriage`. Propose
uses the first received Use ring box2240000–2240015 and the entered partner name;
Accept/Decline answer only a received proposal, and Cancel proposal cancels the
sender's pending request. Replies preserve the entered name. Engagement records
and names are retained from full login as well as live results.

Invite guest uses the received engagement record and first Etc invitation
envelope4031377/4031395; enter a guest name, then wait for server consumption or
refusal. Invitation opens the first received guest ticket4031406/4031407 and shows
the server's couple names; attending still uses the copied Wedding Assistant.
The real assistant script can request a wishlist: enter up to ten names separated
by `|` (each1–100 characters, combined1009), then Send wishlist. This submits without
inventing an acknowledgement; read the result through the copied Gift Registry.

Ceremony packets open a readable gathering/altar/vows/end panel; Bless couple
sends the real wedding-talk request. End packets with visible character IDs play
the real `Effect/BasicEff.img/Wedding` animation on those characters. Close/Escape
closes the presentation; it does not exit the event. Shared semantic actions are
`act social-action 'wedding-propose Name'`, `'wedding-accept -'`,
`'wedding-deny -'`, `'wedding-cancel -'`, `'wedding-invite Name'`,
`'wedding-invitation -'`, `'wedding-wishlist - Shell|Potion'` and
`'wedding-bless -'`. Snapshot `social.wedding` exports received records, wishlist,
ceremony state and effect dispatch counts; native rows/buttons/coordinates are in
`social.window`. A dispatch count alone is not proof of visible rendering.

Wedding photo presentation remains unavailable: upstream `Wedding.OnTakePhoto`
labels its packet v53 and explicitly leaves WvsMapGen work unfinished; the port
has no emitter. No v83 packet format or successful photo is invented. The fan
panels also do not reproduce original wedding artwork or certify a full ceremony
route.

`@rates` / `@showrates` retain server text in `social.rates_hint`. Hints normalize
CRLF and use measured backgrounds, centered to their actual width, with readable
white text. Coupon state comes from the received rate hint and Cash inventory;
no coupon buff icon or client-computed multiplier is invented.


NPC input dialogs now decode v83 text defaults (type2), numeric defaults/bounds
(type3), and style IDs (type7). Their native panel supports keyboard entry,
Enter/Submit, and Cancel/Escape. `act npc-input <value>` uses the same validator;
`act npc-choose close` cancels. Text is limited to255 bytes on one line, numeric
answers must fit the received inclusive range, and style replies are zero-based
indices into received IDs. Invalid input keeps the dialog open. Style IDs are
listed without cosmetic previews; this does not certify original client artwork.
`npc_dialog.input` exports input kind, bounds, styles and field/Submit/Cancel
coordinates. Submission closes the local dialog while awaiting the real server
reply; it does not invent a reward or successful script result.

M172 remains open for complete PQ and expedition routes, including invalid entry
and disconnect recovery. NPC dialog fixture checks alone do not close a route.

PQ controls: `act jump down` uses the normal Down + Jump movement path and refuses unsupported footholds. `act jump up` taps Jump while holding Up for one second to catch a hanging rope through normal collision checks; the result reports input release, so check `player.climbing` and position separately. `act jump left` / `act jump right` hold the direction for one second and tap Jump after a 150 ms run-up; they require a grounded player who is not attacking. Start with room to accelerate before a ledge. The result reports input release, not a successful landing. Active moves must finish before a jump. Direction holds and pending jump taps are canceled on cancellation, death or field changes. Native held Up also checks for ropes while airborne, without relying on OS key repeats. Reactor snapshots include current asset `event_type` and `hittable`; linked reactor assets retain their original script IDs. These observations do not certify a complete PQ route.

`act map-geometry` exports the active field's loaded collision data once in
`dispatch.geometry`: map ID, field-entry sequence, sorted footholds (ID, layer,
previous/next IDs and both endpoints), and climbables (ladder/rope flag, x and
y endpoints). It refuses arguments and requests outside a field. This is asset
geometry, not a route solver or a claim that every span is reachable. Match its
map and field-entry sequence to the current snapshot before reusing a result.
Geometry is omitted from recurring snapshots to avoid repeated large exports.
