
/*
Copyright (C) 2014  Andrew "Akari" Alexeyew

This program is free software; you can redistribute it and/or blah blah blah I don't care.
*/

#ifndef IRC_H
#define IRC_H

#define IRC_MAX_SESSIONS 32
#define IRC_MAX_NICK_LENGTH 128
#define IRC_MAX_MODESTRING_LENGTH 64
#define IRC_MAX_CHANNELNAME_LENGTH 128
#define IRC_MAX_TOPIC_LENGTH 1024
#define IRC_SESSION_STRINGSIZE 128

// A user in channel
typedef struct irc_user_t {
    char            nick[IRC_MAX_NICK_LENGTH];
    char            prefix;
    void            *next;
} irc_user_t;

// A channel in session
typedef struct irc_channel_t {
    char            name[IRC_MAX_CHANNELNAME_LENGTH];
    char            mode[IRC_MAX_MODESTRING_LENGTH];
    char            topic[IRC_MAX_TOPIC_LENGTH];
    irc_user_t      *users;
    void            *next;
} irc_channel_t;

// Represents an IRC server connection
// Incapsulates a libircclient session
typedef struct irc_session_t {
    void            *session;
    char            server[IRC_SESSION_STRINGSIZE];
    unsigned short  port;
    char            nick[IRC_MAX_NICK_LENGTH];
    char            username[IRC_SESSION_STRINGSIZE];
    char            realname[IRC_SESSION_STRINGSIZE];
    char            password[IRC_SESSION_STRINGSIZE];
    char            usermodes[IRC_MAX_MODESTRING_LENGTH];
    char            usermodes_prefixes[IRC_MAX_MODESTRING_LENGTH];
    int             handle;
    irc_channel_t   *channels;
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
int IRC_CTCPRequest(int handle, const char *targ, const char *msg);
int IRC_CTCPReply(int handle, const char *targ, const char *msg);
qboolean IRC_MaskMatches(const char *mask, const char *pattern);
const char* IRC_CurrentNick(int handle);
qboolean IRC_OpenLibrary(void);
void IRC_CloseLibrary(void);
void IRC_Callback_QuakeC(prvm_prog_t *prog, qboolean pre, int handle, const char *event, int numeric, const char *origin, const char **params, unsigned int count);

irc_session_t* IRC_GetSession(int handle);
irc_channel_t* IRC_Tracker_GetChannel(irc_session_t *session, const char *cname);
irc_user_t* IRC_Tracker_GetUser(irc_channel_t *chan, const char *nick);

void IRC_Cmd_Create_f(void);
void IRC_Cmd_Connect_f(void);
void IRC_Cmd_Raw_f(void);
void IRC_Cmd_Quit_f(void);
void IRC_Cmd_Terminate_f(void);

const char*  (*irc_strerror)                         (int ircerrno);

#endif
