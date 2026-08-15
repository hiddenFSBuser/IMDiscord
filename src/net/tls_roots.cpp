// The trust store, in one place.
//
// tlse ships it as a header that defines a static array, which means whatever
// includes it gets a private copy. Including it exactly once and handing the
// pointer out keeps a quarter of a megabyte from being duplicated, and keeps
// the giant literal out of every other translation unit's compile time.

#include "pch.h"
#include "tls_root_ca.h"

extern "C" const char* tlsnet_root_ca_pem();

const char* tlsnet_root_ca_pem()
{
    return ROOT_CA_DEF;
}
