#include "iot.h"

static char *des =
#include "descriptor.txt"
 ;

 
static char *state =
#include "state.txt"
 ;

char *iot_get_descriptor(void)
{
    return des;
}

char *iot_get_state(void)
{
    return state;
}
