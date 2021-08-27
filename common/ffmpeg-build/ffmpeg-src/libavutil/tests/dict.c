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

#include "libavutil/dict.c"

static void print_dict(const AVEMDictionary *m)
{
    AVEMDictionaryEntry *t = NULL;
    while ((t = av_em_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX)))
        printf("%s %s   ", t->key, t->value);
    printf("\n");
}

static void test_separators(const AVEMDictionary *m, const char pair, const char val)
{
    AVEMDictionary *dict = NULL;
    char pairs[] = {pair , '\0'};
    char vals[]  = {val, '\0'};

    char *buffer = NULL;
    av_em_dict_copy(&dict, m, 0);
    print_dict(dict);
    av_em_dict_get_string(dict, &buffer, val, pair);
    printf("%s\n", buffer);
    av_em_dict_free(&dict);
    av_em_dict_parse_string(&dict, buffer, vals, pairs, 0);
    av_em_freep(&buffer);
    print_dict(dict);
    av_em_dict_free(&dict);
}

int main(void)
{
    AVEMDictionary *dict = NULL;
    AVEMDictionaryEntry *e;
    char *buffer = NULL;

    printf("Testing av_em_dict_get_string() and av_em_dict_parse_string()\n");
    av_em_dict_get_string(dict, &buffer, '=', ',');
    printf("%s\n", buffer);
    av_em_freep(&buffer);
    av_em_dict_set(&dict, "aaa", "aaa", 0);
    av_em_dict_set(&dict, "b,b", "bbb", 0);
    av_em_dict_set(&dict, "c=c", "ccc", 0);
    av_em_dict_set(&dict, "ddd", "d,d", 0);
    av_em_dict_set(&dict, "eee", "e=e", 0);
    av_em_dict_set(&dict, "f,f", "f=f", 0);
    av_em_dict_set(&dict, "g=g", "g,g", 0);
    test_separators(dict, ',', '=');
    av_em_dict_free(&dict);
    av_em_dict_set(&dict, "aaa", "aaa", 0);
    av_em_dict_set(&dict, "bbb", "bbb", 0);
    av_em_dict_set(&dict, "ccc", "ccc", 0);
    av_em_dict_set(&dict, "\\,=\'\"", "\\,=\'\"", 0);
    test_separators(dict, '"',  '=');
    test_separators(dict, '\'', '=');
    test_separators(dict, ',', '"');
    test_separators(dict, ',', '\'');
    test_separators(dict, '\'', '"');
    test_separators(dict, '"', '\'');
    av_em_dict_free(&dict);

    printf("\nTesting av_em_dict_set()\n");
    av_em_dict_set(&dict, "a", "a", 0);
    av_em_dict_set(&dict, "b", av_em_strdup("b"), AV_DICT_DONT_STRDUP_VAL);
    av_em_dict_set(&dict, av_em_strdup("c"), "c", AV_DICT_DONT_STRDUP_KEY);
    av_em_dict_set(&dict, av_em_strdup("d"), av_em_strdup("d"), AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
    av_em_dict_set(&dict, "e", "e", AV_DICT_DONT_OVERWRITE);
    av_em_dict_set(&dict, "e", "f", AV_DICT_DONT_OVERWRITE);
    av_em_dict_set(&dict, "f", "f", 0);
    av_em_dict_set(&dict, "f", NULL, 0);
    av_em_dict_set(&dict, "ff", "f", 0);
    av_em_dict_set(&dict, "ff", "f", AV_DICT_APPEND);
    e = NULL;
    while ((e = av_em_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)))
        printf("%s %s\n", e->key, e->value);
    av_em_dict_free(&dict);

    av_em_dict_set(&dict, NULL, "a", 0);
    av_em_dict_set(&dict, NULL, "b", 0);
    av_em_dict_get(dict, NULL, NULL, 0);
    e = NULL;
    while ((e = av_em_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)))
        printf("'%s' '%s'\n", e->key, e->value);
    av_em_dict_free(&dict);


    //valgrind sensible test
    printf("\nTesting av_em_dict_set_int()\n");
    av_em_dict_set_int(&dict, "1", 1, AV_DICT_DONT_STRDUP_VAL);
    av_em_dict_set_int(&dict, av_em_strdup("2"), 2, AV_DICT_DONT_STRDUP_KEY);
    av_em_dict_set_int(&dict, av_em_strdup("3"), 3, AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
    av_em_dict_set_int(&dict, "4", 4, 0);
    av_em_dict_set_int(&dict, "5", 5, AV_DICT_DONT_OVERWRITE);
    av_em_dict_set_int(&dict, "5", 6, AV_DICT_DONT_OVERWRITE);
    av_em_dict_set_int(&dict, "12", 1, 0);
    av_em_dict_set_int(&dict, "12", 2, AV_DICT_APPEND);
    e = NULL;
    while ((e = av_em_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)))
        printf("%s %s\n", e->key, e->value);
    av_em_dict_free(&dict);

    //valgrind sensible test
    printf("\nTesting av_em_dict_set() with existing AVEMDictionaryEntry.key as key\n");
    av_em_dict_set(&dict, "key", "old", 0);
    e = av_em_dict_get(dict, "key", NULL, 0);
    av_em_dict_set(&dict, e->key, "new val OK", 0);
    e = av_em_dict_get(dict, "key", NULL, 0);
    printf("%s\n", e->value);
    av_em_dict_set(&dict, e->key, e->value, 0);
    e = av_em_dict_get(dict, "key", NULL, 0);
    printf("%s\n", e->value);
    av_em_dict_free(&dict);

    return 0;
}
