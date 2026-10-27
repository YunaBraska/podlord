#include <QtGlobal>
#include <openssl/bio.h>

extern "C" BIO* __real_BIO_new_mem_buf(const void*, int);

extern "C" BIO* __wrap_BIO_new_mem_buf(const void* data, int length) {
    if (qEnvironmentVariableIsSet("PODLORD_TEST_BIO_ALLOCATION_FAILURE")) return nullptr;
    return __real_BIO_new_mem_buf(data, length);
}
