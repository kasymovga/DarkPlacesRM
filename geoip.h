
#include "quakedef.h"

void GeoIP_Init(void);
void GeoIP_Shutdown(void);
qboolean GeoIP_LookUp(const char *const ipstr, char *buf, size_t buf_size);
