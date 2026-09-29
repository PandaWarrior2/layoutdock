#pragma once

// Shared by the About section and the executable's version resource.
#define LAYOUTDOCK_VERSION "1.0"
#define LAYOUTDOCK_VERSION_NUMBER 1,0,0,0
#define LAYOUTDOCK_AUTHOR "Moonl1ght"
#define LAYOUTDOCK_REPOSITORY "https://github.com/PandaWarrior2/layoutdock"

#ifndef RC_INVOKED
#define LAYOUTDOCK_WIDE_IMPL(value) L##value
#define LAYOUTDOCK_WIDE(value) LAYOUTDOCK_WIDE_IMPL(value)
namespace dock {
inline constexpr wchar_t AppVersion[] = LAYOUTDOCK_WIDE(LAYOUTDOCK_VERSION);
inline constexpr wchar_t AppAuthor[] = LAYOUTDOCK_WIDE(LAYOUTDOCK_AUTHOR);
inline constexpr wchar_t RepositoryUrl[] = LAYOUTDOCK_WIDE(LAYOUTDOCK_REPOSITORY);
}
#undef LAYOUTDOCK_WIDE
#undef LAYOUTDOCK_WIDE_IMPL
#endif
