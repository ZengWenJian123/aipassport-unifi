#pragma once
#include <stdbool.h>
#include <string.h>
#include <strings.h>

/* Preserve API names for every account; no owner-specific display aliases. */
static inline const char *unifi_site_display_name(const char *name)
{
    if (!name) return "";
    return name;
}

/* Default is Network's internal site label, not the console's display name. */
static inline bool unifi_site_generic_name(const char *name)
{
    return !name || !*name || !strcasecmp(name, "default") || !strcmp(name, "Local console");
}
