#ifndef __COM_CONFIG_H
#define __COM_CONFIG_H
#include "Com_Debug.h"
#include "stdint.h"

typedef enum
{
    DB_OK = 0,
    DB_ERROR,
    DB_TIMEOUT,
    DB_OTHER

} db_state_t;
#endif
