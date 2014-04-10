/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
** Copyright (C) 2002-2013 Sourcefire, Inc.
** Copyright (C) 1998-2002 Martin Roesch <roesch@sourcefire.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* Snort Detection Plugin Source File for IP Fragment Bits plugin */

/* sp_ip_fragbits
 *
 * Purpose:
 *
 * Check the fragmentation bits of the IP header for set values.  Possible
 * bits are don't fragment (DF), more fragments (MF), and reserved (RB).
 *
 * Arguments:
 *
 * The keyword to reference this plugin is "fragbits".  Possible arguments are
 * D, M and R for DF, MF and RB, respectively.
 *
 * Effect:
 *
 * Inidicates whether any of the specified bits have been set.
 *
 * Comments:
 *
 * Ofir Arkin should be a little happier now. :)
 *
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "snort_types.h"
#include "detection/treenodes.h"
#include "decode.h"
#include "parser.h"
#include "snort_debug.h"
#include "util.h"
#include "snort.h"
#include "profiler.h"
#include "detection/fpdetect.h"
#include "sfhashfcn.h"
#include "detection/detection_defines.h"
#include "framework/ips_option.h"

#define GREATER_THAN            1
#define LESS_THAN               2

#define FB_NORMAL   0
#define FB_ALL      1
#define FB_ANY      2
#define FB_NOT      3

#define FB_RB  0x8000
#define FB_DF  0x4000
#define FB_MF  0x2000

static const char* s_name = "fragbits";

#ifdef PERF_PROFILING
static THREAD_LOCAL PreprocStats fragBitsPerfStats;

static PreprocStats* fb_get_profile(const char* key)
{
    if ( !strcmp(key, s_name) )
        return &fragBitsPerfStats;

    return nullptr;
}
#endif

typedef struct _FragBitsData
{
    char mode;
    uint16_t frag_bits;

} FragBitsData;

class FragBitsOption : public IpsOption
{
public:
    FragBitsOption(const FragBitsData& c) :
        IpsOption(s_name)
    { config = c; };

    uint32_t hash() const;
    bool operator==(const IpsOption&) const;

    int eval(Packet*);

private:
    FragBitsData config;
};

static uint16_t bitmask = 0x0;

//-------------------------------------------------------------------------
// api methods
//-------------------------------------------------------------------------

uint32_t FragBitsOption::hash() const
{
    uint32_t a,b,c;
    const FragBitsData *data = &config;

    a = data->mode;
    b = data->frag_bits;
    c = 0;

    mix_str(a,b,c,get_name());
    final(a,b,c);

    return c;
}

bool FragBitsOption::operator==(const IpsOption& ips) const
{
    if ( strcmp(get_name(), ips.get_name()) )
        return false;

    FragBitsOption& rhs = (FragBitsOption&)ips;
    FragBitsData *left = (FragBitsData*)&config;
    FragBitsData *right = (FragBitsData*)&rhs.config;

    if ((left->mode == right->mode) &&
        (left->frag_bits == right->frag_bits))
    {
        return true;
    }

    return false;
}

int FragBitsOption::eval(Packet *p)
{
    FragBitsData *fb = &config;
    int rval = DETECTION_OPTION_NO_MATCH;
    PROFILE_VARS;

    if(!IPH_IS_VALID(p))
    {
        return rval;
    }

    PREPROC_PROFILE_START(fragBitsPerfStats);

    DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN, "           <!!> CheckFragBits: ");
           DebugMessage(DEBUG_PLUGIN, "[rule: 0x%X:%d   pkt: 0x%X] ",
                fb->frag_bits, fb->mode, (GET_IPH_OFF(p)&bitmask)););

    switch(fb->mode)
    {
        case FB_NORMAL:
            /* check if the rule bits match the bits in the packet */
            if(fb->frag_bits == (GET_IPH_OFF(p)&bitmask))
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"Got Normal bits match\n"););
                rval = DETECTION_OPTION_MATCH;
            }
            else
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"Normal test failed\n"););
            }
            break;

        case FB_NOT:
            /* check if the rule bits don't match the bits in the packet */
            if((fb->frag_bits & (GET_IPH_OFF(p)&bitmask)) == 0)
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"Got NOT bits match\n"););
                rval = DETECTION_OPTION_MATCH;
            }
            else
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"NOT test failed\n"););
            }
            break;

        case FB_ALL:
            /* check if the rule bits are present in the packet */
            if((fb->frag_bits & (GET_IPH_OFF(p)&bitmask)) == fb->frag_bits)
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"Got ALL bits match\n"););
                rval = DETECTION_OPTION_MATCH;
            }
            else
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"ALL test failed\n"););
            }
            break;

        case FB_ANY:
            /* check if any of the rule bits match the bits in the packet */
            if((fb->frag_bits & (GET_IPH_OFF(p)&bitmask)) != 0)
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"Got ANY bits match\n"););
                rval = DETECTION_OPTION_MATCH;
            }
            else
            {
                DEBUG_WRAP(DebugMessage(DEBUG_PLUGIN,"ANY test failed\n"););
            }
            break;
        default:
            break;
    }

    /* if the test isn't successful, this function *must* return 0 */
    PREPROC_PROFILE_END(fragBitsPerfStats);
    return rval;
}

//-------------------------------------------------------------------------
// api methods
//-------------------------------------------------------------------------

void fragbits_parse(char *data, FragBitsData *ds_ptr)
{
    char *fptr;
    char *fend;

    /* manipulate the option arguments here */
    fptr = data;

    while(isspace((u_char) *fptr))
    {
        fptr++;
    }

    if(strlen(fptr) == 0)
    {
        ParseError("No arguments to the fragbits keyword");
    }

    fend = fptr + strlen(fptr);

    ds_ptr->mode = FB_NORMAL;  /* default value */

    while(fptr < fend)
    {
        switch((*fptr&0xFF))
        {
            case 'd':
            case 'D': /* don't frag bit */
                ds_ptr->frag_bits |= FB_DF;
                break;

            case 'm':
            case 'M': /* more frags bit */
                ds_ptr->frag_bits |= FB_MF;
                break;

            case 'r':
            case 'R': /* reserved bit */
                ds_ptr->frag_bits |= FB_RB;
                break;

            case '!': /* NOT flag, fire if flags are not set */
                ds_ptr->mode = FB_NOT;
                break;

            case '*': /* ANY flag, fire on any of these bits */
                ds_ptr->mode = FB_ANY;
                break;

            case '+': /* ALL flag, fire on these bits plus any others */
                ds_ptr->mode = FB_ALL;
                break;

            default:
                ParseError(
                    "Bad Frag Bits = '%c'. Valid options are: RDM+!*", *fptr);
        }

        fptr++;
    }

    /* put the bits in network order for fast comparisons */
    ds_ptr->frag_bits = htons(ds_ptr->frag_bits);
}

static IpsOption* fragbits_ctor(
    SnortConfig*, char *data, OptTreeNode*)
{
    if ( !bitmask )
        bitmask = htons(0xE000);  // TBD do this only once, not per rule

    FragBitsData ds_ptr;
    memset(&ds_ptr, 0, sizeof(ds_ptr));
    fragbits_parse(data, &ds_ptr);

    return new FragBitsOption(ds_ptr);
}

static void fragbits_dtor(IpsOption* p)
{
    delete p;
}

static void fragbits_ginit(SnortConfig*)
{
#ifdef PERF_PROFILING
    RegisterOtnProfile(s_name, &fragBitsPerfStats, fb_get_profile);
#endif
}

static const IpsApi fragbits_api =
{
    {
        PT_IPS_OPTION,
        s_name,
        IPSAPI_PLUGIN_V0,
        0,
        nullptr,
        nullptr
    },
    OPT_TYPE_DETECTION,
    1, 0,
    fragbits_ginit,
    nullptr,
    nullptr,
    nullptr,
    fragbits_ctor,
    fragbits_dtor,
    nullptr
};

#ifdef BUILDING_SO
SO_PUBLIC const BaseApi* snort_plugins[] =
{
    &fragbits_api.base,
    nullptr
};
#else
const BaseApi* ips_fragbits = &fragbits_api.base;
#endif
