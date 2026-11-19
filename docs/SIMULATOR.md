# Trying the firmware in your browser

The simulator is the real reader firmware, built to run on a computer instead of the Picco. It shows the Picco's screen (480 x 800, portrait) in a Chrome tab. Nothing is installed on your PC: it runs in a GitHub Codespace, a temporary Linux machine that GitHub runs for you.

## Start it

1. On the repository page on github.com, click the green **Code** button, open the **Codespaces** tab and click **Create codespace on main**. (Next time, the same tab lists your codespace: click its name to reopen it.)
2. Wait while it sets itself up. The first time this takes about 5 to 10 minutes, because it installs the tools and builds the simulator. A code editor opens in the browser with a terminal at the bottom (if there is no terminal, press **Ctrl+`**).
3. In the terminal, type this and press Enter:

   ```
   ./scripts/sim.sh
   ```

   It goes through six numbered steps. When it says **Step 6 of 6: ready**, it prints an address such as `https://<your-codespace>-6080.app.github.dev/`. A new tab with the simulator usually opens by itself; if not, **Ctrl+click** the address in the terminal.

You will see a grey page with the Picco's screen in the middle, showing the CrossPoint home screen. Click the screen once so it receives your key presses.

## Controls

| On the Picco | In the simulator |
| --- | --- |
| Tap the touch screen | Click with the mouse |
| Swipe | Hold the mouse button and drag |
| Side keys (page back / forward, move in menus) | **Up** and **Down** arrow keys |
| Power button: sleep | Hold **P** for half a second |
| Wake from sleep | **P**, **Up** or **Down** (a click does not wake it) |
| Slide switch | **T** |
| Confirm | **Return** (Enter) |

If you choose a landscape reading orientation in the settings, the screen in the tab turns with it.

The **Fit to window** button at the top makes the screen as large as the tab allows (slightly soft); **Sharp pixels** (the default) zooms only by whole steps so text stays crisp, which on a small laptop screen can mean the screen looks smaller than the tab.

## Books

The simulated SD card is the folder `fs_` in the Codespace's file list (left side of the editor); books go in `fs_/books`. The script copies the project's test books there. To add your own EPUB, drag it from your PC onto the `fs_/books` folder in that list. It appears in the reader's file browser the next time you open the folder; nothing needs restarting.

## Reset, restart, stop

| To | Do |
| --- | --- |
| Start again as a new device (settings, reading positions and saved answers wiped, books kept) | `./scripts/sim.sh --reset` |
| Restart quickly without checking for code changes | `./scripts/sim.sh --no-build` |
| See whether it is running | `./scripts/sim.sh --status` |
| Stop it | **Ctrl+C** in the terminal where it runs, or `./scripts/sim.sh --stop` |

If the simulator closes or crashes, it starts again on its own within a second or two. If the tab says **Not connected**, the simulator is not running: run `./scripts/sim.sh` again and the tab reconnects by itself.

**Cost:** a Codespace counts against your free monthly GitHub allowance while it is running (this one uses a 4-core machine; the free allowance covers about 30 hours a month of it). GitHub stops it after 30 minutes without activity; you can also stop it yourself from the **Code > Codespaces** menu (**...** next to its name > **Stop codespace**). Stopping keeps your files, books and settings; deleting the codespace removes them.

**Privacy:** the address only opens for you: GitHub checks that you are signed in to your account, and the simulator has no password of its own. Leave the port's visibility as **Private** in the editor's **Ports** tab (making it Public would let anyone with the address use your simulator). The simulator's internal VNC connection listens only inside the Codespace and is never forwarded.

## What the simulator cannot show

- **Refresh speed and flashing.** The e-ink panel's real update times and the black-white flash of a full refresh are not imitated; the simulator draws instantly. (A later task adds an approximate delay for the reading mode.)
- **Ghosting**, the faint remains of earlier pages on e-ink. Only the device shows it.
- **Real touch feel and button feel**, and how the Picco's buttons are placed. A mouse click is a perfect tap.
- **The front light**, battery, charging and Wi-Fi radio. Their settings exist but change nothing visible.
- **Speed and memory limits.** Your Codespace is far faster than the Picco.

## If something goes wrong

Every failure ends with a line starting **FAILED:** that says what to do, and the name of a log file. Running `./scripts/sim.sh` again is always safe. If it still fails, send the developers that log file (the folder `.sim/logs` holds all of them).
