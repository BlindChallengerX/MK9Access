MK9 Access 1.0.8

A screen reader and audio mod for Mortal Kombat Komplete Edition on PC, using
MK9 by Lauta (a fan PC port of the Xbox 360 game, by Lauta / machinebreak).

- Menus, character select, move lists, the Challenge Tower, the Krypt, the
  minigames and the port's own windows are spoken through your screen reader
  using Prism. If no screen reader is running, the Windows voice is used.
- In fights, a soft hum tells you where the opponent is: which side, how far,
  and whether they are in the air. Short tones tell you when either fighter
  drops below half and a quarter of their health.
- Every fatality is named and described, yours and the ones done to you.
- Story Mode's cutscenes are described: a narrator speaks in the pauses
  between the characters' lines. Skipping a cutscene stops her.
- In Practice, the port's frame data and its opponent recording are read.
- Online play: the port's online window (rooms, quick match, chat, profile,
  replays) works from the keyboard and is read, and so is the connection.


CONTENTS

  1. What you need and installing
  2. Starting the game
  3. Keys
  4. Fights
  5. Fatalities
  6. Practice
  7. Challenge Tower, Krypt and minigames
  8. Online
  9. Game and mod updates
  10. Known limits
  11. Credits and licenses


1. WHAT YOU NEED AND INSTALLING

You need:
- Your own copy of Mortal Kombat Komplete Edition for Xbox 360, European
  version, as an .iso file.
- An internet connection the first time, and about 8 GB of free disk space.

MK9 by Lauta, the port, is downloaded for you. If you don't have it yet:
1. Unzip this zip into a new, empty folder, for example Documents\MK9.
2. Start "MK9 Access launch.cmd" in that folder.
3. A console window says "Getting MK9 by Lauta" and downloads the newest
   version of the port from its GitHub page (about 75 MB), unpacks it into the
   folder and says when it is done. Your screen reader reads each step. If
   the download fails, the window says why; press any key to close it and
   start the .cmd again. You can also download the port yourself from
   https://github.com/machinebreak/mk9-lauta/releases (the
   windows-x64.zip) and unzip it into the same folder.
4. The first time, the game files are not there yet, and the same console
   window asks for your .iso. It lists the Mortal Kombat .iso files it finds
   in Downloads, the Desktop, Documents and the game folder: type the number
   of yours and press Enter. Or type B and press Enter to pick the file in a
   Windows Open window, or paste the full path of the .iso (or of a folder
   where it is already unpacked, the one with default.xex) and press Enter.
   Q quits. The port then checks the file and copies about 7 GB of game files
   from it, which takes a few minutes. The console reads the progress ("Copied
   2.3 GB of about 7 GB") and says when it is done; then the game starts by
   itself, and the mod starts speaking once the game has loaded.
   (The port's own "mk9-lauta - Setup" window opens behind it and copies by
   itself: you don't need to use it. Its buttons can't be reached with Tab.)
   The first start of port 1.4.2 also prepares extra costumes from your game
   files and downloads a small package for them; this takes a few seconds.

If you already have the port, unzip this zip into its folder, next to
mk9.exe, and start "MK9 Access launch.cmd".

If Windows says "Windows protected your PC", choose "More info" and then
"Run anyway".


2. STARTING THE GAME

Always start the game with "MK9 Access launch.cmd". Each time, it checks a few
port settings the mod needs, then starts mk9.exe:
- the port's plugins on (the mod is a plugin), and the port's own update
  banner off (the mod updates the port itself, see section 9);
- your saves in the "saves" folder inside the game folder. Windows'
  Controlled Folder Access can close the game when it saves in Documents;
- English game text. The mod sets English once, even if the port was set to
  Spanish (the port's own default). To play in another language, press
  Ctrl+F12 in the game (see section 3); that choice is kept;
- the first time only, the lightest graphics, for smooth play and the least
  input lag. You can change them later in the port's options (F2), and your
  changes are kept.

When the game window opens you hear "MK9 Access loaded".

Until you pick an online username, the port opens its Online window at every
start and it takes the keyboard. Arrow keys move to the name box; press Enter,
type a username and press Enter to save it. Or press F1 to hide the window and
play. See section 8.

At "Press Start", press J (or A on a controller) to reach the main menu.


3. KEYS

Keyboard in the game (a controller works too):
  Arrows or W A S D   move, and move in menus
  J                   select in menus
  K                   back in menus
  U, I, J, K          front punch, back punch, front kick, back kick
  Space or L          block
  O                   throw
  P                   tag
  ;                   flip stance
  Enter               start, pause

Original PC keys: Shift+F12 switches the keyboard to the layout of the 2013
PC release of Mortal Kombat Komplete Edition, and Shift+F12 again brings back
the keys you had before. It works at once and is kept for the next start.
  W A S D or arrows   move, and move in menus
  T, U, G, J          front punch, back punch, front kick, back kick
  Left Ctrl           throw
  Space               block
  Left Shift          flip stance (Shift + Space is the X-Ray)
  Tab                 tag
  1 or Escape         pause
  4                   the controller's Back button
  G or Enter          select in menus
  J or Backspace      back in menus
Either Ctrl and either Shift work. If Windows is set to switch keyboard
languages with Ctrl+Shift, throw and flip stance together may switch it:
remove that shortcut in the Windows language settings.

Other keys: the port's options window (F2, Controls tab) lets you give any
action another key. Options > Controller in the game is the button
configuration: each action is read with its key and button. Select starts
editing; on each action press the key you want for it. The action that had
that key takes the old one, and both are read.

Mod keys:
  F5          read the port's window that is open (F1 online, F2 options);
              in an online session with no window open: the connection
  F6          health of both fighters, in percent
  F8          distance hum on or off
  Shift+F8    health tones on or off
  F10         Practice: the last attack's frame data
  Shift+F10   Practice: frame data read after every attack, on or off
  F4          Krypt: the other koffins in view
  Shift+F4    Krypt: koffin pings on or off
  F12         repeat the last thing said
  Shift+F12   keyboard: original PC keys or the port's keys
  Ctrl+F12    game language: English, Spanish, Portuguese, French, German,
              Italian (Ctrl+Shift+F12 goes the other way, so Spanish and
              English are one press apart). Each press says the language in
              that language. The game changes language when you start it
              again. Menus, dialogs and the game's texts are then read in that
              language; the mod's own words (like "Main menu", the Challenge
              Tower cards, the key lists) and the fatality descriptions stay
              in English. Move lists are in English in every language, as in
              the game itself.

Port keys:
  F1    online (rooms, quick match, chat)
  F2    options
  F5, F6, F7   in Practice: record the opponent, play it back, loop it
  Don't press F9: it switches the port's rendering mode, and the game has
  crashed after it.


4. FIGHTS

The hum plays the whole fight. It comes from the side the opponent is on,
gets higher as they come closer, and wobbles while they are in the air. It
stops while the game is paused or a port window is open. F8 turns it off.

Health tones: two falling notes, in the middle, when you drop below half and
below a quarter of your health. Two rising notes, from the opponent's side,
when they do. F6 tells you both health values in percent. Shift+F8 turns the
tones off.

The round winner is spoken, and so are the game's fight messages. Against
the computer you hear the winning character ("Scorpion wins"). In a match
between players you hear the player: the online username of whoever won, or
"Player 1 wins" / "Player 2 wins" in 2 players Versus Mode on one PC. Combo
hit counts and damage are not spoken.


5. FATALITIES

Fatalities are named and described, yours and the ones done to you. When one
starts you hear its name, or "Fighter: name" when the opponent does one to
you. Then a recorded description tells what happens on screen. All 62 of
them are described (two per fighter).

Training > Fatality Tutorial reads the fatality's name and input when it
loads, and plays the description when you perform it.

The move lists in the pause menu also give the fatality inputs and how close
you have to stand.

Not named yet: Babalities, stage fatalities and other extra finishers.


6. PRACTICE

Training > Practice. The pause menu has the AI Options and Practice Options;
each row is read with its value, and the new value when you change it.

Frame data: after each attack the port measures it (startup, advantage on hit
or on block, total frames). F10 reads the last one, for example "Startup 12.
On block minus 3. Total 25." Shift+F10 makes the mod read it after every
attack.

Recording the opponent (port feature): F5 starts recording. While it records,
your controls move the opponent and your own fighter stands still. F5 again
ends it, and you hear how long the recording is. F6 makes the opponent play
it back, F6 again stops it, and F7 turns the loop on or off. The opponent only
moves if Enemy type is set to Human in the pause menu's AI Options. In
Practice, F5 and F6 belong to the port, so F6 doesn't read health there.


7. CHALLENGE TOWER, KRYPT AND MINIGAMES

Challenge Tower: each challenge card is read with its number, title, fighters,
type and reward. The Details button reads the objective and the skip cost.
In a challenge, the objective, the narrator's lines and the move to land are
read.

Krypt (Extras > Krypt): moving between koffins plays footsteps, and a bump
when you can't go that way. Each koffin is read with its zone, number and
price, and your koin balance on entry. F4 lists the other koffins in view
(side, near or far, price), and soft pings come from their direction.
Buying reads what you unlocked and your new balance. The map reads each zone
with how many koffins you have opened.

Fatality challenges ("Perform the displayed Fatality Input") read the input,
and the fatality you perform is named and described as in any fight. After a
challenge's Success or Failure the distance hum stays quiet behind the
"Continue? Retry" menu.

The pause menu's Controller Preset: Select opens a number beside it; Left and
Right change it ("Preset 2"), Select keeps it and Back cancels.

Test Your Might and Test Your Strike: a rising tone follows the meter, with a
pip at the line you need. Pausing hushes the tone, and it stops if you leave
the minigame from the pause menu. Test Your Sight: a tone follows the cup that hides
the eye, from left to right as it moves. Test Your Luck: each reel is read as
it stops.

The Ladder screen reads the fight number, the next opponent and how many
rounds and matches you lost.


8. ONLINE

F1 opens and closes the port's online window (the main menu's "Online (F1)"
does too). In it, and in the options window (F2):
  Tab, Shift+Tab   every button in order
  Arrows           move around the window
  Enter or Space   choose
  Escape           come back out of a list or a text box
On a text box (your username, a room code, a chat line): reached with Tab,
you are already typing in it ("text box, typing"): type and press Enter.
Reached with the arrows, press Enter first, type, and press Enter again. If
nothing answers to Tab (after Escape at the top of the window), Tab puts the
focus back on the first item. A list (the open rooms, the chats, replays) is entered
with Enter; then the arrows move through its rows and Escape leaves it. Each
row is read whole, for example "Sala de Strato, Strato, about 228 ms, playing,
10 min, Watch". New chat messages, rooms that open and people who come in are
read as they appear. A controller works too: Back + Y opens the window, the
d-pad moves, A chooses, B closes.

The Rooms tab:
- Quick match joins the waiting room with the closest player, or opens yours
  for the next one who searches.
- Create private room: the room's code is read letter by letter. "Copy code"
  puts it on the clipboard (you hear "Code copied"); paste it to your friend,
  who chooses "Join with a code".
- Create public room: anyone can join it from the list.
In the room, when both press "I'm ready", the game starts again by itself,
connected to the other player. Don't press anything while it restarts.

You hear "Connected to (name)". Then it is the game's own menus: 2 players
(Up from Fight, then Right), Versus Mode, One VS One; the other player joins
with Start, and each picks a fighter. Both players move the menus. There is no
pause in online fights. The hum and F6 always follow your own fighter, also
when you are player 2. A bad connection is said once ("Unstable connection",
"Waiting for (name)"); F5 reads the opponent's name, the ping and the input
delay. After the match the loser chooses Rematch, another fighter or the menu.
To leave: F1, Room tab, "End the session and go back to the lobby".

Both players need the same version of MK9 by Lauta; the mod keeps yours up to
date (section 9).


9. GAME AND MOD UPDATES

The mod keeps both MK9 by Lauta and itself up to date. At each start it
checks for newer versions of both, downloads them while you play, and says
"Game update X is ready" or "MK9 Access update X is ready. It installs when
you close the game." The next time you start, you hear "Game updated to
version X" or "MK9 Access updated to version X." Your settings, saves and
keys are kept. The updater's log is MK9Access\update.log.


10. KNOWN LIMITS

- Story Mode descriptions are in English and follow the English voice
  track's timing. Only the opening chapter has been tried so far; the
  short scenes before and after each story fight are not described.
- Online: King of the Hill and the four-PC 2 vs 2 have not been tried with
  the mod; in them the hum may take the wrong fighter as yours. Checkboxes in
  the port's windows don't say whether they are ticked, and the letters you
  type in a text box are not read back by the mod.
- The round timer and the pause screen's match stats are not read.
- Loading the Krypt sometimes crashes the game. It is a bug in the port and
  has been reported.
- Babalities and stage fatalities are not named.
- The pause menu's Controller Preset reads the preset's number only, not
  which button does what in it (Options > Controller reads that).


11. CREDITS AND LICENSES

MK9 Access by BlindChallengerX.
MK9 by Lauta: https://github.com/machinebreak/mk9-lauta (GPL).
Speech: Prism (MPL 2.0). Hooks: MinHook (BSD 2-Clause).
Krypt footsteps: Kenney (CC0).
Fatality and Story Mode descriptions voiced with Kokoro.
The licenses are in the licenses folder.
