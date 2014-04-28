
/*
Copyright (C) 2014  Andrew "Akari" Alexeyew

This program is free software; you can redistribute it and/or blah blah blah I don't care.
*/

#ifndef IRC_H
#define IRC_H

#define IRC_MAX_SESSIONS 32
#define IRC_SESSION_STRINGSIZE 128

// Represents an IRC server connection
// Incapsulates a libircclient session
typedef struct irc_session_t {
    void            *session;
    char            server[IRC_SESSION_STRINGSIZE];
    unsigned short  port;
    char            nick[IRC_SESSION_STRINGSIZE];
    char            username[IRC_SESSION_STRINGSIZE];
    char            realname[IRC_SESSION_STRINGSIZE];
    char            password[IRC_SESSION_STRINGSIZE];
    int             handle;
} irc_session_t;

void IRC_Init(void);
void IRC_Frame(void);
void IRC_Shutdown(void);
int IRC_CreateSession(void);
int IRC_ConnectSession(int handle, const char *server, unsigned short port, const char *server_password, const char *nick, const char *username, const char *realname);
void IRC_DisconnectSession(int handle, const char *reason);
int IRC_ReconnectSession(int handle);
void IRC_TerminateSession(int handle);
qboolean IRC_SessionIsConnected(int handle);
qboolean IRC_SessionExists(int handle);
void IRC_Translate_DP2IRC(const char *msg, char *out, size_t outsize);
void IRC_Translate_IRC2DP(const char *msg, char *out, size_t outsize);
int IRC_SendRaw(int handle, const char *fmt, ...);
int IRC_JoinChannel(int handle, const char *chan, const char *key);
int IRC_PartChannel(int handle, const char *chan);
int IRC_Topic(int handle, const char *chan, const char *topic);
int IRC_Privmsg(int handle, const char *targ, const char *msg);
int IRC_Notice(int handle, const char *targ, const char *msg);
int IRC_ChangeNick(int handle, const char *nick);
int IRC_ChannelMode(int handle, const char *chan, const char *mode);
int IRC_UserMode(int handle, const char *mode);
qboolean IRC_MaskMatches(const char *mask, const char *pattern);
const char* IRC_CurrentNick(int handle);
qboolean IRC_OpenLibrary(void);
void IRC_CloseLibrary(void);
void IRC_Callback_QuakeC(prvm_prog_t *prog, int handle, const char *event, int numeric, const char *origin, const char **params, unsigned int count);
void IRC_Cmd_Create_f(void);
void IRC_Cmd_Connect_f(void);
void IRC_Cmd_Raw_f(void);
void IRC_Cmd_Quit_f(void);
void IRC_Cmd_Terminate_f(void);

const char*  (*irc_strerror)                         (int ircerrno);

#endif
