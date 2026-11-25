# MK9 Access: notes

Screen reader mod for MK9 by Lauta (fan PC recomp of Mortal Kombat Komplete Edition, Xbox 360, ReXGlue).

## Layout and commands
- Game: `..\game` (port auto-updates; build of 2026-11-16 05:43 at last check). Settings: `user_language = "1"`,
  `mk9_online_language = "en"`.
- Build: `sh build.sh` (64-bit w64devkit, `-Wall -Wextra`, builds clean) -> out/plugins/mk9access.dll (the port code
  plugin) + out/MK9Access/MK9Access.dll (the mod). Deploy: `sh deploy.sh` (copies plugins\mk9access.dll + MK9Access\,
  removes any old version.dll, writes MK9 Access launch.cmd, ensures mk9_plugins = true). With `MK9Access\devreload.txt`
  present the plugin hot-swaps a rebuilt MK9Access.dll: just copy it in.
- Release: `python package.py` (after build.sh) -> Downloads\MK9Access-<MOD_VERSION>.zip, unzipped next to mk9.exe
  (README in docs/, launch.cmd + setup.ps1 + mk9update.ps1 in dist/). 1.0.0 built 2026-11-22; 1.0.1 (first-install port download, English once) 2026-11-22.
- Tools: `tools/mk9.py` (launch, keys, click, screenshots to `$MK9_SHOTS` or `%TEMP%\mk9shots`, guest reads),
  `tools/cmd.py` (send RE commands to the mod, read `counts.txt` / log lines), `tools/snap.py` (memory snapshots,
  `track()`), `tools/ue3unpack.exe`, `tools/movelists.py`.
- RE commands (`MK9Access\cmd.txt`, see guest.c): `count arm|reset|dump`, `trace ADDR N` (logs args and the return
  value), `watch`, `poke`, `pad`, `imdump`, `menudebug on|off` (logs every Localize, Resolve, SetText, Invoke, ClipEvent).
- Keyboard in game: arrows/WASD move, J select, K back, U I J K = FP BP FK BK, Space/L block, O throw, ; flip stance,
  P tag, Enter start/pause. Port keys: F1 online, F2 options, F3, F7, F11; in Practice also F5/F6/F7 (1.4.2 opponent recording). Mod keys: F5 read port screen (not in Practice), F10 frame data (Shift: after every attack), F12 repeat, F6 health (not in Practice), F4 Krypt koffins in view, F8 hum. No opponent-position key in fights (no time to
  hear it; the hum carries it - user rule for every fighting game mod, 2026-11-18).
  F9 is the PORT's docked/handheld render switch (bind_sa2_docked, sa2_render_mode.cpp): never use it; pressing it
  mid-game was followed by guest crashes on the next Krypt load (2026-11-18; the mod used F9 until then).
- The main menu returns to the title and then an attract demo after about a minute idle: bots must move fast.

## Plugin loader (replaced version.dll), and Defender
- The mod is a ReXGlue **code plugin** since v1.3.0 (Lauta shipped the loader for issue machinebreak/mk9-lauta#8).
  `game\plugins\mk9access.dll` (from `src/plugin.c`) exports `rex_mod_init(const RexModHost*)` + `rex_mod_shutdown()`
  (rex_mod.h, api_version 1, same ABI as [[dbz3-access]]). Needs `mk9_plugins = true` in mk9.toml (deploy.sh sets it).
  The port calls rex_mod_init in `OnPostLaunchModule` (game image loaded, before its first instruction); the plugin
  stores the host and starts MK9Access.dll (hot-reload via devreload.txt kept). The mod uses `host->guest_base`
  (guest_ready prefers it, falls back to the 4 GB scan). Verified 2026-11-16 on port 1.3.0: plugin loaded, host
  api 1 / region eu / guest_base 0x100000000, all 5 hooks enabled, speech + menus working, title read.
- No more proxy version.dll, so the Bearfoos.B!ml proxy-DLL false positive surface is gone. plugin.rc still carries an
  honest VERSIONINFO as cheap insurance. Never packing or obfuscation.
- Saves and lightest graphics can't be set from the plugin (it loads after the port reads config): they moved to the
  launch step (see Saves and Lightest graphics below).
- Updating the port: the mod does it (see "Port updates" below). The port's in-game banner is OFF
  (`mk9_update_check = false`, set by deploy.sh): mouse-only, and its installer failed here (in-use DLLs).
  By hand, with the game CLOSED: `powershell -File game\MK9Access\mk9update.ps1` (no -WaitPid = install now).
  The same script updates the mod itself from its GitHub releases (see "Port updates").

## How the game's menus are read (menu.c)
Game functions (guest addresses, unchanged by port updates because the 360 code is fixed):
- `82a235b0` Localize(section, key, default) -> char * English text. Every menu text passes here.
- `82a399a0` Resolve(holder, parent holder, path): points a Scaleform value holder at a clip, e.g. `option3.text.varText`.
- `82a38648` SetText(holder, text).
- `82a38050` Invoke(holder, "clip.method", printf format, args): args are script values {type, value} at r6
  (value at +4; arrays of 8-byte values for several args). Highlights: `option3.gotoAndPlay("BuildOn")`,
  `slider4.gotoAndPlay("HighlightOn")`, slider value `slider4.slider.gotoAndStop(%d)`, move list `UpdateHighlight(%d)`.
- `82a37f08` ClipEvent(holder, name): `FocusGain` / `FocusLost` on a menu item clip (pause menu).
- Item records in script data: {label key*, desc key*, id, 0, 0}, stride 0x14 (main menu and its submenus).

Readers, in order of use:
1. Lists with descriptions (main menu, Fight, Options): on a move the script localizes the new item's desc key;
   the record holding it gives the label key. Text from Coalesced.eng (the port's patched copy in
   `game\cache\localization`, else `game_root\Localization`) or from what Localize returned earlier.
   A burst with 2+ label lookups = a list was built: push a menu stack (title = item that opened it), or pop back.
2. Lone Localize right after the player's input (character select names). Grid cell from
   `this._parent.HighlightN.Surround.gotoAndPlay("GainFocus")`, N 1-based, 4 columns.
3. Flash clip map: text per (parent holder, path) from Resolve + SetText; spoken on BuildOn / HighlightOn /
   FocusGain; list title = a text whose path contains "title"; sliders add their value.
4. Message boxes: a body key (`*Body`, `*Message`, sections with `Msg`) plus [Options] YES/NO -> "body", "Yes or No".
5. Move list (pause > Move List; J switches main / Super Moves page, K closes): the open burst localizes all
   [movelist] rows (heading, blank or a "(Uses three bars of super)" note, name/input pairs).
   Row in focus (2026-11-20 rewrite): every move sends Invoke `ScrollBarUpdate(%d)` = round(100 * row index /
   row count) (Split Decision 44/54 -> 81, Babality 52/54 -> 96, Front Punch 2/64 -> 3); the nearest move name
   to that point is the row. Replaces counting `UpdateHighlight(N)` (window slot 0..16, repeat at the edge =
   scrolled), which ran past the end on extra Downs at the bottom and lost its place on J: the game keeps the
   scroll point across pages (Back Kick on the main page -> Takedown on Super Moves). At the ends nothing scrolls,
   no ScrollBarUpdate, the last row is said again. Page open / J sends UpdateHighlight 0 then another in one
   frame: the row waits 100 ms and the new page's burst cancels it (no cut-off old row before the title).
   Finishing moves: the range tag (Touching/Close/Sweep/Jump/Far/Varies/Anywhere/Mid) is a SetText just before the
   highlight; kept per fighter+page and said as ". Distance: jump". Headings followed by a note (X-RAY ATTACK,
   ENHANCED ATTACKS) count as headings and say the note. Closing the list sends no event (pause menu not re-said).
6. Test Your Luck (Fight menu), `slot_event`: the result is decided BEFORE the spin: SetText
   slot.textAnim.text<N>.conditionText.text per reel (1 = opponent), arrowLeft/arrowRight On|Off per reel (set just
   before "slot <N>reels"; left = player 1 = "You", right = "Your opponent", both = "Both fighters" - reading of the
   arrows, not proven in play), intro, "" BuildOn = Spin button up -> "Test Your Luck. Slot machine with 4 reels ...
   Press Select to spin.", slot.triggerButton trigger -> "Spinning", slot.reels.reelAnim_<N> stopScroll ~2.5 s apart ->
   "<rule>: <description>. <who>." queued. Descriptions: [TowerIcons] SLOT_MACHINE_<kind>_MODIFIER_DESCRIPTION_<n>
   (looked up by the rule's name). The [TowerMission] SLOT_MACHINE_* burst at slot setup is NOT said (it spoiled the
   spin); other slot lines (gift messages) are.
7. Challenge Tower texts ([TowerMission]/[TowerIcons]) are said only in tower mode (`g_tower_session`: set by tower
   card events, cleared when the main menu list is built = [MainMenu] ChallengeTower; NOT on [MainMenu] LOADING,
   which every challenge load uses). Every fight load looks up a [TowerIcons] badge name (Story: DAMAGE_BOOST_NAME):
   that was the user's "Blocking Disabled / Confused" after ladder wins. In tower mode the title banner
   (UpperCenterTextAnim) is skipped: the title + objective lookups follow 50 ms later. The minigame lists (Test Your
   Might/Sight/Strike) use the tower card reader; a card whose type equals its title no longer says it twice.
   practiceHUD gotoAndPlay Success / Failure = the result, said. Lone [TowerMission] CHALLENGE_NAME is dropped.
   Speech cut-offs (user 2026-11-20): the loading screen's objective + conditions ([TowerMission] *_PAUSE_DETAILS +
   *_ADDITIONAL_DETAILS), then 0.5 s later the condition icon name alone ([TowerIcons] GROUND_POUND_NAME, which looked
   like the tower menu because the card is re-read during loading -> now icons alone never count as menu), then the
   narrator lines, each interrupted the last. The tower reader now queues behind a message still being spoken
   (~55 ms a character) unless a key was pressed since (Continue skips a narrator line). The dialogue box's name field
   (LeftTextAnim / RightTextAnim BuildOn, a bare "Jax" 30 ms before "Jax. <line>") is skipped in tower mode.
   say() drops doubled marks ("Tarkatans..", "Ambushed!."), keeps "...". Verified on challenge 9 "Ambushed!"
   (test saves; challenges 1-9 skipped there with test koins). Message boxes built from pieces around a number
   ([MsgBox] ConfirmSkipMissionStart/End) take the whole sentence from the SetText just before ("The cost to skip
   is 150 koins. Are you sure ...").
8. Ladder screen (before each Arcade Ladder fight): burst [PlayerSelect] player + 10 opponents in order, [Ladder]
   RoundsLost etc.; SetText ladderStats.statN value comes just before labelN ("ROUNDS LOST:"); "ladder_main"
   gotoAndPlay stage<N> = fight N (before fight 1 it comes after the burst: default 1). Said "Ladder, fight 2 of 10:
   Johnny Cage. Rounds lost 0, matches lost 0."
9. Minigames (src/minigame.c, see its header for all addresses and cue design): Test Your Might / Strike meter
   (meter.fill frame vs meter.pointPosition / pointCell target on the same scale; Might won at 161 and 231 with
   target 157, lost at 75) -> slot-3 tone 220 Hz empty / 440 at target / up to 880, 440 Hz pip every 0.8 s, Might:
   tremolo + ding above the marker; Strike: "Charged" on fill_loop.fill ready (its band glow is always "idle").
   [TestYour*] Material* -> "Break: Pine Boards". Test Your Sight: heads = class 0x820b4148 actors (Location +0xDC)
   at height 100.0 that lift >104 after Begin, eyeball = class 0x8249a188 at the start side of the right head,
   spotlight = class 0x82152140 made at the pick; heap scan 0x40000000-0x50000000 (16-byte stride, committed pages).
   Says "The eye is under the middle head. Follow its sound.", slot-4 tone panned with that head, then "Spotlight on
   the middle" / "left" / "right". Verified heads table, challenge 1 only (others locked on the test saves; cups /
   skulls tables unverified).

## Fights (fight.c, audio.c)
Game data from Lauta's notes (mk9-lauta `docs/online/engine-map.md`, `app/src/online/fight_state.cpp`), checked live:
- match info `0x833C6C54`, per player block `0xC8C`: `+0xB6C` "CHAR_SCORPION", `+0xDF4` fighter object.
- fighter object `+0x78C0` health float (1 full); `+8` actor: `+0xE0` position along the fight (grows rightwards),
  `+0xE4` height. Camera caps the gap at 600; standing in contact is about 89.
- game state `[0x833848D4]+4` (8 fighting, 3 main menu, 7 character select, 9 game over, 11 Finish Him + fatality, 12 ladder).
- paused: u32 `0x833DD100` = 1 while the pause menu is up (found by toggling pause).
Cues (spatial-audio-kit rules; SC2 Access synth copied, plus tremolo and a clean shutdown for hot reload):
- hum slot 0, W_SOFT: 110 Hz far to 330 Hz touching (exponential), gain 0.035..0.10, equal-power pan 0.8 to the
  opponent's side (full side unless overlapping), 7 Hz tremolo while the opponent is in the air. Silent when not
  fighting, paused, or the port overlay is up. Measured from the mod's own mix (`audiorec`): far -40 dBFS RMS,
  contact -27 dBFS RMS / -22 peak, -25.7 LUFS, 0 % in the 1-4 kHz speech band.
- health tones at 50 % and 25 %: mine two falling notes centred, the opponent's two rising notes from their side;
  peak -13.8 dBFS. Health refills (Practice, new round) make no sound.
- keys: F6 health ("Scorpion 72 percent. Sub-Zero 40 percent."), (the F9/F4 opponent-side key was removed 2026-11-18: no time to hear speech mid-fight), metres, airborne,
  F8 hum on/off, Shift+F8 health tones on/off. Round winner spoken from `[Gameplay.Fighting] Wins` ("Noob wins",
  checked in a Ladder fight). The game state stays 8 between rounds, so the hum continues through the round reset.
- Round winner as a player (forum request 2026-11-23, fight_winner_name): online 1-on-1 sessions say the username
  ("BlindChallengerX wins": own name from --mk9_online_name on the session's command line, else online_name.txt;
  rival from the session corner), local 2 Players > Versus Mode says "Player 1 wins" / "Player 2 wins"; against the
  CPU (Ladder, Team Ladder), watching, replays, King of the Hill and an even double KO keep the character. Winner =
  more health left when WINS is drawn (KO, time-out, after Finish Him). Local Versus = main menu focus Versus2Mode,
  kept through its One VS One / Two VS Two items. Tested 2026-11-23 on test saves: Scorpion mirror match with the
  port's second keyboard player (--mnk_second_player=true; NumLock is off here, so the bot sends numpad keys as
  VK_NUMPAD codes, not scancodes): "Player 2 wins", "Player 1 wins", "Player 1 wins" after Finish Him; Ladder still
  "Scorpion wins". Not tested in a real online match (needs a second player).
- RE command `audiorec SECONDS` writes MK9Access\audiorec.wav; analyse with spatial-audio-kit\analyze.py.

### Distance clicks, fight silence, PC-layout key filter (2026-11-25, forum feedback; deployed, not released)
- Forum (an old MK9 player): wanted the extra cues and the "constant humming" switchable, and no screen reader in fights.
  User's call: the distance cue becomes clicks that speed up as the opponent nears, panned to their side; no speech in fights.
- Clicks (audio.c click_set, timed in the mix thread so the rhythm stays even): damped 700 Hz body + 2.76x mode + 1 ms
  noise tap, 50 ms. Interval 450 ms (d 600) to 85 ms (contact 89), geometric in k; gain (0.075 + 0.035k) x volume;
  pan as the hum; opponent airborne (z > 40 on, < 25 off) = 935 Hz, a fourth up. Measured in a Practice fight with
  audiorec: 2.2/s far, 11.8/s contact, peak -21.5 to -18.2 dBFS, -36 LUFS, 5 % in 1-4 kHz, right-left +9.5 dB.
  Airborne pitch not heard in game (the dummy never jumped).
- F8 cycles clicks / hum / off, Ctrl+F8 volume 1-4 (x0.5, 0.71, 1, 1.41), Shift+F8 health tones, Ctrl+Shift+F8
  speech during fights (off by default). Saved in MK9Access\settings.txt (not in the zip, so updates keep it).
- Fight silence: speech.c drops every speak() while fight_quiet() (both fighters in play, not paused, no port window,
  no challenge-result hold) and logs it as "QUIET (fight)". speech_force() lets a thread through: the key loop
  (every mod key), Challenge Tower objectives/narrator/move to land, Test Your Might material, fatality names. The
  challenge result now holds the cues before it speaks. Verified: Practice "Scorpion. Sub-Zero" card held back,
  F8/Ctrl+F8 spoken mid-fight, pause menu read.
- PC layout (Shift+F12): SDL in the port folds both Shifts into kShift and both Ctrls into kControl
  (sdk/src/ui/sdl_virtual_key.cpp), so Right Shift flipped stance and Right Ctrl threw; Enter/Backspace kicked through
  the A/B binds. keys.c hooks WH_GETMESSAGE on the game window's thread (keys_poll takes the biggest visible window
  of the process) and turns those WM_KEYDOWNs into WM_NULL: right Shift (scan 0x36) and right Ctrl (extended bit)
  always, Enter/Backspace only while fight_live(). Key-ups always pass. Port layout untouched. Verified by bot in
  Practice: right Ctrl/Shift, Enter, Backspace at contact did nothing (no damage, "key N dropped" in the log), left
  Ctrl threw (12 %), Escape paused and Enter resumed.
- The title's attract demo is a fight (state 8, both fighters): cues play and speech is held during it, as the hum
  did before. "Press Start" came after it ended in the test.

### Attack height sounds (2026-11-25, user request "like MK1"; deployed, not released)
- Contact = the attacker's damage dealt (fighter object +0x3014, float) rises: at every hit and every blocked hit
  (MK9 chips on block, even normals), never on a whiff. A new round starts it from 0 again.
- Height = the touched fighter's +0x7D84 (u32), set at contact, projectiles included. It is kind*2 + a flag bit:
  kind 0 high, 1 low, 2 overhead, 3 throw, 4 unblockable, 5 mid. The attacker's own copy (+0x794C, current-attack
  block at +0x7920.. with the attack table pointer at +0x7934) misses projectiles, so the defender's is used.
- Hit or blocked = the touched fighter's kind (+0x1D4 bits 8-15) a few ms after the damage rose: 0x0A standing block,
  0x09 crouching block, 0x4F crouching block reaction = blocked; 0x46/0x47/0x42/0x62 = landed. A standing block shows
  0x46 for an instant mid-frame, so fight.c reads it again 4 ms later (6 more if it changed).
- Ground truth from Practice, Scorpion vs Sub-Zero, AI Options > Block Mode Always (accept with J; K backs out and
  drops the change), Movement Mode Stand / Duck (tools/hitlevel.py, data in work/hitlevel): high 0/1 = 1, 2, d2, Spear,
  Teleport (blocked standing, whiff over the ducking dummy); low 2/3 = d3, d4, b4 Sweep, f4 Shin Blast, Takedown
  (through the standing block); overhead 5 = b2 Blade Overhead, jump punch (through the crouching block), 4 =
  Takedown's slam after its grab; mid 0xB = 3, 4, d1, jump kick, string follow-ups; 6 = throw; 8 = Demon Fire, went
  through both blocks. In MK9 a jab blocks crouching at point blank: the level is the move's label, not a promise.
- Sounds data/sounds/height_<high|mid|low|over>_<hit|block>.wav from tools/gen_height_sounds.py: wooden mallet,
  low D4 / mid A4 / high E5 (fifths), overhead a falling sweep D6->D5 in 60 ms; landed bright 0.22 s, blocked
  low-passed 0.12 s and 5 dB softer; equal loudness across heights. My attacks pan to the opponent's side, theirs on
  me sit in the middle. Rate limit 70 ms per attacker. No sound for throws, unblockables and unknown levels (logged
  "fight: me/opponent <height> landed|blocked (level N, kind K)").
- Keys: Alt+F8 volume 1-4 then off (x0.5/0.71/1/1.41, default 2; settings.txt height_sounds=N), plays a mid knock
  at the new level after the words; Ctrl+Alt+F8 learn sequence (name, landed, blocked for each height).
- Measured in a Ladder fight with audiorec at volume 2: knocks peak about -12 dBFS (-10.6 to -13), clicks -18 to
  -19 at contact. Detection to sound: one 16 ms poll + 4 ms + the 60 ms output buffer at most.
- Verified by bot on test saves: every probe above logged its right height and hit/block in Practice (stand,
  duck, no block, strings), the CPU's attacks on me in a Medium Ladder fight (its low blocked by a crouch block,
  overheads landing), keys. Not heard by the user yet.

### Nightly report fixes + description options (2026-11-25; deployed, not released)
From the nightly mod check of 2026-11-25 (Discord/forum, most reports were on 1.0.6 and already fixed in 1.0.8/1.0.9):
- Fatality Tutorial replayed the description on a later press (Liam): the tutorial re-sends BOTH fatalityHUD displays
  (command + yourMove) on every press (menudebug: SETTEXT command |[Towards][Down][Towards][BP]| each time), so once
  the 8 s guard ran out a press re-matched the full input. It also played when the input was right but the distance
  wrong (gap 253, no fatality). Now the tutorial uses the game's Fatality<N>CinemaPostLoadCallback like matches and
  the tower (it fires in the tutorial ~1.4 s after the display completes, only when the fatality really starts);
  fatality_input_settext only notes "tutorial on" (keeps the motion fallback out). Verified: far = silent, close =
  name + clip once, 4 presses after = nothing.
- "Fatality descriptions don't always trigger" (Liam): a fatality's own callback name is now kept in its own slot
  (g_cb_fat), so another *CinemaPostLoadCallback looked up in the same 16 ms poll can't overwrite it. No miss was
  reproduced; Johnny Cage's second fatality and Kratos have no rows (unchanged).
- Fatality Tutorial intro card (practiceHUD.messageBox) is read: [TutorialMissions] <NAME> + <NAME>_MSG, then
  FATALITY_LEFT_LABEL ([X]) / FATALITY_RIGHT_LABEL ([A]) -> "Scorpion. <tagline>. Start with Normal Timer: U, X
  button. Start with Infinite Timer: J, A button." (keys from keys_for_button, so the PC layout says its keys).
- Descriptions (user request + Liam "story dialogue much quieter than the descriptions"): Alt+F12 cycles recorded
  voice / screen reader / off for story AND fatality descriptions; Alt+Shift+F12 = voice volume 1-5 (gains 0.25 0.35
  0.5 0.71 1.0), default 3. Measured: clips about -18 LUFS, movie English tracks -23 to -28 LUFS, so the old full
  level sat 5-10 dB over the dialogue; default is now -6 dB (fatality clips too). settings.txt descriptions=,
  description_volume=. Screen reader mode queues the text (speak interrupt 0, forced past the fight silence) at the
  cue time; skipping a movie stops it. Text comes from cues.tsv column 4 (gen_story.py --index-only) and index.tsv
  column 6 (gen_fatalities.py --index-only). desc_play() in story.c; settings in fight.c. Verified in Story chapter 1
  (mk001_002_003): screen reader cues at 3.2/21.6/26.1/30.6 s, off at 35.2, voice from 39.3; keys + settings saved.
  Not heard by the user; the fatality screen-reader path was not run in game (same desc_play).
- Not done (port/other): Krypt crash (port, #19), DarkLightning's "image not supported" (needs Komplete Edition EU),
  an ISO how-to write-up (SightlessKombat), port 1.4.4 compatibility check (user folder still on 1.4.2).

## Saves in the game folder (Controlled Folder Access)
Windows Security's Controlled Folder Access blocks mk9.exe writing to `OneDrive\Documents` (Defender event 1123);
the port throws (E06D7363 on the save thread) and the game closes on its first save. The user chose (2026-11-16)
to save in the game folder: `user_data_root = "C:/Users/michm/Downloads/MK9 Access/game/saves"` in game\mk9.toml.
The port sets up folders before reading mk9.toml (main: cvar::Init, ApplyEnvironment, SetupEnvironment, LoadConfig;
mk9_installer.h confirms the toml reaches the paths only from the command line), and a plugin loads far too late to
help. So the **launch** sets it: `game\MK9 Access launch.cmd` starts mk9.exe with `--user_data_root="%~dp0saves"`
(the command line wins), and the Arcade Cabinet entry points at that .cmd. tools/mk9.py passes the same flag for
tests. Verified earlier: the save that crashed twice (Practice > Player Select, MK9PSET) lands in game\saves; shader
cache stays in game\cache.
2026-11-20: the user started mk9.exe without the flag, and opening the Challenge Tower (first MK9PSET write) crashed
the game under CFA (looked like "Defender closed the game"). Now the plugin covers every way of starting the game:
`relaunch_for_saves` in src/plugin.c runs first in rex_mod_init. If the command line has no `--user_data_root`
and mk9.toml has an uncommented `user_data_root = "..."`, it starts mk9.exe again with the same arguments plus
`--user_data_root="<toml value>"` and TerminateProcess-es itself (before the guest runs, nothing saved yet; the
bare copy lives ~2.5 s and its plugin log line is lost). Env MK9ACCESS_RELAUNCHED=1 stops a loop. With no toml line
it does nothing (saves in Documents on purpose). Verified: bare mk9.exe -> relaunched with the flag -> Challenge
Tower opened, MK9PSET written to game\saves, game alive, no 1123 event.

## Lightest graphics (persisted in mk9.toml)
The player is blind: smooth play and low input lag matter, the picture does not. The port auto-detects a
quality preset from the PC's hardware on first run (`mk9_hardware.cpp`), which on a strong GPU picks a heavy
one. The deployed mk9.toml already pins the port author's lightest preset, **"Muy baja"** (`options_ui.cpp` `Presets()`):
`renut_resolution=480p`, `renut_shadow_resolution=1x`, `renut_ambient_occlusion=Half`,
`renut_anisotropic_filtering=Game`, `renut_post_process_precision=Game`, `mk9_draw_mode=auto`,
`mk9_dynamic_resolution=false`. It also sets `mk9_preset_detected="Muy baja"` and `mk9_first_run_done=true`,
because the hardware auto-detect overrides the individual cvars unless `mk9_preset_detected` is non-empty
(`mk9_hardware.cpp:287`).
- These values plus `mk9_preset_detected = "Muy baja"` and `mk9_first_run_done = true` are written in the deployed
  mk9.toml (the old version.dll loader wrote them once; now they just live in the toml, which the updater keeps as a
  Kept file). `mk9_preset_detected` must be non-empty or the hardware auto-detect overrides the individual cvars
  (`mk9_hardware.cpp:287`). For a fresh install, put these lines in mk9.toml (or set them in the launcher); the plugin
  can't, it loads after config. `MK9Access\gfx_lightest.applied` is a leftover marker from the old loader.
- The player's own later F2 changes are kept (they just edit the toml). The renderer picks the dedicated GPU itself (`renut_gpu_adapter`
  default = most-VRAM = the 2070 Super), so no Windows GpuPreference registry change is needed; the log line
  `Renderer: D3D12 on NVIDIA GeForce RTX 2070 Super` confirms it.
- Verified 2026-11-16: from a heavy Alta/1080p toml with no marker, launch rendered at 853x480 (`Render
  resolution ... (0.667x)`) and the toml became Muy baja; with the marker present, a user Alta/1080p toml was
  left alone (rendered 1920x1080). Build clean, mod still loads and speaks.

## System dialogs (sysdialog.c)
The runtime's `XamShowMessageBoxUI` dialogs (e.g. "Profile Selection. You are not signed in... Sign in /
Continue without saving", game text `XenonSaveMsgs` `ContinueWithoutGamerProfile`) are drawn by rexruntimerd.dll's
own ImGui: mouse only, and imgui.c only reads the port's (mk9.exe) ImGui. The game waits on them, so to a blind
player the game looked frozen (2026-11-16, entering Practice).
- mk9.exe's import `__imp__XamShowMessageBoxUI` is wrapped (as pad.c wraps XamInputGetState): logs
  `sysdialog: user N asked "<title>": "<text>"` plus the buttons, speaks title, text, "<button>, 1 of N" and the keys.
- Left/Right or Up/Down (keys, D-pad, stick via XInput) choose; A, Enter, Space or J confirm. The runtime's exported
  `ImGui::ButtonEx` is hooked and returns true for the chosen label, so the dialog closes through its own path.
- menu.c's message-box reader stays quiet while one is up (`sysdialog_recent`), or it cut the choices off.
- For the profile question the mod also says what each answer does: the SDK's `XamShowSigninUI` shows nothing and
  sends the game back to the main menu; "Continue without saving" goes on but that player may not save.
- The mod never answers by itself: which answer is safe depends on which player was asked.
- Tested: forced with `--mk9_profile_every_controller=false --mk9_profile_prompt_auto=false` and a scripted pad 2
  (`--mk9_input_script=32 BACK+Y 0.3;35 START;43 A;47 P2:RIGHT;49 P2:A;52 P2:A`): spoken in full, Right + Enter
  answered, game went on to character select. Real XInput controller presses not tested (no physical pad here).

### Open: why the port asked at all (2026-11-16)
The port signs every controller into the PC profile (`xam_signin_every_user`, log `[profile] every controller signed
in ... yes`) and its `sub_829D62E8` hook auto-answers players 2-4, so this question should never appear. In the
user's session it did, entering Training > Practice; the profile manager then read state 1, request
`[0x834261A0]` = 16 (0 in every clean run), user 0 flags +3152=1 +3156=0. Not reproduced with: keyboard, scripted
pad 1, idle return to the title, Challenge Tower. Not caused by the graphics change. The next occurrence logs the
asked user in MK9Access.log (`sysdialog: user N`); then make the mod skip it for that case.

## Port 1.4.0 + self-updater (2026-11-17)
- Updated the port 1.3.0 -> 1.4.0 (King of the Hill, 2v2, frame data in Practice, localization). The mod loads
  and works unchanged on 1.4.0 (pattern/GFx hooks survived): plugin + 5 hooks, menus, movelist, Practice/AI options,
  Challenge Tower all verified. mk9.exe 134033920 -> 134349824 bytes.
- (Superseded 2026-11-18, see "Port updates".) NB on 1.4.x the main menu is advanced from "Press Start" with the
  SELECT button (J / A), not Start/Enter.

## Port updates (2026-11-18)
- Why it changed: the first updater ran only from "MK9 Access launch.cmd"; the user started mk9.exe directly, so it
  never ran, and the port's banner they got instead is mouse-only (NoFocusOnAppearing) and fails to install here.
- Now src/update.c starts `MK9Access\mk9update.ps1` hidden at EVERY game start (CreateProcess powershell,
  CREATE_NO_WINDOW, breakaway from job if allowed) with `-GameDir <game> -WaitPid <pid> -Installed <host port_version>`.
  The script: GitHub latest (generic UA) -> if newer, downloads + unpacks to MK9Access\pending\<ver>\ while the game
  runs -> `update_status.txt` = "ready X" (mod: "Game update X is ready. It installs when you close the game.") ->
  waits for every mk9.exe from the folder to exit -> copies (mk9.exe first, backup + restore on failure, keeps
  mk9.toml) -> "applied X" (mod at next start, after "MK9 Access loaded": "Game updated to version X.") or "failed X".
  One instance at a time (MK9Access\update.lock). Log: MK9Access\update.log (one line per check). MK9A_NOUPDATE=1 skips.
- First install (1.0.1): the launch .cmd runs mk9update.ps1 in its own console when the folder has no mk9.exe (mod
  zip unzipped into an empty folder): Installed = 0.0.0, progress lines printed for the screen reader, the port's
  mk9.toml copied (kept only when one exists), no update_status. Failure prints the reason, pauses, exits 1.
  Tested 2026-11-22 in a scratch folder: 1.4.2 downloaded (74 MB, ~5 s) and installed, setup wrote English etc.;
  404 path tested with a bad repo name.
- Mod self-update (2026-11-23, for 1.0.6): the same script also checks BlindChallengerX/MK9Access releases/latest
  (asset `MK9Access-*.zip`, marker `plugins\mk9access.dll`) against `-ModVersion` (update.c passes MOD_VERSION; by
  hand it reads plugins\mk9access.dll's ProductVersion - build.sh fails if plugin.rc and MOD_VERSION differ).
  Pending zips in `MK9Access\pending\port|mod\<ver>`; mod status in `MK9Access\mod_update_status.txt`, same
  words; speech "MK9 Access update X is ready. It installs when you close the game." / "MK9 Access updated to
  version X." On close the port installs first, then the mod (each with its own backup/restore); the new zip
  replaces mk9update.ps1 itself (safe: PowerShell has read the script). First install (no mk9.exe) skips the mod.
  Releases must keep the zip asset name `MK9Access-<ver>.zip` and tag `v<ver>`. 1.0.5 and older only update the
  port, so players move to 1.0.6 by hand once. Test without publishing: MK9A_MOD_FEED = local JSON shaped like
  releases/latest with a local zip path. Verified 2026-11-23: scratch folder 1.0.4 -> real 1.0.5 (798 files,
  mk9.toml + user file kept), current check, first-install port download; in game with a local 1.0.6 feed:
  "ready" spoken while playing, installed 6 s after close, "MK9 Access updated to 1.0.6" logged at next start.
  Released as 1.0.6 (2026-11-23); verified from GitHub: a 1.0.5 folder pulled 1.0.6 (798 files). Known: run by hand,
  the port version is unknown (1.4.x logs no "this program X.Y.Z", mk9.exe has no VERSIONINFO), so a hand run only
  updates the mod; in-game runs get -Installed from the host.
- The launch .cmd no longer runs the updater (it was a silent 30 s wait); it only passes --user_data_root.
- Verified 2026-11-18 on the user's folder: bare mk9.exe start on 1.4.0 -> download 3 s, "ready 1.4.1" spoken; game
  closed -> 53 files installed 2 s later, mk9.toml kept; relaunch -> port 1.4.1, 5 hooks, "Game updated to version
  1.4.1", menus read. No Defender detection.
- The port's Online window opens by itself at every start until a username is saved (game\online_name.txt) and
  takes the keyboard (keys go to its text box). The mod now adds "Type a username and press Enter to save it, or press
  F1 to hide this window and play." (after "MK9 Access loaded", and whenever it reopens) and "Online window hidden. F1
  shows it again." (imgui.c im_name_prompt_open). Verified by bot.
- The bread-and-butter combo reference ships in the package: deploy.sh copies docs/"MK9 BnB Combos.html" to the game root.

## Krypt - started (2026-11-18)
The Krypt (Extras > Krypt) is a 3D graveyard; moving (stick / arrows) carries the camera from koffin to koffin.
The 2026-11-17 note that it fires no GFx events was wrong (that pass never locked onto a koffin). What it sends:
- Lock-on: `navAnim` gotoAndPlay `On_2LockedIn` (released: `LockedIn_2On`); label in
  `navAnim.purchaseInfo.label`: zone code + number - DL Deadlands, BM Bloodmarsh, MD Meadow of Despair (verified),
  HI Hollow of Infestation (assumed, initials like MD); "Nekropolis" at the Nekropolis gate.
- ~1.4 s later price markers `koin0`..`koin4` build (gotoAndPlay BuildOn, SetX/SetY/SetXScale as doubles - a `%f`
  script value is a big-endian double AT the arg pointer, see the menudebug `%f raw` line); price in
  `koinN.price.amount`. `koin0` is always the koffin locked on (scale 75; the others 45 / 21). koin1-4 = nearby koffins
  with screen positions: the raw material for panned beacons.
- Balance: Invoke `walletAnim.PlayNewAmount` ("%s" "1250") on entry (and presumably after a purchase).
- Zone name: SetText on `locationAnim.location.locationLabel` on arrival.
- Map (I): fast travel. `mapAnim.map` gotoAndPlay `Zone_N_BuildOn`; `info.info1/2/3.text` = name, "1 / 121", "0.83%".
  Zones 1 Nekropolis, 2 Hollow of Infestation (40), 3 Meadow of Despair (55), 4 Bloodmarsh (83), 5 Deadlands (121).
- Prize names are game text: [Krypt] SHORT1.. ("Reptile Primary Costume Concept"); which koffin holds what is hidden
  until bought (sighted players only see the price too).
Camera position: see "Krypt 2026-11-19" below (the 525-unit grid seen first was one run's moves). Purchase with too
few koins does nothing on screen.
2026-11-18 later: koin1-4 screen X/scale are captured from Invoke SetX/SetXScale (doubles); after the price, one soft
W_SOFT ping per other koffin in view, left to right 90 ms apart: pan = side of view (equal power, 0.8), pitch step
660 Hz near (scale >= 35) / 440 Hz far; measured -19 dBFS peak, 1 % speech band. F4 lists them ("left, near, 1140
koins. ... You have 0 koins"), Shift+F4 pings on/off. Select on an unaffordable koffin: "Not enough koins. It costs
1100, you have 0." (Back or any message box clears the koffin lock so the exit question's Select doesn't trigger it).
[Krypt] SHORT<n> within 15 s of Select = "Unlocked: <prize>"; NavClaim / NavEnter add "Claim prize" / "Enter"; the
welcome text is read with "Close" - these three are untested (no koins, welcome already seen).
Built (menu.c "Krypt"): entry "Deadlands" + "0 koins"; lock-on "Deadlands 6" then "800 koins"; map "Bloodmarsh. 0 of 83
koffins opened, 0 percent"; travel arrival says the first koffin (zone label skipped when the koffin names it).
Holders resolve once on Krypt entry, so after a hot reload re-enter the Krypt before testing.
Entering asks for a profile ("requires that you sign into a profile", read) but continues.

### Krypt 2026-11-19 (finished: purchase, prize, walking sounds)
- Test saves: `work\testsaves` is a copy of game\saves; every purchase test runs there (tools/drive.py launch_test,
  `--user_data_root`). Koin balance in guest memory: **0x833d1d8c** (and a copy at 0x833d1d90); 0x833cb020 is a separate
  running total (lifetime earned). Found by a Ladder win (100 -> 105 -> 206). `poke 833d1d8c 50000` + `poke 833d1d90
  50000` on the test session gave koins, and the game autosaved it into the test save (now ~49900 koins there).
- Purchase verified: lock-on "Deadlands 3", "1100 koins"; Select -> BuyAnim, then the prize view
  (`UnlockAnim.gotoAndPlay BuildOn`, art loadMovie `img://#KryptArt5...`, [Krypt] SHORT317) -> "Unlocked: Cage Fatality
  Sketch. Select closes it." Closing -> walletAnim.PlayNewAmount 49902 -> "49902 koins". A bought koffin is no longer a
  lock target (no "opened" state needed). The map then says "2 of 121 koffins opened, 1.65 percent".
- Walking: the camera's TARGET position is two floats at +0x1F4 in an object whose first word is its class pointer
  0x820228a0 (one such object; at 0x40e2cb70 in every run so far, so x = 0x40e2cd64, y = 0x40e2cd68). It jumps within one
  frame of the press (the visible glide takes ~1 s); distances vary (~300-600). Not a 525 grid (that note was one
  run's moves). menu.c krypt_motion(): a position change = footsteps (Kenney CC0 footstep_grass 000-003 =
  MK9Access\sounds\krypt_step1-4.wav, 2 steps or 3 for >= 450 units, 310 ms apart, alternating pan +-0.12, rate
  0.94-1.02, gain 0.17 = about -19 dBFS like the pings); a direction press (arrows, WASD, d-pad, left stick) with no
  move, no speech and no map/prize view within 350 ms = bump (Kenney impactSoft_heavy_000 = krypt_bump.wav, gain 0.2,
  about -16 dBFS). Presses within 1.2 s of a move are ignored (the game ignores them during the glide). Fast travel
  (> 3000 units) makes no steps. If the class pointer is not at 0x40e2cb70, the heap 0x40000000-0x50000000 is scanned
  for it (every 3 s while missing). The map flags itself with mapAnim BuildOn_Zone / BuildOff_Zone, the prize view with
  UnlockAnim BuildOn (cleared by the wallet update when it closes).
- audio.c gained sample voices (sfx_load / sfx_play: 16 samples, 12 voices, delay/gain/pan/rate), mixed before the
  clip duck. data/sounds/ ships via deploy.sh (data/. -> MK9Access/), with Kenney's licence.
- Koins outside the Krypt: after a fight win the game shows the old balance and counts up ("0 koins", "100 koins" were
  both spoken). wallet_poll now says only the settled new total (1.5 s after the last update) and nothing when unchanged;
  in the Krypt it is said at once (entry: after the zone name). The wallet no longer marks "in the Krypt".
- Still not done: the welcome dialog (seen already on both saves), Nekropolis contents, a description of each prize's art
  (only its name is read; hundreds of prizes).

## Krypt-load crash (port bug, 2026-11-19)
Entering the Krypt sometimes kills the game a few seconds after "Loading" ("Unhandled guest access violation" in
logs\mk9_*.log; no crash dumps were found). Seen on 1.4.0 and 1.4.1: 12:24 and 23:52 (write of guest 0x820E0858 at
mk9.exe+0x263AFB4), 12:27 (write 0x8210DA68 at +0x3097648), 00:41 (write 0x78780000 at +0x27592FF), 00:59 (READ of
0x4B525054 = ASCII "KRPT" at +0x3C98C5C: Krypt data used as a pointer). tools/krypt_crashrate.py alternates fresh
starts with the mod on / unloaded just before Select (rename MK9Access.dll with devreload.txt -> "stopped for
reload"): off 1 crash of 5, on 0 of 5. So it happens with no mod code in the game: the port (or the game under it), not
the mod. About 4 crashes in ~17 Krypt loads today. Reported 2026-11-19 as
https://github.com/machinebreak/mk9-lauta/issues/19 (5 crash signatures, repro, logs offered). The F9 explanation of 2026-11-18 was a coincidence.

## Described fatalities - DONE in the Fatality Tutorial (2026-11-17)
In Training > Fatality Tutorial, each fatality is described. Flow: the arena loads and the game localizes the fatality
name; menu.c's on_group passes it to fatality_try() (src/fatality.c), which (in game state 11, the fatality state)
speaks "<Name>. <input in words>." through the screen reader and remembers the fatality's clip. When the player
PERFORMS it, a recorded Kokoro clip describes it. Execution is detected via the tutorial's fatalityHUD: it has two
bracket-input displays (the required command and the player's "yourMove" progress, e.g. "[Towards][Down][Towards][BP]");
menu.c's SetText hook feeds every bracket string to fatality_input_settext(); when two displays hold the same full
input, the fatality fired -> clip_play of the current clip. Verified: Scorpion Split Decision (read + clip on perform)
and Ermac Mind Over Splatter (read, incl. Hold/Release Block).

Audio: audio.c gained a voice-clip channel (load_wav + clip_play/clip_stop/clip_playing): loads a 16-bit WAV (the
24 kHz mono Kokoro clips), resamples into the 44.1 kHz mix, and ducks the mod's own tones/hums under it (s_duck).
Voice: Kokoro **af_heart** (the Tekken 3 AD voice), the gen_ad.py pipeline, speed 1.12; the clips are DESCRIPTION ONLY
(name + input go through the screen reader). Content: data/fatalities.tsv (31 chars x 2 = 62, from a cross-checked
guide); tools/gen_fatalities.py makes out/MK9Access/fatalities/<key>.wav + index.tsv (<name><TAB><input words><TAB>
<clip>); deploy.sh ships the folder. Names are matched normalized (letters+digits, lower case).
Live matches: see the next section. Tutorial RETRY needs a profile and pops the "sign in / Continue without saving"
dialog (sysdialog reads it; choose Continue without saving). Some descriptions were the research agent's
best-recollection (Johnny Cage Heads Up!, Jax Smash and Grab, Smoke x2, Sektor Scarecrow, Noob As One, Baraka Take a
Spin, Raiden Transplant, Skarlet x2) - names/inputs solid, a visual detail may want a video check. The tutorial shows
one fatality per character pick; re-enter / Select a New Fighter for others. See [[kokoro-ad-voice]].

## Fatalities named by the game, either side (2026-11-22, primary; motion matching below is the fallback)
The CPU's fatality on the player is now named and described too. The game names the fatality itself: the MKO
script VM looks up functions by name with `82a10380(script, name, ...)` (Lauta's engine-map: `sub_82A10380`), and
when a fatality cinematic loads it looks up `Fatality1CinemaPostLoadCallback` or `Fatality2CinemaPostLoadCallback`
(then `GenericFatalityPostloadCallback`, `Fatality<N>CinemaPostActivatedCallback`), ~1.4 s before the actors snap
together. No fatality: `VictoryCinemaPostLoadCallback`; fight start: `IntroCinemaPostLoadCallback`. fatality.c hooks
82a10380 (called many times a second: ai_entry, p_attack, process_game_event...; the hook only copies names ending in
`CinemaPostLoadCallback`), and fatality_poll, in state 11 outside the tutorial, takes the winner (higher health) as
the attacker and their N-th row in index.tsv (Fatality1 = the first finishing move in the game's move list; all 31
characters' rows are in move-list order, checked against data/movelists - Johnny Cage's list lacks his second, his
first matches). Player 1's own: "<Name>"; player 2's (the CPU on the user): "<Character>: <Name>", clip 1.7 s later.
Verified in Ladder fights (opponent health poked low on the test saves, fatality inputs by injected pad):
Fatality1 -> "Split Decision" + clip, Fatality2 -> "Nether-Gate" + clip (7.8 s). A CPU fatality was NOT seen: 8
Expert fights and 3 Medium ones, the CPU never chose one (it sweeps you or walks to the Victory cinema), so the
player-2 wording is unheard; the callback is part of the cinematic, not of who triggered it. Babality / stage /
klassic fatalities (Toasty!, Spine Rip, Yummy!): callback names unknown (log line "fatality named: <cb> ... -> none").
Driving tip: the main menu ignores injected pad input while the game is in the background (pause menu, Continue and
fights take it); `trace 82a10380 600` around a fatality lists the callbacks.
- Also 2026-11-22: [SocialMessages] templates ("%s just performed their first Fatality...", read after the first
  fatality) and the combo counter labels ([Gameplay.Fighting] Hit/Hits/Damage, said "Damage. Hits" on every combo)
  are quiet. Still spoken mid-fight: [Gameplay.Messages] pop-ups "Wakeup Attack", "Recovery" (open question for the
  user). The Fatality Tutorial's intro card (character, tagline, "U Normal Timer / J Infinite Timer") is not read.

## Fatalities in live matches (2026-11-18, replaces the 2026-11-17 id approach)
Measured in Ladder fights: state 8 -> 11 happens at FINISH HIM (not when a fatality fires) and stays 11 through the
fatality. The actor's +0x2c is a spawn counter (3/4, 5/6, 7/8 in successive fights), NOT a move id - the old
fatality_ids.tsv map was junk and is gone. When the fatality starts, the cinematic puts both actors at exactly the same
+0xE0 position (never happens while they can move). fatality.c: in state 11 after 8, both x equal for 3 polls =
cinematic -> match_fatality(): pad.c records each controller's directions (L R U D, diagonals only separate) and
attack buttons (X Y A B = 1 2 3 4) per frame; sides become F/B from the attacker's facing (tracked while they can move);
scored against the attacker's two fatalities (index.tsv now has character + raw input columns; gen_fatalities.py
--index-only rewrites it without re-recording). Exact suffix of directions wins (+ button match, longer motion
first: Stryker D F D F vs F D F). Speaks the name, then the description clip 1.1 s later. A CPU fatality (no motion),
stage fatalities and babalities stay unnamed. fight.c keeps the distance hum on through Finish Him (range matters)
and stops it when the cinematic starts. Verified: Scorpion Split Decision (cinematic seen) and Nether-Gate
("B F B 3" exact -> name + clip), a CPU Liu Kang fatality detected and left silent. Not verified: player on the
right side, hold-block fatalities, controller (keyboard tested).
- Fix - Finish Him pause read the stats panel / button icons ("[A]", "Difficulty Level:"): clip holders are reused
  across menus and stale children lingered in g_clip; clip_text's item fallback now takes the NEWEST text (g_clip seq).
- Fix - Super Moves List garbled in state 11: fatality_try was firing inside the move list (it localizes fatality names
  in Finishing Moves) and hijacked rows with "...at sweep range". Now gated to mlcount<4 (not when the move list is open).
- Fix - Finishing Moves read a lone range word ("Varies"/"Jump"): those are [Movelist.Details] tags (keyed by the word);
  quiet_key now drops the range words (Touching/Close/Sweep/Jump/Far/Varies/Anywhere/Mid) but keeps notes like
  "Can be done in the air". Verified: all five Scorpion finishing moves read name+input cleanly, in order.

## Status (2026-11-18)
Tested in game with the bot (speech checked in the log, screens checked by screenshot):
- Loads and speaks; port ImGui found by byte pattern after the port update; online name prompt read.
- Title "Press Start"; main menu ("Main menu. Fight", descriptions); Fight submenu ("Fight. Ladder"); Options menu.
- Character select: "Choose your fighter", names with row/column; Difficulty list ("Difficulty. Easy", moves).
- Quit and exit dialogs; Audio/Video sliders ("Effects, 17", new value on Left/Right).
- Pause menu ("Pause. Resume", items); in-game move list with sections and inputs in words, through scrolling.
- Super Moves list (press Select in the move list) now says "<Fighter> super moves" (was a bare "move list"); the
  main list says "<Fighter> move list". Told apart by the first heading ("SPECIAL ATTACKS" -> super moves).
- Practice/AI Options (pause menu, Training > Practice) now read each row as "<Label>, <Value>" on focus
  ("Damage Info, On"; "Super Meter, Normal"; "Block Mode, Off"), and the new value on Left/Right. These are
  optionN row clips (containers mc_selections.frame.optionN) that gain focus via Invoke gotoAndPlay HighlightOn with
  an EMPTY sub-path; label = optionLabel.label, value = optionText.valueTxt (on_focus_event handles the empty-target case).
- Challenge Tower (menu.c, on_focus_event "challengeInfo"): on open / scroll the card at the nameplate slot fires
  Invoke "containers.containerN.challengeInfo.gotoAndPlay" focusOn -> speaks "Challenge <n>, <title>[, locked], reward
  <k> koins". Fields: numberTextClip.numberText, titleTextClipTop.titleText, reward.group.rewardCoins.text, and a
  lockStatus clip (gotoAndPlay locked/unlocked, tracked per container). Details (I) = InfoOn flip -> infoText.text
  (objective) + skipAnim.skipCost.text. The open/fast-scroll passes every card through the slot, so the announce is
  debounced 500 ms (speak only the resting card; g_tower_pending in menu_poll). Titles are [TowerMission] *_TITLE,
  objectives [TowerMission] *_DETAILS; [ChallengeTower] section is all chrome (quiet_key). The exit confirm ([MsgBox]
  ConfirmQuitChallengeTower) and the "Profile Selection / not signed in" dialog (XamShowMessageBoxUI, choose "Continue
  without saving" to play) already read via sysdialog. Tower RE: parent holder 40e6b820; "[-]" bullet stripped in tidy.
  At a fresh profile only challenge 1 is unlocked so manual up/down does not scroll; the open auto-scrolls to current.
- Challenge Tower 2026-11-18: the card also says the fighter and opponents and the challenge type ("Challenge 1,
  Training: Special Tech. Sonya Blade against Jax. Reward 100 koins"): from `.image.loadMovie` Invokes (now queued as
  focus events, stop 3): `playerModule.playerCharacter1` (img://char_sonya), per card `opponents.characterN` + the
  `opponents` frame "CharsN" (count), `challengeIcon` (icon_Vs = fight, icon_TestYourMight/Strike/Sight,
  icon_SlotMachine = Test Your Luck). Names via [PlayerSelect] lower-case keys, aliases for zombie/cyberninja/etc.
- In a challenge (on_group, before the generic reader): any [TowerMission] / [TowerIcons] group outside the tower menu
  (no card event for 3 s and < 3 *_TITLE keys) is spoken in full: intro "Training: Special Tech. Land each special
  attack within 25 seconds." (TITLE + INITIAL_DETAILS - was title only, the intro comes 3+ s after input), narrator
  lines, the training move to land + its [TutorialMissions] input ("Energy Ring Blast: back, forward, back punch"),
  pause-screen rules, condition badges. Lone TutorialMissions refreshes are dropped. tidy: `
` and `[-]` end a
  sentence; [RT]/[LT]/[RB]/[LB] in words. Verified: intro, narrator, move to land. Not verified (needs later
  challenges): [TowerIcons] badges, *_PAUSE_DETAILS / ADDITIONAL_DETAILS pause screen, minigame HELP/DIRECTIONS, the
  next training move after landing one, the 0/5 progress counter and the timer (not read).
- Message boxes: an ALL-CAPS [MsgBox] label ("QUIT TO THE MAIN MENU") was taken as the body and read after
  "Continue? Retry"; a body now needs a lower-case letter.
- Leaving the tower raised the "not signed in" XamShowMessageBoxUI for USER 0 (logged) although the port said every
  controller was signed in - so it is the game re-checking user 0, not a second pad.
Not done / known gaps:
- (Fixed 2026-11-23, see "Online play": keyboard navigation in F1/F2.) Port F2 overlay language: English via mk9_online_language. Check its
  "Pantalla e idioma" tab for an English option; find how keyboard/pad navigation works there.
- Pause stats panel (time, difficulty, rounds lost) and fight HUD (health, timer, round results) not read.
- Header switch DONE 2026-11-19: Up from Fight lands on it ([MainMenu] NumPLayersToggleDesc alone) -> "1 player. Change
  the number of players with Left and Right."; Left/Right ([MainMenu] NumPlayers1/2 + the rebuilt menu) -> "2 players".
- Message-box choices: a legend redrawn in the same batch (King of the Hill "Invite Friends", 8 times, [Options]) was read
  as 8 choices; choices that repeat in the batch are now skipped (fix not re-seen in game: the dialog only comes after
  changing the button configuration).
- Krypt-load crashes: see "Krypt-load crash" below.
- Not yet visited: Story, Nekropolis, Tag Ladder, Test Your games, the A/V screen titles ("Audio settings").
- Challenge Tower manual scrolling (fast-scroll shoulder buttons, browsing locked challenges) and the "completed"
  card state are untested - a fresh profile has only challenge 1 unlocked, so only the open auto-scroll was exercised.
- Fights: distance hum, health tones and F6/F8 done (tested in Practice). Not done: round timer, attack/
  projectile cues, which player is the user in 2-player, controller shortcuts for F6. Selecting "Online (F1)" in the game menu after hiding the port panel froze menu input once (bot, 2026-11-18).
- Practice character select says the P1 pick and P2 cursor together ("Scorpion. Sub-Zero"): make it
  "Scorpion chosen. Opponent: Sub-Zero".
- Hot reload after `count arm` can leave threads returning into the old DLL: core_stop now pins the old copy when
  threads are still inside; restarting the game is still the safest after count arm.
- The data/movelists files (from packages) are now only a fallback; the in-game move list is read directly.

## Release 1.0.0 on port 1.4.2 (2026-11-22)
- Port 1.4.2 (Vita costumes, practice recording, two players on one keyboard, portable.txt): installed with the mod's
  updater; the mod loads unchanged (plugin api 1, 6 hooks, ImGui found by pattern). Bot-verified on test saves: title,
  main menu, Training, Practice character select, Ladder screen, fatality named by callback (Split Decision + clip),
  F6 health in Ladder.
- Practice panels (imgui.c "Practice panels"): the frame data window (since 1.4.0) and the recording window (1.4.2)
  are always drawn in Practice fights. Before, they counted as "a port screen is open" (im_menu_on), which MUTED THE HUM
  in Practice since 1.4.0, and their text was read as screens. Now split_panels takes their lines out of the generic
  view (titles "FRAME DATA" / "RECORD THE OPPONENT", known line prefixes, bare numbers beside the frame data rows; the
  auto-sized windows' first frames draw lines away from the title, so lines are matched anywhere), im_menu_on only
  counts the rest. Recording: mode changes spoken (record start with a one-time Human hint, "Recorded 8.2 seconds",
  "Playing back[, in a loop]", "Playback stopped", "Loop on/off"). Frame data: F10, Shift+F10 auto (new result
  detected by the value's white flash, col >= FCFCFC). In Practice the mod leaves F5/F6 to the port (F6 with nothing
  recorded says so). Note: while recording, player 1's pad goes to the dummy, so the pause menu doesn't move until F5
  ends it (port behaviour). All verified by bot; hum measured in Practice with audiorec (-28 dBFS RMS).
- setup.ps1 (run by "MK9 Access launch.cmd" before mk9.exe; the cmd also passes --mk9_plugins=true so even a first
  start loads the plugin): forces mk9_plugins = true and mk9_update_check = false; adds user_data_root (game\saves),
  user_language "1" + mk9_online_language "en" when missing (1.0.1: forced ONCE per install even over a saved
  Spanish - the port's built-in default is user_language 5 and its first-start window saves Spanish if accepted;
  MK9Access\english_set.txt marks it done, so a later F2/toml choice is kept); when mk9_preset_detected is missing (fresh toml, before
  the port's hardware detect) writes the Muy baja preset + mk9_first_run_done = true (skips the port's 4-step
  first-start window: language/name/graphics/controller). Tested on the 1.4.2 shipped toml, the user's toml
  (unchanged) and no toml; idempotent; UTF-8 without BOM. Fresh install from the port zip + ISO not run end to end
  (7 GB extraction); the installer is a native Win32 window (buttons, progress bar).

## Options screens and keyboard layouts (2026-11-23, 1.0.2)
- `82a393e0` SetVariable(holder, path, char *text): Options > Gameplay values
  (`gameplay.optionN.optionText.var_valueTxt` "MEDIUM", "90") and the Button Configuration preset
  (`btnConfig.presetSlider.preset.var_GamepadPresetToggle`) are set this way, not with SetText. h_setvar stores them in
  the clip map and queues stop-5 focus events (value said after Left/Right on the row in focus, not on open).
- Gameplay Options: rows `gameplay.optionN` HighlightOn, label `.optionLabel.varLabel`; header
  `gameplay.header.var_titletxt` said when its SetText is new ("Gameplay Options. Controller Vibration, On").
  Was silent on Up/Down before (forum report 2026-11-23: "the menu where you configure subtitles").
- Button Configuration (Options > Controller), btncfg_event: rows `btnConfig.btnN` (label.var_text,
  buttonAnim.var_valueBtn "[X]" -> the key from mk9.toml's keybind_x + "X button"). Down from the preset enters the rows;
  pressing a button on a row gives it to that action (`btnN.selectionFlash` Trigger, the other action takes the old
  button, all values re-SetText, focus moves on) -> "Front Punch, G, A button. Front Kick, T, X button". Exit dialog no
  longer lists [Options] KOTHCONTROLS_* legend words as choices.
- keys.c, Shift+F12: port layout <-> original PC Komplete Edition keys (W A S D/arrows, T U G J, Ctrl throw, Space
  block, Shift flip, Tab tag, 1/Escape start, 4 back; G/Enter accept, J/Backspace back). Sources: VGTimes 2013 guide +
  shortcutposters.com (GameFAQs and the Steam guide refused automated fetches). The SDK's binds match modifiers exactly
  and a bare Shift/Control bind never fires (mnk_input_driver.cpp), so every key is written with Shift+/Ctrl+/Ctrl+Shift+
  variants and Shift/Ctrl as "Shift+Shift" / "Ctrl+Control" (SDL maps both sides to one VK). Applied live with
  rexruntimerd `rex::cvar::SetFlagByName` (string_views passed by pointer) on the game thread in h_get_state, and saved
  to mk9.toml; the previous binds go to MK9Access\keys_saved.txt (its presence = PC layout on). Bot-verified: live
  switch both ways, G accepts, Shift and Ctrl press LT / RB, Shift/Ctrl+Right still change a value. Not tried in a
  fight (X-Ray with Shift+Space follows from the same modifier variants).

## Game language (2026-11-23, 1.0.3)
- Ctrl+F12 / Ctrl+Shift+F12 (keys.c lang_cycle): user_language 1 en, 5 es, 9 pt, 4 fr, 3 de, 6 it, written to mk9.toml
  with mk9_online_language (es/en/pt; others en); the game reads it at its next start. The language in use is read at
  mod start with rexruntimerd `rex::cvar::GetFlagByName` (MSVC std::string returned through a hidden pointer: 32 bytes,
  data inline when capacity <= 15), so a `--user_language=5` command line counts too (handy for tests: the user's
  mk9.toml stays as it is).
- The game's 8-bit texts are Windows-1252 (Spanish accents garbled in speech before): game_text_utf8() converts
  Localize results, SetText, SetVariable and Coalesced values. load_coalesced reads Coalesced.<eng|spa|por|fra|ger|ita>.
  tidy() title-cases accented capitals (UTF-8 C3 80-9E). Ladder stats match [Ladder] RoundsLost/MatchesLost by key.
  Message-box choices joined with lang_or() ("Sí o No").
- Verified in Spanish by bot (test saves): title, main menu + descriptions, Fight menu, character select, difficulty,
  Ladder screen, pause menu, move list (English in the game too), quit dialogs, Options > Gameplay, Button
  Configuration; the switch both ways across a real restart. Still English: the mod's own phrases, fatality clips, the
  Fatality Tutorial name match (fatality_try compares English names). Other languages not tried.

## Story Mode audio description (2026-11-23, story.c)
- Story cutscenes are 64 pre-rendered movies, `game_root\Movies\mk*.wmv` (WMV3 720p 30 fps, 103 minutes; audio tracks
  ger, spa, ita, fre, eng - English is track 4). The game's own subtitle script with timings is `MK9Story.eng` inside
  Coalesced.eng: sections `[mk004]` named like the movie files, rows `n=startframe,endframe,text` at 30 fps (1441 lines;
  `[NIS_*]` sections are the short in-engine pre/post-fight scenes, not described).
- Data: `data/story/<movie>.txt`, one description per line `SECONDS<TAB>text`, placed in silences between subtitle
  lines. `tools/story/prep.py` writes `work/story/<movie>/lines.txt` (dialogue + gaps) and contact sheets (1 picture/s
  in and before gaps, 1/2 s otherwise) for writing; `tools/story/gen_story.py [movie...]` records Kokoro af_heart clips
  to out/MK9Access/story/<movie>_<n>.wav (speeds up to 1.45x to fit before the next spoken line, reports overruns) and
  rewrites cues.tsv; deploy.sh and package.py copy that folder. 638 clips, ~86 MB WAV. BRIEF.md in work/story holds the
  writing rules (major moments, looks with footwear on first appearance, no meta, 2.8 words/s).
- Runtime: the movie's name from the host open of `...\Movies\<name>.wmv` (MinHook on ntdll NtCreateFile; the port's
  file system opens it just before the codec starts - the port uses the same signal for its .bik pictures); the time
  from the XMV codec's GetOutput `82D25390` (the port's FFmpeg replacement; *r8 = picture time in ms, big-endian; time
  of the first picture = 0); Init `82D255C0` = new movie; Close `82D25280` = ended or skipped (clip stopped). A cue plays
  when the picture time passes it; it waits while the previous story clip still speaks (dropped after 2.5 s late).
  Verified 2026-11-23: start-up logos detected by name with a test cue at exactly 1000 ms; Story chapter 1 opening
  movie mk001_002_003 named and described from 3.2 s.
- Timing measured by loopback (tools/story/synccheck.py) over all of mk001_002_003: every clip landed 0.18-0.22 s
  early (the codec hands out pictures ahead of display), so story.c waits LAG_MS 200 after the cue time.
- The fight is loaded behind each Story movie (game state 8 with both fighters from ~3 s in), so the distance hum and
  health tones ran through the movies (user report 2026-11-23); fight_poll is off while story_playing().
  Not yet checked: whether the hum runs during the short in-engine NIS scenes before/after story fights.
- Names (user 2026-11-23): Raiden = RAY-den (ɹˈeɪdən), Liu Kang = Loo Kang, Kang as in bang (lˈuː kˈæŋ); fixed in
  gen_story.py and gen_fatalities.py LEXICON, all affected story clips and all fatality clips re-recorded.
- Writing was done by subagents from the contact sheets; fast fight beats can be misread from 1-2 s sampling
  (mk117's Sindel kills were wrong and were fixed from 0.5 s frames). mk109's subtitle row "You. You are not worthy"
  starts at 77.0 in the game's data but is spoken at 107.0.
- Other game languages: the movie plays that language's track; cues stay on the English timings (dubs mostly follow
  the same lip timing; not checked).

## Online play (2026-11-23, imgui.c + fight.c; released in 1.0.5)
How the port's online works (mk9-lauta docs/online/guia-jugadores.md): F1 window (tabs Rooms, Global chat, Profile,
Replays; Room once in one). Quick match / public / private room (6-letter code). Both press "I'm ready" -> the game
restarts itself as a session (--mk9_net_room, --mk9_net_role=host|guest); then the game's own 2 PLAYERS > Versus menus,
both pads drive the menus (lockstep), no pause in fights, CONTINUE? after the match. Username in game\online_name.txt
(BlindChallengerX saved 2026-11-23).
Fixed:
- Keyboard could not reach F1/F2 at all (the port only sets NavEnableGamepad; the name box could not be focused
  without a mouse). The mod sets ImGuiConfigFlags_NavEnableKeyboard (io at ctx+0x28 from rexruntimerd GetIO) every
  poll: arrows, Tab/Shift+Tab (in order), Enter/Space choose, Escape out. While a port window is open the port keeps
  all input from the game (GameMayRead), so keys don't move the menu behind.
- Unlabelled items: "text box. Enter to type in it" (wide, empty box), "list. Enter goes into it" (scrolling tables
  and chats are child windows: Enter goes in, then arrows), "picture button" (square). Item boxes come from ItemAdd.
- A focused button that disappears (Create private room -> the room opens) left Tab dead until an arrow press: after
  300 ms the mod moves NavId to the window's first button below the title bar.
- Table rows: a focused button gets the plain text of its row before it ("Sala de X, X, about 191 ms, playing, 7 min,
  Watch"), from the last "Label:" on the row. "· 7 min" dot dropped, "~191 ms" -> "about 191 ms", avatar letters
  (alpha 0xEB, one char) dropped, half-scrolled lines (middle outside ImDrawList._CmdHeader.ClipRect, +96) dropped.
- Screen changes: lines compared with digits folded (ping, online count, countdowns never re-read); a mostly-same
  screen speaks only its new lines with their row (chat messages, rooms appearing, status changes, tooltips) after the
  focused item; a new screen is said after 150 ms of settling (chats scroll to the bottom on frame 2: read once).
- Session corner (DrawSessionBadge, "vs <rival>", "<ping> ms - delay N", warnings): taken out of the generic reading
  (it muted the hum all session and re-spoke at every ping change). "Connected to <rival>. F5 reads the connection."
  once; Unstable connection / Waiting for X / Slow connection / desync said once (again only after 10 s gone);
  F5 with no port window reads the corner. Text-only windows (corner, toast) no longer count as "port window open".
- Guest = player 2 (netplay.h: host pad player 1, guest player 2): fight.c me_slot() from --mk9_net_role=guest or
  --mk9_net_join. Hum pan, health tones (mine/theirs), F6 order follow it.
Tested 2026-11-23: real lobby (name typed by keyboard, all four tabs, room list entered, private room made + left).
Local direct session (two copies, --mk9_net_host=7777 / --mk9_net_join=127.0.0.1:7777, --mk9_lobby_url=off, guest
scripted by --mk9_input_script_file): guest's mod read the synced menus, F6 "Sub-Zero 100. Scorpion 100" (own
first). A direct session has no corner (needs a lobby room): corner reading checked offline only (scratch harness
feeding frames to imgui.c). Windows Firewall asked about mk9.exe when the direct host listened (left for the user).
Open: King of the Hill / 2v2 four-PC sides (me_slot assumes guest = P2; KOTH LocalSide differs); checkbox state not
spoken (the text above it usually says it); typed characters not echoed by the mod (InputText text is not drawn via
the hooked AddText); attract-mode "Loading" from the title screen interrupts lobby reading.
1.0.5 also: the room code is spelled ("Room R V A U J K"; "Sala" too) and the port's own "Copy code" button (it copies
silently) is confirmed from the clipboard sequence number: "Code copied: R V A U J K. Paste it to the other player;
they choose Join with a code." ("Copy diagnostics" -> "Diagnostics copied."). F2 options also move by keyboard now.

## 1.0.8: forum report of 2026-11-24 (played on 1.0.6)
- First install: the port's installer window (mk9_installer.cpp) has BS_PUSHBUTTONs without WS_TABSTOP and no initial
  focus, so Tab reaches nothing (the player needed ViewPoint to click "Choose the ISO"). dist/install.ps1, run by
  launch.cmd in its console after setup.ps1: if no game files (toml game_data_root, game_root / assets\game_root /
  .nxpack here or in a parent, as mk9_app.h OnConfigurePaths), lists Mortal Kombat .iso files >= 6 GB (Downloads
  depth 3, Desktop, Documents, game folder, its parent), or B = WinForms OpenFileDialog, or a typed/pasted path
  (.iso or folder with default.xex); starts mk9.exe with MK9_TEST_INSTALL = that path (the installer's WM_TIMER starts
  copying by itself), pulls the console back to the front (AttachThreadInput + SetForegroundWindow), prints
  "Copied X GB of about 7 GB" every 15 s from game_root.partial and "[installer]" warnings from logs\mk9_*.log, exits 3
  when game_root\default.xex exists (launch.cmd then does not start a second copy). Tested end to end in workresh
  (port download + ISO install ~40 s + game start + mod speech); input piped, so the console-front step is untested.
- Hum "in the menu after minigames": (1) after a challenge's Success/Failure the game stays in state 8 with both
  fighters behind "Continue? Retry", so fight.c kept the hum on. fight_hold() on practiceHUD Success/Failure, released
  on [*] LOADING (Retry, Return To Tower), when the fight state ends or both fighters are back at full health.
  (2) minigame tones only stopped on lockIn/buildOff/Select/result: Quit from the pause menu left the meter tone or
  the Test Your Sight head tone on in the tower menu. Meter: silent when no meter frame for 1 s (paused), off after
  20 s; Sight: off when a head actor's class word changes or after 90 s, silent while paused; mg_reset() on tower
  card events and the main menu list. Verified: tone off 1 s after pausing Test Your Might, stays off in the tower.
- Pause menu > Controller Preset (tower and fights): Select opens mc_option_N.SubToggle.PresetSelector (value by
  SetVariable optionText.var_valueTxt "1".."3", arrows' BuildOn/BuildOff with target "PresetSelector.arrowLeft"
  relative to the SubToggle holder). menu.c preset_event: open "Controller preset 1. Left and Right change it, Select
  keeps it, Back cancels.", change "Preset 2" (80 ms wait so open/close replace it), close "Controller preset N"
  (Back restores the old value first). Verified both ways.
- Fatalities in the tower (challenge 5 "Training: Finishing Techniques"): its HUD shows the input as bracket strings
  like the Fatality Tutorial, so fatality_input_settext set g_tut_t and g_play_t (with no clip) and the callback name
  was skipped as "tutorial". Now only a played clip sets g_play_t, the callback is skipped only within 8 s of a clip,
  and g_cur_clip is cleared when a fight state starts. Verified: "Scissor Split" named + clip in challenge 5. In that
  challenge round 1 is a normal KO; Finish Him comes when Baraka's health is poked low and he is hit (bot:
  tools/tower_fatality.py, side-aware, walks to ~230 units = jump range).
- Online "the app unfocuses and becomes unresponsive", "could not get out of the room": tested with two copies on
  this PC (tools/duo.py: host = game\ on test saves, guest = workresh with name MKATest; post_key sends keys to the
  window not in front). Found and fixed:
  1. session.cpp Back() (end of session -> lobby) restarts the game without --user_data_root; the plugin's
     relaunch_for_saves then restarted it AGAIN, and that second copy (started by a windowless process) could open
     without the keyboard focus. MK9ACCESS_RELAUNCHED was also inherited by later children. Now every copy sets
     REX_USER_DATA_ROOT (toml value; the port applies REX_<CVAR> env before plugins, the command line outranks it), so
     the port's own restarts inherit the saves folder and no relaunch happens; the loop marker is cleared in the new
     copy; a bare-start relaunch calls AllowSetForegroundWindow(ASFW_ANY). Verified: session -> Back = one restart per
     copy, mod logs "saves: user_data_root = .../game/saves" in the Back copy.
  2. Escape at the top of a port window leaves NavId 0: Tab does nothing until an arrow. tab_revive (imgui.c): Tab
     edge with nothing focused in an open port window sends one Down. Verified.
  3. Tab into a text box activates it (ImGui); the hint said "Enter to type", and that Enter closed the box. Hint now
     reads ActiveId (offset from rexruntimerd IsItemActive, +14e4): "text box, typing. Type, then press Enter."
  4. Focus lost when the focused button disappears now goes to a labelled item at exactly the old position (I'm
     ready -> Not ready keeps the focus there), else the first button; a merely overlapping item (the rooms table
     header under a vanished "Leave the room") was in a child window where Tab is dead.
  Session corner message adds "To leave, press F1 and choose End the session." Leaving a room, ending a session from
  the host (guest hears "Session over... Back to the lobby in 6 s") and the room events (join, ready, leave) all
  worked by keyboard. Not reproduced: the foreground actually moving away on a single-copy PC (two copies on one PC
  share the foreground, so this test cannot show it).
- Krypt crash: port bug, machinebreak/mk9-lauta#19 (Lauta could not reproduce on 1.4.2 and asked for dumps, CPU, disk,
  whether the plugin writes game memory).
- Krypt crash rerun 2026-11-24 on port 1.4.2 (tools/krypt_crashrate.py 4): mod unloaded 2 crashes of 4 (write 0x8210DA68
  at mk9.exe+0x263AFB4 sub_8280F3D8+0x11E4; writes 0x78780080/0x78780000 at +0x2F99091 sub_8294B270+0xD51), mod loaded
  0 of 4. crash_*.dmp were 0 bytes (the script kills the game 30 s after Select). Comment for #19 drafted in
  work\drafts\lauta-issue19-comment.md (posted 2026-11-24). Installer Tab bug filed as machinebreak/mk9-lauta#24.
