#ifndef DSVIEW_CLI_JSON_H
#define DSVIEW_CLI_JSON_H

#include <stdint.h>

#include <glib.h>

struct cli_support_json_value;

struct cli_support_json_value *cli_support_json_new_null(void);
struct cli_support_json_value *cli_support_json_new_object(void);
struct cli_support_json_value *cli_support_json_new_array(void);
struct cli_support_json_value *cli_support_json_new_string(const char *value);
struct cli_support_json_value *cli_support_json_new_uint64(uint64_t value);
struct cli_support_json_value *cli_support_json_new_int(int value);
struct cli_support_json_value *cli_support_json_new_bool(gboolean value);

void cli_support_json_object_set(struct cli_support_json_value *object,
			 const char *key,
			 struct cli_support_json_value *value);
void cli_support_json_object_set_string(struct cli_support_json_value *object,
				const char *key, const char *value);
void cli_support_json_object_set_uint64(struct cli_support_json_value *object,
				const char *key, uint64_t value);
void cli_support_json_object_set_int(struct cli_support_json_value *object,
			     const char *key, int value);
void cli_support_json_object_set_bool(struct cli_support_json_value *object,
			      const char *key, gboolean value);
void cli_support_json_array_append(struct cli_support_json_value *array,
			   struct cli_support_json_value *value);

char *cli_support_json_render(const struct cli_support_json_value *value);
void cli_support_json_value_free(struct cli_support_json_value *value);

int cli_support_json_write_text_file(const char *path, const char *text);
int cli_support_json_write_value_file(const char *path,
			      const struct cli_support_json_value *value);
int cli_support_json_write_envelope(const char *path, const char *command,
			gboolean success,
			const struct cli_support_json_value *result,
			const char *error_text);

#endif
