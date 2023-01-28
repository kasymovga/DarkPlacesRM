
// Written by Forest Hale 2003-06-15 and placed into public domain.

#ifdef WIN32
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
# ifndef NOSUPPORTIPV6
// Windows XP or higher is required for getaddrinfo, but the inclusion of wspiapi provides fallbacks for older versions
# define _WIN32_WINNT 0x0501
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
# ifdef USE_WSPIAPI_H
#  include <wspiapi.h>
# endif
#endif

#include "quakedef.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef NOSUPPORTIPV6
#include <net/if.h>
#endif
#endif

#ifdef __MORPHOS__
#include <proto/socket.h>
#endif

// for Z_Malloc/Z_Free in quake
#include "zone.h"
#include "sys.h"
#include "netconn.h"

#include "lhnet.h"
#include "thread.h"

#if defined(WIN32)

#ifdef EWOULDBLOCK
    #undef EWOULDBLOCK
#endif

#ifdef ECONNREFUSED
    #undef ECONNREFUSED
#endif

#define EWOULDBLOCK WSAEWOULDBLOCK
#define ECONNREFUSED WSAECONNREFUSED

#define SOCKETERRNO WSAGetLastError()

#define IOC_VENDOR 0x18000000
#define _WSAIOW(x,y) (IOC_IN|(x)|(y))
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)

#define SOCKLEN_T int
#elif defined(__MORPHOS__)
#define ioctlsocket IoctlSocket
#define closesocket CloseSocket
#define SOCKETERRNO Errno()

#define SOCKLEN_T int
#else
#define ioctlsocket ioctl
#define closesocket close
#define SOCKETERRNO errno

#define SOCKLEN_T socklen_t
#endif

#ifdef MSG_DONTWAIT
#define LHNET_RECVFROM_FLAGS MSG_DONTWAIT
#define LHNET_SENDTO_FLAGS 0
#else
#define LHNET_RECVFROM_FLAGS 0
#define LHNET_SENDTO_FLAGS 0
#endif

typedef struct lhnetaddressnative_s
{
	lhnetaddresstype_t addresstype;
	int port;
	union
	{
		struct sockaddr sock;
		struct sockaddr_in in;
#ifndef NOSUPPORTIPV6
		struct sockaddr_in6 in6;
#endif
	}
	addr;
}
lhnetaddressnative_t;

// to make LHNETADDRESS_FromString resolve repeated hostnames faster, cache them
#define MAX_NAMECACHE 64
static struct namecache_s
{
	lhnetaddressnative_t address;
	double expirationtime;
	char name[64];
}
namecache[MAX_NAMECACHE];
static int namecacheposition = 0;
static void *namecache_mutex;

int LHNETADDRESS_FromPort(lhnetaddress_t *vaddress, lhnetaddresstype_t addresstype, int port)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	if (!address)
		return 0;
	switch(addresstype)
	{
	default:
		break;
	case LHNETADDRESSTYPE_LOOP:
		// local:port  (loopback)
		memset(address, 0, sizeof(*address));
		address->addresstype = LHNETADDRESSTYPE_LOOP;
		address->port = port;
		return 1;
	case LHNETADDRESSTYPE_INET4:
		// 0.0.0.0:port  (INADDR_ANY, binds to all interfaces)
		memset(address, 0, sizeof(*address));
		address->addresstype = LHNETADDRESSTYPE_INET4;
		address->port = port;
		address->addr.in.sin_family = AF_INET;
		address->addr.in.sin_port = htons((unsigned short)port);
		return 1;
#ifndef NOSUPPORTIPV6
	case LHNETADDRESSTYPE_INET6:
		// [0:0:0:0:0:0:0:0]:port  (IN6ADDR_ANY, binds to all interfaces)
		memset(address, 0, sizeof(*address));
		address->addresstype = LHNETADDRESSTYPE_INET6;
		address->port = port;
		address->addr.in6.sin6_family = AF_INET6;
		address->addr.in6.sin6_port = htons((unsigned short)port);
		return 1;
#endif
	}
	return 0;
}

#ifndef NOSUPPORTIPV6
static int LHNETADDRESS_Resolve(lhnetaddressnative_t *address, const char *name, int port)
{
	char port_buff [16];
	struct addrinfo hints;
	struct addrinfo* addrinf;
	int err;

	dpsnprintf (port_buff, sizeof (port_buff), "%d", port);
	port_buff[sizeof (port_buff) - 1] = '\0';

	memset(&hints, 0, sizeof (hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	//hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(name, port_buff, &hints, &addrinf);
	if (err != 0 || addrinf == NULL)
		return 0;
	if (addrinf->ai_addr->sa_family != AF_INET6 && addrinf->ai_addr->sa_family != AF_INET)
	{
		freeaddrinfo (addrinf);
		return 0;
	}

	// great it worked
	if (addrinf->ai_addr->sa_family == AF_INET6)
	{
		address->addresstype = LHNETADDRESSTYPE_INET6;
		memcpy(&address->addr.in6, addrinf->ai_addr, sizeof(address->addr.in6));
	}
	else
	{
		address->addresstype = LHNETADDRESSTYPE_INET4;
		memcpy(&address->addr.in, addrinf->ai_addr, sizeof(address->addr.in));
	}
	address->port = port;
	
	freeaddrinfo (addrinf);
	return 1;
}

int LHNETADDRESS_FromString(lhnetaddress_t *vaddress, const char *string, int defaultport)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	int i, port, d1, d2, d3, d4, resolved = 0;
	size_t namelen;
	unsigned char *a;
	char name[128];
	const char* addr_start;
	const char* addr_end = NULL;
	const char* port_name = NULL;
	int addr_family = AF_UNSPEC;
	qboolean namecache_mutex_locked = false;

	if (!address || !string || !*string)
		goto finish;
	memset(address, 0, sizeof(*address));
	address->addresstype = LHNETADDRESSTYPE_NONE;
	port = 0;

	// If it's a bracketed IPv6 address
	if (string[0] == '[')
	{
		const char* end_bracket = strchr(string, ']');

		if (end_bracket == NULL)
			goto finish;

		if (end_bracket[1] == ':')
			port_name = end_bracket + 2;
		else if (end_bracket[1] != '\0')
			goto finish;

		addr_family = AF_INET6;
		addr_start = &string[1];
		addr_end = end_bracket;
	}
	else
	{
		const char* first_colon;

		addr_start = string;

		// If it's a numeric non-bracket IPv6 address (-> no port),
		// or it's a numeric IPv4 address, or a name, with a port
		first_colon = strchr(string, ':');
		if (first_colon != NULL)
		{
			const char* last_colon = strrchr(first_colon + 1, ':');

			// If it's an numeric IPv4 address, or a name, with a port
			if (last_colon == NULL)
			{
				addr_end = first_colon;
				port_name = first_colon + 1;
			}
			else
				addr_family = AF_INET6;
		}
	}

	if (addr_end != NULL)
		namelen = addr_end - addr_start;
	else
		namelen = strlen (addr_start);

	if (namelen >= sizeof(name))
		namelen = sizeof(name) - 1;
	memcpy (name, addr_start, namelen);
	name[namelen] = 0;

	if (port_name)
		port = atoi(port_name);

	if (port == 0)
		port = defaultport;

	// handle loopback
	if (!strcmp(name, "local"))
	{
		address->addresstype = LHNETADDRESSTYPE_LOOP;
		address->port = port;
		resolved = 1;
		goto finish;
	}
	// try to parse as dotted decimal ipv4 address first
	// note this supports partial ip addresses
	d1 = d2 = d3 = d4 = 0;
#if _MSC_VER >= 1400
#define sscanf sscanf_s
#endif
	if (addr_family != AF_INET6 &&
		sscanf(name, "%d.%d.%d.%d", &d1, &d2, &d3, &d4) >= 1 && (unsigned int)d1 < 256 && (unsigned int)d2 < 256 && (unsigned int)d3 < 256 && (unsigned int)d4 < 256)
	{
		// parsed a valid ipv4 address
		address->addresstype = LHNETADDRESSTYPE_INET4;
		address->port = port;
		address->addr.in.sin_family = AF_INET;
		address->addr.in.sin_port = htons((unsigned short)port);
		a = (unsigned char *)&address->addr.in.sin_addr;
		a[0] = d1;
		a[1] = d2;
		a[2] = d3;
		a[3] = d4;
		resolved = 1;
		goto finish;
	}
	if (namecache_mutex)
	{
		namecache_mutex_locked = true;
		Thread_LockMutex(namecache_mutex);
	}
	for (i = 0;i < MAX_NAMECACHE;i++)
		if (!strcmp(namecache[i].name, name))
			break;
	if (i < MAX_NAMECACHE && realtime < namecache[i].expirationtime)
	{
		*address = namecache[i].address;
		address->port = port;
		if (address->addresstype == LHNETADDRESSTYPE_INET6)
		{
			address->addr.in6.sin6_port = htons((unsigned short)port);
			resolved = 1;
			goto finish;
		}
		else if (address->addresstype == LHNETADDRESSTYPE_INET4)
		{
			address->addr.in.sin_port = htons((unsigned short)port);
			resolved = 1;
			goto finish;
		}
		goto finish;
	}

	for (i = 0;i < (int)sizeof(namecache[namecacheposition].name)-1 && name[i];i++)
		namecache[namecacheposition].name[i] = name[i];
	namecache[namecacheposition].name[i] = 0;
	namecache[namecacheposition].expirationtime = realtime + 12 * 3600; // 12 hours

	// try resolving the address (handles dns and other ip formats)
	if (namecache_mutex_locked)
	{
		Thread_UnlockMutex(namecache_mutex);
		namecache_mutex_locked = false;
	}
	resolved = LHNETADDRESS_Resolve(address, name, port);
	if (namecache_mutex)
	{
		Thread_LockMutex(namecache_mutex);
		namecache_mutex_locked = true;
	}
	if (resolved)
	{
		namecache[namecacheposition].address = *address;
	}
	else
	{
		namecache[namecacheposition].address.addresstype = LHNETADDRESSTYPE_NONE;
	}
	
	namecacheposition = (namecacheposition + 1) % MAX_NAMECACHE;
finish:
	if (namecache_mutex_locked) Thread_UnlockMutex(namecache_mutex);
	return resolved;
}
#else
int LHNETADDRESS_FromString(lhnetaddress_t *vaddress, const char *string, int defaultport)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	int i, port, namelen, d1, d2, d3, d4, resolved = 0;
	struct hostent *hostentry;
	unsigned char *a;
	const char *colon;
	char name[128];
	qboolean namecache_mutex_locked = false;
	if (!address || !string || !*string)
		goto finish;
	memset(address, 0, sizeof(*address));
	address->addresstype = LHNETADDRESSTYPE_NONE;
	port = 0;
	colon = strrchr(string, ':');
	if (colon && (colon == strchr(string, ':') || (string[0] == '[' && colon - string > 0 && colon[-1] == ']')))
	//           EITHER: colon is the ONLY colon  OR: colon comes after [...] delimited IPv6 address
	//           fixes misparsing of IPv6 addresses without port
	{
		port = atoi(colon + 1);
	}
	else
		colon = string + strlen(string);
	if (port == 0)
		port = defaultport;
	namelen = colon - string;
	if (namelen > 127)
		namelen = 127;
	if (string[0] == '[' && namelen > 0 && string[namelen-1] == ']') // ipv6
	{
		string++;
		namelen -= 2;
	}
	memcpy(name, string, namelen);
	name[namelen] = 0;
	// handle loopback
	if (!strcmp(name, "local"))
	{
		address->addresstype = LHNETADDRESSTYPE_LOOP;
		address->port = port;
		resolved = 1;
		goto finish;
	}
	// try to parse as dotted decimal ipv4 address first
	// note this supports partial ip addresses
	d1 = d2 = d3 = d4 = 0;
#if _MSC_VER >= 1400
#define sscanf sscanf_s
#endif
	if (sscanf(name, "%d.%d.%d.%d", &d1, &d2, &d3, &d4) >= 1 && (unsigned int)d1 < 256 && (unsigned int)d2 < 256 && (unsigned int)d3 < 256 && (unsigned int)d4 < 256)
	{
		// parsed a valid ipv4 address
		address->addresstype = LHNETADDRESSTYPE_INET4;
		address->port = port;
		address->addr.in.sin_family = AF_INET;
		address->addr.in.sin_port = htons((unsigned short)port);
		a = (unsigned char *)&address->addr.in.sin_addr;
		a[0] = d1;
		a[1] = d2;
		a[2] = d3;
		a[3] = d4;
		resolved = 1;
		goto finish;
	}
	if (namecache_mutex)
	{
		Thread_LockMutex(namecache_mutex);
		namecache_mutex_locked = true;
	}
	for (i = 0;i < MAX_NAMECACHE;i++)
		if (!strcmp(namecache[i].name, name))
			break;
	if (i < MAX_NAMECACHE && realtime < namecache[i].expirationtime)
	{
		*address = namecache[i].address;
		address->port = port;
		if (address->addresstype == LHNETADDRESSTYPE_INET6)
		{
#ifndef NOSUPPORTIPV6
			address->addr.in6.sin6_port = htons((unsigned short)port);
			resolved = true;
			goto finish;
#endif
		}
		else if (address->addresstype == LHNETADDRESSTYPE_INET4)
		{
			address->addr.in.sin_port = htons((unsigned short)port);
			resolved = true;
			goto finish;
		}
		goto finish;
	}
	// try gethostbyname (handles dns and other ip formats)
	if (namecache_mutex_locked)
	{
		Thread_UnlockMutex(namecache_mutex);
		namecache_mutex_locked = false;
	}
	hostentry = gethostbyname(name);
	if (namecache_mutex)
	{
		Thread_LockMutex(namecache_mutex);
		namecache_mutex_locked = true;
	}
	if (hostentry)
	{
		if (hostentry->h_addrtype == AF_INET6)
		{
#ifndef NOSUPPORTIPV6
			// great it worked
			address->addresstype = LHNETADDRESSTYPE_INET6;
			address->port = port;
			address->addr.in6.sin6_family = hostentry->h_addrtype;
			address->addr.in6.sin6_port = htons((unsigned short)port);
			memcpy(&address->addr.in6.sin6_addr, hostentry->h_addr_list[0], sizeof(address->addr.in6.sin6_addr));
			for (i = 0;i < (int)sizeof(namecache[namecacheposition].name)-1 && name[i];i++)
				namecache[namecacheposition].name[i] = name[i];
			namecache[namecacheposition].name[i] = 0;
			namecache[namecacheposition].expirationtime = realtime + 12 * 3600; // 12 hours
			namecache[namecacheposition].address = *address;
			namecacheposition = (namecacheposition + 1) % MAX_NAMECACHE;
			resolved = true;
			goto finish;
#endif
		}
		else if (hostentry->h_addrtype == AF_INET)
		{
			// great it worked
			address->addresstype = LHNETADDRESSTYPE_INET4;
			address->port = port;
			address->addr.in.sin_family = hostentry->h_addrtype;
			address->addr.in.sin_port = htons((unsigned short)port);
			memcpy(&address->addr.in.sin_addr, hostentry->h_addr_list[0], sizeof(address->addr.in.sin_addr));
			for (i = 0;i < (int)sizeof(namecache[namecacheposition].name)-1 && name[i];i++)
				namecache[namecacheposition].name[i] = name[i];
			namecache[namecacheposition].name[i] = 0;
			namecache[namecacheposition].expirationtime = realtime + 12 * 3600; // 12 hours
			namecache[namecacheposition].address = *address;
			namecacheposition = (namecacheposition + 1) % MAX_NAMECACHE;
			resolved = true;
			goto finish;
		}
	}
	for (i = 0;i < (int)sizeof(namecache[namecacheposition].name)-1 && name[i];i++)
		namecache[namecacheposition].name[i] = name[i];
	namecache[namecacheposition].name[i] = 0;
	namecache[namecacheposition].expirationtime = realtime + 12 * 3600; // 12 hours
	namecache[namecacheposition].address.addresstype = LHNETADDRESSTYPE_NONE;
	namecacheposition = (namecacheposition + 1) % MAX_NAMECACHE;
finish:
	if (namecache_mutex_locked) Thread_UnlockMutex(namecache_mutex);
	return resolved;
}
#endif

int LHNETADDRESS_ToString(const lhnetaddress_t *vaddress, char *string, int stringbuffersize, int includeport)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	const unsigned char *a;
	if (!address || !string || stringbuffersize < 1)
		return 0;
	*string = 0;
	switch(address->addresstype)
	{
	default:
		break;
	case LHNETADDRESSTYPE_LOOP:
		if (includeport)
		{
			if (stringbuffersize >= 12)
			{
				dpsnprintf(string, stringbuffersize, "local:%d", address->port);
				return 1;
			}
		}
		else
		{
			if (stringbuffersize >= 6)
			{
				memcpy(string, "local", 6);
				return 1;
			}
		}
		break;
	case LHNETADDRESSTYPE_INET4:
		a = (const unsigned char *)(&address->addr.in.sin_addr);
		if (includeport)
		{
			if (stringbuffersize >= 22)
			{
				dpsnprintf(string, stringbuffersize, "%d.%d.%d.%d:%d", a[0], a[1], a[2], a[3], address->port);
				return 1;
			}
		}
		else
		{
			if (stringbuffersize >= 16)
			{
				dpsnprintf(string, stringbuffersize, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
				return 1;
			}
		}
		break;
#ifndef NOSUPPORTIPV6
	case LHNETADDRESSTYPE_INET6:
		a = (const unsigned char *)(&address->addr.in6.sin6_addr);
		if (includeport)
		{
			if (stringbuffersize >= 88)
			{
				dpsnprintf(string, stringbuffersize, "[%x:%x:%x:%x:%x:%x:%x:%x]:%d", a[0] * 256 + a[1], a[2] * 256 + a[3], a[4] * 256 + a[5], a[6] * 256 + a[7], a[8] * 256 + a[9], a[10] * 256 + a[11], a[12] * 256 + a[13], a[14] * 256 + a[15], address->port);
				return 1;
			}
		}
		else
		{
			if (stringbuffersize >= 80)
			{
				dpsnprintf(string, stringbuffersize, "%x:%x:%x:%x:%x:%x:%x:%x", a[0] * 256 + a[1], a[2] * 256 + a[3], a[4] * 256 + a[5], a[6] * 256 + a[7], a[8] * 256 + a[9], a[10] * 256 + a[11], a[12] * 256 + a[13], a[14] * 256 + a[15]);
				return 1;
			}
		}
		break;
#endif
	}
	return 0;
}

int LHNETADDRESS_GetAddressType(const lhnetaddress_t *address)
{
	if (address)
		return address->addresstype;
	else
		return LHNETADDRESSTYPE_NONE;
}

const char *LHNETADDRESS_GetInterfaceName(const lhnetaddress_t *vaddress, char *ifname, size_t ifnamelength)
{
#ifndef NOSUPPORTIPV6
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;

	if (address && address->addresstype == LHNETADDRESSTYPE_INET6)
	{
#ifndef _WIN32

		if (if_indextoname(address->addr.in6.sin6_scope_id, ifname) == ifname)
			return ifname;

#else

		// The Win32 API doesn't have if_indextoname() until Windows Vista,
		// but luckily it just uses the interface ID as the interface name

		if (dpsnprintf(ifname, ifnamelength, "%lu", address->addr.in6.sin6_scope_id) > 0)
			return ifname;

#endif
	}
#endif

	return NULL;
}

int LHNETADDRESS_GetPort(const lhnetaddress_t *address)
{
	if (!address)
		return -1;
	return address->port;
}

int LHNETADDRESS_SetPort(lhnetaddress_t *vaddress, int port)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	if (!address)
		return 0;
	address->port = port;
	switch(address->addresstype)
	{
	case LHNETADDRESSTYPE_LOOP:
		return 1;
	case LHNETADDRESSTYPE_INET4:
		address->addr.in.sin_port = htons((unsigned short)port);
		return 1;
#ifndef NOSUPPORTIPV6
	case LHNETADDRESSTYPE_INET6:
		address->addr.in6.sin6_port = htons((unsigned short)port);
		return 1;
#endif
	default:
		return 0;
	}
}

int LHNETADDRESS_Compare(const lhnetaddress_t *vaddress1, const lhnetaddress_t *vaddress2)
{
	lhnetaddressnative_t *address1 = (lhnetaddressnative_t *)vaddress1;
	lhnetaddressnative_t *address2 = (lhnetaddressnative_t *)vaddress2;
	if (!address1 || !address2)
		return 1;
	if (address1->addresstype != address2->addresstype)
		return 1;
	switch(address1->addresstype)
	{
	case LHNETADDRESSTYPE_LOOP:
		if (address1->port != address2->port)
			return -1;
		return 0;
	case LHNETADDRESSTYPE_INET4:
		if (address1->addr.in.sin_family != address2->addr.in.sin_family)
			return 1;
		if (memcmp(&address1->addr.in.sin_addr, &address2->addr.in.sin_addr, sizeof(address1->addr.in.sin_addr)))
			return 1;
		if (address1->port != address2->port)
			return -1;
		return 0;
#ifndef NOSUPPORTIPV6
	case LHNETADDRESSTYPE_INET6:
		if (address1->addr.in6.sin6_family != address2->addr.in6.sin6_family)
			return 1;
		if (memcmp(&address1->addr.in6.sin6_addr, &address2->addr.in6.sin6_addr, sizeof(address1->addr.in6.sin6_addr)))
			return 1;
		if (address1->port != address2->port)
			return -1;
		return 0;
#endif
	default:
		return 1;
	}
}

typedef struct lhnetpacket_s
{
	void *data;
	int length;
	int sourceport;
	int destinationport;
	time_t timeout;
	double sentdoubletime;
	struct lhnetpacket_s *next, *prev;
}
lhnetpacket_t;

static int lhnet_active;
static lhnetsocket_t lhnet_socketlist;
static lhnetpacket_t lhnet_packetlist;
static int lhnet_default_dscp = 0;
#ifdef WIN32
static int lhnet_didWSAStartup = 0;
static WSADATA lhnet_winsockdata;
#endif

void LHNET_Init(void)
{
	if (lhnet_active)
		return;
	lhnet_socketlist.next = lhnet_socketlist.prev = &lhnet_socketlist;
	lhnet_packetlist.next = lhnet_packetlist.prev = &lhnet_packetlist;
	lhnet_active = 1;
#ifdef WIN32
	lhnet_didWSAStartup = !WSAStartup(MAKEWORD(1, 1), &lhnet_winsockdata);
	if (!lhnet_didWSAStartup)
		Con_Print("LHNET_Init: WSAStartup failed, networking disabled\n");
#endif
	if (Thread_HasThreads())
		namecache_mutex = Thread_CreateMutex();
}

int LHNET_DefaultDSCP(int dscp)
{
#ifdef IP_TOS
	int prev = lhnet_default_dscp;
	if(dscp >= 0)
		lhnet_default_dscp = dscp;
	return prev;
#else
	return -1;
#endif
}

void LHNET_Shutdown(void)
{
	lhnetpacket_t *p;
	if (!lhnet_active)
		return;
	while (lhnet_socketlist.next != &lhnet_socketlist)
		LHNET_CloseSocket(lhnet_socketlist.next);
	while (lhnet_packetlist.next != &lhnet_packetlist)
	{
		p = lhnet_packetlist.next;
		p->prev->next = p->next;
		p->next->prev = p->prev;
		Z_Free(p);
	}
#ifdef WIN32
	if (lhnet_didWSAStartup)
	{
		lhnet_didWSAStartup = 0;
		WSACleanup();
	}
#endif
	lhnet_active = 0;
	if (namecache_mutex) {
		Thread_DestroyMutex(namecache_mutex);
		namecache_mutex = NULL;
	}
}

static const char *LHNETPRIVATE_StrError(void)
{
#ifdef WIN32
	int i = WSAGetLastError();
	switch (i)
	{
		case WSAEINTR:           return "WSAEINTR";
		case WSAEBADF:           return "WSAEBADF";
		case WSAEACCES:          return "WSAEACCES";
		case WSAEFAULT:          return "WSAEFAULT";
		case WSAEINVAL:          return "WSAEINVAL";
		case WSAEMFILE:          return "WSAEMFILE";
		case WSAEWOULDBLOCK:     return "WSAEWOULDBLOCK";
		case WSAEINPROGRESS:     return "WSAEINPROGRESS";
		case WSAEALREADY:        return "WSAEALREADY";
		case WSAENOTSOCK:        return "WSAENOTSOCK";
		case WSAEDESTADDRREQ:    return "WSAEDESTADDRREQ";
		case WSAEMSGSIZE:        return "WSAEMSGSIZE";
		case WSAEPROTOTYPE:      return "WSAEPROTOTYPE";
		case WSAENOPROTOOPT:     return "WSAENOPROTOOPT";
		case WSAEPROTONOSUPPORT: return "WSAEPROTONOSUPPORT";
		case WSAESOCKTNOSUPPORT: return "WSAESOCKTNOSUPPORT";
		case WSAEOPNOTSUPP:      return "WSAEOPNOTSUPP";
		case WSAEPFNOSUPPORT:    return "WSAEPFNOSUPPORT";
		case WSAEAFNOSUPPORT:    return "WSAEAFNOSUPPORT";
		case WSAEADDRINUSE:      return "WSAEADDRINUSE";
		case WSAEADDRNOTAVAIL:   return "WSAEADDRNOTAVAIL";
		case WSAENETDOWN:        return "WSAENETDOWN";
		case WSAENETUNREACH:     return "WSAENETUNREACH";
		case WSAENETRESET:       return "WSAENETRESET";
		case WSAECONNABORTED:    return "WSAECONNABORTED";
		case WSAECONNRESET:      return "WSAECONNRESET";
		case WSAENOBUFS:         return "WSAENOBUFS";
		case WSAEISCONN:         return "WSAEISCONN";
		case WSAENOTCONN:        return "WSAENOTCONN";
		case WSAESHUTDOWN:       return "WSAESHUTDOWN";
		case WSAETOOMANYREFS:    return "WSAETOOMANYREFS";
		case WSAETIMEDOUT:       return "WSAETIMEDOUT";
		case WSAECONNREFUSED:    return "WSAECONNREFUSED";
		case WSAELOOP:           return "WSAELOOP";
		case WSAENAMETOOLONG:    return "WSAENAMETOOLONG";
		case WSAEHOSTDOWN:       return "WSAEHOSTDOWN";
		case WSAEHOSTUNREACH:    return "WSAEHOSTUNREACH";
		case WSAENOTEMPTY:       return "WSAENOTEMPTY";
		case WSAEPROCLIM:        return "WSAEPROCLIM";
		case WSAEUSERS:          return "WSAEUSERS";
		case WSAEDQUOT:          return "WSAEDQUOT";
		case WSAESTALE:          return "WSAESTALE";
		case WSAEREMOTE:         return "WSAEREMOTE";
		case WSAEDISCON:         return "WSAEDISCON";
		case 0:                  return "no error";
		default:                 return "unknown WSAE error";
	}
#else
	return strerror(errno);
#endif
}

void LHNET_SleepUntilPacket_Microseconds(int microseconds)
{
#ifdef FD_SET
	fd_set fdreadset;
	struct timeval tv;
	int lastfd;
	lhnetsocket_t *s;
	FD_ZERO(&fdreadset);
	lastfd = 0;
	for (s = lhnet_socketlist.next;s != &lhnet_socketlist;s = s->next)
	{
		if (s->address.addresstype == LHNETADDRESSTYPE_INET4 || s->address.addresstype == LHNETADDRESSTYPE_INET6)
		{
			if (lastfd < s->inetsocket)
				lastfd = s->inetsocket;
#if defined(WIN32) && !defined(_MSC_VER)
			FD_SET((int)s->inetsocket, &fdreadset);
#else
			FD_SET((unsigned int)s->inetsocket, &fdreadset);
#endif
		}
	}
	tv.tv_sec = microseconds / 1000000;
	tv.tv_usec = microseconds % 1000000;
	select(lastfd + 1, &fdreadset, NULL, NULL, &tv);
#else
	Sys_Sleep(microseconds);
#endif
}

lhnetsocket_t *LHNET_OpenSocket_Connectionless(lhnetaddress_t *address)
{
	lhnetsocket_t *lhnetsocket, *s;
	if (!address)
		return NULL;
	lhnetsocket = (lhnetsocket_t *)Z_Malloc(sizeof(*lhnetsocket));
	if (lhnetsocket)
	{
		memset(lhnetsocket, 0, sizeof(*lhnetsocket));
		lhnetsocket->address = *address;
		switch(lhnetsocket->address.addresstype)
		{
		case LHNETADDRESSTYPE_LOOP:
			if (lhnetsocket->address.port == 0)
			{
				// allocate a port dynamically
				// this search will always terminate because there is never
				// an allocated socket with port 0, so if the number wraps it
				// will find the port is unused, and then refuse to use port
				// 0, causing an intentional failure condition
				lhnetsocket->address.port = 1024;
				for (;;)
				{
					for (s = lhnet_socketlist.next;s != &lhnet_socketlist;s = s->next)
						if (s->address.addresstype == lhnetsocket->address.addresstype && s->address.port == lhnetsocket->address.port)
							break;
					if (s == &lhnet_socketlist)
						break;
					lhnetsocket->address.port++;
				}
			}
			// check if the port is available
			for (s = lhnet_socketlist.next;s != &lhnet_socketlist;s = s->next)
				if (s->address.addresstype == lhnetsocket->address.addresstype && s->address.port == lhnetsocket->address.port)
					break;
			if (s == &lhnet_socketlist && lhnetsocket->address.port != 0)
			{
				lhnetsocket->next = &lhnet_socketlist;
				lhnetsocket->prev = lhnetsocket->next->prev;
				lhnetsocket->next->prev = lhnetsocket;
				lhnetsocket->prev->next = lhnetsocket;
				return lhnetsocket;
			}
			break;
		case LHNETADDRESSTYPE_INET4:
#ifndef NOSUPPORTIPV6
		case LHNETADDRESSTYPE_INET6:
#endif
#ifdef WIN32
			if (lhnet_didWSAStartup)
			{
#endif
#ifndef NOSUPPORTIPV6
				if ((lhnetsocket->inetsocket = socket(address->addresstype == LHNETADDRESSTYPE_INET6 ? PF_INET6 : PF_INET, SOCK_DGRAM, IPPROTO_UDP)) != -1)
#else
				if ((lhnetsocket->inetsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) != -1)
#endif
				{
#ifdef WIN32
					u_long _false = 0;
#endif
#ifdef MSG_DONTWAIT
					if (1)
#else
#ifdef WIN32
					u_long _true = 1;
#else
					char _true = 1;
#endif
					if (ioctlsocket(lhnetsocket->inetsocket, FIONBIO, &_true) != -1)
#endif
					{
#ifdef IPV6_V6ONLY
						// We need to set this flag to tell the OS that we only listen on IPv6. If we don't
						// most OSes will create a dual-protocol socket that also listens on IPv4. In this case
						// if an IPv4 socket is already bound to the port we want, our bind() call will fail.
						int ipv6_only = 1;
						if (address->addresstype != LHNETADDRESSTYPE_INET6
							|| setsockopt (lhnetsocket->inetsocket, IPPROTO_IPV6, IPV6_V6ONLY,
										   (const char *)&ipv6_only, sizeof(ipv6_only)) == 0
#ifdef WIN32
							// The Win32 API only supports IPV6_V6ONLY since Windows Vista, but fortunately
							// the default value is what we want on Win32 anyway (IPV6_V6ONLY = true)
							|| SOCKETERRNO == WSAENOPROTOOPT
#endif
							)
#endif
						{
							lhnetaddressnative_t *localaddress = (lhnetaddressnative_t *)&lhnetsocket->address;
							SOCKLEN_T namelen;
							int bindresult;

#if defined(SOL_RFC1149) && defined(RFC1149_1149ONLY)
							// we got reports of massive lags when this protocol was chosen as transport
							// so better turn it off
							{
								int rfc1149only = 0;
								int rfc1149enabled = 0;
								if(setsockopt(lhnetsocket->inetsocket, SOL_RFC1149, RFC1149_1149ONLY, &rfc1149only))
									Con_Printf("LHNET_OpenSocket_Connectionless: warning: setsockopt(RFC1149_1149ONLY) returned error: %s\n", LHNETPRIVATE_StrError());
								if(setsockopt(lhnetsocket->inetsocket, SOL_RFC1149, RFC1149_ENABLED, &rfc1149enabled))
									Con_Printf("LHNET_OpenSocket_Connectionless: warning: setsockopt(RFC1149_ENABLED) returned error: %s\n", LHNETPRIVATE_StrError());
							}
#endif

#ifndef NOSUPPORTIPV6
							if (address->addresstype == LHNETADDRESSTYPE_INET6)
							{
								namelen = sizeof(localaddress->addr.in6);
								bindresult = bind(lhnetsocket->inetsocket, &localaddress->addr.sock, namelen);
								if (bindresult != -1)
								{
									if (getsockname(lhnetsocket->inetsocket, &localaddress->addr.sock, &namelen))
									{
										// If getsockname failed, we can assume the bound socket is useless.
										bindresult = -1;
									}
								}
							}
							else
#endif
							{
								namelen = sizeof(localaddress->addr.in);
								bindresult = bind(lhnetsocket->inetsocket, &localaddress->addr.sock, namelen);
								if (bindresult != -1)
								{
									if (getsockname(lhnetsocket->inetsocket, &localaddress->addr.sock, &namelen))
									{
										// If getsockname failed, we can assume the bound socket is useless.
										bindresult = -1;
									}
								}
							}
							if (bindresult != -1)
							{
								int i = 1;
								// enable broadcast on this socket
								setsockopt(lhnetsocket->inetsocket, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i));
#ifdef IP_TOS
								{
									// enable DSCP for ToS support
									int tos = lhnet_default_dscp << 2;
									if (setsockopt(lhnetsocket->inetsocket, IPPROTO_IP, IP_TOS, (char *) &tos, sizeof(tos)))
									{
										// Error in setsockopt - fine, we'll simply set no TOS then.
									}
								}
#endif
								lhnetsocket->next = &lhnet_socketlist;
								lhnetsocket->prev = lhnetsocket->next->prev;
								lhnetsocket->next->prev = lhnetsocket;
								lhnetsocket->prev->next = lhnetsocket;
#ifdef WIN32
								if (ioctlsocket(lhnetsocket->inetsocket, SIO_UDP_CONNRESET, &_false) == -1)
									Con_DPrintf("LHNET_OpenSocket_Connectionless: ioctlsocket SIO_UDP_CONNRESET returned error: %s\n", LHNETPRIVATE_StrError());
#endif
								return lhnetsocket;
							}
							else
								Con_Printf("LHNET_OpenSocket_Connectionless: bind returned error: %s\n", LHNETPRIVATE_StrError());
						}
#ifdef IPV6_V6ONLY
						else
							Con_Printf("LHNET_OpenSocket_Connectionless: setsockopt(IPV6_V6ONLY) returned error: %s\n", LHNETPRIVATE_StrError());
#endif
					}
					else
						Con_Printf("LHNET_OpenSocket_Connectionless: ioctlsocket returned error: %s\n", LHNETPRIVATE_StrError());
					closesocket(lhnetsocket->inetsocket);
				}
				else
					Con_Printf("LHNET_OpenSocket_Connectionless: socket returned error: %s\n", LHNETPRIVATE_StrError());
#ifdef WIN32
			}
			else
				Con_Print("LHNET_OpenSocket_Connectionless: can't open a socket (WSAStartup failed during LHNET_Init)\n");
#endif
			break;
		default:
			break;
		}
		Z_Free(lhnetsocket);
	}
	return NULL;
}

void LHNET_CloseSocket(lhnetsocket_t *lhnetsocket)
{
	if (lhnetsocket)
	{
		// unlink from socket list
		if (lhnetsocket->next == NULL)
			return; // invalid!
		lhnetsocket->next->prev = lhnetsocket->prev;
		lhnetsocket->prev->next = lhnetsocket->next;
		lhnetsocket->next = NULL;
		lhnetsocket->prev = NULL;

		// no special close code for loopback, just inet
		if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_INET4 || lhnetsocket->address.addresstype == LHNETADDRESSTYPE_INET6)
		{
			closesocket(lhnetsocket->inetsocket);
		}
		Z_Free(lhnetsocket);
	}
}

lhnetaddress_t *LHNET_AddressFromSocket(lhnetsocket_t *sock)
{
	if (sock)
		return &sock->address;
	else
		return NULL;
}

int LHNET_Read(lhnetsocket_t *lhnetsocket, void *content, int maxcontentlength, lhnetaddress_t *vaddress)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	int value = 0;
	if (!lhnetsocket || !address || !content || maxcontentlength < 1)
		return -1;
	if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_LOOP)
	{
		time_t currenttime;
		lhnetpacket_t *p, *pnext;
		// scan for any old packets to timeout while searching for a packet
		// that is waiting to be delivered to this socket
		currenttime = time(NULL);
		for (p = lhnet_packetlist.next;p != &lhnet_packetlist;p = pnext)
		{
			pnext = p->next;
			if (p->timeout < currenttime)
			{
				// unlink and free
				p->next->prev = p->prev;
				p->prev->next = p->next;
				Z_Free(p);
				continue;
			}
			if (cl_netlocalping.value && (realtime - cl_netlocalping.value * (1.0 / 2000.0)) < p->sentdoubletime)
				continue;
			if (value == 0 && p->destinationport == lhnetsocket->address.port)
			{
				if (p->length <= maxcontentlength)
				{
					lhnetaddressnative_t *localaddress = (lhnetaddressnative_t *)&lhnetsocket->address;
					*address = *localaddress;
					address->port = p->sourceport;
					memcpy(content, p->data, p->length);
					value = p->length;
				}
				else
					value = -1;
				// unlink and free
				p->next->prev = p->prev;
				p->prev->next = p->next;
				Z_Free(p);
			}
		}
	}
	else if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_INET4)
	{
		SOCKLEN_T inetaddresslength;
		address->addresstype = LHNETADDRESSTYPE_NONE;
		inetaddresslength = sizeof(address->addr.in);
		value = recvfrom(lhnetsocket->inetsocket, (char *)content, maxcontentlength, LHNET_RECVFROM_FLAGS, &address->addr.sock, &inetaddresslength);
		if (value > 0)
		{
			address->addresstype = LHNETADDRESSTYPE_INET4;
			address->port = ntohs(address->addr.in.sin_port);
			return value;
		}
		else if (value < 0)
		{
			int e = SOCKETERRNO;
			if (e == EWOULDBLOCK)
				return 0;
			switch (e)
			{
				case ECONNREFUSED:
					Con_Print("Connection refused\n");
					return 0;
			}
			Con_DPrintf("LHNET_Read: recvfrom returned error: %s\n", LHNETPRIVATE_StrError());
		}
	}
#ifndef NOSUPPORTIPV6
	else if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_INET6)
	{
		SOCKLEN_T inetaddresslength;
		address->addresstype = LHNETADDRESSTYPE_NONE;
		inetaddresslength = sizeof(address->addr.in6);
		value = recvfrom(lhnetsocket->inetsocket, (char *)content, maxcontentlength, LHNET_RECVFROM_FLAGS, &address->addr.sock, &inetaddresslength);
		if (value > 0)
		{
			address->addresstype = LHNETADDRESSTYPE_INET6;
			address->port = ntohs(address->addr.in6.sin6_port);
			return value;
		}
		else if (value == -1)
		{
			int e = SOCKETERRNO;
			if (e == EWOULDBLOCK)
				return 0;
			switch (e)
			{
				case ECONNREFUSED:
					Con_Print("Connection refused\n");
					return 0;
			}
			Con_DPrintf("LHNET_Read: recvfrom returned error: %s\n", LHNETPRIVATE_StrError());
		}
	}
#endif
	return value;
}

int LHNET_Write(lhnetsocket_t *lhnetsocket, const void *content, int contentlength, const lhnetaddress_t *vaddress)
{
	lhnetaddressnative_t *address = (lhnetaddressnative_t *)vaddress;
	int value = -1;
	if (!lhnetsocket || !address || !content || contentlength < 1)
		return -1;
	if (lhnetsocket->address.addresstype != address->addresstype)
		return -1;
	if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_LOOP)
	{
		lhnetpacket_t *p;
		p = (lhnetpacket_t *)Z_Malloc(sizeof(*p) + contentlength);
		p->data = (void *)(p + 1);
		memcpy(p->data, content, contentlength);
		p->length = contentlength;
		p->sourceport = lhnetsocket->address.port;
		p->destinationport = address->port;
		p->timeout = time(NULL) + 10;
		p->next = &lhnet_packetlist;
		p->prev = p->next->prev;
		p->next->prev = p;
		p->prev->next = p;
		p->sentdoubletime = realtime;
		value = contentlength;
	}
	else if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_INET4)
	{
		value = sendto(lhnetsocket->inetsocket, (char *)content, contentlength, LHNET_SENDTO_FLAGS, (struct sockaddr *)&address->addr.in, sizeof(struct sockaddr_in));
		if (value == -1)
		{
			if (SOCKETERRNO == EWOULDBLOCK)
				return 0;
			Con_DPrintf("LHNET_Write: sendto returned error: %s\n", LHNETPRIVATE_StrError());
		}
	}
#ifndef NOSUPPORTIPV6
	else if (lhnetsocket->address.addresstype == LHNETADDRESSTYPE_INET6)
	{
		value = sendto(lhnetsocket->inetsocket, (char *)content, contentlength, 0, (struct sockaddr *)&address->addr.in6, sizeof(struct sockaddr_in6));
		if (value == -1)
		{
			if (SOCKETERRNO == EWOULDBLOCK)
				return 0;
			Con_DPrintf("LHNET_Write: sendto returned error: %s\n", LHNETPRIVATE_StrError());
		}
	}
#endif
	return value;
}
