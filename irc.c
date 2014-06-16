
/*
Copyright (C) 2014  Andrew "Akari" Alexeyew

This program is free software; you can redistribute it and/or blah blah blah I don't care.
*/

#include "quakedef.h"
#include "irc.h"
#include "ft2.h"

// cvars
cvar_t irc_initialized = {CVAR_READONLY, "irc_initialized", "0", "Indicates the IRC library version, or 0 when failed to initialize"};
cvar_t irc_enabled = {CVAR_SAVE, "irc_enabled", "1", "Allows IRC sessions to be created and IRC events to be processed"};
cvar_t irc_eventlog = {CVAR_SAVE, "irc_eventlog", "0", "Print all IRC events to the console"};
cvar_t irc_translate_dp2irc_qfont = {CVAR_SAVE, "irc_translate_dp2irc_qfont", "1", "Convert graphical quake characters into a rough ascii equivalent when sending human-readable IRC messages"};
cvar_t irc_translate_dp2irc_color = {CVAR_SAVE, "irc_translate_dp2irc_color", "2", "0 = leave color codes as they are, 1 = strip color codes, 2 = convert color codes to mirc equivalents"};
cvar_t irc_translate_irc2dp_color = {CVAR_SAVE, "irc_translate_irc2dp_color", "2", "0 = leave color codes as they are, 1 = strip color codes, 2 = convert color codes to quake equivalents"};

// Handle for libircclient dll
dllhandle_t irc_dll = NULL;

// libircclient library functions
static void*        (*irc_create_session)                   (void *callbacks);
static void         (*irc_destroy_session)                  (void *session);
static int          (*irc_connect)                          (void *session, const char *server, unsigned short port, const char *server_password, const char *nick, const char *username, const char *realname);
static void         (*irc_disconnect)                       (void *session);
static int          (*irc_is_connected)                     (void *session);
static int          (*irc_add_select_descriptors)           (void *session, fd_set *in_set, fd_set *out_set, int *maxfd);
static int          (*irc_process_select_descriptors)       (void *session, fd_set *in_set, fd_set *out_set);
static int          (*irc_send_raw)                         (void *session, const char *format, ...);
static int          (*irc_cmd_quit)                         (void *session, const char *reason);
static int          (*irc_cmd_ctcp_reply)                   (void *session, const char *nick, const char *reply);
static void         (*irc_target_get_nick)                  (const char *target, char *nick, size_t size);
static void         (*irc_target_get_host)                  (const char *target, char *nick, size_t size);
static void         (*irc_get_version)                      (unsigned int *high, unsigned int *low);
static void         (*irc_set_ctx)                          (void *session, void *ctx);
static void*        (*irc_get_ctx)                          (void *session);
static int          (*irc_errno)                            (void *session);
// static const char*  (*irc_strerror)                         (int ircerrno);
static void         (*irc_option_set)                       (void *session, unsigned int option);
static void         (*irc_option_reset)                     (void *session, unsigned int option);

static dllfunction_t irc_funcs[] = {
    {"irc_create_session",                                  (void**) &irc_create_session},
    {"irc_destroy_session",                                 (void**) &irc_destroy_session},
    {"irc_connect",                                         (void**) &irc_connect},
    {"irc_disconnect",                                      (void**) &irc_disconnect},
    {"irc_is_connected",                                    (void**) &irc_is_connected},
    {"irc_add_select_descriptors",                          (void**) &irc_add_select_descriptors},
    {"irc_process_select_descriptors",                      (void**) &irc_process_select_descriptors},
    {"irc_send_raw",                                        (void**) &irc_send_raw},
    {"irc_cmd_quit",                                        (void**) &irc_cmd_quit},
    {"irc_cmd_ctcp_reply",                                  (void**) &irc_cmd_ctcp_reply},
    {"irc_target_get_nick",                                 (void**) &irc_target_get_nick},
    {"irc_target_get_host",                                 (void**) &irc_target_get_host},
    {"irc_get_version",                                     (void**) &irc_get_version},
    {"irc_set_ctx",                                         (void**) &irc_set_ctx},
    {"irc_get_ctx",                                         (void**) &irc_get_ctx},
    {"irc_errno",                                           (void**) &irc_errno},
    {"irc_strerror",                                        (void**) &irc_strerror},
    {"irc_option_set",                                      (void**) &irc_option_set},
    {"irc_option_reset",                                    (void**) &irc_option_reset},
    
    {NULL, NULL}
};

typedef void (*irc_event_callback_t) (void *session, const char *event, const char *origin, const char **params, unsigned int count);
typedef void (*irc_eventcode_callback_t) (void *session, unsigned int event, const char *origin, const char **params, unsigned int count);
typedef void (*irc_event_dcc_chat_t) (void *session, const char *nick, const char *addr, unsigned int dccid);
typedef void (*irc_event_dcc_send_t) (void *session, const char *nick, const char *addr, const char *filename, unsigned long size, unsigned int dccid);

typedef struct {
    irc_event_callback_t        event_connect;
    irc_event_callback_t        event_nick;
    irc_event_callback_t        event_quit;
    irc_event_callback_t        event_join;
    irc_event_callback_t        event_part;
    irc_event_callback_t        event_mode;
    irc_event_callback_t        event_umode;
    irc_event_callback_t        event_topic;
    irc_event_callback_t        event_kick;
    irc_event_callback_t        event_channel;
    irc_event_callback_t        event_privmsg;
    irc_event_callback_t        event_notice;
    irc_event_callback_t        event_channel_notice;
    irc_event_callback_t        event_invite;
    irc_event_callback_t        event_ctcp_req;
    irc_event_callback_t        event_ctcp_rep;
    irc_event_callback_t        event_ctcp_action;
    irc_event_callback_t        event_unknown;
    irc_eventcode_callback_t    event_numeric;
    irc_event_dcc_chat_t        event_dcc_chat_req;
    irc_event_dcc_send_t        event_dcc_send_req;
} irc_callbacks_t;

#define LIBIRC_OPTION_DEBUG         (1 << 1)
#define LIBIRC_OPTION_STRIPNICKS    (1 << 2)

#define FOR_ACTIVE_IRC_SESSIONS(i) for(i = 0; i < IRC_MAX_SESSIONS; ++i) if(irc_sessions[i].session && irc_is_connected(irc_sessions[i].session))
static irc_session_t irc_sessions[IRC_MAX_SESSIONS];

#define IS_VALID_IRC_SESSION(h) (h >= 0 && h < IRC_MAX_SESSIONS && irc_sessions[h].session)

/*
====================
IRC_Printf

Internal, used for logging.
====================
*/
static void IRC_Printf(const char *fmt, ...) {
    va_list args;
    char msg[MAX_INPUTLINE];
    
    va_start(args, fmt);
    dpvsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    
    Con_Print("IRC: ");
    Con_Print(msg);
}

/*
====================
IRC_Init

Initialize the IRC module.
====================
*/
void IRC_Init(void) {
    int handle;
    
    Cvar_RegisterVariable(&irc_initialized);
    
    Cvar_RegisterVariable(&irc_eventlog);
    Cvar_RegisterVariable(&irc_enabled);
    Cvar_RegisterVariable(&irc_translate_dp2irc_qfont);
    Cvar_RegisterVariable(&irc_translate_dp2irc_color);
    Cvar_RegisterVariable(&irc_translate_irc2dp_color);
    
    Cmd_AddCommand("irc_create", IRC_Cmd_Create_f, "Creates a new IRC session");
    Cmd_AddCommand("irc_connect", IRC_Cmd_Connect_f, "Connects to an IRC server");
    Cmd_AddCommand("irc_raw", IRC_Cmd_Raw_f, "Sends a raw message to the IRC server");
    Cmd_AddCommand("irc_quit", IRC_Cmd_Quit_f, "Disconnects from the IRC server");
    Cmd_AddCommand("irc_terminate", IRC_Cmd_Terminate_f, "Terminates an IRC session");
    
    if(IRC_OpenLibrary()) {
        unsigned int high, low;
        char ver[16];
        irc_get_version(&high, &low);
        dpsnprintf(ver, sizeof(ver), "%i.%i", high, low);
        Cvar_SetQuick(&irc_initialized, ver);
        
        if(high == 1 && low < 8)
            IRC_Printf("^1Broken libircclient version detected. Expect CTCP ACTION to not work.\n^1Please update to 1.8 or higher at ^2http://sourceforge.net/projects/libircclient/\n");
        
        for(handle = 0; handle < IRC_MAX_SESSIONS; ++handle)
            irc_sessions[handle].handle = handle;
    }
}

/*
====================
IRC_Shutdown

Shutdown the IRC module and close all connections.
====================
*/
void IRC_Shutdown(void) {
    int handle;
    
    for(handle = 0; handle < IRC_MAX_SESSIONS; ++handle)
        if(IS_VALID_IRC_SESSION(handle))
            IRC_TerminateSession(handle);
    
    IRC_CloseLibrary();
    Cvar_SetQuick(&irc_initialized, "0");
}

/*
====================

A bunch of boring mostly internal routines for the channel/user tracker

====================
*/

static irc_user_t* IRC_Tracker_AllocUser(irc_channel_t *chan) {
    irc_user_t *user = Z_Malloc(sizeof(irc_user_t));
    user->next = chan->users;
    chan->users = user;
    user->prefix = 0;
    memset(user->nick, 0, sizeof(user->nick));
    return user;
}

static void IRC_Tracker_FreeUser(irc_channel_t *chan, irc_user_t *user) {
    irc_user_t *p;
    
    if(chan) {
        for(p = chan->users; p; p = p->next)
            if(p->next == user)
                p->next = user->next;
        
        if(chan->users == user)
            chan->users = user->next;
    }
    
    Z_Free(user);
}

static irc_channel_t* IRC_Tracker_AllocChannel(irc_session_t *session) {
    irc_channel_t *chan = Z_Malloc(sizeof(irc_channel_t));
    chan->users = NULL;
    chan->next = session->channels;
    session->channels = chan;
    memset(chan->name, 0, sizeof(chan->name));
    memset(chan->mode, 0, sizeof(chan->mode));
    memset(chan->topic, 0, sizeof(chan->topic));
    return chan;
}

static void IRC_Tracker_FreeChannel(irc_session_t *session, irc_channel_t *chan) {
    irc_channel_t *p;
    irc_user_t *u, *nu;
    
    if(session) {
        for(p = session->channels; p; p = p->next)
            if(p->next == chan)
                p->next = chan->next;
        
        if(session->channels == chan)
            session->channels = chan->next;
    }
    
    u = chan->users;
    while(u) {
        nu = u->next;
        IRC_Tracker_FreeUser(NULL, u);
        u = nu;
    }
}

irc_channel_t* IRC_Tracker_GetChannel(irc_session_t *session, const char *cname) {
    irc_channel_t *c;
    char name[IRC_MAX_CHANNELNAME_LENGTH];
    COM_ToLowerString(cname, name, sizeof(name));
    
    for(c = session->channels; c; c = c->next)
        if(!strcmp(c->name, name))
            return c;
    
    return NULL;
}

irc_user_t* IRC_Tracker_GetUser(irc_channel_t *chan, const char *nick) {
    irc_user_t *u;
    
    for(u = chan->users; u; u = u->next)
        if(!strcmp(u->nick, nick))
            return u;
    
    return NULL;
}

static void IRC_Tracker_RemoveUser(irc_channel_t *chan, const char *nick) {
    irc_user_t *u;
    
    for(u = chan->users; u; u = u->next) {
        if(!strcmp(u->nick, nick)) {
            IRC_Tracker_FreeUser(chan, u);
            return;
        }
    }
}

static void IRC_Tracker_Free(irc_session_t *session) {
    irc_channel_t *c = session->channels, *nc;
    
    while(c) {
        nc = c->next;
        IRC_Tracker_FreeChannel(NULL, c);
        c = nc;
    }
    
    session->channels = NULL;
}

static irc_channel_t* IRC_Tracker_GetOrAllocChannel(irc_session_t *session, const char *cname) {
    irc_channel_t *c;
    char name[IRC_MAX_CHANNELNAME_LENGTH];
    
    c = IRC_Tracker_GetChannel(session, cname);
    if(c) return c;
    
    c = IRC_Tracker_AllocChannel(session);
    COM_ToLowerString(cname, name, sizeof(name));
    strlcpy(c->name, name, sizeof(c->name));
    return c;
}

static irc_user_t* IRC_Tracker_GetOrAllocUser(irc_channel_t *chan, const char *nick) {
    irc_user_t *u;
    
    u = IRC_Tracker_GetUser(chan, nick);
    if(u) return u;
    
    u = IRC_Tracker_AllocUser(chan);
    strlcpy(u->nick, nick, sizeof(u->nick));
    return u;
}

static void IRC_Tracker_Print(irc_session_t *s) {
    irc_channel_t *c;
    
    if(!developer.integer)
        return;
    
    IRC_Printf("*** \n");
    for(c = s->channels; c; c = c->next) {
        irc_user_t *u;
        
        IRC_Printf("%s: ", c->name);
        for(u = c->users; u; u = u->next) {
            if(u->prefix)
                Con_Printf("(%c)", u->prefix);
            Con_Printf("%s ", u->nick);
        }
        Con_Printf("\n");
    }
    IRC_Printf("*** \n");
}

/*
====================
IRC_Callback_Default

Libircclient event handler for non-numeric events.
====================
*/
static void IRC_Callback_Default(void *session, const char *event, const char *origin, const char **params, unsigned int count) {
    unsigned int i;
    irc_session_t *s = (irc_session_t*)irc_get_ctx(session);
    char nick[MAX_INPUTLINE];
    
    if(irc_eventlog.integer) {
        IRC_Printf("^1%i^7 [^3%s^7] origin: %s, params: \n", s->handle, event, origin);
        for(i = 0; i < count; ++i)
            IRC_Printf("    %i: ^2%s\n", i, params[i]);
    }
    
    IRC_Callback_QuakeC(SVVM_prog, TRUE, s->handle, event, -1, origin, params, count);
    IRC_Callback_QuakeC(MVM_prog, TRUE, s->handle, event, -1, origin, params, count);
    
    if(!strcmp(event, "CONNECT")) {
        // In case the server doesn't like our nickname
        strlcpy(s->nick, params[0], sizeof(s->nick));
    } else if(!strcmp(event, "NICK")) {
        irc_channel_t *c;
        
        irc_target_get_nick(origin, nick, sizeof(nick));
        if(!strcmp(s->nick, nick))  // our nickname changed
            strlcpy(s->nick, params[0], sizeof(s->nick));
            
        for(c = s->channels; c; c = c->next) {
            irc_user_t *u = IRC_Tracker_GetUser(c, nick);
            if(u)
                strlcpy(u->nick, params[0], sizeof(u->nick));
        }
        IRC_Tracker_Print(s);
    } else if(!strcmp(event, "CTCP") && !strncmp(params[0], "VERSION", 7)) {
        char ctcpreply[sizeof(engineversion) + 8];
        irc_target_get_nick(origin, nick, sizeof(nick));
        strlcpy(ctcpreply, "VERSION ", sizeof(ctcpreply));
        strlcat(ctcpreply, engineversion, sizeof(ctcpreply));
        irc_cmd_ctcp_reply(s->session, nick, ctcpreply);
    } else if(!strcmp(event, "JOIN")) {
        irc_target_get_nick(origin, nick, sizeof(nick));
        IRC_Tracker_GetOrAllocUser(IRC_Tracker_GetOrAllocChannel(s, params[0]), nick);
        IRC_Tracker_Print(s);
    } else if(!strcmp(event, "PART")) {
        irc_channel_t *c = IRC_Tracker_GetChannel(s, params[0]);
        
        if(c) {
            irc_target_get_nick(origin, nick, sizeof(nick));
            if(!strcmp(s->nick, nick))
                IRC_Tracker_FreeChannel(s, c);
            else
                IRC_Tracker_RemoveUser(c, nick);
            IRC_Tracker_Print(s);
        }
    } else if(!strcmp(event, "QUIT")) {
        irc_channel_t *c;
        irc_target_get_nick(origin, nick, sizeof(nick));
        for(c = s->channels; c; c = c->next)
            IRC_Tracker_RemoveUser(c, nick);
        IRC_Tracker_Print(s);
    } else if(!strcmp(event, "KICK")) {
        irc_channel_t *c = IRC_Tracker_GetChannel(s, params[0]);
        
        if(c) {
            if(!strcmp(s->nick, params[1]))
                IRC_Tracker_FreeChannel(s, c);
            else
                IRC_Tracker_RemoveUser(c, params[1]);
            IRC_Tracker_Print(s);
        }
    } else if(!strcmp(event, "MODE")) {
        irc_channel_t *c = IRC_Tracker_GetChannel(s, params[0]);
        // TODO: parse the mode string.
        // For now, just send a NAMES to update the prefixes later
        if(c) IRC_SendRaw(s->handle, "NAMES %s", c->name);
    } else if(!strcmp(event, "TOPIC")) {
        irc_channel_t *c = IRC_Tracker_GetChannel(s, params[0]);
        if(c) strlcpy(c->topic, params[1], sizeof(c->topic));
    }
    
    IRC_Callback_QuakeC(SVVM_prog, FALSE, s->handle, event, -1, origin, params, count);
    IRC_Callback_QuakeC(MVM_prog, FALSE, s->handle, event, -1, origin, params, count);
}

/*
====================
IRC_Callback_Numeric

Libircclient event handler for numeric events.
====================
*/
static void IRC_Callback_Numeric(void *session, unsigned int event, const char *origin, const char **params, unsigned int count) {
    unsigned int i;
    irc_session_t *s = (irc_session_t*)irc_get_ctx(session);
    
    if(irc_eventlog.integer) {
        IRC_Printf("^1%i^7 [^5%i^7] origin: %s, params: \n", ((irc_session_t*)irc_get_ctx(session))->handle, event, origin);
        for(i = 0; i < count; ++i)
            IRC_Printf("    %i: ^2%s\n", i, params[i]);
    }
    
    IRC_Callback_QuakeC(SVVM_prog, TRUE, s->handle, NULL, event, origin, params, count);
    IRC_Callback_QuakeC(MVM_prog, TRUE, s->handle, NULL, event, origin, params, count);
    
    if(event == 5) {
        // ISUPPORT reply - set usermodes and usermodes_prefixes
        unsigned int i;
        
        for(i = 0; i < count; ++i) {
            if(!strncmp(params[i], "PREFIX=", 7)) {
                const char *in;
                char *out = s->usermodes;
                
                for(in = params[i] + 8; *in; ++in) {
                    if(*in == ')') {
                        *out = 0;
                        out = s->usermodes_prefixes;
                    } else
                        *out++ = *in;
                }
                *out = 0;
                
                break;
            }
        }
    } if(event == 353) {
        // NAMES reply - update our userlist
        // we shouldn't need to delete users here, just add missing and set prefixes
        irc_channel_t *chan = IRC_Tracker_GetOrAllocChannel(s, params[2]);
        char nickbuff[IRC_MAX_NICK_LENGTH];
        char *nickptr = nickbuff;
        const char *listptr = params[3];
        char prefix = 0;
        qboolean parsedprefix = FALSE;
        
        for(;;) {
            if(!*listptr || *listptr == ' ') { 
                *nickptr = 0;
                IRC_Tracker_GetOrAllocUser(chan, nickbuff)->prefix = prefix;
                
                if(!*listptr)
                    break;
                
                ++listptr;
                nickptr = nickbuff;
                prefix = 0;
                parsedprefix = FALSE;
            } else if(!parsedprefix && strchr(s->usermodes_prefixes, *listptr)) {
                if(!prefix)
                    prefix = *listptr++;
            } else {
                *nickptr++ = *listptr++;
                parsedprefix = TRUE;
            }
        }
        
        IRC_Tracker_Print(s);
    } if(event == 332) {
        // TOPIC reply - save it
        irc_channel_t *chan = IRC_Tracker_GetChannel(s, params[1]);
        if(chan)
            strlcpy(chan->topic, params[2], sizeof(chan->topic));
    }
    
    IRC_Callback_QuakeC(SVVM_prog, FALSE, s->handle, NULL, event, origin, params, count);
    IRC_Callback_QuakeC(MVM_prog, FALSE, s->handle, NULL, event, origin, params, count);
}

/*
====================
IRC_CreateSession

Initiates a new IRC session and returns a handle to it.
Call IRC_ConnectSession to connect to an IRC server.
====================
*/
int IRC_CreateSession(void) {
    int handle = -1, i;
    irc_callbacks_t cb;
    irc_session_t *c;
    
    if(!irc_initialized.integer) {
        IRC_Printf("Error: Failed to create session: IRC module is not initialized\n");
        return -1;
    }
    
    if(!irc_enabled.integer) {
        IRC_Printf("Error: Failed to create session: irc_enabled is off\n");
        return -1;
    }
    
    for(i = 0; i < IRC_MAX_SESSIONS; ++i)
        if(!irc_sessions[i].session) {
            handle = i;
            break;
        }
    
    if(handle < 0) {
        IRC_Printf("Error: Failed to create session: Session limit exceeded\n");
        return handle;
    }
    
    c = &irc_sessions[handle];
    
    memset(&cb, 0, sizeof(cb));
    cb.event_numeric = IRC_Callback_Numeric;
    cb.event_connect = IRC_Callback_Default;
    cb.event_nick = IRC_Callback_Default;
    cb.event_quit = IRC_Callback_Default;
    cb.event_join = IRC_Callback_Default;
    cb.event_part = IRC_Callback_Default;
    cb.event_mode = IRC_Callback_Default;
    cb.event_umode = IRC_Callback_Default;
    cb.event_topic = IRC_Callback_Default;
    cb.event_kick = IRC_Callback_Default;
    cb.event_channel = IRC_Callback_Default;
    cb.event_privmsg = IRC_Callback_Default;
    cb.event_notice = IRC_Callback_Default;
    cb.event_channel_notice = IRC_Callback_Default;
    cb.event_invite = IRC_Callback_Default;
    cb.event_ctcp_req = IRC_Callback_Default;
    cb.event_ctcp_rep = IRC_Callback_Default;
    cb.event_ctcp_action = IRC_Callback_Default;
    cb.event_unknown = IRC_Callback_Default;
    
    c->session = irc_create_session(&cb);
    
    if(!c->session) {
        IRC_Printf("Failed to create an IRC session: %s\n", irc_strerror(irc_errno(c->session)));
        return -1;
    }
    
    // irc_option_set(c->session, LIBIRC_OPTION_DEBUG);
    irc_set_ctx(c->session, (void*)c);
    
    IRC_Tracker_Free(c);
    IRC_Callback_QuakeC(SVVM_prog, FALSE, handle, "SESSION_CREATED", -1, NULL, NULL, 0);
    IRC_Callback_QuakeC(MVM_prog, FALSE, handle, "SESSION_CREATED", -1, NULL, NULL, 0);
    
    IRC_Printf("Created an IRC session with handle %i\n", handle);
    
    return handle;
}

/*
====================
IRC_ConnectSession

Connects a session handle created by IRC_CreateSession to an IRC server.
Returns 0 on success, -1 on invalid handle, error code on libircclient error.
====================
*/
int IRC_ConnectSession(int handle, const char *server, unsigned short port, const char *server_password, const char *nick, const char *username, const char *realname) {
    irc_session_t *s;
    int err;
    
    if(!IS_VALID_IRC_SESSION(handle)) {
        IRC_Printf("Attempted to connect an invalid session %i\n", handle);
        return -1;
    }
    
    if(IRC_SessionIsConnected(handle))
        IRC_DisconnectSession(handle, "Changing server");
    
    s = &irc_sessions[handle];
    strlcpy(s->server, server, sizeof(s->server));
    strlcpy(s->nick, nick, sizeof(s->nick));
    strlcpy(s->username, username, sizeof(s->username));
    strlcpy(s->realname, realname, sizeof(s->realname));
    if(server_password)
        strlcpy(s->password, server_password, sizeof(s->password));
    else
        memset(s->password, 0, sizeof(s->password));
    s->port = port;
    
    irc_disconnect(s->session);
    IRC_Tracker_Free(s);
    
    if(irc_connect(s->session, server, port, server_password, nick, username, realname)) {
        err = irc_errno(s->session);
        IRC_Printf("Connection error on session %i: %s\n", handle, irc_strerror(err));
        return err;
    }
    
    Con_Printf("IRC_ConnectSession: 7\n");
    
    return 0;
}

/*
====================
IRC_SessionIsConnected

Returns true if the session handle is currently connected to an IRC server.
====================
*/
qboolean IRC_SessionIsConnected(int handle) {
    return IS_VALID_IRC_SESSION(handle) && irc_is_connected(irc_sessions[handle].session);
}

/*
====================
IRC_SessionExists

Returns true if the handle points to an unterminated session (not necessarily connected)
====================
*/
qboolean IRC_SessionExists(int handle) {
    return IS_VALID_IRC_SESSION(handle);
}

/*
====================
IRC_DisconnectSession

Disconnects from the IRC server.
====================
*/
void IRC_DisconnectSession(int handle, const char *reason) {
    irc_session_t *s;
    char fmtmsg[MAX_INPUTLINE];
    if(!IRC_SessionIsConnected(handle)) {
        IRC_Printf("Attempted to disconnect an unconnected session %i\n", handle);
        return;
    }
    
    s = &irc_sessions[handle];
    IRC_Translate_DP2IRC(reason, fmtmsg, sizeof(fmtmsg));
    irc_cmd_quit(s->session, fmtmsg);
    
    //if(IRC_SessionIsConnected(handle))
    //    irc_disconnect(s->session);
    
    IRC_Tracker_Free(s);
    IRC_Callback_QuakeC(SVVM_prog, FALSE, handle, "SESSION_DISCONNECTED", -1, NULL, NULL, 0);
    IRC_Callback_QuakeC(MVM_prog, FALSE, handle, "SESSION_DISCONNECTED", -1, NULL, NULL, 0);
    IRC_Printf("Session %i disconnected\n", handle);
}

/*
====================
IRC_ReconnectSession

Reconnects the session to the last server it connected to.
Returns 0 on success, -1 on invalid handle, error code on libircclient error.
====================
*/
int IRC_ReconnectSession(int handle) {
    irc_session_t *s;
    int err;
    
    if(!IS_VALID_IRC_SESSION(handle)) {
        IRC_Printf("Attempted to reconnect an invalid session %i\n", handle);
        return -1;
    }
    
    if(IRC_SessionIsConnected(handle))
        IRC_DisconnectSession(handle, "Reconnecting");
    
    s = &irc_sessions[handle];
    irc_disconnect(s->session);
    IRC_Tracker_Free(s);
    
    if(irc_connect(s->session, s->server, s->port, s->password, s->nick, s->username, s->realname)) {
        err = irc_errno(s->session);
        IRC_Printf("Connection error on session %i: %s\n", handle, irc_strerror(err));
        return err;
    }
    
    return 0;
}

/*
====================
IRC_TerminateSession

Terminates an IRC session completely, freeing allocated memory.
The session handle may be later reused by another session.
====================
*/
void IRC_TerminateSession(int handle) {
    irc_session_t *s;
    
    if(!IS_VALID_IRC_SESSION(handle)) {
        IRC_Printf("Attempted to terminate an invalid session %i\n", handle);
        return;
    }
    
    if(IRC_SessionIsConnected(handle))
        IRC_DisconnectSession(handle, "Session terminated");
    
    IRC_Callback_QuakeC(SVVM_prog, FALSE, handle, "SESSION_TERMINATING", -1, NULL, NULL, 0);
    IRC_Callback_QuakeC(MVM_prog, FALSE, handle, "SESSION_TERMINATING", -1, NULL, NULL, 0);
    
    s = &irc_sessions[handle];
    irc_destroy_session(s->session);
    s->session = NULL;
    
    IRC_Printf("Session %i terminated\n", handle);
}

/*
====================
IRC_Translate_DP2IRC

Formats a DP string to display it on IRC (see irc_translate_dp2irc_* cvars)
====================
*/
void IRC_Translate_DP2IRC(const char *msg, char *sout, size_t outsize) {
    const char *in;
    char outbuf[MAX_INPUTLINE * 3];
    char *out;
    int color, lastcolor = 0;
    
    // Based on Con_MaskPrint() from console.c
    
    for(in = msg, out = outbuf; *in; ++in) {
        if(*in == STRING_COLOR_TAG && irc_translate_dp2irc_color.integer) {
            if(in[1] == STRING_COLOR_RGB_TAG_CHAR && isxdigit(in[2]) && isxdigit(in[3]) && isxdigit(in[4])) {
                char r = tolower(in[2]);
                char g = tolower(in[3]);
                char b = tolower(in[4]);
                // it's a hex digit already, so the else part needs no check --blub
                if(isdigit(r)) r -= '0';
                else r -= 87;
                if(isdigit(g)) g -= '0';
                else g -= 87;
                if(isdigit(b)) b -= '0';
                else b -= 87;
                
                color = Sys_Con_NearestColor(r * 17, g * 17, b * 17);
                in += 3; // 3 only, the switch down there does the fourth
            }
            else
                color = in[1];
            
            switch(color) {
                case STRING_COLOR_TAG:
                    ++in;
                    *out++ = STRING_COLOR_TAG;
                    break;
                case '0':
                case '7':
                case '8':
                    // normal color
                    ++in;
                    if(lastcolor == 0 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 0;
                    *out++ = 0x0F;
                    break;
                case '1':
                    // light red
                    ++in;
                    if(lastcolor == 1 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 1;
                    *out++ = 0x03; *out++ = '0'; *out++ = '4';
                    break;
                case '2':
                    // light green
                    ++in;
                    if(lastcolor == 2 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 2;
                    *out++ = 0x03; *out++ = '0'; *out++ = '9';
                    break;
                case '3':
                    // yellow
                    ++in;
                    if(lastcolor == 3 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 3;
                    *out++ = 0x03; *out++ = '0'; *out++ = '8';
                    break;
                case '4':
                    // light blue
                    ++in;
                    if(lastcolor == 4 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 4;
                    *out++ = 0x03; *out++ = '1'; *out++ = '2';
                    break;
                case '5':
                    // light cyan
                    ++in;
                    if(lastcolor == 5 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 5;
                    *out++ = 0x03; *out++ = '1'; *out++ = '1';
                    break;
                case '6':
                    // light magenta
                    ++in;
                    if(lastcolor == 6 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 6;
                    *out++ = 0x03; *out++ = '0'; *out++ = '6';
                    break;
                // 7 handled above
                // 8 handled above
                case '9':
                    // black
                    ++in;
                    if(lastcolor == 6 || irc_translate_dp2irc_color.integer < 2) 
                        break; else lastcolor = 6;
                    *out++ = 0x03; *out++ = '0'; *out++ = '1';
                    break;
                default:
                    *out++ = STRING_COLOR_TAG;
                    break;
            }
        } else
            *out++ = (utf8_enable.integer || !irc_translate_dp2irc_qfont.integer)? *in : Con_Qfont_Translate(*in);
    }
    
    *out++ = 0;
    
    if(utf8_enable.integer && irc_translate_dp2irc_qfont.integer) {
        char *p;
        const char *q;
        p = outbuf;
        while(*p) {
            int ch = u8_getchar(p, &q);
            if(ch >= 0xE000 && ch <= 0xE0FF) {
                *p = Con_Qfont_Translate(ch - 0xE000);
                if(q > p+1)
                    memmove(p+1, q, strlen(q)+1);
                p = p + 1;
            }
            else
                p = p + (q - p);
        }
    }
    
    strlcpy(sout, outbuf, outsize);
}

/*
====================
IRC_Translate_IRC2DP

Formats an IRC message to display it in DP  (see irc_translate_irc2dp_* cvars)
====================
*/
void IRC_Translate_IRC2DP(const char *msg, char *sout, size_t outsize) {
    if(!irc_translate_irc2dp_color.integer) {
        strlcpy(sout, msg, outsize);
    } else {
        char out[MAX_INPUTLINE], *o;
        const char *m;
        char lastcolor = '7';
        char newcolor = lastcolor;
        
        for(o = out, m = msg; *m; ++m) {
            switch(*m) {
                case 0x02:      // bold
                case 0x1f:      // underline
                case 0x16:      // reverse
                    continue;
                    
                case 0x0f:      // reset colors
                    // XXX: this assumes that the text should be white by default
                    if(lastcolor != '7') {
                        *o++ = '^'; *o++ = '7';
                        lastcolor = '7';
                    }
                    break;
                    
                case 0x03:      // set color
                    if(isdigit(m[1])) {
                        int clr = m[1] - 0x30;
                        ++m;
                        
                        if(isdigit(m[1])) {
                            clr = clr * 10 + m[1] - 0x30;
                            ++m;
                        }
                        
                        // ignore the background color
                        if(m[1] == ',' && isdigit(m[2])) {
                            m += 2;
                            if(isdigit(m[1]))
                                ++m;
                        }
                        
                        if(clr > 15 || irc_translate_irc2dp_color.integer < 2)
                            continue;
                        
                        switch(clr) {
                            // TODO: maybe use DP's ^xRBG color codes to represent these better
                            
                            case 0:      newcolor = '7'; break;     // white
                            case 1:      newcolor = '0'; break;     // black
                            case 2:      newcolor = '4'; break;     // blue (navy)
                            case 3:      newcolor = '2'; break;     // green
                            case 4:      newcolor = '1'; break;     // red
                            case 5:      newcolor = '1'; break;     // brown (maroon)
                            case 6:      newcolor = '6'; break;     // purple
                            case 7:      newcolor = '3'; break;     // orange (olive)
                            case 8:      newcolor = '3'; break;     // yellow
                            case 9:      newcolor = '2'; break;     // light green (lime)
                            case 10:     newcolor = '5'; break;     // teal (a green/blue cyan)
                            case 11:     newcolor = '5'; break;     // light cyan
                            case 12:     newcolor = '4'; break;     // light blue
                            case 13:     newcolor = '6'; break;     // pink (light purple)
                            case 14:     newcolor = '9'; break;     // grey
                            case 15:     newcolor = '8'; break;     // light grey (silver)
                        }
                        
                        if(newcolor != lastcolor) {
                            *o++ = STRING_COLOR_TAG;
                            *o++ = newcolor;
                            lastcolor = newcolor;
                        }
                    }
                    break;
                case STRING_COLOR_TAG:
                    if(isdigit(m[1]) || m[1] == STRING_COLOR_TAG ||
                      (m[1] == STRING_COLOR_RGB_TAG_CHAR && isxdigit(m[2]) && isxdigit(m[3]) && isxdigit(m[4])))
                        *o++ = STRING_COLOR_TAG;
                default:
                    *o++ = *m;
                    break;
            }
        }
        
        *o++ = 0;
        strlcpy(sout, out, outsize);
    }
}

/*
====================
IRC_SendRaw

Sends a raw irc command to the server.
====================
*/
int IRC_SendRaw(int handle, const char *fmt, ...) {
    int err;
    va_list args;
    char cmd[MAX_INPUTLINE];
    
    va_start(args, fmt);
    dpvsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);
    
    if(developer.integer)
        Con_DPrintf("IRC_SendRaw(%i): %s\n", handle, cmd);
        
    if(!IS_VALID_IRC_SESSION(handle)) {
        IRC_Printf("IRC_SendRaw: %i is not a valid session\n", handle);
        return -1;
    }
    
    if(irc_send_raw(irc_sessions[handle].session, "%s", cmd)) {
        err = irc_errno(irc_sessions[handle].session);
        if(err) {
            IRC_Printf("IRC_SendRaw: Error: %s\n", irc_strerror(err));
            return err;
        }
    }
    
    return 0;
}

/*
====================
IRC_JoinChannel

Joins a channel.
====================
*/
int IRC_JoinChannel(int handle, const char *chan, const char *key) {
    if(key)
        return IRC_SendRaw(handle, "JOIN %s :%s", chan, key);
    else
        return IRC_SendRaw(handle, "JOIN %s", chan);
}

/*
====================
IRC_PartChannel

Leaves a channel.
====================
*/
int IRC_PartChannel(int handle, const char *chan) {
    return IRC_SendRaw(handle, "PART %s", chan);
}

/*
====================
IRC_Topic

Sets or requests the channel topic.
====================
*/
int IRC_Topic(int handle, const char *chan, const char *topic) {
    char fmtmsg[MAX_INPUTLINE];
    
    if(topic) {
        IRC_Translate_DP2IRC(topic, fmtmsg, sizeof(fmtmsg));
        return IRC_SendRaw(handle, "TOPIC %s :%s", chan, fmtmsg);
    } else
        return IRC_SendRaw(handle, "TOPIC %s", chan);
}

/*
====================
IRC_Privmsg

Sends a message to a channel or nick.
====================
*/
int IRC_Privmsg(int handle, const char *targ, const char *msg) {
    char fmtmsg[MAX_INPUTLINE];
    IRC_Translate_DP2IRC(msg, fmtmsg, sizeof(fmtmsg));
    return IRC_SendRaw(handle, "PRIVMSG %s :%s", targ, fmtmsg);
}

/*
====================
IRC_Notice

Sends a notice to a channel or nick.
====================
*/
int IRC_Notice(int handle, const char *targ, const char *msg) {
    char fmtmsg[MAX_INPUTLINE];
    IRC_Translate_DP2IRC(msg, fmtmsg, sizeof(fmtmsg));
    return IRC_SendRaw(handle, "NOTICE %s :%s", targ, fmtmsg);
}

/*
====================
IRC_ChangeNick

Changes your IRC nickname
====================
*/
int IRC_ChangeNick(int handle, const char *nick) {
    return IRC_SendRaw(handle, "NICK %s", nick);
}

/*
====================
IRC_ChannelMode

Sets or requests the IRC channel mode
====================
*/
int IRC_ChannelMode(int handle, const char *chan, const char *mode) {
    if(mode)
        return IRC_SendRaw(handle, "MODE %s %s", chan, mode);
    else
        return IRC_SendRaw(handle, "MODE %s", chan);
}


/*
====================
IRC_UserMode

Sets or requests your user mode
====================
*/
int IRC_UserMode(int handle, const char *mode) {
    if(mode)
        return IRC_SendRaw(handle, "MODE %s %s", irc_sessions[handle].nick, mode);
    else
        return IRC_SendRaw(handle, "MODE %s", irc_sessions[handle].nick);
}

/*
====================
IRC_CTCPRequest

Sends a CTCP request
====================
*/
int IRC_CTCPRequest(int handle, const char *targ, const char *msg) {
    char fmtmsg[MAX_INPUTLINE];
    IRC_Translate_DP2IRC(msg, fmtmsg, sizeof(fmtmsg));
    return IRC_SendRaw(handle, "PRIVMSG %s :\x01%s\x01", targ, fmtmsg);
}

/*
====================
IRC_CTCPReply

Sends a CTCP reply
====================
*/
int IRC_CTCPReply(int handle, const char *targ, const char *msg) {
    char fmtmsg[MAX_INPUTLINE];
    IRC_Translate_DP2IRC(msg, fmtmsg, sizeof(fmtmsg));
    return IRC_SendRaw(handle, "NOTICE %s :\x01%s\x01", targ, fmtmsg);
}

/*
====================
IRC_CurrentNick

Returns your current nickname
====================
*/
const char* IRC_CurrentNick(int handle) {
    return irc_sessions[handle].nick;
}

/*
====================
IRC_MaskMatches

Checks if a hostmask matches a given pattern
====================
*/

qboolean IRC_MaskMatches(const char *mask, const char *pattern) {
    const char *m, *p;
    size_t ml, pl;
    
    ml = strlen(mask);
    pl = strlen(pattern);
    
    if(pl > ml || (pattern[pl-1] != '*' && pattern[pl-1] != mask[ml-1]))
        return false;
    
    for(m = mask, p = pattern; *m && *p; ++m) {
        if(*p == '*') {
            if(!p[1])
                return true;
            if(*m == p[1])
                p += 2;
            continue;
        } else if(*m != *p)
            return false;
        ++p;
    }
    
    return true;
}

/*
====================
IRC_GetSession

Returns a pointer to the IRC session object if the handle is valid, NULL otherwise
====================
*/
irc_session_t* IRC_GetSession(int handle) {
    if(IS_VALID_IRC_SESSION(handle))
        return &irc_sessions[handle];
    IRC_Printf("IRC_GetSession: invalid session handle: %i\n", handle);
    return NULL;
}

/*
====================
IRC_Frame

Called every host frame.
Used to poll IRC connections.
====================
*/
void IRC_Frame(void) {
    struct timeval tv;
    fd_set in, out;
    int maxfd = 0;
    int i, err;
    
    if(!irc_initialized.integer || !irc_enabled.integer)
        return;
    
    tv.tv_usec = 0;
    tv.tv_sec = 0;
    
    FD_ZERO(&in);
    FD_ZERO(&out);
    
    FOR_ACTIVE_IRC_SESSIONS(i)
        irc_add_select_descriptors(irc_sessions[i].session, &in, &out, &maxfd);
    
    select(maxfd + 1, &in, &out, 0, &tv);
    
    FOR_ACTIVE_IRC_SESSIONS(i)
        if(irc_process_select_descriptors(irc_sessions[i].session, &in, &out)) {
            err = irc_errno(irc_sessions[i].session);
            if(err)
                IRC_Printf("Error on session %i: %s\n", i, irc_strerror(err));
        }
}

/*
====================
IRC_Cmd_Create_f

Creates a new IRC session.
====================
*/
void IRC_Cmd_Create_f(void) {
    IRC_CreateSession();
}

/*
====================
IRC_Cmd_Connect_f

Connects to an IRC server.
====================
*/
void IRC_Cmd_Connect_f(void) {
    int handle;
    unsigned short port;
    const char *server, *nick, *user, *name, *pass;
    
    if(Cmd_Argc() < 7) {
        Con_Printf("Usage: irc_connect <session handle> <server> <port> <nick> <user> <real name> [password]\n");
        return;
    }
    
    handle = atoi(Cmd_Argv(1));
    server = Cmd_Argv(2);
    port = atoi(Cmd_Argv(3));
    nick = Cmd_Argv(4);
    user = Cmd_Argv(5);
    name = Cmd_Argv(6);
    pass = Cmd_Argv(7);
    
    IRC_ConnectSession(handle, server, port, pass, nick, user, name);
}

/*
====================
IRC_Cmd_Raw_f

Sends a raw message to the IRC server.
====================
*/
void IRC_Cmd_Raw_f(void) {
    if(Cmd_Argc() < 3) {
        Con_Printf("Usage: irc_raw <session handle> <irc command>\n");
        return;
    }
    
    IRC_SendRaw(atoi(Cmd_Argv(1)), "%s", Cmd_Argv(2));
}

/*
====================
IRC_Cmd_Quit_f

Disconnects from the IRC server.
====================
*/
void IRC_Cmd_Quit_f(void) {
    if(Cmd_Argc() < 2) {
        Con_Printf("Usage: irc_quit <session handle> [reason]\n");
        return;
    }
    
    IRC_DisconnectSession(atoi(Cmd_Argv(1)), Cmd_Argv(2));
}

/*
====================
IRC_Cmd_Terminate_f

Terminates an IRC session.
====================
*/
void IRC_Cmd_Terminate_f(void) {
    if(Cmd_Argc() < 2) {
        Con_Printf("Usage: irc_terminate <session handle>\n");
        return;
    }
    
    IRC_TerminateSession(atoi(Cmd_Argv(1)));
}

/*
====================
IRC_OpenLibrary

Try to load the libircclient DLL.
====================
*/
qboolean IRC_OpenLibrary(void) {
    // XXX: non-linux systems untested
    const char *dllnames[] = {
#if WIN32
        "libircclient.dll",
#elif defined(MACOSX)
        "libircclient.dylib",
        "libircclient.1.dylib",
        "libircclient.0.dylib",
#else
        "libircclient.so",
        "libircclient.so.1",
#endif
        NULL
    };
    
    if(irc_dll)
        return true;
    
    return Sys_LoadLibrary(dllnames, &irc_dll, irc_funcs);
}

/*
====================
IRC_CloseLibrary

Unload the libircclient DLL.
====================
*/
void IRC_CloseLibrary(void) {
    Sys_UnloadLibrary(&irc_dll);
}
