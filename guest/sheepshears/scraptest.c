/*
 *  scraptest.c - Sheep Shears self-test: the Scrap Manager round trip with the host pasteboard.
 *
 *  Start-up Items app, run by tools/shears/test.sh clip. It reports what it sees through the Shears LOG
 *  command (visible with NW_VERBOSE=1), so the host script can compare it with what it put on the pasteboard:
 *    step 1  host -> guest: GetScrap TEXT (this call is what imports the host pasteboard), length and checksum
 *    step 2  guest -> host: ZeroScrap, PutScrap TEXT with MacRoman bytes
 *    step 3  guest -> host: a 1 MB TEXT
 *    step 4  host -> guest again (the script changes the pasteboard in between): GetScrap TEXT
 *
 *  (C) 2026 Bill Cavalieri
 */

#include <Quickdraw.h>
#include <Events.h>
#include <stdio.h>
#include "shears_client.h"

static unsigned long Sum(const unsigned char *p, long n)
{
    unsigned long a = 1, b = 0;
    long i;
    for (i = 0; i < n; i++) { a = (a + p[i]) % 65521UL; b = (b + a) % 65521UL; }
    return (b << 16) | a;
}

static void ReportScrap(const char *label, ResType type)
{
    char line[160];
    long off = 0;
    Handle h = NewHandle(0);
    long n = GetScrap(h, type, &off);
    unsigned char *p;
    int i, k = 0;
    SignedByte state;
    if (n < 0) {
        sprintf(line, "%s: GetScrap error %ld", label, n);
        ShearsLog(line);
        DisposeHandle(h);
        return;
    }
    state = HGetState(h);
    HLock(h);
    p = (unsigned char *)*h;
    k = sprintf(line, "%s: len=%ld sum=%08lx hex=", label, n, Sum(p, n));
    for (i = 0; i < n && i < 36; i++)
        k += sprintf(line + k, "%02x", p[i]);
    HSetState(h, state);
    DisposeHandle(h);
    ShearsLog(line);
}

static void PutText(const unsigned char *text, long n, const char *label)
{
    char line[80];
    long err = ZeroScrap();
    long err2 = PutScrap(n, 'TEXT', (Ptr)text);
    sprintf(line, "%s: put %ld bytes zero=%ld put=%ld count=%d", label, n, err, err2, InfoScrap()->scrapCount);
    ShearsLog(line);
}

int main(void)
{
    EventRecord ev;
    unsigned long start, now;
    int step = 0;
    unsigned char *big;
    long i;

    if (!ShearsOpen())
        return 0;
    InitGraf(&qd.thePort);
    if (!Hello())
        return 0;
    start = TickCount();
    for (;;) {
        now = TickCount() - start;
        if (step == 0 && now > 6 * 60) { ReportScrap("STEP1 host->guest TEXT", 'TEXT'); step = 1; }
        if (step == 1 && now > 8 * 60) {
            /* Mac OS Roman: e-acute 8E, u-umlaut 9F, em dash D1, open and close double quotes D2 D3, CR line break */
            {
                static const char text[] = "guest \x8e\x9f \xd1 \xd2quoted\xd3\rsecond line";
                PutText((const unsigned char *)text, sizeof text - 1, "STEP2 guest->host");
            }
            step = 2;
        }
        if (step == 2 && now > 10 * 60) {
            big = (unsigned char *)NewPtr(1024L * 1024L);
            if (big) {
                for (i = 0; i < 1024L * 1024L; i++)
                    big[i] = (unsigned char)('a' + (i % 26));
                PutText(big, 1024L * 1024L, "STEP3 guest->host 1MB");
                {
                    char line[64];
                    sprintf(line, "STEP3 sum=%08lx", Sum(big, 1024L * 1024L));
                    ShearsLog(line);
                }
                DisposePtr((Ptr)big);
            }
            step = 3;
        }
        if (step == 3 && now > 16 * 60) { ReportScrap("STEP4 host->guest TEXT", 'TEXT'); step = 4; }
        if (step == 4 && now > 18 * 60) { ShearsLog("SCRAPTEST done"); step = 5; }
        WaitNextEvent(everyEvent, &ev, 6, NULL);
    }
    return 0;
}
