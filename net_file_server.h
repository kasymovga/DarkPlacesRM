#define NET_FILE_CHUNK_SIZE 1280
#define NET_FILE_NAME_SIZE 256
void Net_File_Server_Packet(client_t *client, unsigned char *packet, int len);
void Net_File_Server_Frame(void);
void Net_File_Server_Shutdown(void);
