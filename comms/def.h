#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// 1. Message Types
typedef enum {
    MSG_ERR = 0,
    MSG_PING,
    MSG_ACK,
    MSG_SS_INIT,        // <-- DON'T FORGET THIS ONE for SS registration!
    MSG_REQ_LIST,
    MSG_RES_LIST,
    MSG_REQ_CREATE,
    MSG_REQ_DELETE,
    MSG_REQ_INFO,
    MSG_REQ_READ_LOC,
    MSG_REQ_WRITE_LOC,
    MSG_RES_LOC,
    MSG_REQ_READ,
    MSG_REQ_WRITE,
    MSG_DATA_CHUNK,
    MSG_STREAM_CHUNK,
    MSG_END_DATA
} MessageType;

// 2. Error Codes
typedef enum {
    ERR_NONE = 0,
    ERR_FILE_NOT_FOUND = 404,
    ERR_ACCESS_DENIED = 403,
    ERR_SENTENCE_LOCKED = 423,
    ERR_SS_OFFLINE = 503,
    ERR_TIMEOUT = 408,
    ERR_UNKNOWN = 999
} ErrorCode;

// 3. The Standard Header
typedef struct {
    uint32_t msg_type;
    uint32_t error_code;
    uint64_t payload_len;
} PacketHeader;

#endif