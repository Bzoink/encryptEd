#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/file_io.h"

#define REQUIRE(expr) do { if (!(expr)) { fprintf(stderr, "Line %d: %s (error %lu)\n", \
    __LINE__, #expr, (unsigned long)GetLastError()); goto cleanup; } } while (0)

int main(void)
{
    wchar_t temp[MAX_PATH], path[MAX_PATH] = {0};
    char utf8[MAX_PATH * 4];
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    DWORD bytes = 0;
    PACL initial_acl = NULL, before_acl = NULL, after_acl = NULL, custom_acl = NULL;
    PSECURITY_DESCRIPTOR initial = NULL, before = NULL, after = NULL;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID system_sid = NULL;
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;
    int result = 1;
    const uint8_t text[] = "synthetic private plaintext";
    REQUIRE(GetTempPathW(MAX_PATH, temp));
    REQUIRE(GetTempFileNameW(temp, L"txx", 0, path));
    REQUIRE(DeleteFileW(path));
    REQUIRE(WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL));
    REQUIRE(txx_write_file_atomic(utf8, text, sizeof(text) - 1) == TXX_OK);
    REQUIRE(GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &initial_acl, NULL, &initial) == ERROR_SUCCESS);
    REQUIRE(initial_acl && initial_acl->AceCount == 1);
    REQUIRE(GetSecurityDescriptorControl(initial, &control, &revision));
    REQUIRE(control & SE_DACL_PROTECTED);
    REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    user = (TOKEN_USER *)malloc(bytes);
    REQUIRE(user && GetTokenInformation(token, TokenUser, user, bytes, &bytes));
    {
        ACCESS_ALLOWED_ACE *ace;
        REQUIRE(GetAce(initial_acl, 0, (void **)&ace));
        REQUIRE(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                EqualSid(&ace->SidStart, user->User.Sid));
    }
    // Give the original file a distinctive ACL that a fresh temp file lacks.
    REQUIRE(AllocateAndInitializeSid(&authority, 1, SECURITY_LOCAL_SYSTEM_RID,
                                      0, 0, 0, 0, 0, 0, 0, &system_sid));
    bytes = sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
            GetLengthSid(user->User.Sid) + GetLengthSid(system_sid);
    custom_acl = (PACL)malloc(bytes);
    REQUIRE(custom_acl && InitializeAcl(custom_acl, bytes, ACL_REVISION));
    REQUIRE(AddAccessAllowedAce(custom_acl, ACL_REVISION, FILE_ALL_ACCESS, user->User.Sid));
    REQUIRE(AddAccessAllowedAce(custom_acl, ACL_REVISION, FILE_GENERIC_READ, system_sid));
    REQUIRE(SetNamedSecurityInfoW(path, SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        NULL, NULL, custom_acl, NULL) == ERROR_SUCCESS);
    REQUIRE(GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &before_acl, NULL, &before) == ERROR_SUCCESS);
    REQUIRE(txx_write_file_atomic(utf8, text, sizeof(text) - 1) == TXX_OK);
    REQUIRE(GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &after_acl, NULL, &after) == ERROR_SUCCESS);
    REQUIRE(before_acl && after_acl && before_acl->AceCount == after_acl->AceCount);
    for (DWORD i = 0; i < before_acl->AceCount; ++i) {
        ACE_HEADER *a, *b;
        REQUIRE(GetAce(before_acl, i, (void **)&a) && GetAce(after_acl, i, (void **)&b));
        REQUIRE(a->AceSize == b->AceSize && memcmp(a, b, a->AceSize) == 0);
    }
    REQUIRE(GetSecurityDescriptorControl(after, &control, &revision));
    REQUIRE(control & SE_DACL_PROTECTED);
    result = 0;
cleanup:
    if (*path) DeleteFileW(path);
    if (token) CloseHandle(token);
    if (system_sid) FreeSid(system_sid);
    LocalFree(initial); LocalFree(before); LocalFree(after);
    free(custom_acl); free(user);
    return result;
}
