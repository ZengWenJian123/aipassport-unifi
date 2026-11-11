#pragma once
#include <stdbool.h>
#include <string.h>
#include <strings.h>

/* User-selected display aliases only. Never change API IDs or remote names. */
static inline const char *unifi_site_display_name(const char *name)
{
    if (!name) return "";
    if (!strcmp(name, "Demo Office")) return "Demo Office";
    if (!strcmp(name, "Demo Home")) return "Demo Home";
    return name;
}

/* Default is Network's internal site label, not the console's display name. */
static inline bool unifi_site_generic_name(const char *name)
{
    return !name || !*name || !strcasecmp(name, "default") || !strcmp(name, "Local console");
}
