#include "json.h"

#include <stdio.h>

enum cli_support_json_kind {
	CLI_SUPPORT_JSON_KIND_NULL = 0,
	CLI_SUPPORT_JSON_KIND_OBJECT,
	CLI_SUPPORT_JSON_KIND_ARRAY,
	CLI_SUPPORT_JSON_KIND_STRING,
	CLI_SUPPORT_JSON_KIND_UINT64,
	CLI_SUPPORT_JSON_KIND_INT,
	CLI_SUPPORT_JSON_KIND_BOOL,
};

struct cli_support_json_member {
	char *key;
	struct cli_support_json_value *value;
};

struct cli_support_json_value {
	enum cli_support_json_kind kind;
	union {
		GPtrArray *members;
		GPtrArray *items;
		char *string_value;
		uint64_t uint64_value;
		int int_value;
		gboolean bool_value;
	} data;
};

static void append_escaped_text(GString *out, const char *s)
{
	const unsigned char *p = (const unsigned char *)(s ? s : "");

	for (; *p; p++) {
		switch (*p) {
		case '\\':
			g_string_append(out, "\\\\");
			break;
		case '"':
			g_string_append(out, "\\\"");
			break;
		case '\b':
			g_string_append(out, "\\b");
			break;
		case '\f':
			g_string_append(out, "\\f");
			break;
		case '\n':
			g_string_append(out, "\\n");
			break;
		case '\r':
			g_string_append(out, "\\r");
			break;
		case '\t':
			g_string_append(out, "\\t");
			break;
		default:
			if (*p < 0x20)
				g_string_append_printf(out, "\\u%04x", *p);
			else
				g_string_append_c(out, (char)*p);
			break;
		}
	}
}

static struct cli_support_json_value *alloc_value(enum cli_support_json_kind kind)
{
	struct cli_support_json_value *value;

	value = g_new0(struct cli_support_json_value, 1);
	if (!value)
		return NULL;
	value->kind = kind;
	return value;
}

static void free_member(gpointer data)
{
	struct cli_support_json_member *member =
		(struct cli_support_json_member *)data;

	if (!member)
		return;

	g_free(member->key);
	cli_support_json_value_free(member->value);
	g_free(member);
}

static void append_rendered_value(GString *out,
				  const struct cli_support_json_value *value)
{
	if (!value) {
		g_string_append(out, "null");
		return;
	}

	switch (value->kind) {
	case CLI_SUPPORT_JSON_KIND_NULL:
		g_string_append(out, "null");
		break;
	case CLI_SUPPORT_JSON_KIND_OBJECT:
		g_string_append_c(out, '{');
		for (guint i = 0; value->data.members && i < value->data.members->len;
		     i++) {
			const struct cli_support_json_member *member =
				(const struct cli_support_json_member *)
					g_ptr_array_index(value->data.members, i);

			if (i > 0)
				g_string_append_c(out, ',');
			g_string_append_c(out, '"');
			append_escaped_text(out, member && member->key ?
					    member->key : "");
			g_string_append(out, "\":");
			append_rendered_value(out, member ? member->value : NULL);
		}
		g_string_append_c(out, '}');
		break;
	case CLI_SUPPORT_JSON_KIND_ARRAY:
		g_string_append_c(out, '[');
		for (guint i = 0; value->data.items && i < value->data.items->len; i++) {
			if (i > 0)
				g_string_append_c(out, ',');
			append_rendered_value(
				out,
				(const struct cli_support_json_value *)
					g_ptr_array_index(value->data.items, i));
		}
		g_string_append_c(out, ']');
		break;
	case CLI_SUPPORT_JSON_KIND_STRING:
		g_string_append_c(out, '"');
		append_escaped_text(out, value->data.string_value);
		g_string_append_c(out, '"');
		break;
	case CLI_SUPPORT_JSON_KIND_UINT64:
		g_string_append_printf(out, "%llu",
				       (unsigned long long)value->data.uint64_value);
		break;
	case CLI_SUPPORT_JSON_KIND_INT:
		g_string_append_printf(out, "%d", value->data.int_value);
		break;
	case CLI_SUPPORT_JSON_KIND_BOOL:
		g_string_append(out, value->data.bool_value ? "true" : "false");
		break;
	default:
		g_string_append(out, "null");
		break;
	}
}

struct cli_support_json_value *cli_support_json_new_null(void)
{
	return alloc_value(CLI_SUPPORT_JSON_KIND_NULL);
}

struct cli_support_json_value *cli_support_json_new_object(void)
{
	struct cli_support_json_value *value =
		alloc_value(CLI_SUPPORT_JSON_KIND_OBJECT);

	if (!value)
		return NULL;
	value->data.members = g_ptr_array_new_with_free_func(free_member);
	if (!value->data.members) {
		g_free(value);
		return NULL;
	}
	return value;
}

struct cli_support_json_value *cli_support_json_new_array(void)
{
	struct cli_support_json_value *value =
		alloc_value(CLI_SUPPORT_JSON_KIND_ARRAY);

	if (!value)
		return NULL;
	value->data.items = g_ptr_array_new_with_free_func(
		(GDestroyNotify)cli_support_json_value_free);
	if (!value->data.items) {
		g_free(value);
		return NULL;
	}
	return value;
}

struct cli_support_json_value *cli_support_json_new_string(const char *value)
{
	struct cli_support_json_value *json_value =
		alloc_value(CLI_SUPPORT_JSON_KIND_STRING);

	if (!json_value)
		return NULL;
	json_value->data.string_value = g_strdup(value ? value : "");
	if (!json_value->data.string_value) {
		g_free(json_value);
		return NULL;
	}
	return json_value;
}

struct cli_support_json_value *cli_support_json_new_uint64(uint64_t value)
{
	struct cli_support_json_value *json_value =
		alloc_value(CLI_SUPPORT_JSON_KIND_UINT64);

	if (!json_value)
		return NULL;
	json_value->data.uint64_value = value;
	return json_value;
}

struct cli_support_json_value *cli_support_json_new_int(int value)
{
	struct cli_support_json_value *json_value =
		alloc_value(CLI_SUPPORT_JSON_KIND_INT);

	if (!json_value)
		return NULL;
	json_value->data.int_value = value;
	return json_value;
}

struct cli_support_json_value *cli_support_json_new_bool(gboolean value)
{
	struct cli_support_json_value *json_value =
		alloc_value(CLI_SUPPORT_JSON_KIND_BOOL);

	if (!json_value)
		return NULL;
	json_value->data.bool_value = value;
	return json_value;
}

void cli_support_json_object_set(struct cli_support_json_value *object,
			 const char *key,
			 struct cli_support_json_value *value)
{
	struct cli_support_json_member *member;

	if (!object || object->kind != CLI_SUPPORT_JSON_KIND_OBJECT ||
	    !object->data.members) {
		cli_support_json_value_free(value);
		return;
	}

	member = g_new0(struct cli_support_json_member, 1);
	if (!member) {
		cli_support_json_value_free(value);
		return;
	}

	member->key = g_strdup(key ? key : "");
	if (!member->key) {
		cli_support_json_value_free(value);
		g_free(member);
		return;
	}

	member->value = value ? value : cli_support_json_new_null();
	g_ptr_array_add(object->data.members, member);
}

void cli_support_json_object_set_string(struct cli_support_json_value *object,
				const char *key, const char *value)
{
	cli_support_json_object_set(object, key,
				    cli_support_json_new_string(value));
}

void cli_support_json_object_set_uint64(struct cli_support_json_value *object,
				const char *key, uint64_t value)
{
	cli_support_json_object_set(object, key,
				    cli_support_json_new_uint64(value));
}

void cli_support_json_object_set_int(struct cli_support_json_value *object,
			     const char *key, int value)
{
	cli_support_json_object_set(object, key,
				    cli_support_json_new_int(value));
}

void cli_support_json_object_set_bool(struct cli_support_json_value *object,
			      const char *key, gboolean value)
{
	cli_support_json_object_set(object, key,
				    cli_support_json_new_bool(value));
}

void cli_support_json_array_append(struct cli_support_json_value *array,
			   struct cli_support_json_value *value)
{
	if (!array || array->kind != CLI_SUPPORT_JSON_KIND_ARRAY ||
	    !array->data.items) {
		cli_support_json_value_free(value);
		return;
	}

	g_ptr_array_add(array->data.items,
			value ? value : cli_support_json_new_null());
}

char *cli_support_json_render(const struct cli_support_json_value *value)
{
	GString *out = g_string_new(NULL);
	char *text;

	if (!value)
		return NULL;
	if (!out)
		return NULL;

	append_rendered_value(out, value);
	text = g_string_free(out, FALSE);
	return text;
}

void cli_support_json_value_free(struct cli_support_json_value *value)
{
	if (!value)
		return;

	switch (value->kind) {
	case CLI_SUPPORT_JSON_KIND_OBJECT:
		if (value->data.members)
			g_ptr_array_free(value->data.members, TRUE);
		break;
	case CLI_SUPPORT_JSON_KIND_ARRAY:
		if (value->data.items)
			g_ptr_array_free(value->data.items, TRUE);
		break;
	case CLI_SUPPORT_JSON_KIND_STRING:
		g_free(value->data.string_value);
		break;
	default:
		break;
	}

	g_free(value);
}

int cli_support_json_write_text_file(const char *path, const char *text)
{
	FILE *f;

	if (!path || !*path)
		return 0;

	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "failed to open JSON output: %s\n", path);
		return -1;
	}

	if (text && *text)
		fputs(text, f);
	fclose(f);
	return 0;
}

int cli_support_json_write_value_file(const char *path,
			      const struct cli_support_json_value *value)
{
	char *text;
	int rc;

	text = cli_support_json_render(value);
	if (!text)
		return -1;

	rc = cli_support_json_write_text_file(path, text);
	g_free(text);
	return rc;
}

int cli_support_json_write_envelope(const char *path, const char *command,
			gboolean success,
			const struct cli_support_json_value *result,
			const char *error_text)
{
	GString *json;
	int rc;

	if (!path || !*path)
		return 0;

	json = g_string_new("{\"command\":\"");
	if (!json)
		return -1;

	append_escaped_text(json, command ? command : "");
	g_string_append(json, "\",\"success\":");
	g_string_append(json, success ? "true" : "false");

	if (success && result) {
		g_string_append(json, ",\"result\":");
		append_rendered_value(json, result);
	} else if (!success) {
		g_string_append(json, ",\"error\":\"");
		append_escaped_text(json, error_text ? error_text : "unknown error");
		g_string_append_c(json, '"');
	}

	g_string_append(json, "}\n");
	rc = cli_support_json_write_text_file(path, json->str);
	g_string_free(json, TRUE);
	return rc;
}
