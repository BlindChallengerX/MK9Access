MK9 Access 1.0.1

A screen reader and audio mod for Mortal Kombat Komplete Edition on PC, using
MK9 by Lauta (a fan PC port of the Xbox 360 game, by Lauta / machinebreak).

- Menus, character select, move lists, the Challenge Tower, the Krypt, the
  minigames and the port's own windows are spoken through your screen reader
  using Prism. If no screen reader is running, the Windows voice is used.
- In fights, a soft hum tells you where the opponent is: which side, how far,
  and whether they are in the air. Short tones tell you when either fighter
  drops below half and a quarter of their health.
- Every fatality is named and described, yours and the ones done to you.
- In Practice, the port's frame data and its opponent recording are read.


CONTENTS

  1. What you need and installing
  2. Starting the game
  3. Keys
  4. Fights
  5. Fatalities
  6. Practice
  7. Challenge Tower, Krypt and minigames
  8. Game updates
  9. Known limits
  10. Credits and licenses


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
4. The first time, the port opens its "mk9-lauta - Setup" window, a normal
   Windows window: Tab moves between its buttons. Choose "Choose the ISO...",
   pick your .iso file in the Open window and press Enter. The port checks the
   file and copies about 7 GB of game files from it, which takes a few
   minutes; a progress bar shows how far it is. Then the game starts by
   itself, and the mod starts speaking once the game has loaded.
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
  banner off (the mod updates the port itself, see section 8);
- your saves in the "saves" folder inside the game folder. Windows'
  Controlled Folder Access can close the game when it saves in Documents;
- English game text. The mod sets English once, even if the port was set to
  Spanish (the port's own default). If you choose another language later,
  that choice is kept: change user_language in mk9.toml (1 English,
  5 Spanish, 9 Portuguese, 3 German, 4 French, 6 Italian);
- the first time only, the lightest graphics, for smooth play and the least
  input lag. You can change them later in the port's options (F2), and your
  changes are kept.

When the game window opens you hear "MK9 Access loaded".

Until you pick an online username, the port opens its Online window at every
start and it takes the keyboard. Type a username and press Enter to save it,
or press F1 to hide the window and play.

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

Mod keys:
  F5          read the port's window that is open (F1 online, F2 options)
  F6          health of both fighters, in percent
  F8          distance hum on or off
  Shift+F8    health tones on or off
  F10         Practice: the last attack's frame data
  Shift+F10   Practice: frame data read after every attack, on or off
  F4          Krypt: the other koffins in view
  Shift+F4    Krypt: koffin pings on or off
  F12         repeat the last thing said

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

The round winner is spoken, and so are the game's fight messages. Combo hit
counts and damage are not spoken.


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

Test Your Might and Test Your Strike: a rising tone follows the meter, with a
pip at the line you need. Test Your Sight: a tone follows the cup that hides
the eye, from left to right as it moves. Test Your Luck: each reel is read as
it stops.

The Ladder screen reads the fight number, the next opponent and how many
rounds and matches you lost.


8. GAME UPDATES

The mod keeps MK9 by Lauta up to date. At each start it checks for a newer
version, downloads it while you play, and says "Game update X is ready. It
installs when you close the game." The next time you start, you hear "Game
updated to version X." Your settings are kept. The updater's log is
MK9Access\update.log.


9. KNOWN LIMITS

- Story Mode has not been tested with the mod.
- The port's options window (F2) is read when it opens; moving inside it is
  not read yet.
- The round timer and the pause screen's match stats are not read.
- Loading the Krypt sometimes crashes the game. It is a bug in the port and
  has been reported.
- Babalities and stage fatalities are not named.


10. CREDITS AND LICENSES

MK9 Access by BlindChallengerX.
MK9 by Lauta: https://github.com/machinebreak/mk9-lauta (GPL).
Speech: Prism (MPL 2.0). Hooks: MinHook (BSD 2-Clause).
Krypt footsteps: Kenney (CC0).
Fatality descriptions voiced with Kokoro.
The licenses are in the licenses folder.
