#include <string.h>
#include <glib.h>
#include <gtk/gtkmarshal.h>

#include "gwycontainer.h"
#include "gwyserializable.h"
#include "gwywatchable.h"

#define GWY_CONTAINER_TYPE_NAME "GwyContainer"

typedef struct {
    guchar *buffer;
    gsize size;
} SerializeData;

static void     gwy_container_serializable_init  (gpointer giface);
static void     gwy_container_class_init         (GwyContainerClass *klass);
static void     gwy_container_init               (GwyContainer *container);
static void     value_destroy_func               (gpointer data);
static void     gwy_container_finalize           (GObject *obj);
static gboolean gwy_container_try_set_one        (GwyContainer *container,
                                                  GQuark key,
                                                  GValue *value,
                                                  gboolean do_replace,
                                                  gboolean do_create);
static void     gwy_container_try_setv           (GwyContainer *container,
                                                  gsize nvalues,
                                                  GwyKeyVal *values,
                                                  gboolean do_replace,
                                                  gboolean do_create);
static void     gwy_container_try_set_valist     (GwyContainer *container,
                                                  va_list ap,
                                                  gboolean do_replace,
                                                  gboolean do_create);
static void     gwy_container_set_by_name_valist (GwyContainer *container,
                                                  va_list ap,
                                                  gboolean do_replace,
                                                  gboolean do_create);
static guchar*  gwy_container_serialize          (GObject *obj,
                                                  guchar *buffer,
                                                  gsize *size);
static void     hash_serialize_func              (gpointer hkey,
                                                  gpointer hvalue,
                                                  gpointer hdata);
static GObject* gwy_container_deserialize        (const guchar *buffer,
                                                  gsize size,
                                                  gsize *position);



GType
gwy_container_get_type(void)
{
    static GType gwy_container_type = 0;

    if (!gwy_container_type) {
        static const GTypeInfo gwy_container_info = {
            sizeof(GwyContainerClass),
            NULL,
            NULL,
            (GClassInitFunc)gwy_container_class_init,
            NULL,
            NULL,
            sizeof(GwyContainer),
            0,
            (GInstanceInitFunc)gwy_container_init,
            NULL,
        };

        GInterfaceInfo gwy_serializable_info = {
            (GInterfaceInitFunc)gwy_container_serializable_init, NULL, 0
        };

        #ifdef DEBUG
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
        #endif
        gwy_container_type = g_type_register_static(G_TYPE_OBJECT,
                                                    GWY_CONTAINER_TYPE_NAME,
                                                    &gwy_container_info,
                                                    0);
        g_type_add_interface_static(gwy_container_type,
                                    GWY_TYPE_SERIALIZABLE,
                                    &gwy_serializable_info);
    }

    return gwy_container_type;
}

static void
gwy_container_serializable_init(gpointer giface)
{
    GwySerializableClass *iface = giface;

    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif
    /*g_assert(iface_data == GUINT_TO_POINTER(42));*/
    g_assert(G_TYPE_FROM_INTERFACE(iface) == GWY_TYPE_SERIALIZABLE);

    /* initialize stuff */
    iface->serialize = gwy_container_serialize;
    iface->deserialize = gwy_container_deserialize;
}

static void
gwy_container_class_init(GwyContainerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif

    gobject_class->finalize = gwy_container_finalize;
}

static void
gwy_container_init(GwyContainer *container)
{
    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif
    container->values = NULL;
    container->watching = NULL;
}

static void
gwy_container_finalize(GObject *obj)
{
    GwyContainer *container = (GwyContainer*)obj;

    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif

    g_hash_table_destroy(container->values);
    g_hash_table_destroy(container->watching);
}

/**
 * gwy_container_new:
 *
 * Creates a new #GwyContainer.
 *
 * Returns: The container, as a #GObject.
 **/
GObject*
gwy_container_new(void)
{
    GwyContainer *container;

    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif
    container = g_object_new(GWY_TYPE_CONTAINER, NULL);

    /* assume GQuarks are good enough hash keys */
    container->values = g_hash_table_new_full(NULL, NULL,
                                              NULL, value_destroy_func);
    container->watching = g_hash_table_new(NULL, NULL);

    return (GObject*)(container);
}

static void
value_destroy_func(gpointer data)
{
    GValue *val = (GValue*)data;

    g_value_unset(val);
    g_free(val);
}

/**
 * gwy_container_value_type:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the type of value in @container identified by @key.
 *
 * Returns: The value type as #GType.
 **/
GType
gwy_container_value_type(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(key, 0);
    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    g_return_val_if_fail(p, 0);
    return G_VALUE_TYPE(p);
}

/**
 * gwy_container_contains:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns #TRUE if @container contains a value identified by @key.
 *
 * Returns: Whether @container contains something at @key.
 **/
gboolean
gwy_container_contains(GwyContainer *container, GQuark key)
{
    g_return_val_if_fail(key, 0);
    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    return g_hash_table_lookup(container->values,
                               GUINT_TO_POINTER(key)) != NULL;
}

/**
 * gwy_container_get_value:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the value in @container identified by @key.
 *
 * Returns: The value as a #GValue.
 **/
GValue
gwy_container_get_value(GwyContainer *container, GQuark key)
{
    GValue value;
    GValue *p;

    memset(&value, 0, sizeof(value));
    g_return_val_if_fail(key, value);
    g_return_val_if_fail(GWY_IS_CONTAINER(container), value);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    g_return_val_if_fail(p, value);

    g_assert(G_IS_VALUE(p));
    g_value_init(&value, G_VALUE_TYPE(p));
    g_value_copy(p, &value);

    return value;
}

/**
 * gwy_container_get_boolean:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the boolean in @container identified by @key.
 *
 * Returns: The boolean as #gboolean.
 **/
gboolean
gwy_container_get_boolean(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_BOOLEAN(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as boolean (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_int(p);
}

/**
 * gwy_container_get_uchar:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the unsigned character in @container identified by @key.
 *
 * Returns: The character as #guchar.
 **/
guchar
gwy_container_get_uchar(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_UCHAR(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as uchar (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_uchar(p);
}

/**
 * gwy_container_get_int32:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the 32bit integer in @container identified by @key.
 *
 * Returns: The integer as #guint32.
 **/
gint32
gwy_container_get_int32(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_INT(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as int32 (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_int(p);
}

/**
 * gwy_container_get_int64:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the 64bit integer in @container identified by @key.
 *
 * Returns: The 64bit integer as #guint64.
 **/
gint64
gwy_container_get_int64(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_INT64(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as int64 (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_int64(p);
}

/**
 * gwy_container_get_double:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the double in @container identified by @key.
 *
 * Returns: The double as #gdouble.
 **/
gdouble
gwy_container_get_double(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_DOUBLE(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as double (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_double(p);
}

/**
 * gwy_container_get_string:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the string in @container identified by @key.
 *
 * The returned string should be considered constant and shouldn't be freed
 * or modified.
 *
 * Returns: The string.
 **/
G_CONST_RETURN guchar*
gwy_container_get_string(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_STRING(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as string (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_string(p);
}

/**
 * gwy_container_get_object:
 * @container: A #GwyContainer.
 * @key: A #GQuark key.
 *
 * Returns the object in @container identified by @key.
 *
 * The returned object doesn't have it's reference count increased, use
 * g_object_ref() if you want to access it even when @container may cease
 * to exist.
 *
 * Returns: The object as #GObject.
 **/
GObject*
gwy_container_get_object(GwyContainer *container, GQuark key)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), 0);
    g_return_val_if_fail(key, 0);
    p = (GValue*)g_hash_table_lookup(container->values,
                                     GUINT_TO_POINTER(key));
    if (!p) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: no value for key %u", GWY_CONTAINER_TYPE_NAME, key);
        return 0;
    }
    if (!G_VALUE_HOLDS_OBJECT(p)) {
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
              "%s: trying to get %s as object (key %u)",
              GWY_CONTAINER_TYPE_NAME, G_VALUE_TYPE_NAME(p), key);
        return 0;
    }
    return g_value_get_object(p);
}

static gboolean
gwy_container_try_set_one(GwyContainer *container,
                          GQuark key,
                          GValue *value,
                          gboolean do_replace,
                          gboolean do_create)
{
    GValue *p;

    g_return_val_if_fail(GWY_IS_CONTAINER(container), FALSE);
    g_return_val_if_fail(key, FALSE);
    g_return_val_if_fail(G_IS_VALUE(value), FALSE);

    /* Allow only some sane types to be stored, at least for now */
    if (G_VALUE_HOLDS_OBJECT(value)) {
        GObject *obj = g_value_peek_pointer(value);

        g_return_val_if_fail(GWY_IS_SERIALIZABLE(obj)
                             && GWY_IS_WATCHABLE(obj),
                             FALSE);
    }
    else {
        GType type = G_VALUE_TYPE(value);

        g_return_val_if_fail(G_TYPE_FUNDAMENTAL(type)
                             && type != G_TYPE_BOXED
                             && type != G_TYPE_POINTER
                             && type != G_TYPE_PARAM,
                             FALSE);
    }

    p = (GValue*)g_hash_table_lookup(container->values, GINT_TO_POINTER(key));
    if (p) {
        if (!do_replace)
            return FALSE;
        g_assert(G_IS_VALUE(p));
        g_value_unset(p);
    }
    else {
        if (!do_create)
            return FALSE;
        p = g_new0(GValue, 1);
        g_hash_table_insert(container->values, GINT_TO_POINTER(key), p);
    }
    g_value_init(p, G_VALUE_TYPE(value));
    g_value_copy(value, p);

    return TRUE;
}

static void
gwy_container_try_setv(GwyContainer *container,
                       gsize nvalues,
                       GwyKeyVal *values,
                       gboolean do_replace,
                       gboolean do_create)
{
    gsize i;

    for (i = 0; i < nvalues; i++)
        values[i].changed = gwy_container_try_set_one(container,
                                                      values[i].key,
                                                      values[i].value,
                                                      do_replace,
                                                      do_create);

    /* TODO:
    gwy_container_item_changed(container, key);
    */
}

static void
gwy_container_try_set_valist(GwyContainer *container,
                             va_list ap,
                             gboolean do_replace,
                             gboolean do_create)
{
    GwyKeyVal *values;
    gsize n, i;
    GQuark key;

    n = 16;
    values = g_new(GwyKeyVal, n);
    i = 0;
    key = va_arg(ap, GQuark);
    while (key) {
        if (i == n) {
            n += 16;
            values = g_renew(GwyKeyVal, values, n);
        }
        values[i].value = va_arg(ap, GValue*);
        values[i].key = key;
        values[i].changed = FALSE;
        i++;
        key = va_arg(ap, GQuark);
    }
    gwy_container_try_setv(container, i, values, do_replace, do_create);
    g_free(values);
}

void
gwy_container_set_value(GwyContainer *container,
                        ...)
{
    va_list ap;

    va_start(ap, container);
    gwy_container_try_set_valist(container, ap, TRUE, TRUE);
    va_end(ap);
}

static void
gwy_container_set_by_name_valist(GwyContainer *container,
                                 va_list ap,
                                 gboolean do_replace,
                                 gboolean do_create)
{
    GwyKeyVal *values;
    gsize n, i;
    GQuark key;
    guchar *name;

    n = 16;
    values = g_new(GwyKeyVal, n);
    i = 0;
    name = va_arg(ap, guchar*);
    while (name) {
        key = g_quark_from_string(name);
        if (i == n) {
            n += 16;
            values = g_renew(GwyKeyVal, values, n);
        }
        values[i].value = va_arg(ap, GValue*);
        values[i].key = key;
        values[i].changed = FALSE;
        i++;
        name = va_arg(ap, guchar*);
    }
    gwy_container_try_setv(container, i, values, do_replace, do_create);
    g_free(values);
}

void
gwy_container_set_value_by_name(GwyContainer *container,
                                ...)
{
    va_list ap;

    va_start(ap, container);
    gwy_container_set_by_name_valist(container, ap, TRUE, TRUE);
    va_end(ap);
}

void
gwy_container_set_boolean(GwyContainer *container,
                          GQuark key,
                          gboolean value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_BOOLEAN);
    g_value_set_boolean(&gvalue, value);
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
}

void
gwy_container_set_char(GwyContainer *container,
                       GQuark key,
                       guchar value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_UCHAR);
    g_value_set_uchar(&gvalue, value);
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
}

void
gwy_container_set_int32(GwyContainer *container,
                        GQuark key,
                        gint32 value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_INT);
    g_value_set_int(&gvalue, value);
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
}

void
gwy_container_set_int64(GwyContainer *container,
                        GQuark key,
                        gint64 value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_INT64);
    g_value_set_int64(&gvalue, value);
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
}

void
gwy_container_set_double(GwyContainer *container,
                         GQuark key,
                         gdouble value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_DOUBLE);
    g_value_set_double(&gvalue, value);
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
}

void
gwy_container_set_string(GwyContainer *container,
                         GQuark key,
                         const guchar *value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_STRING);
    g_value_set_string_take_ownership(&gvalue, (gchar*)value);
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
}

void
gwy_container_set_object(GwyContainer *container,
                         GQuark key,
                         GObject *value)
{
    GValue gvalue;

    memset(&gvalue, 0, sizeof(GValue));
    g_value_init(&gvalue, G_TYPE_OBJECT);
    g_value_set_object(&gvalue, value);  /* this increases refcount too */
    gwy_container_try_set_one(container, key, &gvalue, TRUE, TRUE);
    g_object_unref(value);
}

static guchar*
gwy_container_serialize(GObject *obj,
                        guchar *buffer,
                        gsize *size)
{
    GwyContainer *container;
    SerializeData sdata;

    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif
    g_return_val_if_fail(GWY_IS_CONTAINER(obj), NULL);

    container = GWY_CONTAINER(obj);
    buffer = gwy_serialize_pack(buffer, size, "s",
                                GWY_CONTAINER_TYPE_NAME);
    sdata.buffer = buffer;
    sdata.size = *size;
    g_hash_table_foreach(container->values, hash_serialize_func, &sdata);
    sdata.buffer = gwy_serialize_pack(sdata.buffer, &sdata.size, "i",
                                      0);  /* sentinel */

    *size = sdata.size;
    return sdata.buffer;
}

static void
hash_serialize_func(gpointer hkey, gpointer hvalue, gpointer hdata)
{
    GQuark key = GPOINTER_TO_UINT(hkey);
    GValue *value = (GValue*)hvalue;
    SerializeData *sdata = (SerializeData*)hdata;
    GType type = G_VALUE_TYPE(value);

    sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "is",
                                       type, g_quark_to_string(key));
    switch (type) {
        case G_TYPE_OBJECT:
        sdata->buffer = gwy_serializable_serialize(g_value_get_object(value),
                                                   sdata->buffer, &sdata->size);
        break;

        case G_TYPE_BOOLEAN:
        sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "b",
                                           g_value_get_boolean(value));
        break;

        case G_TYPE_UCHAR:
        sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "c",
                                           g_value_get_uchar(value));
        break;

        case G_TYPE_INT:
        sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "i",
                                           g_value_get_int(value));
        break;

        case G_TYPE_INT64:
        sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "q",
                                           g_value_get_int64(value));
        break;

        case G_TYPE_DOUBLE:
        sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "d",
                                           g_value_get_double(value));
        break;

        case G_TYPE_STRING:
        sdata->buffer = gwy_serialize_pack(sdata->buffer, &sdata->size, "s",
                                           g_value_get_string(value));
        break;

        default:
        g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_ERROR,
              "Cannot pack GValue holding %s", g_type_name(type));
        break;
    }
}

static GObject*
gwy_container_deserialize(const guchar *buffer,
                          gsize size,
                          gsize *position)
{
    gsize pos;
    GwyContainer *container;
    GType type;

    #ifdef DEBUG
    g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", __FUNCTION__);
    #endif
    g_return_val_if_fail(buffer, NULL);

    pos = gwy_serialize_check_string(buffer, size, *position,
                                     GWY_CONTAINER_TYPE_NAME);
    g_return_val_if_fail(pos, NULL);
    *position += pos;

    container = (GwyContainer*)gwy_container_new();
    type = gwy_serialize_unpack_int32(buffer, size, position);
    while (type) {
        guchar *name = gwy_serialize_unpack_string(buffer, size, position);
        GQuark key = g_quark_from_string(name);

        g_free(name);
        switch (type) {
            case G_TYPE_OBJECT:
            gwy_container_set_object(container, key,
                                     gwy_serializable_deserialize(buffer, size,
                                                                  position));
            break;

            case G_TYPE_BOOLEAN:
            gwy_container_set_boolean(container, key,
                                      gwy_serialize_unpack_boolean(buffer, size,
                                                                   position));
            break;

            case G_TYPE_UCHAR:
            gwy_container_set_char(container, key,
                                   gwy_serialize_unpack_char(buffer, size,
                                                             position));
            break;

            case G_TYPE_INT:
            gwy_container_set_int32(container, key,
                                    gwy_serialize_unpack_int32(buffer, size,
                                                               position));
            break;

            case G_TYPE_INT64:
            gwy_container_set_int64(container, key,
                                    gwy_serialize_unpack_int64(buffer, size,
                                                               position));
            break;

            case G_TYPE_DOUBLE:
            gwy_container_set_double(container, key,
                                     gwy_serialize_unpack_double(buffer, size,
                                                                 position));
            break;

            case G_TYPE_STRING:
            gwy_container_set_string(container, key,
                                     gwy_serialize_unpack_string(buffer, size,
                                                                 position));
            break;

            default:
            g_log(GWY_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                "Cannot unpack GValue holding type #%d", (gint)type);
            break;
        }
        type = gwy_serialize_unpack_int32(buffer, size, position);
    }

    return (GObject*)container;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
