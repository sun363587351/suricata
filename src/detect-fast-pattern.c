/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Anoop Saldanha <poonaatsoc@gmail.com>
 *
 * Implements the fast_pattern keyword
 */

#include "suricata-common.h"
#include "detect.h"
#include "flow.h"
#include "detect-content.h"
#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-fast-pattern.h"

#include "util-error.h"
#include "util-debug.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"

#define DETECT_FAST_PATTERN_REGEX "^(\\s*only\\s*)|\\s*([0-9]+)\\s*,\\s*([0-9]+)\\s*$"

static pcre *parse_regex = NULL;
static pcre_extra *parse_regex_study = NULL;

static int DetectFastPatternSetup(DetectEngineCtx *, Signature *, char *);
void DetectFastPatternRegisterTests(void);

/**
 * \brief Registration function for fast_pattern keyword
 */
void DetectFastPatternRegister(void)
{
    sigmatch_table[DETECT_FAST_PATTERN].name = "fast_pattern";
    sigmatch_table[DETECT_FAST_PATTERN].Match = NULL;
    sigmatch_table[DETECT_FAST_PATTERN].Setup = DetectFastPatternSetup;
    sigmatch_table[DETECT_FAST_PATTERN].Free  = NULL;
    sigmatch_table[DETECT_FAST_PATTERN].RegisterTests = DetectFastPatternRegisterTests;

    sigmatch_table[DETECT_FAST_PATTERN].flags |= SIGMATCH_PAYLOAD;

    const char *eb;
    int eo;
    int opts = 0;

    parse_regex = pcre_compile(DETECT_FAST_PATTERN_REGEX, opts, &eb, &eo, NULL);
    if(parse_regex == NULL)
    {
        SCLogError(SC_ERR_PCRE_COMPILE, "pcre compile of \"%s\" failed at "
                   "offset %" PRId32 ": %s", DETECT_FAST_PATTERN_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if(eb != NULL)
    {
        SCLogError(SC_ERR_PCRE_STUDY, "pcre study failed: %s", eb);
        goto error;
    }

    return;

 error:
    /* get some way to return an error code! */
    return;
}

//static int DetectFastPatternParseArg(

/**
 * \brief Configures the previous content context for a fast_pattern modifier
 *        keyword used in the rule.
 *
 * \param de_ctx   Pointer to the Detection Engine Context.
 * \param s        Pointer to the Signature to which the current keyword belongs.
 * \param m        Pointer to the SigMatch.
 * \param null_str Should hold an empty string always.
 *
 * \retval  0 On success.
 * \retval -1 On failure.
 */
static int DetectFastPatternSetup(DetectEngineCtx *de_ctx, Signature *s, char *arg)
{
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];
    const char *arg_substr = NULL;
    DetectContentData *cd = NULL;
    DetectUricontentData *ud = NULL;

    if (s->sm_lists_tail[DETECT_SM_LIST_PMATCH] == NULL && s->umatch_tail == NULL) {
        SCLogWarning(SC_WARN_COMPATIBILITY, "fast_pattern found inside the "
                     "rule, without a preceding content based keyword.  "
                     "Currently we provide fast_pattern support for content "
                     "and uricontent");
        return -1;
    }

    SigMatch *pm = SigMatchGetLastSMFromLists(s, 4,
                                              DETECT_CONTENT, s->sm_lists_tail[DETECT_SM_LIST_PMATCH],
                                              DETECT_URICONTENT, s->umatch_tail);
    if (pm == NULL) {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "fast_pattern found inside "
                   "the rule, without a content context. Please use a "
                   "content based keyword before using fast_pattern");
        return -1;
    }

    if (pm->type == DETECT_CONTENT) {
        cd = pm->ctx;
    } else if (pm->type == DETECT_URICONTENT) {
        ud = pm->ctx;
    }

    if (arg == NULL|| strcmp(arg, "") == 0) {
        if (pm->type == DETECT_CONTENT) {
            cd->flags |= DETECT_CONTENT_FAST_PATTERN;
        } else if (pm->type == DETECT_URICONTENT) {
            ud->flags |= DETECT_URICONTENT_FAST_PATTERN;
        }
        return 0;
    }

    if (pm->type == DETECT_CONTENT) {
        if (cd->flags & DETECT_CONTENT_NEGATED &&
            (cd->flags & DETECT_CONTENT_DISTANCE ||
             cd->flags & DETECT_CONTENT_WITHIN ||
             cd->flags & DETECT_CONTENT_OFFSET ||
             cd->flags & DETECT_CONTENT_DEPTH)) {

            /* we can't have any of these if we are having "only" */
            SCLogError(SC_ERR_INVALID_SIGNATURE, "fast_pattern; cannot be "
                       "used with negated content, along with relative modifiers.");
            goto error;
        }
    } else if (pm->type == DETECT_URICONTENT) {
        if (ud->flags & DETECT_URICONTENT_NEGATED &&
            (ud->flags & DETECT_URICONTENT_DISTANCE ||
             ud->flags & DETECT_URICONTENT_WITHIN ||
             ud->flags & DETECT_URICONTENT_OFFSET ||
             ud->flags & DETECT_URICONTENT_DEPTH)) {

            /* we can't have any of these if we are having "only" */
            SCLogError(SC_ERR_INVALID_SIGNATURE, "fast_pattern; cannot be "
                       "used with negated uricontent, along with relative modifiers.");
            goto error;
        }
    } else {
        printf("we will never hit else");
    }

    /* Execute the regex and populate args with captures. */
    ret = pcre_exec(parse_regex, parse_regex_study, arg,
                    strlen(arg), 0, 0, ov, MAX_SUBSTRINGS);
    if (ret == 2) {
        if (pm->type == DETECT_CONTENT) {
            if (cd->flags & DETECT_CONTENT_NEGATED ||
                cd->flags & DETECT_CONTENT_DISTANCE ||
                cd->flags & DETECT_CONTENT_WITHIN ||
                cd->flags & DETECT_CONTENT_OFFSET ||
                cd->flags & DETECT_CONTENT_DEPTH) {

                /* we can't have any of these if we are having "only" */
                SCLogError(SC_ERR_INVALID_SIGNATURE, "fast_pattern: only; cannot be "
                           "used with negated content or with any of the relative "
                           "modifiers like distance, within, offset, depth");
                goto error;
            }
            cd->flags |= DETECT_CONTENT_FAST_PATTERN_ONLY;
        } else if (pm->type == DETECT_URICONTENT) {
            if (ud->flags & DETECT_URICONTENT_NEGATED ||
                ud->flags & DETECT_URICONTENT_DISTANCE ||
                ud->flags & DETECT_URICONTENT_WITHIN ||
                ud->flags & DETECT_URICONTENT_OFFSET ||
                ud->flags & DETECT_URICONTENT_DEPTH) {

                /* we can't have any of these if we are having "only" */
                SCLogError(SC_ERR_INVALID_SIGNATURE, "fast_pattern: only; cannot be "
                           "used with negated uricontent");
                goto error;
            }
            ud->flags |= DETECT_URICONTENT_FAST_PATTERN_ONLY;
        } else {
            printf("we will never hit else");
        }
    } else if (ret == 4) {
        res = pcre_get_substring((char *)arg, ov, MAX_SUBSTRINGS,
                                 2, &arg_substr);
        if (res < 0) {
            SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed "
                       "for fast_pattern offset");
            goto error;
        }
        int offset = atoi(arg_substr);
        if (offset > 65535) {
            SCLogError(SC_ERR_INVALID_SIGNATURE, "Fast pattern offset exceeds "
                       "limit");
            goto error;
        }

        res = pcre_get_substring((char *)arg, ov, MAX_SUBSTRINGS,
                                 3, &arg_substr);
        if (res < 0) {
            SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed "
                       "for fast_pattern offset");
            goto error;
        }
        int length = atoi(arg_substr);
        if (offset > 65535) {
            SCLogError(SC_ERR_INVALID_SIGNATURE, "Fast pattern length exceeds "
                       "limit");
            goto error;
        }

        if (offset + length > 65535) {
            SCLogError(SC_ERR_INVALID_SIGNATURE, "Fast pattern (length + offset) "
                       "exceeds limit pattern length limit");
            goto error;
        }

        if (pm->type == DETECT_CONTENT) {
            cd->fp_chop_offset = offset;
            cd->fp_chop_len = length;
            cd->flags |= DETECT_CONTENT_FAST_PATTERN_CHOP;
        } else if (pm->type == DETECT_URICONTENT) {
            ud->fp_chop_offset = offset;
            ud->fp_chop_len = length;
            ud->flags |= DETECT_URICONTENT_FAST_PATTERN_CHOP;
        }
    } else {
        SCLogError(SC_ERR_PCRE_PARSE, "parse error, ret %" PRId32
                   ", string %s", ret, arg);
        goto error;
    }

    //int args;
    //args = 0;
    //printf("ret-%d\n", ret);
    //for (args = 0; args < ret; args++) {
    //    res = pcre_get_substring((char *)arg, ov, MAX_SUBSTRINGS,
    //                             args, &arg_substr);
    //    if (res < 0) {
    //        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_get_substring failed "
    //                   "for arg 1");
    //        goto error;
    //    }
    //    printf("%d-%s\n", args, arg_substr);
    //}

    if (pm->type == DETECT_CONTENT) {
        cd->flags |= DETECT_CONTENT_FAST_PATTERN;
    } else if (pm->type == DETECT_URICONTENT) {
        ud->flags |= DETECT_URICONTENT_FAST_PATTERN;
    }

    return 0;

 error:
    return -1;
}

/*----------------------------------Unittests---------------------------------*/

#ifdef UNITTESTS

/**
 * \test Checks if a fast_pattern is registered in a Signature
 */
int DetectFastPatternTest01(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:\"/one/\"; tcpv4-csum:valid; fast_pattern; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    while (sm != NULL) {
        if (sm->type == DETECT_CONTENT) {
            if ( ((DetectContentData *)sm->ctx)->flags &
                 DETECT_CONTENT_FAST_PATTERN) {
                result = 1;
                break;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks if a fast_pattern is registered in a Signature
 */
int DetectFastPatternTest02(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:\"/one/\"; fast_pattern; "
                               "content:boo; fast_pattern; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    while (sm != NULL) {
        if (sm->type == DETECT_CONTENT) {
            if (((DetectContentData *)sm->ctx)->flags &
                DETECT_CONTENT_FAST_PATTERN) {
                result = 1;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that we have no fast_pattern registerd for a Signature when the
 *       Signature doesn't contain a fast_pattern
 */
int DetectFastPatternTest03(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:\"/one/\"; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    while (sm != NULL) {
        if (sm->type == DETECT_CONTENT) {
            if ( !(((DetectContentData *)sm->ctx)->flags &
                   DETECT_CONTENT_FAST_PATTERN)) {
                result = 1;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a fast_pattern is not registered in a Signature, when we
 *       supply a fast_pattern with an argument
 */
int DetectFastPatternTest04(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:\"/one/\"; fast_pattern:boo; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a fast_pattern is used in the mpm phase.
 */
int DetectFastPatternTest05(void)
{
    uint8_t *buf = (uint8_t *) "Oh strin1.  But what "
        "strin2.  This is strings3.  We strins_str4. we "
        "have strins_string5";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));

    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings_str4; content:strings_string5; "
                               "sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("sig parse failed: ");
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    if (PacketPatternSearch(&th_v, det_ctx, p) != 0)
        result = 1;

    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

end:
    UTHFreePackets(&p, 1);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a fast_pattern is used in the mpm phase.
 */
int DetectFastPatternTest06(void)
{
    uint8_t *buf = (uint8_t *) "Oh this is a string1.  But what is this with "
        "string2.  This is strings3.  We have strings_str4.  We also have "
        "strings_string5";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings_str4; content:strings_string5; "
                               "sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    if (PacketPatternSearch(&th_v, det_ctx, p) != 0)
        result = 1;

    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

end:
    UTHFreePackets(&p, 1);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a fast_pattern is used in the mpm phase, when the payload
 *       doesn't contain the fast_pattern string within it.
 */
int DetectFastPatternTest07(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  now here comes our "
        "dark knight strings_string5.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings_str4; content:strings_string5; "
                               "sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    if (PacketPatternSearch(&th_v, det_ctx, p) == 0)
        result = 1;

    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

end:
    UTHFreePackets(&p, 1);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a fast_pattern is used in the mpm phase and that we get
 *       exactly 1 match for the mpm phase.
 */
int DetectFastPatternTest08(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  now here comes our "
        "dark knight strings3.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        printf("de_ctx init: ");
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings_str4; content:strings_string5; "
                               "sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("sig parse failed: ");
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    uint32_t r = PacketPatternSearch(&th_v, det_ctx, p);
    if (r != 1) {
        printf("expected 1, got %"PRIu32": ", r);
        goto end;
    }

    result = 1;
end:
    UTHFreePackets(&p, 1);
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}
/**
 * \test Checks that a fast_pattern is used in the mpm phase, when the payload
 *       doesn't contain the fast_pattern string within it.
 */
int DetectFastPatternTest09(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  no_strings4 _imp now here "
        "comes our dark knight strings3.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings4_imp; fast_pattern; "
                               "content:strings_string5; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    if (PacketPatternSearch(&th_v, det_ctx, p) == 0)
        result = 1;

    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

end:
    UTHFreePackets(&p, 1);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a the SigInit chooses the fast_pattern with better pattern
 *       strength, when we have multiple fast_patterns in the Signature.  Also
 *       checks that we get a match for the fast_pattern from the mpm phase.
 */
int DetectFastPatternTest10(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  strings4_imp now here "
        "comes our dark knight strings5.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        printf("de_ctx init: ");
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings4_imp; fast_pattern; "
                               "content:strings_string5; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("sig parse failed: ");
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    uint32_t r = PacketPatternSearch(&th_v, det_ctx, p);
    if (r != 1) {
        printf("expected 1, got %"PRIu32": ", r);
        goto end;
    }

    result = 1;
end:
    UTHFreePackets(&p, 1);
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a the SigInit chooses the fast_pattern with better pattern
 *       strength, when we have multiple fast_patterns in the Signature.  Also
 *       checks that we get no matches for the fast_pattern from the mpm phase.
 */
int DetectFastPatternTest11(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  strings5_imp now here "
        "comes our dark knight strings5.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; fast_pattern; "
                               "content:strings4_imp; fast_pattern; "
                               "content:strings_string5; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    if (PacketPatternSearch(&th_v, det_ctx, p) == 0)
        result = 1;


end:
    UTHFreePackets(&p, 1);
    if (de_ctx != NULL) {
        SigGroupCleanup(de_ctx);
        SigCleanSignatures(de_ctx);
        if (det_ctx != NULL)
            DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
        DetectEngineCtxFree(de_ctx);
    }
    return result;
}

/**
 * \test Checks that we don't get a match for the mpm phase.
 */
int DetectFastPatternTest12(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  strings5_imp now here "
        "comes our dark knight strings5.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; "
                               "content:strings4_imp; "
                               "content:strings_string5; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    if (PacketPatternSearch(&th_v, det_ctx, p) == 0)
        result = 1;

    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

end:
    UTHFreePackets(&p, 1);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks that a the SigInit chooses the fast_pattern with a better
 *       strength from the available patterns, when we don't specify a
 *       fast_pattern.  We also check that we get a match from the mpm
 *       phase.
 */
int DetectFastPatternTest13(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  strings5_imp now here "
        "comes our dark knight strings_string5.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        printf("de_ctx init: ");
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:string1; "
                               "content:string2; content:strings3; "
                               "content:strings4_imp; "
                               "content:strings_string5; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        printf("sig parse failed: ");
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    /* start the search phase */
    det_ctx->sgh = SigMatchSignaturesGetSgh(de_ctx, det_ctx, p);
    uint32_t r = PacketPatternSearch(&th_v, det_ctx, p);
    if (r != 1) {
        printf("expected 1 result, got %"PRIu32": ", r);
        goto end;
    }

    result = 1;
end:
    UTHFreePackets(&p, 1);
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks to make sure that other sigs work that should when fast_pattern is inspecting on the same payload
 *
 */
int DetectFastPatternTest14(void)
{
    uint8_t *buf = (uint8_t *) "Dummy is our name.  Oh yes.  From right here "
        "right now, all the way to hangover.  right.  strings5_imp now here "
        "comes our dark knight strings_string5.  Yes here is our dark knight";
    uint16_t buflen = strlen((char *)buf);
    Packet *p = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;
    int alertcnt = 0;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));
    p = UTHBuildPacket(buf,buflen,IPPROTO_TCP);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto end;

    FlowInitConfig(FLOW_QUIET);

    de_ctx->mpm_matcher = MPM_B3G;
    de_ctx->flags |= DE_QUIET;

    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"fast_pattern test\"; content:\"strings_string5\"; content:\"knight\"; fast_pattern; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    de_ctx->sig_list->next = SigInit(de_ctx, "alert tcp any any -> any any "
                                     "(msg:\"test different content\"; content:\"Dummy is our name\"; sid:2;)");
    if (de_ctx->sig_list->next == NULL)
        goto end;

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);
    if (PacketAlertCheck(p, 1)){
        alertcnt++;
    }else{
        SCLogInfo("could not match on sig 1 with when fast_pattern is inspecting payload");
        goto end;
    }
    if (PacketAlertCheck(p, 2)){
        result = 1;
    }else{
        SCLogInfo("match on sig 1 fast_pattern no match sig 2 inspecting same payload");
    }
end:
    UTHFreePackets(&p, 1);
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);

    DetectEngineCtxFree(de_ctx);
    FlowShutdown();
    return result;
}

#endif

/**
 * \test Checks if a fast_pattern is registered in a Signature
 */
int DetectFastPatternTest15(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:\"/one/\"; fast_pattern:only; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    while (sm != NULL) {
        if (sm->type == DETECT_CONTENT) {
            if ( ((DetectContentData *)sm->ctx)->flags &
                 DETECT_CONTENT_FAST_PATTERN) {
                result = 1;
                break;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks if a fast_pattern is registered in a Signature
 */
int DetectFastPatternTest16(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:\"/one/\"; fast_pattern:3,4; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    while (sm != NULL) {
        if (sm->type == DETECT_CONTENT) {
            if ( ((DetectContentData *)sm->ctx)->flags &
                 DETECT_CONTENT_FAST_PATTERN) {
                result = 1;
                break;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest17(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    DetectContentData *cd = sm->ctx;
    if (sm != NULL && sm->type == DETECT_CONTENT) {
        if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
            cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY &&
            !(cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP) &&
            cd->fp_chop_offset == 0 &&
            cd->fp_chop_len == 0) {
            result = 1;
        } else {
            result = 0;
        }
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest18(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_PMATCH];
    DetectContentData *cd = sm->ctx;
    if (sm != NULL && sm->type == DETECT_CONTENT) {
        if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
            !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
            cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
            cd->fp_chop_offset == 3 &&
            cd->fp_chop_len == 4) {
            result = 1;
        } else {
            result = 0;
        }
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest19(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:only; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest20(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; distance:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest21(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:only; within:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest22(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; within:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest23(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:only; offset:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest24(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; offset:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest25(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:only; depth:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest26(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; depth:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest27(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:!two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest28(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content: one; content:two; distance:30; content:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY &&
        !(cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP) &&
        cd->fp_chop_offset == 0 &&
        cd->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest29(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; within:30; content:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY &&
        !(cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP) &&
        cd->fp_chop_offset == 0 &&
        cd->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest30(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; offset:30; content:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY &&
        !(cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP) &&
        cd->fp_chop_offset == 0 &&
        cd->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest31(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; depth:30; content:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY &&
        !(cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP) &&
        cd->fp_chop_offset == 0 &&
        cd->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest32(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:!one; fast_pattern; content:two; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        cd->flags & DETECT_CONTENT_NEGATED &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        !(cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP) &&
        cd->fp_chop_offset == 0 &&
        cd->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest33(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:two; content:!one; fast_pattern; distance:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest34(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:two; content:!one; fast_pattern; within:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest35(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:two; content:!one; fast_pattern; offset:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest36(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:two; content:!one; fast_pattern; depth:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest37(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:3,4; content:three; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest38(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:3,4; content:three; distance:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest39(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:3,4; content:three; within:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest40(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:3,4; content:three; offset:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest41(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:3,4; content:three; depth:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest42(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; distance:10; content:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest43(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; within:10; content:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest44(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; offset:10; content:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest45(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; depth:10; content:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest46(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:65977,4; content:three; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest47(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:3,65977; content:three; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest48(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:two; fast_pattern:65534,4; content:three; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest49(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:!two; fast_pattern:3,4; content:three; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectContentData *cd = de_ctx->sig_list->sm_lists_tail[DETECT_SM_LIST_PMATCH]->prev->ctx;
    if (cd->flags & DETECT_CONTENT_FAST_PATTERN &&
        cd->flags & DETECT_CONTENT_NEGATED &&
        !(cd->flags & DETECT_CONTENT_FAST_PATTERN_ONLY) &&
        cd->flags & cd->flags & DETECT_CONTENT_FAST_PATTERN_CHOP &&
        cd->fp_chop_offset == 3 &&
        cd->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest50(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:!two; fast_pattern:3,4; distance:10; content:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest51(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:!two; fast_pattern:3,4; within:10; content:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest52(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:!two; fast_pattern:3,4; offset:10; content:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest53(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(content:one; content:!two; fast_pattern:3,4; depth:10; content:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks if a fast_pattern is registered in a Signature for uricontent.
 */
int DetectFastPatternTest54(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:\"/one/\"; fast_pattern:only; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->umatch;
    while (sm != NULL) {
        if (sm->type == DETECT_URICONTENT) {
            if ( ((DetectUricontentData *)sm->ctx)->flags &
                 DETECT_URICONTENT_FAST_PATTERN) {
                result = 1;
                break;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

/**
 * \test Checks if a fast_pattern is registered in a Signature for uricontent.
 */
int DetectFastPatternTest55(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:\"/one/\"; fast_pattern:3,4; "
                               "msg:\"Testing fast_pattern\"; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->umatch;
    while (sm != NULL) {
        if (sm->type == DETECT_URICONTENT) {
            if ( ((DetectUricontentData *)sm->ctx)->flags &
                 DETECT_URICONTENT_FAST_PATTERN) {
                result = 1;
                break;
            } else {
                result = 0;
                break;
            }
        }
        sm = sm->next;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest56(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->umatch;
    DetectUricontentData *ud = sm->ctx;
    if (sm != NULL && sm->type == DETECT_URICONTENT) {
        if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
            ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY &&
            !(ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP) &&
            ud->fp_chop_offset == 0 &&
            ud->fp_chop_len == 0) {
            result = 1;
        } else {
            result = 0;
        }
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest57(void)
{
    SigMatch *sm = NULL;
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    result = 0;
    sm = de_ctx->sig_list->umatch;
    DetectUricontentData *ud = sm->ctx;
    if (sm != NULL && sm->type == DETECT_URICONTENT) {
        if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
            !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
            ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
            ud->fp_chop_offset == 3 &&
            ud->fp_chop_len == 4) {
            result = 1;
        } else {
            result = 0;
        }
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest58(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:only; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest59(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; distance:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest60(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:only; within:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest61(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; within:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest62(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:only; offset:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest63(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; offset:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest64(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:only; depth:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest65(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; depth:10; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest66(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:!two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest67(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent: one; uricontent:two; distance:30; uricontent:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;

    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY &&
        !(ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP) &&
        ud->fp_chop_offset == 0 &&
        ud->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest68(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; within:30; uricontent:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY &&
        !(ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP) &&
        ud->fp_chop_offset == 0 &&
        ud->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest69(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; offset:30; uricontent:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY &&
        !(ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP) &&
        ud->fp_chop_offset == 0 &&
        ud->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest70(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; depth:30; uricontent:two; fast_pattern:only; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY &&
        !(ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP) &&
        ud->fp_chop_offset == 0 &&
        ud->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest71(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:!one; fast_pattern; uricontent:two; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        ud->flags & DETECT_URICONTENT_NEGATED &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        !(ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP) &&
        ud->fp_chop_offset == 0 &&
        ud->fp_chop_len == 0) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest72(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:two; uricontent:!one; fast_pattern; distance:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest73(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:two; uricontent:!one; fast_pattern; within:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest74(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:two; uricontent:!one; fast_pattern; offset:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest75(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:two; uricontent:!one; fast_pattern; depth:20; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest76(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:3,4; uricontent:three; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest77(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:3,4; uricontent:three; distance:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest78(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:3,4; uricontent:three; within:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest79(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:3,4; uricontent:three; offset:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest80(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:3,4; uricontent:three; depth:30; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest81(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; distance:10; uricontent:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest82(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; within:10; uricontent:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest83(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; offset:10; uricontent:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest84(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; depth:10; uricontent:three; fast_pattern:3,4; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }


    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest85(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:65977,4; uricontent:three; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest86(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:3,65977; uricontent:three; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest87(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:two; fast_pattern:65534,4; uricontent:three; distance:10; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest88(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:!two; fast_pattern:3,4; uricontent:three; sid:1;)");
    if (de_ctx->sig_list == NULL)
        goto end;
    DetectUricontentData *ud = de_ctx->sig_list->umatch_tail->prev->ctx;
    if (ud->flags & DETECT_URICONTENT_FAST_PATTERN &&
        ud->flags & DETECT_URICONTENT_NEGATED &&
        !(ud->flags & DETECT_URICONTENT_FAST_PATTERN_ONLY) &&
        ud->flags & ud->flags & DETECT_URICONTENT_FAST_PATTERN_CHOP &&
        ud->fp_chop_offset == 3 &&
        ud->fp_chop_len == 4) {
        result = 1;
    } else {
        result = 0;
    }

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest89(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:!two; fast_pattern:3,4; distance:10; uricontent:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest90(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:!two; fast_pattern:3,4; within:10; uricontent:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest91(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:!two; fast_pattern:3,4; offset:10; uricontent:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

int DetectFastPatternTest92(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;

    if ( (de_ctx = DetectEngineCtxInit()) == NULL)
        goto end;

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert icmp any any -> any any "
                               "(uricontent:one; uricontent:!two; fast_pattern:3,4; depth:10; uricontent:three; sid:1;)");
    if (de_ctx->sig_list != NULL)
        goto end;

    result = 1;

 end:
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);
    return result;
}

void DetectFastPatternRegisterTests(void)
{

#ifdef UNITTESTS
    UtRegisterTest("DetectFastPatternTest01", DetectFastPatternTest01, 1);
    UtRegisterTest("DetectFastPatternTest02", DetectFastPatternTest02, 1);
    UtRegisterTest("DetectFastPatternTest03", DetectFastPatternTest03, 1);
    UtRegisterTest("DetectFastPatternTest04", DetectFastPatternTest04, 1);
    UtRegisterTest("DetectFastPatternTest05", DetectFastPatternTest05, 1);
    UtRegisterTest("DetectFastPatternTest06", DetectFastPatternTest06, 1);
    UtRegisterTest("DetectFastPatternTest07", DetectFastPatternTest07, 1);
    UtRegisterTest("DetectFastPatternTest08", DetectFastPatternTest08, 1);
    UtRegisterTest("DetectFastPatternTest09", DetectFastPatternTest09, 1);
    UtRegisterTest("DetectFastPatternTest10", DetectFastPatternTest10, 1);
    UtRegisterTest("DetectFastPatternTest11", DetectFastPatternTest11, 1);
    UtRegisterTest("DetectFastPatternTest12", DetectFastPatternTest12, 1);
    UtRegisterTest("DetectFastPatternTest13", DetectFastPatternTest13, 1);
    UtRegisterTest("DetectFastPatternTest14", DetectFastPatternTest14, 1);
    UtRegisterTest("DetectFastPatternTest15", DetectFastPatternTest15, 1);
    UtRegisterTest("DetectFastPatternTest16", DetectFastPatternTest16, 1);
    UtRegisterTest("DetectFastPatternTest17", DetectFastPatternTest17, 1);
    UtRegisterTest("DetectFastPatternTest18", DetectFastPatternTest18, 1);
    UtRegisterTest("DetectFastPatternTest19", DetectFastPatternTest19, 1);
    UtRegisterTest("DetectFastPatternTest20", DetectFastPatternTest20, 1);
    UtRegisterTest("DetectFastPatternTest21", DetectFastPatternTest21, 1);
    UtRegisterTest("DetectFastPatternTest22", DetectFastPatternTest22, 1);
    UtRegisterTest("DetectFastPatternTest23", DetectFastPatternTest23, 1);
    UtRegisterTest("DetectFastPatternTest24", DetectFastPatternTest24, 1);
    UtRegisterTest("DetectFastPatternTest25", DetectFastPatternTest25, 1);
    UtRegisterTest("DetectFastPatternTest26", DetectFastPatternTest26, 1);
    UtRegisterTest("DetectFastPatternTest27", DetectFastPatternTest27, 1);
    UtRegisterTest("DetectFastPatternTest28", DetectFastPatternTest28, 1);
    UtRegisterTest("DetectFastPatternTest29", DetectFastPatternTest29, 1);
    UtRegisterTest("DetectFastPatternTest30", DetectFastPatternTest30, 1);
    UtRegisterTest("DetectFastPatternTest31", DetectFastPatternTest31, 1);
    UtRegisterTest("DetectFastPatternTest32", DetectFastPatternTest32, 1);
    UtRegisterTest("DetectFastPatternTest33", DetectFastPatternTest33, 1);
    UtRegisterTest("DetectFastPatternTest34", DetectFastPatternTest34, 1);
    UtRegisterTest("DetectFastPatternTest35", DetectFastPatternTest35, 1);
    UtRegisterTest("DetectFastPatternTest36", DetectFastPatternTest36, 1);
    UtRegisterTest("DetectFastPatternTest37", DetectFastPatternTest37, 1);
    UtRegisterTest("DetectFastPatternTest38", DetectFastPatternTest38, 1);
    UtRegisterTest("DetectFastPatternTest39", DetectFastPatternTest39, 1);
    UtRegisterTest("DetectFastPatternTest40", DetectFastPatternTest40, 1);
    UtRegisterTest("DetectFastPatternTest41", DetectFastPatternTest41, 1);
    UtRegisterTest("DetectFastPatternTest42", DetectFastPatternTest42, 1);
    UtRegisterTest("DetectFastPatternTest43", DetectFastPatternTest43, 1);
    UtRegisterTest("DetectFastPatternTest44", DetectFastPatternTest44, 1);
    UtRegisterTest("DetectFastPatternTest45", DetectFastPatternTest45, 1);
    UtRegisterTest("DetectFastPatternTest46", DetectFastPatternTest46, 1);
    UtRegisterTest("DetectFastPatternTest47", DetectFastPatternTest47, 1);
    UtRegisterTest("DetectFastPatternTest48", DetectFastPatternTest48, 1);
    UtRegisterTest("DetectFastPatternTest49", DetectFastPatternTest49, 1);
    UtRegisterTest("DetectFastPatternTest50", DetectFastPatternTest50, 1);
    UtRegisterTest("DetectFastPatternTest51", DetectFastPatternTest51, 1);
    UtRegisterTest("DetectFastPatternTest52", DetectFastPatternTest52, 1);
    UtRegisterTest("DetectFastPatternTest53", DetectFastPatternTest53, 1);
    /*    content fast_pattern tests ^ */
    /* uricontent fast_pattern tests v */
    UtRegisterTest("DetectFastPatternTest54", DetectFastPatternTest54, 1);
    UtRegisterTest("DetectFastPatternTest55", DetectFastPatternTest55, 1);
    UtRegisterTest("DetectFastPatternTest56", DetectFastPatternTest56, 1);
    UtRegisterTest("DetectFastPatternTest57", DetectFastPatternTest57, 1);
    UtRegisterTest("DetectFastPatternTest58", DetectFastPatternTest58, 1);
    UtRegisterTest("DetectFastPatternTest59", DetectFastPatternTest59, 1);
    UtRegisterTest("DetectFastPatternTest60", DetectFastPatternTest60, 1);
    UtRegisterTest("DetectFastPatternTest61", DetectFastPatternTest61, 1);
    UtRegisterTest("DetectFastPatternTest62", DetectFastPatternTest62, 1);
    UtRegisterTest("DetectFastPatternTest63", DetectFastPatternTest63, 1);
    UtRegisterTest("DetectFastPatternTest64", DetectFastPatternTest64, 1);
    UtRegisterTest("DetectFastPatternTest65", DetectFastPatternTest65, 1);
    UtRegisterTest("DetectFastPatternTest66", DetectFastPatternTest66, 1);
    UtRegisterTest("DetectFastPatternTest67", DetectFastPatternTest67, 1);
    UtRegisterTest("DetectFastPatternTest68", DetectFastPatternTest68, 1);
    UtRegisterTest("DetectFastPatternTest69", DetectFastPatternTest69, 1);
    UtRegisterTest("DetectFastPatternTest70", DetectFastPatternTest70, 1);
    UtRegisterTest("DetectFastPatternTest71", DetectFastPatternTest71, 1);
    UtRegisterTest("DetectFastPatternTest72", DetectFastPatternTest72, 1);
    UtRegisterTest("DetectFastPatternTest73", DetectFastPatternTest73, 1);
    UtRegisterTest("DetectFastPatternTest74", DetectFastPatternTest74, 1);
    UtRegisterTest("DetectFastPatternTest75", DetectFastPatternTest75, 1);
    UtRegisterTest("DetectFastPatternTest76", DetectFastPatternTest76, 1);
    UtRegisterTest("DetectFastPatternTest77", DetectFastPatternTest77, 1);
    UtRegisterTest("DetectFastPatternTest78", DetectFastPatternTest78, 1);
    UtRegisterTest("DetectFastPatternTest79", DetectFastPatternTest79, 1);
    UtRegisterTest("DetectFastPatternTest80", DetectFastPatternTest80, 1);
    UtRegisterTest("DetectFastPatternTest81", DetectFastPatternTest81, 1);
    UtRegisterTest("DetectFastPatternTest82", DetectFastPatternTest82, 1);
    UtRegisterTest("DetectFastPatternTest83", DetectFastPatternTest83, 1);
    UtRegisterTest("DetectFastPatternTest84", DetectFastPatternTest84, 1);
    UtRegisterTest("DetectFastPatternTest85", DetectFastPatternTest85, 1);
    UtRegisterTest("DetectFastPatternTest86", DetectFastPatternTest86, 1);
    UtRegisterTest("DetectFastPatternTest87", DetectFastPatternTest87, 1);
    UtRegisterTest("DetectFastPatternTest88", DetectFastPatternTest88, 1);
    UtRegisterTest("DetectFastPatternTest89", DetectFastPatternTest89, 1);
    UtRegisterTest("DetectFastPatternTest90", DetectFastPatternTest90, 1);
    UtRegisterTest("DetectFastPatternTest91", DetectFastPatternTest91, 1);
    UtRegisterTest("DetectFastPatternTest92", DetectFastPatternTest92, 1);
#endif

    return;
}
