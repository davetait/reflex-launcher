#ifndef INI_DOC_H
#define INI_DOC_H
#include <stdbool.h>
#include <stddef.h>

// A line-preserving INI document. It follows inih's parsing rules so that
// ini_doc_get() returns what the launcher would load, and it writes back
// every comment, blank line, and unknown key unchanged.
typedef struct ini_doc IniDoc;

IniDoc *ini_doc_parse(const char *text);
IniDoc *ini_doc_load(const char *path);
char *ini_doc_serialize(const IniDoc *doc);
int ini_doc_save(const IniDoc *doc, const char *path);
void ini_doc_free(IniDoc *doc);

const char *ini_doc_get(const IniDoc *doc, const char *section, const char *key);
bool ini_doc_set(IniDoc *doc, const char *section, const char *key, const char *value);
void ini_doc_unset(IniDoc *doc, const char *section, const char *key);
bool ini_doc_value_is_representable(const char *value);

size_t ini_doc_get_list(const IniDoc *doc, const char *section, const char ***values);
bool ini_doc_set_list(IniDoc *doc, const char *section, const char *prefix,
                      const char *const *values, size_t count);

size_t ini_doc_sections(const IniDoc *doc, const char ***names);
bool ini_doc_has_section(const IniDoc *doc, const char *section);
void ini_doc_add_section(IniDoc *doc, const char *section);
void ini_doc_remove_section(IniDoc *doc, const char *section);
void ini_doc_rename_section(IniDoc *doc, const char *from, const char *to);

size_t ini_doc_dirty_count(const IniDoc *doc);
void ini_doc_dirty_get(const IniDoc *doc, size_t index, const char **section, const char **key);
bool ini_doc_changed_on_disk(const IniDoc *doc, const char *path);
void ini_doc_mark_saved(IniDoc *doc, const char *path);

#endif
