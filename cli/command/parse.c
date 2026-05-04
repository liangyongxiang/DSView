#include "parse.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void split_key_value(char *text, char **key, char **val)
{
	char *k, *v;
	char *pos;

	if (key)
		*key = NULL;
	if (val)
		*val = NULL;
	if (!text || !*text)
		return;

	k = text;
	v = NULL;
	pos = strchr(k, '=');
	if (pos) {
		*pos = '\0';
		v = ++pos;
	}
	if (key)
		*key = k;
	if (val)
		*val = v;
}

GHashTable *cli_command_parse_generic_arg(const char *arg,
			      gboolean sep_first,
			      const char *key_first)
{
	GHashTable *hash;
	char **elements;
	int i;
	char *k, *v;

	if (!arg || !arg[0])
		return NULL;
	if (key_first && !key_first[0])
		key_first = NULL;

	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	elements = g_strsplit(arg, ":", 0);
	i = 0;
	if (sep_first) {
		k = g_strdup("sigrok_key");
		v = g_strdup(elements[i++]);
		g_hash_table_insert(hash, k, v);
	} else if (key_first) {
		split_key_value(elements[i], &k, &v);
		if (k && g_ascii_strcasecmp(k, key_first) == 0)
			k = "sigrok_key";
		if (k) {
			k = g_strdup(k);
			v = v ? g_strdup(v) : NULL;
			g_hash_table_insert(hash, k, v);
			i++;
		}
	}
	for (; elements[i]; i++) {
		if (!elements[i][0])
			continue;
		split_key_value(elements[i], &k, &v);
		if (!k || !*k)
			continue;
		k = g_strdup(k);
		v = v ? g_strdup(v) : NULL;
		g_hash_table_insert(hash, k, v);
	}
	g_strfreev(elements);

	return hash;
}

void cli_command_parse_driver_spec(const char *driver_spec,
		       char *driver_name, size_t driver_name_sz,
		       char *conn, size_t conn_sz)
{
	GHashTable *args;
	char *text;

	driver_name[0] = '\0';
	if (conn && conn_sz > 0)
		conn[0] = '\0';
	if (!driver_spec || !*driver_spec)
		return;

	args = cli_command_parse_generic_arg(driver_spec, TRUE, NULL);
	if (!args)
		return;

	text = g_hash_table_lookup(args, "sigrok_key");
	if (text)
		snprintf(driver_name, driver_name_sz, "%s", text);
	if (conn && conn_sz > 0) {
		text = g_hash_table_lookup(args, "conn");
		if (text)
			snprintf(conn, conn_sz, "%s", text);
	}

	g_hash_table_destroy(args);
}
