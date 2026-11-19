# Picco early check: setup, identify, backup (read-only)

These steps **change nothing on the Picco**. They check that your PC can run the session tools, check that the Picco can be reached over USB, and make the backup that the real session needs later. You can do them any day before the session. Allow about an hour the first time.

There is no flashing here. If anything on screen asks you to type `REPLACE ONYX`, stop: that belongs to the later session, not this check.

## What you need

* A Windows 10 or 11 PC (64-bit). You do not need administrator rights.
* The Picco, charged, and a USB cable that can carry data. Some cables only charge. If you are unsure, use one that you have used to copy files to a phone.
* An internet connection for step 2.
* About 250 MB of free disk space. That covers the downloaded files (about 70 MB), the private Python the tool sets up for itself (about 100 MB), and the backup (35 MB while it is being made, 16 MB afterwards).
* A USB stick or a second disk, to keep a copy of the backup.

## 1. Get the files

There are two ways to get the tool. Both give the same `session.ps1`. Use the kit if the team has told you that a kit is ready; otherwise use the repository ZIP.

**With the device kit** (the files you will also use in the real session):

1. Sign in to github.com and open <https://github.com/c-tphr/eink-learner-aid/releases>. The kit is in a release marked **Draft**, which you see only when you are signed in.
2. Under **Assets**, download `picco-2slot-<version>.zip` (`<version>` is the release's version).
3. Right-click the ZIP and choose **Extract All…**. Extract it to a short, plain folder such as `C:\picco\picco-2slot-<version>`. Avoid Documents, Desktop and OneDrive folders: they can interfere with large files.
4. In File Explorer, open that folder (the kit folder: it holds `session.ps1`). Click the address bar, type `powershell` and press Enter. A blue or black PowerShell window opens in that folder. Type every command below into that window. Where this sheet says `tools\device`, read "the kit folder".

**With the repository ZIP** (works without a release):

1. Open <https://github.com/c-tphr/eink-learner-aid>. Click the green **Code** button, then **Download ZIP**.
2. Right-click the downloaded ZIP and choose **Extract All…**. Extract it to a short, plain folder such as `C:\picco`. Avoid Documents, Desktop and OneDrive folders: they can interfere with large files.
3. In File Explorer, open `C:\picco\eink-learner-aid-main\tools\device`. Click the address bar, type `powershell` and press Enter. A blue or black PowerShell window opens in that folder. Type every command below into that window.

If you use the repository ZIP now and get the kit later, the session checklist (`DEVICE_SESSION.md`, "If you already did the early check") says how to bring this check's `work` folder into the kit folder.

## 2. Setup (once; needs the internet)

Type:

Command: <!--cmd-->`powershell -ExecutionPolicy Bypass -File .\session.ps1 setup`

The `-ExecutionPolicy Bypass` part lets Windows run the script this one time. It changes no setting on your PC.

The first time, the tool downloads its own copy of Python (from nuget.org, published by the Python Software Foundation) and the flashing library esptool (from pypi.org). It checks every download against a fingerprint built into the script, then keeps them in a `runtime` folder beside the script. Nothing is installed in Windows. To remove it all later, delete the `runtime` folder.

**It worked if** you see <!--ps-->`Private Python ready.` and then a line `piccoflash 1.0.0`. On later runs the setup is skipped. Estimated time: 2 to 10 minutes, depending on your connection.

**If it stops**, it prints a line starting <!--ps-->`[session] STOPPED:` and says that the Picco was not touched:

| The message contains | What to do |
| --- | --- |
| <!--ps-->`could not download Python` | Check the internet connection. Work or school networks sometimes block nuget.org or pypi.org; try a home connection. |
| <!--ps-->`It was deleted; do not use it.` | The download did not match its fingerprint. Do not retry repeatedly. Report it (step 6). |
| <!--ps-->`pip could not install` | Usually a blocked or broken connection to pypi.org. Try once more on another network, then report it. |
| <!--ps-->`Could not start Python` | Security software may have blocked the new `python.exe`. Allow it for this folder, or report it. |

## 3. Identify (read-only; about a minute)

1. Switch the Picco on and leave it on its **Home screen**. Do not use USB drive mode.
2. Plug the cable straight into the PC, not through a hub or a monitor.
3. Type:

Command: <!--cmd-->`powershell -ExecutionPolicy Bypass -File .\session.ps1 identify`

While the tool is connected, the Picco's screen may freeze or the Picco may restart. That is normal: the tool restarts the chip into its built-in read mode and back. If the screen stays frozen afterwards, restart the Picco with its power button.

**It worked if** it ends with <!--q-->`RESULT: PASS. This is a stock Picco that matches what the tools expect.` Then continue with step 4.

Other results:

| You see | Meaning and what to do |
| --- | --- |
| <!--q-->`RESULT: STOP. Do not continue with backup or flash.` | The Picco is not what the tools expect: for example a different chip, different flash memory, security features switched on, or a different partition layout. Nothing was changed. Do **not** run backup. Go to step 6 and report. |
| <!--q-->`RESULT: ALREADY CONVERTED.` | This Picco already has the new layout. That is not expected on a stock device. Go to step 6 and report. |
| <!--q-->`COULD NOT REACH THE PICCO'S CHIP. Nothing was read or written.` | See [If the Picco is not found](#if-the-picco-is-not-found). |
| <!--q-->`No Espressif USB serial port was found.` | See [If the Picco is not found](#if-the-picco-is-not-found). |
| <!--q-->`More than one Espressif device is connected:` | Unplug the other device. Or name the port shown in the list, for example: <!--cmd-->`powershell -ExecutionPolicy Bypass -File .\session.ps1 identify -Port COM5` |

## 4. Backup (read-only; probably 2 to 20 minutes)

Type:

Command: <!--cmd-->`powershell -ExecutionPolicy Bypass -File .\session.ps1 backup`

The tool reads the Picco's whole 16 MB memory twice, showing <!--q-->`Read 1 of 2: all 16 MB (this may take several minutes)...` and then read 2. It keeps the backup only if both reads are identical. Keep the cable still. The time is an estimate: nobody has yet measured how fast this Picco can be read over USB.

**It worked if** you see <!--q-->`BACKUP VERIFIED: both reads are identical.` The tool then shows where the backup file is and its SHA-256, a 64-character fingerprint. It asks you to copy the file (see "Keep the backup safe" below) and then asks:

The question: <!--q-->`When the copy is done, type the LAST 8 characters of the SHA-256 above:`

Type the last 8 characters of the fingerprint and press Enter. The answer is <!--q-->`Thank you. The backup is confirmed.` If it does not match three times, you see <!--q-->`The backup is verified but not yet confirmed.` That is fine; you will be asked again in the real session.

If it stops:

| You see | What to do |
| --- | --- |
| <!--q-->`STOP: THE TWO READS DIFFER. No backup was marked as verified. Do not flash.` | Try again with another cable, or another USB port on the PC. If it happens twice, report it. |
| <!--q-->`REFUSED: there is no passing 'identify' in the work folder` | Run step 3 first, with this Picco connected. |
| <!--q-->`The connection to the chip failed during the backup:` | Check the cable and run the backup again. |
| <!--q-->`REFUSED: the work folder needs at least 40 MB of free disk space.` | Free some space and run it again. |

### Keep the backup safe

The backup is two files in the `work` folder, `picco-backup-….bin` and `picco-backup-….json`. It is the **only** way to put the Picco back to its original Onyx firmware.

* Copy both files to a USB stick or another disk, and keep that copy.
* **Never upload it anywhere, attach it to a GitHub issue, or email it.** The project's GitHub repository is public. The backup holds the Picco's own settings area, which may include your Wi-Fi details, and Onyx's firmware.
* The same applies to the other `.bin` files in `work` (the `identify-…-bootloader.bin`, `-table.bin` and `-otadata.bin` files): keep them private.

## 5. Make the summary to send back

Type:

Command: <!--cmd-->`powershell -ExecutionPolicy Bypass -File .\session.ps1 summary`

It prints <!--q-->`Summary written:` followed by a file name, `work\early-check-summary-….txt`. This one text file is safe to share. It contains what the tools found (chip, memory, security settings, layout), fingerprints of the files rather than the files themselves, the commands you ran, and the last lines of their logs. The Picco's MAC address is partly hidden, and your Windows user name is removed from file paths. The summary does not contact the Picco.

## 6. Send the results

Open a new issue with the **Device early check** form: <https://github.com/c-tphr/eink-learner-aid/issues/new?template=device-early-check.md>. Attach the summary file (or paste its text), answer the questions, and submit. If step 2 stopped, paste the `[session] STOPPED:` line instead.

Attach only the summary file. **Never attach** any `.bin` file, the `work` folder, or a ZIP of it.

## If the Picco is not found

1. Use a cable that carries data, and try a second one.
2. Plug it straight into the PC.
3. Make sure the Picco is switched on, on its Home screen, and not in USB drive mode.
4. Open Device Manager (right-click Start, then **Device Manager**) and expand **Ports (COM & LPT)**. Unplug and replug the Picco and watch for an entry that appears, such as "USB Serial Device (COM5)" or "USB JTAG/serial debug unit (COM5)". If one appears, run identify again with that port: `-Port COM5`.
5. Close any other program that might be using the port, such as a serial terminal or the Arduino IDE.

If nothing appears with two cables, stop there and report it (step 6, with the summary). Please do not try button combinations or special start-up modes: none are known to be safe for this device, and the team will say if one is needed.

## For reference

| Exit code shown as `[exit N: …]` | Meaning |
| --- | --- |
| 0 | worked |
| 1 | stopped or refused; nothing was written |
| 3 | already converted |
| 4 | the Picco could not be reached; nothing was written |

Everything the tools produce is in the `work` folder beside `session.ps1` (`tools\device\work`, or `work` in the kit folder):

* `identify-….json` and three `.bin` files (keep private);
* the backup `.bin` and `.json` (keep private, keep a copy);
* `logs\` (what was shown on screen);
* the summary `.txt` (the only file to share).
