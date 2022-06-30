void Net_File_Client_Packet(unsigned char *packet, unsigned int size);
qboolean Net_File_Client_Download_Start(const char *filename);
void Net_File_Client_Frame(void);
qboolean Net_File_Client_Active(void);
float Net_File_Client_Progress(void);
qboolean Net_File_Client_Success(void);
float Net_File_Client_Speed(void);
void Net_File_Client_Stop(void);
