/* SpeedTest.rexx: how long GFOpenBrowser takes to start and to load a few
   fixed pages, on this machine, through its ARexx port (docs/AREXX.md).

     rx SpeedTest.rexx            from the GFOpenBrowser drawer's Rexx drawer
     rx SpeedTest.rexx QUICK      the first three pages only

   It starts the browser when it is not running (and then closes it again
   at the end), loads each page once, and writes the times to
   RAM:GFOpenBrowser-Speed.txt and to the Shell. Nothing else is changed.
   Run it twice to see the second, cached, loads.

   A start is only a start with no engine in memory. The engine can stay
   loaded after the window has closed, and the next start then uses it
   again and is much shorter. So before it starts the browser this waits
   up to 30 seconds for an engine from an earlier run to go, and the
   report says which kind of start was measured.

   MIT, Copyright (c) 2026 AmiCradle Team. */
OPTIONS RESULTS
PARSE UPPER ARG option .
port = 'GFOPENBROWSER'
report = 'RAM:GFOpenBrowser-Speed.txt'
limit = 180                      /* seconds a page may take */

page.1 = 'about:blank'
page.2 = 'https://example.com/'
page.3 = 'https://aminet.net/'
page.4 = 'https://www.bbc.co.uk/'
page.5 = 'https://en.wikipedia.org/wiki/Amiga'
pages = 5
IF option = 'QUICK' THEN pages = 3

/* this script is in <drawer>/Rexx: the browser is one drawer up */
PARSE SOURCE . . . me .
drawer = ''
slash = LASTPOS('/', me)
colon = LASTPOS(':', me)
IF slash > 0 THEN DO
    drawer = LEFT(me, slash - 1)
    cut = MAX(LASTPOS('/', drawer), LASTPOS(':', drawer))
    drawer = LEFT(drawer, cut)
END
ELSE IF colon > 0 THEN drawer = LEFT(me, colon)

started = 0
startup = -1
reused = 0
IF ~SHOW('P', port) THEN DO
    IF ~EXISTS(drawer'GFOpenBrowser') THEN DO
        SAY 'GFOpenBrowser is not running and is not in "'drawer'": start it, then run this again.'
        EXIT 10
    END
    IF engineLoaded() THEN DO
        SAY 'An engine from an earlier run is still loaded: waiting for it to go...'
        CALL TIME 'R'
        DO WHILE engineLoaded() & TIME('E') < 30
            ADDRESS COMMAND 'Wait 1'
        END
        reused = engineLoaded()
    END
    SAY 'Starting GFOpenBrowser...'
    CALL TIME 'R'
    ADDRESS COMMAND 'Run >NIL: <NIL: "'drawer'GFOpenBrowser" about:blank'
    DO WHILE ~SHOW('P', port)
        IF TIME('E') > 120 THEN DO
            SAY 'GFOpenBrowser did not open its ARexx port within 120 seconds.'
            EXIT 10
        END
        ADDRESS COMMAND 'Wait 1'
    END
    /* the port opens with the window; the first page is loading or done */
    IF waitDone(1) < 0 THEN DO
        SAY 'The first page did not finish loading.'
        EXIT 10
    END
    startup = TIME('E')
    started = 1
END

ADDRESS VALUE port
'VERSION'
version = RESULT
line.1 = version', 'DATE()' 'TIME()
IF startup >= 0 & reused THEN line.2 = 'Start to first page shown: 'FORMAT(startup, , 1)' s (an engine from an earlier run was still loaded and was used again)'
ELSE IF startup >= 0 THEN line.2 = 'Start to first page shown: 'FORMAT(startup, , 1)' s (no engine was loaded)'
ELSE line.2 = 'Start: the browser was already running'
line.3 = ''
line.4 = LEFT('Page', 40) RIGHT('Load ms', 8)'  Title'
lines = 4
failed = 0
DO i = 1 TO pages
    'LOADCOUNT'
    before = RESULT
    'OPEN' page.i
    IF RC ~= 0 THEN ms = 'RC 'RC
    ELSE DO
        ms = waitDone(before + 1)
        IF ms < 0 THEN DO
            ms = 'over 'limit' s'
            'STOP'
            failed = failed + 1
        END
    END
    'GETTITLE'
    lines = lines + 1
    line.lines = LEFT(page.i, 40) RIGHT(ms, 8)'  'LEFT(RESULT, 40)
    SAY line.lines
END

IF OPEN('out', report, 'W') THEN DO
    DO i = 1 TO lines
        CALL WRITELN 'out', line.i
    END
    CALL CLOSE 'out'
    SAY ''
    SAY line.1
    SAY line.2
    SAY 'Written to 'report
END
ELSE SAY 'Could not write 'report
IF started THEN DO
    'QUIT'
    /* so that a run straight after this one starts from nothing */
    CALL TIME 'R'
    DO WHILE (SHOW('P', port) | engineLoaded()) & TIME('E') < 30
        ADDRESS COMMAND 'Wait 1'
    END
    IF engineLoaded() THEN SAY 'The engine is still loaded 30 seconds after QUIT: the next start will use it again.'
END
IF failed > 0 THEN EXIT 5
EXIT 0

/* 1 when a WebBrowserEngine process is loaded, from the Shell's Status. */
engineLoaded: PROCEDURE
    list = 'T:GFOpenBrowser-Speed.status'
    found = 0
    ADDRESS COMMAND 'Status >'list
    IF OPEN('st', list, 'R') THEN DO
        DO WHILE ~EOF('st')
            IF POS('WEBBROWSERENGINE', UPPER(READLN('st'))) > 0 THEN found = 1
        END
        CALL CLOSE 'st'
    END
    ADDRESS COMMAND 'Delete >NIL: 'list
    RETURN found

/* Waits until the browser has finished at least `count` loads and is not
   loading: the last load's time in ms, or -1 after `limit` seconds. */
waitDone: PROCEDURE EXPOSE port limit
    PARSE ARG count
    ADDRESS VALUE port
    DO tries = 1 TO limit
        'LOADCOUNT'
        done = RESULT
        'STATUS'
        IF done >= count & WORD(RESULT, 1) = 'DONE' THEN DO
            'LOADTIME'
            IF RC = 0 THEN RETURN RESULT
        END
        ADDRESS COMMAND 'Wait 1'
    END
    RETURN -1
