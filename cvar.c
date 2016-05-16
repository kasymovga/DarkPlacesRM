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
#include "hashtable.h"

const char *cvar_dummy_description = "custom cvar";

cvar_t *cvar_vars = NULL;
hashtable_t *cvar_hashtable = NULL;
const char *cvar_null_string = "";

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (const char *var_name)
{
	cvar_t *cvar;
	cvar = (cvar_t*) HashTable_Locate(var_name, cvar_hashtable);

	return cvar;
}

cvar_t *Cvar_FindVarAfter (const char *prev_var_name, int neededflags)
{
	cvar_t *var;

	if (*prev_var_name)
	{
		var = Cvar_FindVar(prev_var_name);
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

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValueOr (const char *var_name, float def)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return def;
	return atof (var->string);
}

inline float Cvar_VariableValue (const char *var_name)
{
	return Cvar_VariableValueOr(var_name, 0);
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
	int j;
	const char *s;
	vec3_t v;
	prvm_prog_t *prog;
	for (i = 0;i < PRVM_PROG_MAX;i++)
	{
		prog = &prvm_prog_list[i];
		if (prog->loaded && var->globaldefindex[i] >= 0)
		{
			// MUST BE SYNCED WITH prvm_edict.c PRVM_LoadProgs
			switch(prog->globaldefs[var->globaldefindex[i]].type & ~DEF_SAVEGLOBAL)
			{
			case ev_float:
				PRVM_GLOBALFIELDFLOAT(prog->globaldefs[var->globaldefindex[i]].ofs) = var->value;
				break;
			case ev_vector:
				s = var->string;
				VectorClear(v);
				for (j = 0;j < 3;j++)
				{
					while (*s && ISWHITESPACE(*s))
						s++;
					if (!*s)
						break;
					v[j] = atof(s);
					while (!ISWHITESPACE(*s))
						s++;
					if (!*s)
						break;
				}
				VectorCopy(v, PRVM_GLOBALFIELDVECTOR(prog->globaldefs[var->globaldefindex[i]].ofs));
				break;
			case ev_string:
				PRVM_ChangeEngineString(prog, var->globaldefindex_stringno[i], var->string);
				PRVM_GLOBALFIELDSTRING(prog->globaldefs[var->globaldefindex[i]].ofs) = var->globaldefindex_stringno[i];
				break;
			}
		}
	}
}

// called after loading a savegame
void Cvar_UpdateAllAutoCvars(void)
{
	cvar_t *cvar;
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
		Cvar_UpdateAutoCvar(cvar);
}

static void Cvar_NotifyProg(prvm_prog_t *prog, cvar_t *cvar, char *oldvalue) {
    int func = PRVM_allfunction(CvarUpdated);
    if(!func)
        return;

    PRVM_G_INT(OFS_PARM0) = PRVM_SetTempString(prog, cvar->name);
    PRVM_G_INT(OFS_PARM1) = PRVM_SetTempString(prog, oldvalue);
    prog->ExecuteProgram(prog, func, "QC function CvarUpdated is missing");
}

static void Cvar_NotifyAllProgs(cvar_t *cvar, char *oldvalue) {
    int i;

    for(i = 0; i < PRVM_PROG_MAX; ++i) {
        prvm_prog_t *prog = &prvm_prog_list[i];
        if(prog->loaded)
            Cvar_NotifyProg(prog, cvar, oldvalue);
    }
}

/*
============
Cvar_Set
============
*/
extern cvar_t sv_disablenotify;
static void Cvar_SetQuick_Internal (cvar_t *var, const char *value)
{
	qboolean changed;
	size_t valuelen;
	char vabuf[1024], *oldval = NULL;

	changed = strcmp(var->string, value) != 0;
	// LordHavoc: don't reallocate when there is no change
	if (!changed)
		return;

	if(var->flags & CVAR_WATCHED) {
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
#endif
	}

	Cvar_UpdateAutoCvar(var);

	if(oldval) { // CVAR_WATCHED
		Cvar_NotifyAllProgs(var, oldval);
		Z_Free(oldval);
	}
}

void Cvar_SetQuick (cvar_t *var, const char *value)
{
	if (var == NULL)
	{
		Con_Print("Cvar_SetQuick: var == NULL\n");
		return;
	}

	if (developer_extra.integer)
		Con_DPrintf("Cvar_SetQuick({\"%s\", \"%s\", %i, \"%s\"}, \"%s\");\n", var->name, var->string, var->flags, var->defstring, value);

	Cvar_SetQuick_Internal(var, value);
}

void Cvar_Set (const char *var_name, const char *value)
{
	cvar_t *var;

	if(IS_OLDNEXUIZ_DERIVED(gamemode) && !strcmp(var_name, "r_glsl")) {
		Con_Printf("Cvar_Set: Attempted to set %s, updating vid_gl20 instead to preserve Nexuiz compatibility\n", var_name);
		var = Cvar_FindVar("vid_gl20");
	} else
		var = Cvar_FindVar(var_name);

	if (var == NULL)
	{
		Con_Printf("Cvar_Set: variable %s not found\n", var_name);
		return;
	}
	Cvar_SetQuick(var, value);
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValueQuick(cvar_t *var, float value)
{
	char val[MAX_INPUTLINE];

	if ((float)((int)value) == value)
		dpsnprintf(val, sizeof(val), "%i", (int)value);
	else
		dpsnprintf(val, sizeof(val), "%f", value);
	Cvar_SetQuick(var, val);
}

void Cvar_SetValue(const char *var_name, float value)
{
	char val[MAX_INPUTLINE];

	if ((float)((int)value) == value)
		dpsnprintf(val, sizeof(val), "%i", (int)value);
	else
		dpsnprintf(val, sizeof(val), "%f", value);
	Cvar_Set(var_name, val);
}

/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable (cvar_t *var)
{
	cvar_t *current, *next, *cvar;
	char *oldstr;
	size_t alloclen;
	int i;

	if (developer_extra.integer)
		Con_DPrintf("Cvar_RegisterVariable({\"%s\", \"%s\", %i});\n", var->name, var->string, var->flags);

// first check to see if it has already been defined
	cvar = Cvar_FindVar (var->name);
	if (cvar)
	{
		if (cvar->flags & CVAR_ALLOCATED)
		{
			if (developer_extra.integer)
				Con_DPrintf("...  replacing existing allocated cvar {\"%s\", \"%s\", %i}\n", cvar->name, cvar->string, cvar->flags);
			// fixed vars replace allocated ones
			// (because the engine directly accesses fixed vars)
			// NOTE: this isn't actually used currently
			// (all cvars are registered before config parsing)
			var->flags |= (cvar->flags & ~CVAR_ALLOCATED);
			// cvar->string is now owned by var instead
			var->string = cvar->string;
			var->defstring = cvar->defstring;
			var->value = atof (var->string);
			var->integer = (int) var->value;
			// Preserve autocvar status.
			memcpy(var->globaldefindex, cvar->globaldefindex, sizeof(var->globaldefindex));
			memcpy(var->globaldefindex_stringno, cvar->globaldefindex_stringno, sizeof(var->globaldefindex_stringno));
			// replace cvar with this one...
			var->next = cvar->next;
			if (cvar_vars == cvar)
			{
				// head of the list is easy to change
				cvar_vars = var;
			}
			else
			{
				// otherwise find it somewhere in the list
				for (current = cvar_vars;current->next != cvar;current = current->next)
					;
				current->next = var;
			}

			HashTable_Add(var->name, cvar_hashtable, var);
			// get rid of old allocated cvar
			// (but not cvar->string and cvar->defstring, because we kept those)
			Z_Free((char *)cvar->name);
			Z_Free(cvar);
		}
		else
			Con_DPrintf("Can't register var %s, already defined\n", var->name);
		return;
	}

// check for overlap with a command
	if (Cmd_Exists (var->name))
	{
		Con_Printf("Cvar_RegisterVariable: %s is a command\n", var->name);
		return;
	}

// copy the value off, because future sets will Z_Free it
	oldstr = (char *)var->string;
	alloclen = strlen(var->string) + 1;
	var->string = (char *)Z_Malloc (alloclen);
	memcpy ((char *)var->string, oldstr, alloclen);
	var->defstring = (char *)Z_Malloc (alloclen);
	memcpy ((char *)var->defstring, oldstr, alloclen);
	var->value = atof (var->string);
	var->integer = (int) var->value;

	// Mark it as not an autocvar.
	for (i = 0;i < PRVM_PROG_MAX;i++)
		var->globaldefindex[i] = -1;

// link the variables alphanumerical order
	for( current = NULL, next = cvar_vars ; next && strcmp( next->name, var->name ) < 0 ; current = next, next = next->next )
		;
	if( current ) {
		current->next = var;
	} else {
		cvar_vars = var;
	}
	var->next = next;

	HashTable_Add(var->name, cvar_hashtable, var);
}

/*
============
Cvar_Get

Adds a newly allocated variable to the variable list or sets its value.
============
*/
cvar_t *Cvar_Get (const char *name, const char *value, int flags, const char *newdescription)
{
	cvar_t *current, *next, *cvar;
	int i;

	if (developer_extra.integer)
		Con_DPrintf("Cvar_Get(\"%s\", \"%s\", %i);\n", name, value, flags);

	if(IS_OLDNEXUIZ_DERIVED(gamemode) && !strcmp(name, "r_glsl")) {
		Con_Printf("Cvar_Set: Attempted to set %s, updating vid_gl20 instead to preserve Nexuiz compatibility\n", name);
		cvar = Cvar_FindVar("vid_gl20");
		Cvar_SetQuick(cvar, value);
		return cvar;
	}

// first check to see if it has already been defined
	cvar = Cvar_FindVar(name);
	if (cvar)
	{
		cvar->flags |= flags;
		Cvar_SetQuick_Internal(cvar, value);
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

// check for overlap with a command
	if (Cmd_Exists (name))
	{
		Con_Printf("Cvar_Get: %s is a command\n", name);
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

	HashTable_Add(name, cvar_hashtable, cvar);

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

	return cvar;
}


/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_Command (void) {
	cvar_t *v;

// check variables
	v = Cvar_FindVar(Cmd_Argv(0));
	if (!v)
		return false;

// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		Con_Printf("\"%s\" is \"%s\" [\"%s\"]\n", v->name, ((v->flags & CVAR_PRIVATE) ? "********"/*hunter2*/ : v->string), v->defstring);
		return true;
	}

	if (developer_extra.integer)
		Con_DPrint("Cvar_Command: ");

	if ((v->flags & CVAR_READONLY) && (IS_OLDNEXUIZ_DERIVED(gamemode) && strcmp(v->name, "r_glsl")))
	{
		Con_Printf("%s is read-only\n", v->name);
		return true;
	}
	Cvar_Set(v->name, Cmd_Argv(1));
	if (developer_extra.integer)
		Con_DPrint("\n");
	return true;
}


void Cvar_UnlockDefaults (void)
{
	cvar_t *cvar;
	// unlock the default values of all cvars
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
		cvar->flags &= ~CVAR_DEFAULTSET;
}


void Cvar_LockDefaults_f (void)
{
	cvar_t *cvar;
	// lock in the default values of all cvars
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
	{
		if (!(cvar->flags & CVAR_DEFAULTSET))
		{
			size_t alloclen;

			//Con_Printf("locking cvar %s (%s -> %s)\n", cvar->name, cvar->string, cvar->defstring);
			cvar->flags |= CVAR_DEFAULTSET;
			Z_Free((char *)cvar->defstring);
			alloclen = strlen(cvar->string) + 1;
			cvar->defstring = (char *)Z_Malloc(alloclen);
			memcpy((char *)cvar->defstring, cvar->string, alloclen);
		}
	}
}

void Cvar_SaveInitState(void)
{
	cvar_t *cvar;
	for (cvar = cvar_vars;cvar;cvar = cvar->next)
	{
		cvar->initstate = true;
		cvar->initflags = cvar->flags;
		cvar->initdefstring = Mem_strdup(zonemempool, cvar->defstring);
		cvar->initstring = Mem_strdup(zonemempool, cvar->string);
		cvar->initvalue = cvar->value;
		cvar->initinteger = cvar->integer;
		VectorCopy(cvar->vector, cvar->initvector);
	}
}

void Cvar_RestoreInitState(void)
{
	cvar_t *cvar, *cvar_previous;
	for (cvar_previous = cvar_vars;(cvar = cvar_previous);)
	{
		if (cvar->initstate)
		{
			// restore this cvar, it existed at init
			if (((cvar->flags ^ cvar->initflags) & CVAR_MAXFLAGSVAL)
			 || strcmp(cvar->defstring ? cvar->defstring : "", cvar->initdefstring ? cvar->initdefstring : "")
			 || strcmp(cvar->string ? cvar->string : "", cvar->initstring ? cvar->initstring : ""))
			{
				Con_DPrintf("Cvar_RestoreInitState: Restoring cvar \"%s\"\n", cvar->name);
				if (cvar->defstring)
					Z_Free((char *)cvar->defstring);
				cvar->defstring = Mem_strdup(zonemempool, cvar->initdefstring);
				if (cvar->string)
					Z_Free((char *)cvar->string);
				cvar->string = Mem_strdup(zonemempool, cvar->initstring);
			}
			cvar->flags = cvar->initflags;
			cvar->value = cvar->initvalue;
			cvar->integer = cvar->initinteger;
			VectorCopy(cvar->initvector, cvar->vector);
			cvar_previous = cvar->next;
		}
		else
		{
			if (!(cvar->flags & CVAR_ALLOCATED))
			{
				Con_DPrintf("Cvar_RestoreInitState: Unable to destroy cvar \"%s\", it was registered after init!\n", cvar->name);
				// In this case, at least reset it to the default.
				if((cvar->flags & CVAR_NORESETTODEFAULTS) == 0)
					Cvar_SetQuick(cvar, cvar->defstring);
				cvar_previous = cvar->next;
				continue;
			}
			if (Cvar_IsAutoCvar(cvar))
			{
				Con_DPrintf("Cvar_RestoreInitState: Unable to destroy cvar \"%s\", it is an autocvar used by running progs!\n", cvar->name);
				// In this case, at least reset it to the default.
				if((cvar->flags & CVAR_NORESETTODEFAULTS) == 0)
					Cvar_SetQuick(cvar, cvar->defstring);
				cvar_previous = cvar->next;
				continue;
			}
			// remove this cvar, it did not exist at init
			Con_DPrintf("Cvar_RestoreInitState: Destroying cvar \"%s\"\n", cvar->name);
			HashTable_Remove(cvar->name, cvar_hashtable);

			// unlink struct from main list
			cvar_previous = cvar->next;
			
			// free strings
			if (cvar->defstring)
				Z_Free((char *)cvar->defstring);
			if (cvar->string)
				Z_Free((char *)cvar->string);
			if (cvar->description && cvar->description != cvar_dummy_description)
				Z_Free((char *)cvar->description);
			// free struct
			Z_Free(cvar);
		}
	}
}

void Cvar_ResetToDefaults_All_f (void)
{
	cvar_t *var;
	// restore the default values of all cvars
	for (var = cvar_vars ; var ; var = var->next)
		if((var->flags & CVAR_NORESETTODEFAULTS) == 0)
			Cvar_SetQuick(var, var->defstring);
}


void Cvar_ResetToDefaults_NoSaveOnly_f (void)
{
	cvar_t *var;
	// restore the default values of all cvars
	for (var = cvar_vars ; var ; var = var->next)
		if ((var->flags & (CVAR_NORESETTODEFAULTS | CVAR_SAVE)) == 0)
			Cvar_SetQuick(var, var->defstring);
}


void Cvar_ResetToDefaults_SaveOnly_f (void)
{
	cvar_t *var;
	// restore the default values of all cvars
	for (var = cvar_vars ; var ; var = var->next)
		if ((var->flags & (CVAR_NORESETTODEFAULTS | CVAR_SAVE)) == CVAR_SAVE)
			Cvar_SetQuick(var, var->defstring);
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

	// don't save cvars that match their default value
	for (var = cvar_vars ; var ; var = var->next)
		if ((var->flags & CVAR_SAVE) && (strcmp(var->string, var->defstring) || ((var->flags & CVAR_ALLOCATED) && !(var->flags & CVAR_DEFAULTSET))))
		{
			Cmd_QuoteString(buf1, sizeof(buf1), var->name, "\"\\$", false);
			Cmd_QuoteString(buf2, sizeof(buf2), var->string, "\"\\$", false);
			FS_Printf(f, "%s\"%s\" \"%s\"\n", var->flags & CVAR_ALLOCATED ? "seta " : "", buf1, buf2);
		}
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
	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (len && (ispattern ? !matchpattern_with_separator(cvar->name, partial, false, "", false) : strncmp (partial,cvar->name,len)))
			continue;

		Con_Printf("%s is \"%s\" [\"%s\"] %s\n", cvar->name, ((cvar->flags & CVAR_PRIVATE) ? "********"/*hunter2*/ : cvar->string), cvar->defstring, cvar->description);
		count++;
	}

	if (len)
	{
		if(ispattern)
			Con_Printf("%i cvar%s matching \"%s\"\n", count, (count > 1) ? "s" : "", partial);
		else
			Con_Printf("%i cvar%s beginning with \"%s\"\n", count, (count > 1) ? "s" : "", partial);
	}
	else
		Con_Printf("%i cvar(s)\n", count);

	HashTable_List(cvar_hashtable);
}
// 2000-01-09 CvarList command by Maddes

void Cvar_Set_f (void)
{
	cvar_t *cvar;

	// make sure it's the right number of parameters
	if (Cmd_Argc() < 3)
	{
		Con_Printf("Set: wrong number of parameters, usage: set <variablename> <value> [<description>]\n");
		return;
	}

	// check if it's read-only
	cvar = Cvar_FindVar(Cmd_Argv(1));
	if ((cvar && cvar->flags & CVAR_READONLY) && (IS_OLDNEXUIZ_DERIVED(gamemode) && strcmp(cvar->name, "r_glsl")))
	{
		Con_Printf("Set: %s is read-only\n", cvar->name);
		return;
	}

	if (developer_extra.integer)
		Con_DPrint("Set: ");

	// all looks ok, create/modify the cvar
	Cvar_Get(Cmd_Argv(1), Cmd_Argv(2), 0, Cmd_Argc() > 3 ? Cmd_Argv(3) : NULL);
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

	// check if it's read-only
	cvar = Cvar_FindVar(Cmd_Argv(1));
	if ((cvar && cvar->flags & CVAR_READONLY) && (IS_OLDNEXUIZ_DERIVED(gamemode) && strcmp(cvar->name, "r_glsl")))
	{
		Con_Printf("SetA: %s is read-only\n", cvar->name);
		return;
	}

	if (developer_extra.integer)
		Con_DPrint("SetA: ");

	// all looks ok, create/modify the cvar
	Cvar_Get(Cmd_Argv(1), Cmd_Argv(2), CVAR_SAVE, Cmd_Argc() > 3 ? Cmd_Argv(3) : NULL);
}

void Cvar_Del_f (void)
{
	int i;
	cvar_t *cvar;

	if(Cmd_Argc() < 2)
	{
		Con_Printf("Del: wrong number of parameters, useage: unset <variablename1> [<variablename2> ...]\n");
		return;
	}
	for(i = 1; i < Cmd_Argc(); ++i)
	{
		cvar = Cvar_FindVar(Cmd_Argv(i));
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
		} else {
			cvar_t *cvar_previous;
			for (cvar_previous = cvar_vars; strcmp(cvar->name, cvar_previous->name); cvar_previous = cvar_previous->next)
				;
			cvar_previous->next = cvar->next;
		}

		HashTable_Remove(Cmd_Argv(i), cvar_hashtable);

		if(cvar->description != cvar_dummy_description)
			Z_Free((char *)cvar->description);

		Z_Free((char *)cvar->name);
		Z_Free((char *)cvar->string);
		Z_Free((char *)cvar->defstring);
		Z_Free(cvar);
	}
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
		Cvar_SetQuick(var, buf);
	}
	Z_Free(buf);
}
#endif /* FILLALLCVARSWITHRUBBISH */
