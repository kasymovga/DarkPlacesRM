/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cvar.c -- dynamic variable tracking

#include "quakedef.h"
#include "siphash.h"
#include "thread.h"

#include <time.h>

const char *cvar_dummy_description = "custom cvar";

cvar_t *cvar_vars = NULL;
cvar_t *cvar_hashtable[CVAR_HASHSIZE];
const char *cvar_null_string = "";

static uint8_t hashtable_key[16];

static inline uint64_t siphash_Block(const char* name, const size_t len) {
    uint64_t res;
    siphash(&res, (const uint8_t*)name, len, hashtable_key);

    return res;
}

static void *cvar_mutex;

qboolean Cvar_IsReadOnly(cvar_t **varp)
{
	if (*varp && ((*varp)->flags & CVAR_READONLY))
	{
		if (!strcmp((*varp)->name, "r_glsl"))
		{
			Con_Printf("Cvar_Set: Attempted to set r_glsl, updating vid_gl20 instead to preserve Nexuiz compatibility\n");
			*varp = Cvar_FindVar("vid_gl20");
			return false;
		}
		Con_Printf("Cvar_Set: cvar %s is read-only\n", (*varp)->name);
		return true;
	}
	return false;
}

void Cvar_InitTable(void) {
    uint64_t tmp = 0;
    int i;
    for(i=0; i<16; ++i) {
        if(0 == i) {
            tmp = (uint64_t)clock();
        } else if(8 == i) {
            tmp = (uint64_t)time(NULL);
        }
        hashtable_key[i] ^= ((tmp >> (8*(i%8))) & UINT8_C(0xff));
    }
}

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (const char *var_name)
{
	int hashindex;
	cvar_t *var;

	// use hash lookup to minimize search time
	hashindex = siphash_Block(var_name, strlen(var_name)) % CVAR_HASHSIZE;
	for (var = cvar_hashtable[hashindex];var;var = var->nextonhashchain)
		if (!strcmp (var_name, var->name))
			return var;

	return NULL;
}

cvar_t *Cvar_FindVarAfter (const char *prev_var_name, int neededflags)
{
	cvar_t *var;

	if (*prev_var_name)
	{
		var = Cvar_FindVar (prev_var_name);
		if (!var)
			return NULL;
		var = var->next;
	}
	else
		var = cvar_vars;

	// search for the next cvar matching the needed flags
	while (var)
	{
		if ((var->flags & neededflags) || !neededflags)
			break;
		var = var->next;
	}
	return var;
}

static cvar_t *Cvar_FindVarLink (const char *var_name, cvar_t **parent, cvar_t ***link, cvar_t **prev_alpha)
{
	int hashindex;
	cvar_t *var;

	// use hash lookup to minimize search time
	hashindex = siphash_Block(var_name, strlen(var_name)) % CVAR_HASHSIZE;
	if(parent) *parent = NULL;
	if(prev_alpha) *prev_alpha = NULL;
	if(link) *link = &cvar_hashtable[hashindex];
	for (var = cvar_hashtable[hashindex];var;var = var->nextonhashchain)
	{
		if (!strcmp (var_name, var->name))
		{
			if(!prev_alpha || var == cvar_vars)
				return var;

			*prev_alpha = cvar_vars;
			// if prev_alpha happens to become NULL then there has been some inconsistency elsewhere
			// already - should I still insert '*prev_alpha &&' in the loop?
			while((*prev_alpha)->next != var)
				*prev_alpha = (*prev_alpha)->next;
			return var;
		}
		if(parent) *parent = var;
	}

	return NULL;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValueOr (const char *var_name, float def)
{
	cvar_t *var;
	float r;

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	var = Cvar_FindVar (var_name);
	if (!var)
		r = def;
	else
		r = atof (var->string);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
	return r;
}

float Cvar_VariableValue (const char *var_name)
{
	return Cvar_VariableValueOr(var_name, 0);
}

float Cvar_VariableValue_NotPrivate (const char *var_name)
{
	cvar_t *var;
	float r;

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	var = Cvar_FindVar (var_name);
	if (!var || (var->flags & CVAR_PRIVATE))
		r = 0;
	else
		r = atof (var->string);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
	return r;
}

/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableStringOr (const char *var_name, const char *def)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return def;
	return var->string;
}

const char *Cvar_VariableString (const char *var_name)
{
	return Cvar_VariableStringOr(var_name, cvar_null_string);
}

int Cvar_VariableString_NotPrivate_TempString (prvm_prog_t *prog, const char *var_name)
{
	int r;

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	r = PRVM_SetTempString(prog, Cvar_VariableStringOr(var_name, cvar_null_string));
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
	return r;
}

/*
============
Cvar_VariableDefString
============
*/
const char *Cvar_VariableDefString (const char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->defstring;
}

/*
============
Cvar_VariableDescription
============
*/
const char *Cvar_VariableDescription (const char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->description;
}


/*
============
Cvar_CompleteVariable
============
*/
const char *Cvar_CompleteVariable (const char *partial)
{
	cvar_t		*cvar;
	size_t		len;

	len = strlen(partial);

	if (!len)
		return NULL;

// check functions
	for (cvar=cvar_vars ; cvar ; cvar=cvar->next)
		if (!strncasecmp (partial,cvar->name, len))
			return cvar->name;

	return NULL;
}


/*
	CVar_CompleteCountPossible

	New function for tab-completion system
	Added by EvilTypeGuy
	Thanks to Fett erich@heintz.com

*/
int Cvar_CompleteCountPossible (const char *partial)
{
	cvar_t	*cvar;
	size_t	len;
	int		h;

	h = 0;
	len = strlen(partial);

	if (!len)
		return	0;

	// Loop through the cvars and count all possible matches
	for (cvar = cvar_vars; cvar; cvar = cvar->next)
		if (!strncasecmp(partial, cvar->name, len))
			h++;

	return h;
}

/*
	CVar_CompleteBuildList

	New function for tab-completion system
	Added by EvilTypeGuy
	Thanks to Fett erich@heintz.com
	Thanks to taniwha

*/
const char **Cvar_CompleteBuildList (const char *partial)
{
	const cvar_t *cvar;
	size_t len = 0;
	size_t bpos = 0;
	size_t sizeofbuf = (Cvar_CompleteCountPossible (partial) + 1) * sizeof (const char *);
	const char **buf;

	len = strlen(partial);
	buf = (const char **)Mem_Alloc(tempmempool, sizeofbuf + sizeof (const char *));
	// Loop through the alias list and print all matches
	for (cvar = cvar_vars; cvar; cvar = cvar->next)
		if (!strncasecmp(partial, cvar->name, len))
			buf[bpos++] = cvar->name;

	buf[bpos] = NULL;
	return buf;
}

// written by LordHavoc
void Cvar_CompleteCvarPrint (const char *partial)
{
	cvar_t *cvar;
	size_t len = strlen(partial);
	// Loop through the command list and print all matches
	for (cvar = cvar_vars; cvar; cvar = cvar->next)
		if (!strncasecmp(partial, cvar->name, len))
			Con_Printf ("^3%s^7 is \"%s\" [\"%s\"] %s\n", cvar->name, cvar->string, cvar->defstring, cvar->description);
}

// check if a cvar is held by some progs
static qboolean Cvar_IsAutoCvar(cvar_t *var)
{
	int i;
	prvm_prog_t *prog;
	for (i = 0;i < PRVM_PROG_MAX;i++)
	{
		prog = &prvm_prog_list[i];
		if (prog->loaded && var->globaldefindex[i] >= 0)
			return true;
	}
	return false;
}

// we assume that prog is already set to the target progs
static void Cvar_UpdateAutoCvar(cvar_t *var)
{
	int i;
	for (i = 0;i < PRVM_PROG_MAX;i++)
	{
		VM_cvar_set_autocvar(&prvm_prog_list[i], var, var->string);
	}
}

// called after loading a savegame
void Cvar_UpdateAllAutoCvars(void)
{
	cvar_t *var;
	int i;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (var = cvar_vars ; var ; var = var->next)
		for (i = 0;i < PRVM_PROG_MAX;i++)
		{
			VM_cvar_set_autocvar(&prvm_prog_list[i], var, var->string);
		}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

static void Cvar_NotifyAllProgs(cvar_t *var, char *oldvalue) {
	int i;
	char namecopy[MAX_INPUTLINE];
	strlcpy(namecopy, var->name, sizeof(namecopy));
	for(i = 0; i < PRVM_PROG_MAX; ++i) {
		if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
		VM_cvar_set_updated(&prvm_prog_list[i], namecopy, oldvalue);
		if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	}
}

/*
============
Cvar_Set
============
*/
extern cvar_t sv_disablenotify;
static void Cvar_SetQuick_Internal (cvar_t *var, const char *value, qboolean cvar_notify)
{
	qboolean changed;
	size_t valuelen;
	char *oldval = NULL;
	#ifndef CONFIG_SV
	char vabuf[1024];
	#endif
	changed = strcmp(var->string, value) != 0;
	// LordHavoc: don't reallocate when there is no change
	if (!changed)
		return;

	if((var->flags & CVAR_WATCHED) && cvar_notify) {
		valuelen = strlen(var->string);
		oldval = (char*)Z_Malloc(valuelen + 1);
		memcpy(oldval, var->string, valuelen + 1);
	}

	// LordHavoc: don't reallocate when the buffer is the same size
	valuelen = strlen(value);
	if (!var->string || strlen(var->string) != valuelen)
	{
		Z_Free ((char *)var->string);	// free the old value string

		var->string = (char *)Z_Malloc (valuelen + 1);
	}
	memcpy ((char *)var->string, value, valuelen + 1);
	var->value = atof (var->string);
	var->integer = (int) var->value;
	if ((var->flags & CVAR_NOTIFY) && changed && sv.active && !sv_disablenotify.integer)
		SV_BroadcastPrintf("\"%s\" changed to \"%s\"\n", var->name, var->string);
#if 0
	// TODO: add infostring support to the server?
	if ((var->flags & CVAR_SERVERINFO) && changed && sv.active)
	{
		InfoString_SetValue(svs.serverinfo, sizeof(svs.serverinfo), var->name, var->string);
		if (sv.active)
		{
			MSG_WriteByte (&sv.reliable_datagram, svc_serverinfostring);
			MSG_WriteString (&sv.reliable_datagram, var->name);
			MSG_WriteString (&sv.reliable_datagram, var->string);
		}
	}
#endif
	#ifndef CONFIG_SV
	if ((var->flags & CVAR_USERINFO) && cls.state != ca_dedicated)
		CL_SetInfo(var->name, var->string, true, false, false, false);
	else if ((var->flags & CVAR_NQUSERINFOHACK) && cls.state != ca_dedicated)
	{
		// update the cls.userinfo to have proper values for the
		// silly nq config variables.
		//
		// this is done when these variables are changed rather than at
		// connect time because if the user or code checks the userinfo and it
		// holds weird values it may cause confusion...
		if (!strcmp(var->name, "_cl_color"))
		{
			int top = (var->integer >> 4) & 15, bottom = var->integer & 15;
			CL_SetInfo("topcolor", va(vabuf, sizeof(vabuf), "%i", top), true, false, false, false);
			CL_SetInfo("bottomcolor", va(vabuf, sizeof(vabuf), "%i", bottom), true, false, false, false);
			if (cls.protocol != PROTOCOL_QUAKEWORLD && cls.netcon)
			{
				MSG_WriteByte(&cls.netcon->message, clc_stringcmd);
				MSG_WriteString(&cls.netcon->message, va(vabuf, sizeof(vabuf), "color %i %i", top, bottom));
			}
		}
		else if (!strcmp(var->name, "_cl_rate"))
			CL_SetInfo("rate", va(vabuf, sizeof(vabuf), "%i", var->integer), true, false, false, false);
		else if (!strcmp(var->name, "_cl_rate_burstsize"))
			CL_SetInfo("rate_burstsize", va(vabuf, sizeof(vabuf), "%i", var->integer), true, false, false, false);
		else if (!strcmp(var->name, "_cl_playerskin"))
			CL_SetInfo("playerskin", var->string, true, false, false, false);
		else if (!strcmp(var->name, "_cl_playermodel"))
			CL_SetInfo("playermodel", var->string, true, false, false, false);
		else if (!strcmp(var->name, "_cl_name"))
			CL_SetInfo("name", var->string, true, false, false, false);
		else if (!strcmp(var->name, "rcon_secure"))
		{
			// whenever rcon_secure is changed to 0, clear rcon_password for
			// security reasons (prevents a send-rcon-password-as-plaintext
			// attack based on NQ protocol session takeover and svc_stufftext)
			if(var->integer <= 0)
				Cvar_Set("rcon_password", "");
		}
#ifdef CONFIG_MENU
		else if (!strcmp(var->name, "net_slist_favorites"))
			NetConn_UpdateFavorites();
		else if (!strcmp(var->name, "net_slist_extra"))
			NetConn_UpdateExtra();
#endif
	}
	#endif
	if (cvar_notify)
	{
		Con_DPrintf("cvar %s changed and require prog notification\n", var->name);
		Cvar_UpdateAutoCvar(var);
		if(oldval) { // CVAR_WATCHED
			Cvar_NotifyAllProgs(var, oldval);
			Z_Free(oldval);
		}
	}
}

void Cvar_SetQuick (cvar_t *var, const char *value)
{
	if (var == NULL)
	{
		Con_Print("Cvar_SetQuick: var == NULL\n");
		return;
	}
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	if (developer_extra.integer)
		Con_DPrintf("Cvar_SetQuick({\"%s\", \"%s\", %i, \"%s\"}, \"%s\");\n", var->name, var->string, var->flags, var->defstring, value);

	Cvar_SetQuick_Internal(var, value, false);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_SetQuick_Notify (cvar_t *var, const char *value)
{
	if (var == NULL)
	{
		Con_Print("Cvar_SetQuick_Notify: var == NULL\n");
		return;
	}
	if (developer_extra.integer)
		Con_DPrintf("Cvar_SetQuick_Notify({\"%s\", \"%s\", %i, \"%s\"}, \"%s\");\n", var->name, var->string, var->flags, var->defstring, value);

	Cvar_SetQuick_Internal(var, value, true);
}

void Cvar_Set (const char *var_name, const char *value)
{
	cvar_t *var;

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	var = Cvar_FindVar(var_name);
	if (var == NULL)
	{
		Con_Printf("Cvar_Set: variable %s not found\n", var_name);
		goto finish;
	}
	Cvar_SetQuick(var, value);
finish:
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValueQuick(cvar_t *var, float value)
{
	char val[MAX_INPUTLINE];
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);

	if ((float)((int)value) == value)
		dpsnprintf(val, sizeof(val), "%i", (int)value);
	else
		dpsnprintf(val, sizeof(val), "%f", value);
	Cvar_SetQuick(var, val);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_SetValue(const char *var_name, float value)
{
	char val[MAX_INPUTLINE];

	if ((float)((int)value) == value)
		dpsnprintf(val, sizeof(val), "%i", (int)value);
	else
		dpsnprintf(val, sizeof(val), "%f", value);
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	Cvar_Set(var_name, val);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable (cvar_t *variable)
{
	int hashindex;
	cvar_t *current, *next, *cvar;
	char *oldstr;
	size_t alloclen;
	int i;

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	if (developer_extra.integer)
		Con_DPrintf("Cvar_RegisterVariable({\"%s\", \"%s\", %i});\n", variable->name, variable->string, variable->flags);

// first check to see if it has already been defined
	cvar = Cvar_FindVar (variable->name);
	if (cvar)
	{
		if (cvar->flags & CVAR_ALLOCATED)
		{
			if (developer_extra.integer)
				Con_DPrintf("...  replacing existing allocated cvar {\"%s\", \"%s\", %i}\n", cvar->name, cvar->string, cvar->flags);
			// fixed variables replace allocated ones
			// (because the engine directly accesses fixed variables)
			// NOTE: this isn't actually used currently
			// (all cvars are registered before config parsing)
			variable->flags |= (cvar->flags & ~CVAR_ALLOCATED);
			// cvar->string is now owned by variable instead
			variable->string = cvar->string;
			variable->defstring = cvar->defstring;
			variable->value = atof (variable->string);
			variable->integer = (int) variable->value;
			// Preserve autocvar status.
			memcpy(variable->globaldefindex, cvar->globaldefindex, sizeof(variable->globaldefindex));
			memcpy(variable->globaldefindex_stringno, cvar->globaldefindex_stringno, sizeof(variable->globaldefindex_stringno));
			// replace cvar with this one...
			variable->next = cvar->next;
			if (cvar_vars == cvar)
			{
				// head of the list is easy to change
				cvar_vars = variable;
			}
			else
			{
				// otherwise find it somewhere in the list
				for (current = cvar_vars;current->next != cvar;current = current->next)
					;
				current->next = variable;
			}

			// get rid of old allocated cvar
			// (but not cvar->string and cvar->defstring, because we kept those)
			Z_Free((char *)cvar->name);
			Z_Free(cvar);
		}
		else
			Con_DPrintf("Can't register variable %s, already defined\n", variable->name);
		goto finish;
	}

// copy the value off, because future sets will Z_Free it
	oldstr = (char *)variable->string;
	alloclen = strlen(variable->string) + 1;
	variable->string = (char *)Z_Malloc (alloclen);
	memcpy ((char *)variable->string, oldstr, alloclen);
	variable->defstring = (char *)Z_Malloc (alloclen);
	memcpy ((char *)variable->defstring, oldstr, alloclen);
	variable->value = atof (variable->string);
	variable->integer = (int) variable->value;

	// Mark it as not an autocvar.
	for (i = 0;i < PRVM_PROG_MAX;i++)
		variable->globaldefindex[i] = -1;

// link the variable in
// alphanumerical order
	for( current = NULL, next = cvar_vars ; next && strcmp( next->name, variable->name ) < 0 ; current = next, next = next->next )
		;
	if( current ) {
		current->next = variable;
	} else {
		cvar_vars = variable;
	}
	variable->next = next;

	// link to head of list in this hash table index
	hashindex = siphash_Block(variable->name, strlen(variable->name)) % CVAR_HASHSIZE;
	variable->nextonhashchain = cvar_hashtable[hashindex];
	cvar_hashtable[hashindex] = variable;
finish:
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

/*
============
Cvar_Get

Adds a newly allocated variable to the variable list or sets its value.
============
*/
cvar_t *Cvar_Get (const char *name, const char *value, int flags, const char *newdescription)
{
	int hashindex;
	cvar_t *current, *next, *cvar;
	int i;

	if (developer_extra.integer)
		Con_DPrintf("Cvar_Get(\"%s\", \"%s\", %i);\n", name, value, flags);

// first check to see if it has already been defined
	cvar = Cvar_FindVar (name);
	if (cvar)
	{
		cvar->flags |= flags;
		Cvar_SetQuick_Internal (cvar, value, false);
		if(newdescription && (cvar->flags & CVAR_ALLOCATED))
		{
			if(cvar->description != cvar_dummy_description)
				Z_Free((char *)cvar->description);

			if(*newdescription)
				cvar->description = (char *)Mem_strdup(zonemempool, newdescription);
			else
				cvar->description = cvar_dummy_description;
		}
		return cvar;
	}

// check for pure evil
	if (!*name)
	{
		Con_Printf("Cvar_Get: invalid variable name\n");
		return NULL;
	}

// allocate a new cvar, cvar name, and cvar string
// TODO: factorize the following code with the one at the end of Cvar_RegisterVariable()
// FIXME: these never get Z_Free'd
	cvar = (cvar_t *)Z_Malloc(sizeof(cvar_t));
	cvar->flags = flags | CVAR_ALLOCATED;
	cvar->name = (char *)Mem_strdup(zonemempool, name);
	cvar->string = (char *)Mem_strdup(zonemempool, value);
	cvar->defstring = (char *)Mem_strdup(zonemempool, value);
	cvar->value = atof (cvar->string);
	cvar->integer = (int) cvar->value;

	if(newdescription && *newdescription)
		cvar->description = (char *)Mem_strdup(zonemempool, newdescription);
	else
		cvar->description = cvar_dummy_description; // actually checked by VM_cvar_type

	// Mark it as not an autocvar.
	for (i = 0;i < PRVM_PROG_MAX;i++)
		cvar->globaldefindex[i] = -1;

// link the variable in
// alphanumerical order
	for( current = NULL, next = cvar_vars ; next && strcmp( next->name, cvar->name ) < 0 ; current = next, next = next->next )
		;
	if( current )
		current->next = cvar;
	else
		cvar_vars = cvar;
	cvar->next = next;

	// link to head of list in this hash table index
	hashindex = siphash_Block(cvar->name, strlen(cvar->name)) % CVAR_HASHSIZE;
	cvar->nextonhashchain = cvar_hashtable[hashindex];
	cvar_hashtable[hashindex] = cvar;

	return cvar;
}


/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean	Cvar_Command (void)
{
	cvar_t			*v;
	qboolean r = false;

// check variables
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v)
		goto finish;

	r = true;
// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		Con_Printf("\"%s\" is \"%s\" [\"%s\"]\n", v->name, ((v->flags & CVAR_PRIVATE) ? "********"/*hunter2*/ : v->string), v->defstring);
		goto finish;
	}
	else if (Cvar_IsReadOnly(&v))
		goto finish;

	if (developer_extra.integer)
		Con_DPrint("Cvar_Command: ");

	Cvar_SetQuick_Notify (v, Cmd_Argv(1));
	if (developer_extra.integer)
		Con_DPrint("\n");
finish:
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
	return r;
}


void Cvar_UnlockDefaults (void)
{
	cvar_t *var;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	// unlock the default values of all cvars
	for (var = cvar_vars ; var ; var = var->next)
		var->flags &= ~CVAR_DEFAULTSET;
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}


void Cvar_LockDefaults_f (void)
{
	cvar_t *var;
	// lock in the default values of all cvars
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!(var->flags & CVAR_DEFAULTSET))
		{
			size_t alloclen;

			//Con_Printf("locking cvar %s (%s -> %s)\n", var->name, var->string, var->defstring);
			var->flags |= CVAR_DEFAULTSET;
			Z_Free((char *)var->defstring);
			alloclen = strlen(var->string) + 1;
			var->defstring = (char *)Z_Malloc(alloclen);
			memcpy((char *)var->defstring, var->string, alloclen);
		}
	}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_SaveInitState(void)
{
	cvar_t *c;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (c = cvar_vars;c;c = c->next)
	{
		c->initstate = true;
		c->initflags = c->flags;
		c->initdefstring = Mem_strdup(zonemempool, c->defstring);
		c->initstring = Mem_strdup(zonemempool, c->string);
		c->initvalue = c->value;
		c->initinteger = c->integer;
		VectorCopy(c->vector, c->initvector);
	}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_RestoreInitState(void)
{
	int hashindex;
	cvar_t *c, **cp;
	cvar_t *c2, **cp2;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (cp = &cvar_vars;(c = *cp);)
	{
		if (c->initstate)
		{
			// restore this cvar, it existed at init
			if (((c->flags ^ c->initflags) & CVAR_MAXFLAGSVAL)
			 || strcmp(c->defstring ? c->defstring : "", c->initdefstring ? c->initdefstring : "")
			 || strcmp(c->string ? c->string : "", c->initstring ? c->initstring : ""))
			{
				Con_DPrintf("Cvar_RestoreInitState: Restoring cvar \"%s\"\n", c->name);
				if (c->defstring)
					Z_Free((char *)c->defstring);
				c->defstring = Mem_strdup(zonemempool, c->initdefstring);
				if (c->string)
					Z_Free((char *)c->string);
				c->string = Mem_strdup(zonemempool, c->initstring);
			}
			c->flags = c->initflags;
			c->value = c->initvalue;
			c->integer = c->initinteger;
			VectorCopy(c->initvector, c->vector);
			cp = &c->next;
		}
		else
		{
			if (!(c->flags & CVAR_ALLOCATED))
			{
				Con_DPrintf("Cvar_RestoreInitState: Unable to destroy cvar \"%s\", it was registered after init!\n", c->name);
				// In this case, at least reset it to the default.
				if((c->flags & CVAR_NORESETTODEFAULTS) == 0)
					Cvar_SetQuick(c, c->defstring);
				cp = &c->next;
				continue;
			}
			if (Cvar_IsAutoCvar(c))
			{
				Con_DPrintf("Cvar_RestoreInitState: Unable to destroy cvar \"%s\", it is an autocvar used by running progs!\n", c->name);
				// In this case, at least reset it to the default.
				if((c->flags & CVAR_NORESETTODEFAULTS) == 0)
					Cvar_SetQuick(c, c->defstring);
				cp = &c->next;
				continue;
			}
			// remove this cvar, it did not exist at init
			Con_DPrintf("Cvar_RestoreInitState: Destroying cvar \"%s\"\n", c->name);
			// unlink struct from hash
			hashindex = siphash_Block(c->name, strlen(c->name)) % CVAR_HASHSIZE;
			for (cp2 = &cvar_hashtable[hashindex];(c2 = *cp2);)
			{
				if (c2 == c)
				{
					*cp2 = c2->nextonhashchain;
					break;
				}
				else
					cp2 = &c2->nextonhashchain;
			}
			// unlink struct from main list
			*cp = c->next;
			// free strings
			if (c->defstring)
				Z_Free((char *)c->defstring);
			if (c->string)
				Z_Free((char *)c->string);
			if (c->description && c->description != cvar_dummy_description)
				Z_Free((char *)c->description);
			// free struct
			Z_Free(c);
		}
	}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_ResetToDefaults_All_f (void)
{
	cvar_t *var;
	// restore the default values of all cvars
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (var = cvar_vars ; var ; var = var->next)
		if((var->flags & CVAR_NORESETTODEFAULTS) == 0)
			Cvar_SetQuick_Notify(var, var->defstring);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}


void Cvar_ResetToDefaults_NoSaveOnly_f (void)
{
	cvar_t *var;
	// restore the default values of all cvars
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (var = cvar_vars ; var ; var = var->next)
		if ((var->flags & (CVAR_NORESETTODEFAULTS | CVAR_SAVE)) == 0)
			Cvar_SetQuick_Notify(var, var->defstring);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}


void Cvar_ResetToDefaults_SaveOnly_f (void)
{
	cvar_t *var;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	// restore the default values of all cvars
	for (var = cvar_vars ; var ; var = var->next)
		if ((var->flags & (CVAR_NORESETTODEFAULTS | CVAR_SAVE)) == CVAR_SAVE)
			Cvar_SetQuick_Notify(var, var->defstring);
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (qfile_t *f)
{
	cvar_t	*var;
	char buf1[MAX_INPUTLINE], buf2[MAX_INPUTLINE];

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	// don't save cvars that match their default value
	for (var = cvar_vars ; var ; var = var->next)
		if ((var->flags & CVAR_SAVE) && (strcmp(var->string, var->defstring) || ((var->flags & CVAR_ALLOCATED) && !(var->flags & CVAR_DEFAULTSET))))
		{
			Cmd_QuoteString(buf1, sizeof(buf1), var->name, "\"\\$", false);
			Cmd_QuoteString(buf2, sizeof(buf2), var->string, "\"\\$", false);
			FS_Printf(f, "%s\"%s\" \"%s\"\n", var->flags & CVAR_ALLOCATED ? "seta " : "", buf1, buf2);
		}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}


// Added by EvilTypeGuy eviltypeguy@qeradiant.com
// 2000-01-09 CvarList command By Matthias "Maddes" Buecher, http://www.inside3d.com/qip/
/*
=========
Cvar_List
=========
*/
void Cvar_List_f (void)
{
	cvar_t *cvar;
	const char *partial;
	size_t len;
	int count;
	qboolean ispattern;

	if (Cmd_Argc() > 1)
	{
		partial = Cmd_Argv (1);
		len = strlen(partial);
		ispattern = (strchr(partial, '*') || strchr(partial, '?'));
	}
	else
	{
		partial = NULL;
		len = 0;
		ispattern = false;
	}

	count = 0;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (len && (ispattern ? !matchpattern_with_separator(cvar->name, partial, false, "", false) : strncmp (partial,cvar->name,len)))
			continue;

		Con_Printf("%s is \"%s\" [\"%s\"] %s\n", cvar->name, ((cvar->flags & CVAR_PRIVATE) ? "********"/*hunter2*/ : cvar->string), cvar->defstring, cvar->description);
		count++;
	}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);

	if (len)
	{
		if(ispattern)
			Con_Printf("%i cvar%s matching \"%s\"\n", count, (count > 1) ? "s" : "", partial);
		else
			Con_Printf("%i cvar%s beginning with \"%s\"\n", count, (count > 1) ? "s" : "", partial);
	}
	else
		Con_Printf("%i cvar(s)\n", count);
}
// 2000-01-09 CvarList command by Maddes

void Cvar_Set_f (void)
{
	cvar_t *cvar;

	// make sure it's the right number of parameters
	if (Cmd_Argc() < 3)
	{
		Con_Printf("Set: wrong number of parameters, usage: set <variablename> <value> [<description>]\n");
		goto finish;
	}

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	// check if it's read-only
	cvar = Cvar_FindVar(Cmd_Argv(1));
	if (Cvar_IsReadOnly(&cvar)) goto finish;
	if (cvar)
	{
		Cvar_SetQuick_Notify(cvar, Cmd_Argv(2));
		goto finish;
	}
	if (developer_extra.integer)
		Con_DPrint("Set: ");

	// all looks ok, create/modify the cvar
	Cvar_Get(Cmd_Argv(1), Cmd_Argv(2), 0, Cmd_Argc() > 3 ? Cmd_Argv(3) : NULL);
finish:
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_SetA_f (void)
{
	cvar_t *cvar;

	// make sure it's the right number of parameters
	if (Cmd_Argc() < 3)
	{
		Con_Printf("SetA: wrong number of parameters, usage: seta <variablename> <value> [<description>]\n");
		return;
	}

	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	// check if it's read-only
	cvar = Cvar_FindVar(Cmd_Argv(1));
	if (Cvar_IsReadOnly(&cvar)) goto finish;
	if (cvar)
	{
		Cvar_SetQuick_Notify(cvar, Cmd_Argv(2));
		cvar->flags |= CVAR_SAVE;
		goto finish;
	}
	if (developer_extra.integer)
		Con_DPrint("SetA: ");

	// all looks ok, create/modify the cvar
	Cvar_Get(Cmd_Argv(1), Cmd_Argv(2), CVAR_SAVE, Cmd_Argc() > 3 ? Cmd_Argv(3) : NULL);
finish:
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

void Cvar_Del_f (void)
{
	int i;
	cvar_t *cvar, *parent, **link, *prev;

	if(Cmd_Argc() < 2)
	{
		Con_Printf("Del: wrong number of parameters, useage: unset <variablename1> [<variablename2> ...]\n");
		return;
	}
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for(i = 1; i < Cmd_Argc(); ++i)
	{
		cvar = Cvar_FindVarLink(Cmd_Argv(i), &parent, &link, &prev);
		if(!cvar)
		{
			Con_Printf("Del: %s is not defined\n", Cmd_Argv(i));
			continue;
		}
		if(cvar->flags & CVAR_READONLY)
		{
			Con_Printf("Del: %s is read-only\n", cvar->name);
			continue;
		}
		if(!(cvar->flags & CVAR_ALLOCATED))
		{
			Con_Printf("Del: %s is static and cannot be deleted\n", cvar->name);
			continue;
		}
		if(cvar == cvar_vars)
		{
			cvar_vars = cvar->next;
		}
		else
		{
			// in this case, prev must be set, otherwise there has been some inconsistensy
			// elsewhere already... should I still check for prev != NULL?
			prev->next = cvar->next;
		}

		if(parent)
			parent->nextonhashchain = cvar->nextonhashchain;
		else if(link)
			*link = cvar->nextonhashchain;

		if(cvar->description != cvar_dummy_description)
			Z_Free((char *)cvar->description);

		Z_Free((char *)cvar->name);
		Z_Free((char *)cvar->string);
		Z_Free((char *)cvar->defstring);
		Z_Free(cvar);
	}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}

#ifdef FILLALLCVARSWITHRUBBISH
void Cvar_FillAll_f()
{
	char *buf, *p, *q;
	int n, i;
	cvar_t *var;
	qboolean verify;
	if(Cmd_Argc() != 2)
	{
		Con_Printf("Usage: %s length to plant rubbish\n", Cmd_Argv(0));
		Con_Printf("Usage: %s -length to verify that the rubbish is still there\n", Cmd_Argv(0));
		return;
	}
	n = atoi(Cmd_Argv(1));
	verify = (n < 0);
	if(verify)
		n = -n;
	buf = Z_Malloc(n + 1);
	buf[n] = 0;
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
	for(var = cvar_vars; var; var = var->next)
	{
		for(i = 0, p = buf, q = var->name; i < n; ++i)
		{
			*p++ = *q++;
			if(!*q)
				q = var->name;
		}
		if(verify && strcmp(var->string, buf))
		{
			Con_Printf("\n%s does not contain the right rubbish, either this is the first run or a possible overrun was detected, or something changed it intentionally; it DOES contain: %s\n", var->name, var->string);
		}
		Cvar_SetQuick_Notify(var, buf);
	}
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
	Z_Free(buf);
}
#endif /* FILLALLCVARSWITHRUBBISH */
#ifndef CONFIG_SV
void Cvar_EnableThreads(void)
{
	cvar_mutex = Thread_CreateMutex();
}

void Cvar_DisableThreads(void)
{
	Thread_DestroyMutex(cvar_mutex);
	cvar_mutex = NULL;
}

void Cvar_LockThreadMutex(void)
{
	if (cvar_mutex) Thread_LockMutex(cvar_mutex);
}

void Cvar_UnlockThreadMutex(void)
{
	if (cvar_mutex) Thread_UnlockMutex(cvar_mutex);
}
#endif
