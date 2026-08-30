/*
 * security.c — Windows security model, translated onto Linux uids.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/security.h"

void airlock_sid_make(airlock_sid_t *s, uint32_t authority, uint32_t rid)
{
    if (!s)
        return;
    memset(s, 0, sizeof *s);
    s->revision = 1;
    s->sub_count = 1;
    s->authority = authority;
    s->sub[0] = rid;
}

int airlock_sid_eq(const airlock_sid_t *a, const airlock_sid_t *b)
{
    uint8_t i;
    if (!a || !b)
        return 0;
    if (a->revision != b->revision || a->sub_count != b->sub_count ||
        a->authority != b->authority)
        return 0;
    for (i = 0; i < a->sub_count && i < 8; i++)
        if (a->sub[i] != b->sub[i])
            return 0;
    return 1;
}

uint32_t airlock_sid_rid(const airlock_sid_t *s)
{
    if (!s || s->sub_count == 0)
        return 0;
    return s->sub[s->sub_count - 1];
}

uint32_t airlock_sid_to_uid(const airlock_sid_t *s)
{
    uint32_t rid = airlock_sid_rid(s);
    /* NT AUTHORITY\SYSTEM is RID 18 — map to root. Everyone else keeps the RID
     * so a typical Windows user SID ...-1000 lands on Linux uid 1000. */
    if (s && s->authority == 5 && rid == 18)
        return 0;
    return rid;
}

void airlock_token_default(airlock_token_t *t)
{
    if (!t)
        return;
    memset(t, 0, sizeof *t);
    airlock_sid_make(&t->user, 5, 1000);
    airlock_sid_make(&t->groups[0], 5, 513); /* Domain Users */
    t->group_count = 1;
    t->privileges = 0;
}

airlock_status_t airlock_token_impersonate(airlock_token_t *t, const airlock_sid_t *s)
{
    if (!t || !s)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    t->impersonated = *s;
    t->impersonating = 1;
    return AIRLOCK_OK;
}

void airlock_token_revert(airlock_token_t *t)
{
    if (!t)
        return;
    t->impersonating = 0;
    memset(&t->impersonated, 0, sizeof t->impersonated);
}

airlock_status_t airlock_acl_add(airlock_acl_t *acl, int allow,
                               const airlock_sid_t *sid, uint32_t mask)
{
    if (!acl || !sid)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (acl->count >= 16)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    acl->aces[acl->count].allow = allow ? 1 : 0;
    acl->aces[acl->count].sid = *sid;
    acl->aces[acl->count].mask = mask;
    acl->count++;
    return AIRLOCK_OK;
}

static int token_has_sid(const airlock_token_t *tok, const airlock_sid_t *sid)
{
    size_t i;
    const airlock_sid_t *user = (tok->impersonating) ? &tok->impersonated
                                                    : &tok->user;
    if (airlock_sid_eq(user, sid))
        return 1;
    for (i = 0; i < tok->group_count; i++)
        if (airlock_sid_eq(&tok->groups[i], sid))
            return 1;
    return 0;
}

int airlock_acl_check(const airlock_sd_t *sd, const airlock_token_t *tok,
                     uint32_t mask)
{
    size_t i;
    int allowed = 0;
    if (!sd || !tok)
        return 0;
    if (sd->dacl.count == 0)
        return 0; /* empty DACL → deny */
    for (i = 0; i < sd->dacl.count; i++) {
        const airlock_ace_t *ace = &sd->dacl.aces[i];
        if (!token_has_sid(tok, &ace->sid))
            continue;
        if ((ace->mask & mask) != mask)
            continue;
        if (!ace->allow)
            return 0; /* deny wins */
        allowed = 1;
    }
    return allowed;
}
