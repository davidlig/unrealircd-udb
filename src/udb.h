/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Public Module Header & Constants
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

#ifndef UDB_H
#define UDB_H

#define UDB_VERSION "4.0.0"

/* ========================================================================
 * Block Identifiers
 * ======================================================================== */
#define UDB_BLOCK_NICKS    'N'
#define UDB_BLOCK_CHANNELS 'C'
#define UDB_BLOCK_IPS      'I'
#define UDB_BLOCK_SETTINGS 'S'
#define UDB_BLOCK_LINKS    'L'
#define UDB_BLOCK_LINES    'K'

#define UDB_NUM_BLOCKS 6

/* ========================================================================
 * Sub-record Keys
 * ======================================================================== */

/* Nick sub-records: N::<nick>::<key> <value> */
#define NKEY_ACCESS    "access"    /* IP/CIDR access restriction */
#define NKEY_PASS      "pass"      /* Password hash */
#define NKEY_VHOST     "vhost"     /* Virtual host */
#define NKEY_FORBID    "forbid"    /* Forbidden nick (value = reason) */
#define NKEY_SUSPENDED "suspended" /* Suspended nick (value = reason) */
#define NKEY_OPER      "oper"      /* Operclass name string (e.g. "locop", "netadmin-with-override") */
#define NKEY_CHALLENGE "challenge" /* Password hash method */
#define NKEY_MODES     "modes"     /* Allowed oper modes */
#define NKEY_SNOMASKS  "snomasks"  /* Allowed snomasks */
#define NKEY_SWHOIS    "swhois"    /* Custom SWHOIS line */

/* Channel sub-records: C::<#chan>::<key> <value> */
#define CKEY_FOUNDER    "founder"    /* Founder nick */
#define CKEY_MODES      "modes"      /* Locked channel modes */
#define CKEY_MLOCK      "mlock"      /* Channel mode lock flag (*1 = locked, *0 = unlocked) */
#define CKEY_TOPICLOCK  "topiclock"  /* Channel topic lock flag (*1 = locked, *0 = unlocked) */
#define CKEY_TOPIC      "topic"      /* Persistent topic */
#define CKEY_ACCESS     "access"     /* Access list (has sub-records per nick) */
#define CKEY_FORBID     "forbid"     /* Forbidden channel (value = reason) */
#define CKEY_SUSPENDED  "suspended"  /* Suspended channel */
#define CKEY_PASS       "pass"       /* Channel password for +ao */
#define CKEY_CHALLENGE  "challenge"  /* Channel password hash method */
#define CKEY_OPTIONS    "options"    /* Channel option flags (*N) */
#define CKEY_PERSISTENT "persistent" /* Keep the channel alive through native +P (*1 or *0) */

/* IP sub-records: I::<ip|host>::<key> <value> */
#define IKEY_CLONES  "clones"  /* Max clones allowed (*N) */
#define IKEY_NOLINES "nolines" /* Ban exception types (eg. GZQSTmc) */
#define IKEY_HOST    "host"    /* Reverse DNS override */

/* Settings sub-records: S::<key> <value> */
#define SKEY_CRYPT_KEY   "encryption_key" /* Host cloaking key */
#define SKEY_SUFFIX      "suffix"         /* Virtual host suffix */
#define SKEY_NICKSERV    "nickserv"       /* NickServ bot mask */
#define SKEY_CHANSERV    "chanserv"       /* ChanServ bot mask */
#define SKEY_IPSERV      "ipserv"         /* IpServ bot mask */
#define SKEY_CLONES      "clones"         /* Global max clones (*N) */
#define SKEY_QUIT_IPS    "quit_ips"       /* Quit message for IP limit */
#define SKEY_QUIT_CLONES "quit_clones"    /* Quit message for clone limit */
#define SKEY_CHALLENGE   "challenge"      /* Global hash method */
#define SKEY_FLOOD       "flood"          /* Password flood limit V:S */
#define SKEY_PROPAGATOR  "propagator"     /* Cluster authoritative propagator(s) */

/* Link sub-records: L::<server>::<key> <value> */
#define LKEY_OPTIONS "options" /* Link option flags (*N) */

/* Line sub-records: K::<type>::<pattern>::<key> <value> */
#define KKEY_TYPE     "type"     /* Spamfilter target type */
#define KKEY_ACTION   "action"   /* Spamfilter action */
#define KKEY_DURATION "duration" /* TKL duration */
#define KKEY_REASON   "reason"   /* Ban reason */

/* Spamfilter pattern encoding: K::F::b64:<RFC 4648 base64>::... */
#define UDB_SPAMFILTER_B64_PREFIX  "b64:"
#define UDB_SPAMFILTER_PATTERN_MAX 3072

/* ========================================================================
 * Error Codes (for DB ERR protocol messages)
 * ======================================================================== */
#define UDB_ERR_NO_BLOCK    1  /* Block does not exist */
#define UDB_ERR_OFFSET      2  /* Data offset mismatch */
#define UDB_ERR_NOT_HUB     3  /* Only hub can insert/delete */
#define UDB_ERR_PARAMS      4  /* Missing parameters */
#define UDB_ERR_CANNOT_OPEN 5  /* Cannot open block file */
#define UDB_ERR_FATAL       6  /* Fatal / internal error */
#define UDB_ERR_SYNC_ACTIVE 7  /* Sync already in progress */
#define UDB_ERR_NO_SYNC     8  /* No sync was requested */
#define UDB_ERR_FORBIDDEN   9  /* Forbidden server */
#define UDB_ERR_DUPLICATE   10 /* Duplicate record */

/* SHA-256 is deliberately handled by UDB, not Auth_Check(). */
#define UDB_AUTHTYPE_SHA256 1001


/* ========================================================================
 * Channel Option Flags (bitmask in C::<#chan>::options *<value>)
 * ======================================================================== */
#define UDB_CHOPT_PROTECT_BANS 0x1 /* Only ban author can remove their bans */
#define UDB_CHOPT_LOCK_MODES   0x2 /* Channel modes are locked */

/* ========================================================================
 * Link Option Flags (bitmask in L::<server>::options *<value>)
 * ======================================================================== */
#define UDB_LNKOPT_DEBUG 0x1 /* Debug: receives all UDB mode changes */

#endif /* UDB_H */
