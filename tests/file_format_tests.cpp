#include "app/file_format.h"
#include <cstdio>

int main()
{
    for (const char *name : {"secret.txx", "secret.TXX", "secret.TxX", "folder/secret.tXx"}) {
        if (!has_txx_extension(name)) {
            std::fprintf(stderr, "Encrypted extension misclassified: %s\n", name);
            return 1;
        }
    }
    for (const char *name : {"", "txx", "secret.txt", "folder.txx/secret", "secret.txx.txt"}) {
        if (has_txx_extension(name)) return 1;
    }
    return 0;
}
