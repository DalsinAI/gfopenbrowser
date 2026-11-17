# GFOpenBrowser's ARexx port

While its window is open, GFOpenBrowser has an ARexx port named
`GFOPENBROWSER` (a second copy gets `GFOPENBROWSER.2`, and so on). It needs
`rexxsyslib.library`, which AmigaOS 3.2 has.

```
/* Load a page and say how long it took */
OPTIONS RESULTS
ADDRESS 'GFOPENBROWSER'
'OPEN https://aminet.net/'
DO UNTIL WORD(RESULT, 1) = 'DONE'
    CALL DELAY 25
    'STATUS'
END
'GETTITLE'
title = RESULT
'LOADTIME'
SAY title 'loaded in' RESULT 'ms'
```

## The speed test

`Rexx/SpeedTest.rexx` in the GFOpenBrowser drawer times the browser on the
machine it runs on: how long it takes to start, and how long each of five
fixed pages takes to load (`QUICK`: the first three). From a Shell:

```
rx Work:GFOpenBrowser/Rexx/SpeedTest.rexx
```

It starts the browser when it is not running and closes it again at the
end, prints one line per page and writes the table to
`RAM:GFOpenBrowser-Speed.txt`. Run it twice to see the cached loads.

## Rules

The same as every GFOpen application's port:

- Commands are in any case. An argument is plain or in quotes.
- `RC` is 0 when the command was done, 5 when it is not possible now, 10
  for an error in the command, 20 for an unknown command.
- On any `RC` but 0, `LASTERROR` gives the reason.
- With `OPTIONS RESULTS`, a command's answer is in `RESULT`.
- No requester is ever shown for a command.

## Commands

| Command | What it does | `RESULT` |
| --- | --- | --- |
| `VERSION` | | The program's name, version and date |
| `OPEN <address>` | Loads a page. `OPENURL` is the same command, and `URL=` may come before the address | |
| `RELOAD` | Loads the page again | |
| `STOP` | Stops the load | |
| `BACK`, `FORWARD` | One page back or forward. `RC` 5 when there is no page to go to | |
| `GETURL` | | The page's address |
| `GETTITLE` | | The page's title |
| `STATUS` | | `LOADING <percent>` while a page loads, `DONE 100` once it has |
| `LOADTIME` | `RC` 5 while a page loads and before the first has | The last finished load, in milliseconds |
| `LOADCOUNT` | | How many loads have finished since the window opened. A script notes it, sends `OPEN`, and waits for it to go up |
| `GETTEXT` | | The page's text, cut to 4095 characters |
| `ACTIVATE` | Brings the window to the front and makes it active | |
| `DEACTIVATE` | Sends the window to the back | |
| `FAULT <code>` | | AmigaDOS's text for an error code |
| `LASTERROR` | | Why the last command failed, empty when it did not |
| `HELP` | | The commands' names |
| `QUIT` | Closes the browser | |

Text is Latin-1, as in the window's own gadgets: characters outside it come
back as `?`.

Saving and printing a page, its source, running a script on the page and
tabs are not there yet.

The commands are in `src/webcore/ob_rexx.c` (plain C, tested on the host by
`tests/test_ob_rexx.c`); the port itself is in `src/webcore/ob_browser.c`.
GFOpenBrowser Lite has its own, smaller port, `AMICRADLE.BROWSER` (see the
README).
