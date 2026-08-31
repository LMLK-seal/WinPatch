/*
 * WINPATCH.C
 *
 * Patches C:\WINDOWS\SYSTEM.INI to add the MaxPhysPage / VCACHE limits
 * that prevent the Windows 98 "Windows Protection Error" on machines
 * with more RAM than the stock VMM32.VXD handles cleanly.
 *
 * IMPORTANT -- the number you pass is a TARGET CEILING, not necessarily
 * your physical RAM:
 *   - If your machine has <= ~1024 MB installed, pass your actual RAM.
 *   - If your machine has MORE than ~1024 MB installed (e.g. 2GB), stock
 *     VMM32.VXD is documented as unreliable no matter what you declare --
 *     you need to pass a REDUCED value below the real amount (commonly
 *     768-1000). This tool warns you but still does what you ask if you
 *     pass something above 1024; it will not silently change your input.
 *
 * Usage (from a DOS prompt, e.g. booted off your USB stick):
 *     WINPATCH <target_RAM_ceiling_in_MB>
 *     WINPATCH 1024
 *
 * What it does:
 *   1. Reads C:\WINDOWS\SYSTEM.INI into memory.
 *   2. Writes an untouched copy to C:\WINDOWS\SYSTEM.BAK first.
 *   3. Sets MaxPhysPage=<hex> under [386Enh]  (adds the section/key if missing,
 *      updates it in place if it already exists).
 *   4. Sets MinFileCache / MaxFileCache under [vcache], auto-scaled to stay
 *      under 80% of the declared ceiling (going over that ratio causes a
 *      separate "Insufficient memory to initialize Windows" error).
 *   5. Writes the result back to SYSTEM.INI.
 *
 * Safe to run more than once -- re-running updates the existing values
 * instead of duplicating lines.
 *
 * Compile as a real-mode DOS .exe:
 *   Open Watcom (on modern Windows/Linux, cross-compiles to DOS):
 *       wcl -bt=dos -ml winpatch.c
 *   Turbo C++ / Borland C++ (under DOSBox or real DOS):
 *       tcc winpatch.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 4000

char *lines[MAX_LINES];
int   num_lines = 0;

char *dup_line(const char *s)
{
    char *p = malloc(strlen(s) + 1);
    if (!p) {
        printf("Out of memory.\n");
        exit(1);
    }
    strcpy(p, s);
    return p;
}

void insert_line(int at, const char *text)
{
    int i;
    if (num_lines >= MAX_LINES) {
        printf("File has more lines than this tool supports.\n");
        exit(1);
    }
    for (i = num_lines; i > at; i--)
        lines[i] = lines[i - 1];
    lines[at] = dup_line(text);
    num_lines++;
}

void replace_line(int idx, const char *text)
{
    free(lines[idx]);
    lines[idx] = dup_line(text);
}

int strip_len(const char *s)
{
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                        s[len - 1] == ' '  || s[len - 1] == '\t'))
        len--;
    return len;
}

int is_section_header(const char *s, const char *name)
{
    char buf[80];
    int len;
    sprintf(buf, "[%s]", name);
    len = strip_len(s);
    return (len == (int)strlen(buf) && strnicmp(s, buf, len) == 0);
}

int find_section(const char *name)
{
    int i;
    for (i = 0; i < num_lines; i++)
        if (is_section_header(lines[i], name))
            return i;
    return -1;
}

int section_end(int start)
{
    int i;
    for (i = start + 1; i < num_lines; i++) {
        int len = strip_len(lines[i]);
        if (len > 0 && lines[i][0] == '[')
            return i;
    }
    return num_lines;
}

void set_key(const char *section, const char *key, const char *value)
{
    int sec, end, i, keylen;
    char newline[160];
    keylen = strlen(key);

    sec = find_section(section);
    if (sec == -1) {
        if (num_lines > 0)
            insert_line(num_lines, "");
        sprintf(newline, "[%s]", section);
        insert_line(num_lines, newline);
        sprintf(newline, "%s=%s", key, value);
        insert_line(num_lines, newline);
        printf("Added new [%s] section with %s=%s\n", section, key, value);
        return;
    }

    end = section_end(sec);
    for (i = sec + 1; i < end; i++) {
        if (strnicmp(lines[i], key, keylen) == 0 && lines[i][keylen] == '=') {
            sprintf(newline, "%s=%s", key, value);
            replace_line(i, newline);
            printf("Updated %s=%s in [%s]\n", key, value, section);
            return;
        }
    }
    sprintf(newline, "%s=%s", key, value);
    insert_line(sec + 1, newline);
    printf("Added %s=%s to [%s]\n", key, value, section);
}

int main(int argc, char *argv[])
{
    FILE *fin, *fbak, *fout;
    char buf[300];
    long ram_mb, pages, ram_kb, max_cache, min_cache;
    char hexval[20], maxcache_str[20], mincache_str[20];
    const char *path   = "C:\\WINDOWS\\SYSTEM.INI";
    const char *backup = "C:\\WINDOWS\\SYSTEM.BAK";
    int i;

    if (argc < 2) {
        printf("Usage: WINPATCH <target_RAM_ceiling_in_MB>\n");
        printf("Example: WINPATCH 1024\n");
        printf("\nThis is the RAM ceiling you want Windows to SEE, not\n");
        printf("necessarily your full physical RAM -- see notes at the\n");
        printf("top of winpatch.c if your machine has more than ~1GB.\n");
        return 1;
    }

    ram_mb = atol(argv[1]);
    if (ram_mb < 8 || ram_mb > 4096) {
        printf("Value looks wrong (expected MB, e.g. 512 or 1024).\n");
        return 1;
    }
    if (ram_mb > 1024) {
        printf("WARNING: stock Windows 98 is documented as unreliable\n");
        printf("above roughly 1024MB no matter what value you declare.\n");
        printf("If this box has more than ~1GB installed, consider\n");
        printf("re-running with a reduced value (commonly 768-1000)\n");
        printf("instead of the full physical amount.\n");
        printf("Proceeding with %ld MB as requested...\n\n", ram_mb);
    }

    pages = (ram_mb * 1024L) / 4L;
    sprintf(hexval, "%lX", (unsigned long) pages);

    /* Keep VCACHE under 80% of the declared ceiling -- going over that
       ratio causes a separate "Insufficient memory to initialize
       Windows" error, independent of the Protection Error this is
       meant to fix. */
    ram_kb = ram_mb * 1024L;
    max_cache = (ram_kb * 80L) / 100L;
    if (max_cache > 262144L)
        max_cache = 262144L;
    min_cache = max_cache / 2L;
    if (min_cache > 131072L)
        min_cache = 131072L;
    sprintf(maxcache_str, "%ld", max_cache);
    sprintf(mincache_str, "%ld", min_cache);

    fin = fopen(path, "rt");
    if (!fin) {
        printf("Could not open %s\n", path);
        return 1;
    }

    fbak = fopen(backup, "wt");
    if (!fbak) {
        printf("Could not create backup file %s\n", backup);
        fclose(fin);
        return 1;
    }

    while (fgets(buf, sizeof(buf), fin)) {
        fputs(buf, fbak);
        {
            int len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = 0;
        }
        if (num_lines >= MAX_LINES) {
            printf("File has more lines than this tool supports.\n");
            fclose(fin);
            fclose(fbak);
            return 1;
        }
        lines[num_lines++] = dup_line(buf);
    }
    fclose(fin);
    fclose(fbak);
    printf("Backed up original to %s\n", backup);

    set_key("386Enh", "MaxPhysPage", hexval);
    set_key("vcache", "MinFileCache", mincache_str);
    set_key("vcache", "MaxFileCache", maxcache_str);

    fout = fopen(path, "wt");
    if (!fout) {
        printf("Could not write %s -- is it read-only?\n", path);
        return 1;
    }
    for (i = 0; i < num_lines; i++) {
        fputs(lines[i], fout);
        fputc('\n', fout);
    }
    fclose(fout);

    printf("\nDone. MaxPhysPage=%s (%ld MB ceiling), MinFileCache=%s, MaxFileCache=%s\n",
           hexval, ram_mb, mincache_str, maxcache_str);
    printf("Reboot to apply the changes.\n");
    return 0;
}
