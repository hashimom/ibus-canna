#include "engine.h"
#include "cannaui.h"

typedef struct _IBusCannaEngine IBusCannaEngine;
typedef struct _IBusCannaEngineClass IBusCannaEngineClass;

struct _IBusCannaEngine {
	IBusEngine parent;

	/* members */
	GString *preedit;
    gint cursor_pos;

    IBusLookupTable *table;
};

struct _IBusCannaEngineClass {
	IBusEngineClass parent;
};

/* functions prototype */
extern "C" {
static void	ibus_canna_engine_class_init	(IBusCannaEngineClass	*klass);
static void	ibus_canna_engine_init		(IBusCannaEngine		*engine);
static void	ibus_canna_engine_destroy		(IBusCannaEngine		*engine);
static gboolean 
ibus_canna_engine_process_key_event
(IBusEngine             *engine,
 guint               	 keyval,
 guint               	 keycode,
 guint               	 modifiers);
static void ibus_canna_engine_focus_in    (IBusEngine             *engine);
static void ibus_canna_engine_focus_out   (IBusEngine             *engine);
static void ibus_canna_engine_reset       (IBusEngine             *engine);
static void ibus_canna_engine_enable      (IBusEngine             *engine);
static void ibus_canna_engine_disable     (IBusEngine             *engine);
static void ibus_engine_set_cursor_location (IBusEngine             *engine,
                                             gint                    x,
                                             gint                    y,
                                             gint                    w,
                                             gint                    h);
static void ibus_canna_engine_set_capabilities
(IBusEngine             *engine,
 guint                   caps);
static void ibus_canna_engine_page_up     (IBusEngine             *engine);
static void ibus_canna_engine_page_down   (IBusEngine             *engine);
static void ibus_canna_engine_cursor_up   (IBusEngine             *engine);
static void ibus_canna_engine_cursor_down (IBusEngine             *engine);
static void ibus_canna_property_activate  (IBusEngine             *engine,
										   const gchar            *prop_name,
										   gint                    prop_state);
static void ibus_canna_engine_property_show
(IBusEngine             *engine,
 const gchar            *prop_name);
static void ibus_canna_engine_property_hide
(IBusEngine             *engine,
 const gchar            *prop_name);

static void ibus_canna_engine_commit_string
(IBusCannaEngine      *canna,
 const gchar            *string);
static void ibus_canna_engine_update      (IBusCannaEngine      *canna);
}

/*
  static EnchantBroker *broker = NULL;
  static EnchantDict *dict = NULL;
*/

G_DEFINE_TYPE (IBusCannaEngine, ibus_canna_engine, IBUS_TYPE_ENGINE)

static void ibus_canna_engine_class_init(IBusCannaEngineClass *klass)
{
	DEBUGM("canna_engine_class_init");
	IBusObjectClass *ibus_object_class = IBUS_OBJECT_CLASS (klass);
	IBusEngineClass *engine_class = IBUS_ENGINE_CLASS (klass);
	
	ibus_object_class->destroy = (IBusObjectDestroyFunc) ibus_canna_engine_destroy;
	engine_class->process_key_event = ibus_canna_engine_process_key_event;
}

static void ibus_canna_engine_init (IBusCannaEngine *engine)
{
	DEBUGM("canna_engine_init");
    engine->preedit = g_string_new ("");
    engine->cursor_pos = 0;

    engine->table = ibus_lookup_table_new(9, 0, TRUE, TRUE);
    g_object_ref_sink(engine->table);
}

static void ibus_canna_engine_destroy (IBusCannaEngine *engine)
{
	DEBUGM("canna_engine_destroy");
	if (engine->preedit != NULL) {
		g_string_free (engine->preedit, TRUE);
		engine->preedit = NULL;
	}
	if (engine->table != NULL) {
		g_object_unref (engine->table);
		engine->table = NULL;
	}
	((IBusObjectClass *)ibus_canna_engine_parent_class)->destroy((IBusObject *)engine);
}

static void ibus_canna_engine_update_lookup_table(IBusCannaEngine *engine) {
	if (engine->preedit->len == 0) {
		ibus_engine_hide_lookup_table((IBusEngine*)engine);
		return;
	}
	DEBUGM("update_lookup_table\n");
	ibus_lookup_table_clear (engine->table);
	ibus_lookup_table_append_candidate(
		engine->table, ibus_text_new_from_string("w"));
	ibus_lookup_table_append_candidate(
		engine->table, ibus_text_new_from_string("vv"));
	/* call super */
	ibus_engine_update_lookup_table ((IBusEngine*)engine, engine->table, TRUE);
}

static void ibus_canna_engine_update_preedit (IBusCannaEngine *canna_engine) {
    IBusText *text;
    text = ibus_text_new_from_static_string (canna_engine->preedit->str);
    text->attrs = ibus_attr_list_new ();
    
    ibus_attr_list_append(text->attrs,
	                      ibus_attr_underline_new(IBUS_ATTR_UNDERLINE_SINGLE, 0, canna_engine->preedit->len));
    ibus_attr_list_append(text->attrs,
						  ibus_attr_foreground_new(0x0000ff, 0, canna_engine->preedit->len));
    ibus_engine_update_preedit_text((IBusEngine*)canna_engine,
									text,
									canna_engine->cursor_pos,
									TRUE);
}

/* commit preedit to client and update preedit */
static gboolean ibus_canna_engine_commit_preedit (IBusCannaEngine *enchant)
{
	if (enchant->preedit->len == 0) {
		return FALSE;
	}
	ibus_canna_engine_commit_string (enchant, enchant->preedit->str);
	g_string_assign (enchant->preedit, "");
	enchant->cursor_pos = 0;

	ibus_canna_engine_update (enchant);

	return TRUE;
}


static void ibus_canna_engine_commit_string(
		IBusCannaEngine *engine,
		const gchar *string)
{
	IBusText *text;
	text = ibus_text_new_from_static_string(string);
	ibus_engine_commit_text((IBusEngine *)engine, text);
}

static void
ibus_canna_engine_update (IBusCannaEngine *enchant)
{
	ibus_canna_engine_update_preedit (enchant);
	ibus_engine_hide_lookup_table ((IBusEngine *)enchant);
}

#define is_alpha(c) (((c) >= IBUS_a && (c) <= IBUS_z) || ((c) >= IBUS_A && (c) <= IBUS_Z))

static gboolean ibus_canna_engine_process_key_event(
		IBusEngine *engine,
		guint keyval,
		guint keycode,
		guint  modifiers)
{
	IBusText *text;
	IBusCannaEngine *enchant = (IBusCannaEngine *)engine;

	if (modifiers & IBUS_RELEASE_MASK) {
		return FALSE;
	}

    modifiers &= (IBUS_CONTROL_MASK | IBUS_MOD1_MASK);

    if (modifiers == IBUS_CONTROL_MASK && keyval == IBUS_s) {
        ibus_canna_engine_update_lookup_table (enchant);
        return TRUE;
    }

    if (modifiers != 0) {
        if (enchant->preedit->len == 0)
            return FALSE;
        else
            return TRUE;
    }


    switch (keyval) {
    case IBUS_space:
		ibus_canna_engine_update_lookup_table (enchant);
		ibus_engine_show_lookup_table(reinterpret_cast<IBusEngine*>(enchant));
		return TRUE;
    case IBUS_Return:
        return ibus_canna_engine_commit_preedit (enchant);

    case IBUS_Escape:
        if (enchant->preedit->len == 0)
            return FALSE;

        g_string_assign (enchant->preedit, "");
        enchant->cursor_pos = 0;
        ibus_canna_engine_update (enchant);
        return TRUE;        

    case IBUS_Left:
        if (enchant->preedit->len == 0)
            return FALSE;
        if (enchant->cursor_pos > 0) {
            enchant->cursor_pos --;
            ibus_canna_engine_update (enchant);
        }
        return TRUE;

    case IBUS_Right:
        if (enchant->preedit->len == 0)
            return FALSE;
        if (enchant->cursor_pos < enchant->preedit->len) {
            enchant->cursor_pos ++;
            ibus_canna_engine_update (enchant);
        }
        return TRUE;
    
    case IBUS_Up:
        if (enchant->preedit->len == 0)
            return FALSE;
        if (enchant->cursor_pos != 0) {
            enchant->cursor_pos = 0;
            ibus_canna_engine_update (enchant);
        }
        return TRUE;

    case IBUS_Down:
        if (enchant->preedit->len == 0)
            return FALSE;
        
        if (enchant->cursor_pos != enchant->preedit->len) {
            enchant->cursor_pos = enchant->preedit->len;
            ibus_canna_engine_update (enchant);
        }
        
        return TRUE;
    
    case IBUS_BackSpace:
        if (enchant->preedit->len == 0)
            return FALSE;
        if (enchant->cursor_pos > 0) {
            enchant->cursor_pos --;
            g_string_erase (enchant->preedit, enchant->cursor_pos, 1);
            ibus_canna_engine_update (enchant);
        }
        return TRUE;
    
    case IBUS_Delete:
        if (enchant->preedit->len == 0)
            return FALSE;
        if (enchant->cursor_pos < enchant->preedit->len) {
            g_string_erase (enchant->preedit, enchant->cursor_pos, 1);
            ibus_canna_engine_update (enchant);
        }
        return TRUE;
    }

    if (is_alpha (keyval)) {
        g_string_insert_c (enchant->preedit,
                           enchant->cursor_pos,
                           keyval);

        enchant->cursor_pos ++;
        ibus_canna_engine_update (enchant);
        
        return TRUE;
    }

    return FALSE;
}