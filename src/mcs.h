#define _MCS_H_

#define MCS_TCP_INIT_ERROR -1
#define MCS_TCP_SOCKET_INIT_ERROR 0x1
#define MCS_TCP_DISCONNECT 0x2
#define MCS_MAX_STRING_SIZE 200

/* utils */
void mcs_splitn(char ** dst, char * src, const char * delimiter, uint32_t max_split);
