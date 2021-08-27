/*
 * copyright (c) 2009 Michael Niedermayer
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>

#include "avstring.h"
#include "dict.h"
#include "internal.h"
#include "mem.h"
#include "bprint.h"

struct AVEMDictionary {
    int count;
    AVEMDictionaryEntry *elems;
};

int av_em_dict_count(const AVEMDictionary *m)
{
    return m ? m->count : 0;
}

AVEMDictionaryEntry *av_em_dict_get(const AVEMDictionary *m, const char *key,
                               const AVEMDictionaryEntry *prev, int flags)
{
    unsigned int i, j;

    if (!m)
        return NULL;

    if (prev)
        i = prev - m->elems + 1;
    else
        i = 0;

    for (; i < m->count; i++) {
        const char *s = m->elems[i].key;
        if (flags & AV_DICT_MATCH_CASE)
            for (j = 0; s[j] == key[j] && key[j]; j++)
                ;
        else
            for (j = 0; av_toupper(s[j]) == av_toupper(key[j]) && key[j]; j++)
                ;
        if (key[j])
            continue;
        if (s[j] && !(flags & AV_DICT_IGNORE_SUFFIX))
            continue;
        return &m->elems[i];
    }
    return NULL;
}

int av_em_dict_set(AVEMDictionary **pm, const char *key, const char *value,
                int flags)
{
    AVEMDictionary *m = *pm;
    AVEMDictionaryEntry *tag = NULL;
    char *oldval = NULL, *copy_key = NULL, *copy_value = NULL;

    if (!(flags & AV_DICT_MULTIKEY)) {
        tag = av_em_dict_get(m, key, NULL, flags);
    }
    if (flags & AV_DICT_DONT_STRDUP_KEY)
        copy_key = (void *)key;
    else
        copy_key = av_em_strdup(key);
    if (flags & AV_DICT_DONT_STRDUP_VAL)
        copy_value = (void *)value;
    else if (copy_key)
        copy_value = av_em_strdup(value);
    if (!m)
        m = *pm = av_em_mallocz(sizeof(*m));
    if (!m || (key && !copy_key) || (value && !copy_value))
        goto err_out;

    if (tag) {
        if (flags & AV_DICT_DONT_OVERWRITE) {
            av_em_free(copy_key);
            av_em_free(copy_value);
            return 0;
        }
        if (flags & AV_DICT_APPEND)
            oldval = tag->value;
        else
            av_em_free(tag->value);
        av_em_free(tag->key);
        *tag = m->elems[--m->count];
    } else if (copy_value) {
        AVEMDictionaryEntry *tmp = av_em_realloc(m->elems,
                                            (m->count + 1) * sizeof(*m->elems));
        if (!tmp)
            goto err_out;
        m->elems = tmp;
    }
    if (copy_value) {
        m->elems[m->count].key = copy_key;
        m->elems[m->count].value = copy_value;
        if (oldval && flags & AV_DICT_APPEND) {
            size_t len = strlen(oldval) + strlen(copy_value) + 1;
            char *newval = av_em_mallocz(len);
            if (!newval)
                goto err_out;
            av_em_strlcat(newval, oldval, len);
            av_em_freep(&oldval);
            av_em_strlcat(newval, copy_value, len);
            m->elems[m->count].value = newval;
            av_em_freep(&copy_value);
        }
        m->count++;
    } else {
        av_em_freep(&copy_key);
    }
    if (!m->count) {
        av_em_freep(&m->elems);
        av_em_freep(pm);
    }

    return 0;

err_out:
    if (m && !m->count) {
        av_em_freep(&m->elems);
        av_em_freep(pm);
    }
    av_em_free(copy_key);
    av_em_free(copy_value);
    return AVERROR(ENOMEM);
}

int av_em_dict_set_int(AVEMDictionary **pm, const char *key, int64_t value,
                int flags)
{
    char valuestr[22];
    snprintf(valuestr, sizeof(valuestr), "%"PRId64, value);
    flags &= ~AV_DICT_DONT_STRDUP_VAL;
    return av_em_dict_set(pm, key, valuestr, flags);
}
int av_em_dict_set_intptr(AVEMDictionary **pm, const char *key, uintptr_t value,
                       int flags)
{
    char valuestr[22];
    snprintf(valuestr, sizeof(valuestr), "%p", value);
    flags &= ~AV_DICT_DONT_STRDUP_VAL;
    return av_em_dict_set(pm, key, valuestr, flags);
}

uintptr_t av_em_dict_get_intptr(const AVEMDictionary *m, const char* key) {
    uintptr_t ptr = NULL;
    AVEMDictionaryEntry *t = NULL;
    if ((t = av_em_dict_get(m, key, NULL, 0))) {
        return av_em_dict_strtoptr(t->value);
    }
    return NULL;
}

uintptr_t av_em_dict_strtoptr(char * value) {
    uintptr_t ptr = NULL;
    char *next = NULL;
    if(!value || value[0] !='0' || (value[1]|0x20)!='x') {
        return NULL;
    }
    ptr = strtoull(value, &next, 16);
    if (next == value) {
        return NULL;
    }
    return ptr;
}

char * av_em_dict_ptrtostr(uintptr_t value) {
    char valuestr[22] = {0};
    snprintf(valuestr, sizeof(valuestr), "%p", value);
    return av_em_strdup(valuestr);
}
static int parse_key_value_pair(AVEMDictionary **pm, const char **buf,
                                const char *key_val_sep, const char *pairs_sep,
                                int flags)
{
    char *key = av_em_get_token(buf, key_val_sep);
    char *val = NULL;
    int ret;

    if (key && *key && strspn(*buf, key_val_sep)) {
        (*buf)++;
        val = av_em_get_token(buf, pairs_sep);
    }

    if (key && *key && val && *val)
        ret = av_em_dict_set(pm, key, val, flags);
    else
        ret = AVERROR(EINVAL);

    av_em_freep(&key);
    av_em_freep(&val);

    return ret;
}

int av_em_dict_parse_string(AVEMDictionary **pm, const char *str,
                         const char *key_val_sep, const char *pairs_sep,
                         int flags)
{
    int ret;

    if (!str)
        return 0;

    /* ignore STRDUP flags */
    flags &= ~(AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);

    while (*str) {
        if ((ret = parse_key_value_pair(pm, &str, key_val_sep, pairs_sep, flags)) < 0)
            return ret;

        if (*str)
            str++;
    }

    return 0;
}

void av_em_dict_free(AVEMDictionary **pm)
{
    AVEMDictionary *m = *pm;

    if (m) {
        while (m->count--) {
            av_em_freep(&m->elems[m->count].key);
            av_em_freep(&m->elems[m->count].value);
        }
        av_em_freep(&m->elems);
    }
    av_em_freep(pm);
}

int av_em_dict_copy(AVEMDictionary **dst, const AVEMDictionary *src, int flags)
{
    AVEMDictionaryEntry *t = NULL;

    while ((t = av_em_dict_get(src, "", t, AV_DICT_IGNORE_SUFFIX))) {
        int ret = av_em_dict_set(dst, t->key, t->value, flags);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int av_em_dict_get_string(const AVEMDictionary *m, char **buffer,
                       const char key_val_sep, const char pairs_sep)
{
    AVEMDictionaryEntry *t = NULL;
    AVEMBPrint bprint;
    int cnt = 0;
    char special_chars[] = {pairs_sep, key_val_sep, '\0'};

    if (!buffer || pairs_sep == '\0' || key_val_sep == '\0' || pairs_sep == key_val_sep ||
        pairs_sep == '\\' || key_val_sep == '\\')
        return AVERROR(EINVAL);

    if (!av_em_dict_count(m)) {
        *buffer = av_em_strdup("");
        return *buffer ? 0 : AVERROR(ENOMEM);
    }

    av_em_bprint_init(&bprint, 64, AV_BPRINT_SIZE_UNLIMITED);
    while ((t = av_em_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX))) {
        if (cnt++)
            av_em_bprint_append_data(&bprint, &pairs_sep, 1);
        av_em_bprint_escape(&bprint, t->key, special_chars, av_em_escape_MODE_BACKSLASH, 0);
        av_em_bprint_append_data(&bprint, &key_val_sep, 1);
        av_em_bprint_escape(&bprint, t->value, special_chars, av_em_escape_MODE_BACKSLASH, 0);
    }
    return av_em_bprint_finalize(&bprint, buffer);
}
