/*
 * UDB 4 - Unreal Database System for UnrealIRCd 6
 * Subsystem: Canonical Wire Paths and Secret Classification
 *
 * Author: David Abuín Fontán ('davidlig') <https://github.com/davidlig/unrealircd-udb>
 * Based on the original UDB concept by Trocotronic.
 *
 * (C) 2026 David Abuín Fontán
 * License: GNU General Public License v2+
 */

/* Canonical path components */
static int udb_path_encode_component(const char *raw, char *buf, size_t bufsz)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *p;
	size_t out_len = 0;

	if (!raw || !buf || bufsz == 0)
		return 0;

	for (p = (const unsigned char *)raw; *p; p++)
	{
		if (*p == ':' || *p == '%' || *p <= 32 || *p >= 127)
		{
			if (out_len + 3 >= bufsz)
				return 0;
			buf[out_len++] = '%';
			buf[out_len++] = hex[(*p >> 4) & 0x0F];
			buf[out_len++] = hex[*p & 0x0F];
		}
		else
		{
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)*p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_decode_component(const char *encoded, char *buf, size_t bufsz)
{
	const char *p;
	size_t out_len = 0;

	if (!encoded || !buf || bufsz == 0)
		return 0;

	for (p = encoded; *p; p++)
	{
		if (*p == '%')
		{
			int high, low;
			if (!p[1] || !p[2])
				return 0;
			if (p[1] >= '0' && p[1] <= '9')
				high = p[1] - '0';
			else if (p[1] >= 'a' && p[1] <= 'f')
				high = p[1] - 'a' + 10;
			else if (p[1] >= 'A' && p[1] <= 'F')
				high = p[1] - 'A' + 10;
			else
				return 0;

			if (p[2] >= '0' && p[2] <= '9')
				low = p[2] - '0';
			else if (p[2] >= 'a' && p[2] <= 'f')
				low = p[2] - 'a' + 10;
			else if (p[2] >= 'A' && p[2] <= 'F')
				low = p[2] - 'A' + 10;
			else
				return 0;

			unsigned char val = (unsigned char)((high << 4) | low);
			if (val == 0) /* Embedded null byte is rejected */
				return 0;
			/* Canonicality check: reject non-canonical percent-encoding when
			 * udb_path_encode_component() would have emitted the byte literally */
			if (val != ':' && val != '%' && val > 32 && val < 127)
				return 0;
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = (char)val;
			p += 2;
		}
		else
		{
			/* A raw colon could be mistaken for :: path syntax.  The encoder
			 * always writes it as %3A, so reject the non-canonical spelling. */
			if (*p == ':')
				return 0;
			if (out_len + 1 >= bufsz)
				return 0;
			buf[out_len++] = *p;
		}
	}
	buf[out_len] = '\0';
	return 1;
}

static int udb_path_foreach(const char *path, unsigned int max_components, UdbPathComponentFunc fn, void *data)
{
	char encoded[UDB_COMPONENT_ENCODED_MAX + 1];
	char decoded[UDB_COMPONENT_RAW_MAX + 1];
	const char *component;
	const char *separator;
	unsigned int index = 0;

	if (!path || !*path)
		return 0;
	component = path;
	for (;;)
	{
		size_t length;

		separator = strstr(component, "::");
		if (separator == component || (separator && !separator[2]))
			return 0;
		length = separator ? (size_t)(separator - component) : strlen(component);
		if (length > UDB_COMPONENT_ENCODED_MAX)
			return 0;
		memcpy(encoded, component, length);
		encoded[length] = '\0';
		if (!udb_path_decode_component(encoded, decoded, sizeof(decoded)))
			return 0;
		if (index >= max_components || !fn(decoded, index, data))
			return 0;
		index++;
		if (!separator)
			break;
		component = separator + 2;
	}
	return (int)index;
}

static int udb_secret_key_is_reserved(const char *key)
{
	return key && (!strcasecmp(key, NKEY_PASS) || !strcasecmp(key, SKEY_CRYPT_KEY) || !strcasecmp(key, CKEY_MODES));
}

static int udb_secret_key_matches(char block_letter, unsigned int depth, const char *key)
{
	if (!key)
		return 0;
	if (!strcasecmp(key, NKEY_PASS))
		return block_letter == UDB_BLOCK_NICKS && depth == 2;
	if (!strcasecmp(key, SKEY_CRYPT_KEY))
		return block_letter == UDB_BLOCK_SETTINGS && depth == 1;
	if (!strcasecmp(key, CKEY_MODES))
		return block_letter == UDB_BLOCK_CHANNELS && depth == 2;
	return 0;
}

typedef struct UdbSecretPathState
{
	char last[UDB_COMPONENT_RAW_MAX + 1];
	unsigned int depth;
} UdbSecretPathState;

static int udb_path_secret_component(const char *decoded, unsigned int index, void *data)
{
	UdbSecretPathState *state = data;

	strlcpy(state->last, decoded, sizeof(state->last));
	state->depth = index + 1;
	return 1;
}

static int udb_path_value_is_secret(const char *path)
{
	UdbSecretPathState state;

	if (!path || path[0] == '\0' || path[1] != ':' || path[2] != ':')
		return 0;
	memset(&state, 0, sizeof(state));
	if (!udb_path_foreach(path + 3, UDB_PATH_MAX_COMPONENTS, udb_path_secret_component, &state))
		return 0;
	return udb_secret_key_matches(path[0], state.depth, state.last);
}
