/*
 * security.h — a compatibility representation of the Windows security model.
 *
 * Access tokens, users, groups, security descriptors, ACLs, privileges and
 * impersonation. Airlock does *not* reproduce the Windows security architecture
 * perfectly: SIDs are translated onto Linux uid/gid semantics so the host
 * remains the authority.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_SECURITY_H
#define AIRLOCK_SECURITY_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

#define AIRLOCK_PRIV_BACKUP   (1u << 0)
#define AIRLOCK_PRIV_RESTORE  (1u << 1)
#define AIRLOCK_PRIV_DEBUG    (1u << 2)
#define AIRLOCK_PRIV_SHUTDOWN (1u << 3)
#define AIRLOCK_PRIV_ADMIN    (1u << 4)

/* FILE_GENERIC_* style masks used by ACL checks. */
#define AIRLOCK_ACCESS_READ    (1u << 0)
#define AIRLOCK_ACCESS_WRITE   (1u << 1)
#define AIRLOCK_ACCESS_EXECUTE (1u << 2)
#define AIRLOCK_ACCESS_ALL     (AIRLOCK_ACCESS_READ | AIRLOCK_ACCESS_WRITE | \
                               AIRLOCK_ACCESS_EXECUTE)

typedef struct airlock_sid {
    uint8_t  revision;
    uint8_t  sub_count;
    uint32_t authority;
    uint32_t sub[8];
} airlock_sid_t;

typedef struct airlock_token {
    airlock_sid_t user;
    airlock_sid_t groups[16];
    size_t       group_count;
    uint32_t     privileges;
    int          impersonating;
    airlock_sid_t impersonated;
} airlock_token_t;

typedef struct airlock_ace {
    int          allow; /* 1 = allow, 0 = deny */
    airlock_sid_t sid;
    uint32_t     mask;
} airlock_ace_t;

typedef struct airlock_acl {
    airlock_ace_t aces[16];
    size_t       count;
} airlock_acl_t;

typedef struct airlock_sd {
    airlock_sid_t owner;
    airlock_sid_t group;
    airlock_acl_t dacl;
} airlock_sd_t;

void airlock_sid_make(airlock_sid_t *s, uint32_t authority, uint32_t rid);
int  airlock_sid_eq(const airlock_sid_t *a, const airlock_sid_t *b);
uint32_t airlock_sid_rid(const airlock_sid_t *s);
uint32_t airlock_sid_to_uid(const airlock_sid_t *s); /* SYSTEM(18) → 0 */

void airlock_token_default(airlock_token_t *t); /* current user, no extra privs */
airlock_status_t airlock_token_impersonate(airlock_token_t *t, const airlock_sid_t *s);
void airlock_token_revert(airlock_token_t *t);

airlock_status_t airlock_acl_add(airlock_acl_t *acl, int allow,
                               const airlock_sid_t *sid, uint32_t mask);
/* Deny ACEs win. Empty DACL → deny. */
int airlock_acl_check(const airlock_sd_t *sd, const airlock_token_t *tok,
                     uint32_t mask);

#endif /* AIRLOCK_SECURITY_H */
