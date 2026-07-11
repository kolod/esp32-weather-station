#include "i18n.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static const char *SUPPORTED[] = {"en", "de", "fr", "uk"};
#define N_SUPPORTED (int)(sizeof(SUPPORTED) / sizeof(SUPPORTED[0]))
#define DEFAULT_LANG "en"

/* Extract primary subtag (before '-' or ';') into buf[3], lowercase. */
static void primary_tag(const char *src, char *buf)
{
    int i = 0;
    while (src[i] && src[i] != '-' && src[i] != ';' && src[i] != ',' && i < 2) {
        buf[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    buf[i] = '\0';
}

/* Return q-value * 1000 (integer) for a tag like "de;q=0.9" */
static int qvalue(const char *tag)
{
    const char *q = strstr(tag, ";q=");
    if (!q) return 1000;
    float v = 0.0f;
    sscanf(q + 3, "%f", &v);
    return (int)(v * 1000.0f);
}

void accept_language_pick(const char *header, char *out_lang, size_t len)
{
    if (!out_lang || len < 3) return;
    strlcpy(out_lang, DEFAULT_LANG, len);

    if (!header || !*header) return;

    /* Make a mutable copy */
    char buf[256];
    strlcpy(buf, header, sizeof(buf));

    int   best_q    = -1;
    char  best_l[3] = "";

    char *saveptr = NULL;
    char *token   = strtok_r(buf, ",", &saveptr);
    while (token) {
        /* Trim leading whitespace */
        while (*token == ' ') token++;

        char lang[3];
        primary_tag(token, lang);
        int q = qvalue(token);

        for (int i = 0; i < N_SUPPORTED; i++) {
            if (strcmp(lang, SUPPORTED[i]) == 0 && q > best_q) {
                best_q = q;
                strlcpy(best_l, lang, sizeof(best_l));
                break;
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (best_q >= 0) {
        strlcpy(out_lang, best_l, len);
    }
}
