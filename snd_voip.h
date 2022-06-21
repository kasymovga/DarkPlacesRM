#ifndef SND_VOIP_H
#define SND_VOIP_H

#define VOIP_FREQ 16000
#define VOIP_WIDTH 2
#define VOIP_CHANNELS 1

//Process voip packet
void S_VOIP_Received(unsigned char *packet, int len, int client);

//Start/stop voip
void S_VOIP_Start(void);
void S_VOIP_Stop(void);

//Start/stop echo test
void S_Echo_Start(void);
void S_Echo_Stop(void);
void S_VOIP_Capture_Callback(unsigned char *stream, int len);

#endif
