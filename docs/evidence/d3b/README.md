# Evidence for task D3b (guided acceptance and profile choice)

Written by the simulator (`simulator_picco`) in the scenarios `devicetest_acceptance` (touch),
`devicetest_acceptance_keys` (Up and Down only) and `devicetest_choose_profile`
(`tools/simtest/scenarios/`). Times in them are modelled (update 3 ms, clear 5 ms, the device test's
clock 50 times faster), not predictions.

| File | What |
| --- | --- |
| `devicetest_acceptance-0001-acceptance.json` | The touch walk-through's results file: Part A (the 600 WPM run stopped once and repeated), step 2 failed "wrong mode after it", step 3 skipped after a kill and restart, the rest passed |
| `devicetest_acceptance-acceptance-progress.txt` | Its progress file at the end (`/.crosspoint/acceptance-progress.txt`) |
| `devicetest_acceptance_keys-0001-acceptance.json` | The keys walk-through: step 2 failed "did not appear", step 3 skipped after a kill, step 12 failed "other" |
| `devicetest_choose_profile-0002-choice.json` | Trial B chosen, Trial C kept, Trials A and D locked: byte 15 to 38 |
| `devicetest_choose_profile-0003-choice.json` | Stock chosen, nothing kept: byte 54 to 0 |
| `validator-output.txt` | `python -m piccoflash.results validate` on these files and on D3a's (version 1, still valid) |
| `scenario-run.txt` | The three scenarios' run |
| `menu.png`, `menu_step_2.png` | The Device test menu, and its row after the owner left during step 2 |
| `acc_start.png` | Part A's start screen |
| `part_a_run_stopped.png`, `part_a_readable.png`, `part_a_slower.png`, `part_a_done.png` | Part A's screens |
| `step_instructions.png`, `step_result.png`, `fail_reasons.png`, `summary.png` | A Part B step, its Pass / Fail question, the fail reasons, the summary |
| `choose_profile.png`, `keep_trials.png`, `choice_done.png` | Choose a profile |
| `settings_display_before.png`, `settings_row.png`, `settings_popup.png` | Settings > Display without the row, with it, and its list |
