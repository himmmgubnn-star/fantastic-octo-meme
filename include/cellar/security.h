/*
 * security.h — a compatibility representation of the Windows security model.
 *
 * Access tokens, users, groups, security descriptors, ACLs, privileges and
 * impersonation. Cellar does *not* reproduce the Windows security architecture
 * perfectly: SIDs are translated onto Linux uid/gid semantics so the host
 * remains the authority.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_SECURITY_H
#define CELLAR_SECURITY_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

#define CELLAR_PRIV_BACKUP   (1u << 0)
#define CELLAR_PRIV_RESTORE  (1u << 1)
#define CELLAR_PRIV_DEBUG    (1u << 2)
#define CELLAR_PRIV_SHUTDOWN (1u << 3)
#define CELLAR_PRIV_ADMIN    (1u << 4)

/* FILE_GENERIC_* style masks used by ACL checks. */
#define CELLAR_ACCESS_READ    (1u << 0)
#define CELLAR_ACCESS_WRITE   (1u << 1)
#define CELLAR_ACCESS_EXECUTE (1u << 2)
#define CELLAR_ACCESS_ALL     (CELLAR_ACCESS_READ | CELLAR_ACCESS_WRITE | \
                               CELLAR_ACCESS_EXECUTE)

typedef struct cellar_sid {
    uint8_t  revision;
    uint8_t  sub_count;
    uint32_t authority;
    uint32_t sub[8];
} cellar_sid_t;

typedef struct cellar_token {
    cellar_sid_t user;
    cellar_sid_t groups[16];
    size_t       group_count;
    uint32_t     privileges;
    int          impersonating;
    cellar_sid_t impersonated;
} cellar_token_t;

typedef struct cellar_ace {
    int          allow; /* 1 = allow, 0 = deny */
    cellar_sid_t sid;
    uint32_t     mask;
} cellar_ace_t;

typedef struct cellar_acl {
    cellar_ace_t aces[16];
    size_t       count;
} cellar_acl_t;

typedef struct cellar_sd {
    cellar_sid_t owner;
    cellar_sid_t group;
    cellar_acl_t dacl;
} cellar_sd_t;

void cellar_sid_make(cellar_sid_t *s, uint32_t authority, uint32_t rid);
int  cellar_sid_eq(const cellar_sid_t *a, const cellar_sid_t *b);
uint32_t cellar_sid_rid(const cellar_sid_t *s);
uint32_t cellar_sid_to_uid(const cellar_sid_t *s); /* SYSTEM(18) → 0 */

void cellar_token_default(cellar_token_t *t); /* current user, no extra privs */
cellar_status_t cellar_token_impersonate(cellar_token_t *t, const cellar_sid_t *s);
void cellar_token_revert(cellar_token_t *t);

cellar_status_t cellar_acl_add(cellar_acl_t *acl, int allow,
                               const cellar_sid_t *sid, uint32_t mask);
/* Deny ACEs win. Empty DACL → deny. */
int cellar_acl_check(const cellar_sd_t *sd, const cellar_token_t *tok,
                     uint32_t mask);

#endif /* CELLAR_SECURITY_H */
