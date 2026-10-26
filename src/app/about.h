#ifndef ENCRYPTED_APP_ABOUT_H
#define ENCRYPTED_APP_ABOUT_H

#include <string>

#ifndef ENCRYPTED_APP_AUTHOR
#define ENCRYPTED_APP_AUTHOR "Unknown"
#endif
#ifndef ENCRYPTED_APP_VERSION
#define ENCRYPTED_APP_VERSION "Unknown"
#endif
#ifndef ENCRYPTED_LUA_VERSION
#define ENCRYPTED_LUA_VERSION "Unknown"
#endif
#ifndef ENCRYPTED_MONOCYPHER_VERSION
#define ENCRYPTED_MONOCYPHER_VERSION "Unknown"
#endif
#ifndef ENCRYPTED_FTXUI_VERSION
#define ENCRYPTED_FTXUI_VERSION "Unknown"
#endif

inline std::string about_text(const std::string &program, const std::string &ui_dependency)
{
    return program + "\nVersion: " ENCRYPTED_APP_VERSION
        "\nAuthor: " ENCRYPTED_APP_AUTHOR
        "\nLicense: GNU GPL version 3 (GPLv3)"
        "\n\nDependencies:\n" + ui_dependency +
        "\nLua " ENCRYPTED_LUA_VERSION " - MIT (plugin runtime)"
        "\nMonocypher " ENCRYPTED_MONOCYPHER_VERSION " - BSD-2-Clause / CC0 (cryptography)"
        "\nmicroz 1.0 - MIT (compression)"
        "\n\nCompression: raw DEFLATE"
        "\nEncryption: XChaCha20-Poly1305"
        "\nKey derivation: Argon2id";
}
#endif
