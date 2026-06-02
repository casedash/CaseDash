#ifndef FORMAT_IFDEF_FIXTURE_HPP
#define FORMAT_IFDEF_FIXTURE_HPP

// Golden fixture for allowed conditional preprocessor formatting.
// This project forbids ifdefs, and these examples come only from userver, so the fixture
// is named format_ifdef_* rather than format_userver_ifdef_*. Keep future userver examples
// that patch whole declarations, statements, fields, methods, macros, or includes here.
// Keep conditional fragments inside expressions or declarations in format_error_input.cpp.

#include <userver/utils/assert.hpp>
#include <algorithm>
#include <boost/atomic/atomic.hpp>

#include "src/kafka/impl/consumer_impl.hpp"
#include <userver/utest/utest.hpp>
#include <fmt/format.h>
#include <string_view>
#include "userver/chaotic/io/my/custom_object.hpp"

#include <userver/logging/log.hpp>
#include <google/protobuf/descriptor.h>
#include <grpcpp/grpcpp.h>
#include <array>

#if FORMAT_USERVER_USE_SYSTEM_HEADER
#include <format_userver/system.hpp>
#else
#include "format_userver/system.hpp"
#endif

#ifdef FORMAT_USERVER_PROTECT_ATTR
#define FORMAT_USERVER_PROTECTED_ATTR __attribute__((noinline, flatten))
#else
#define FORMAT_USERVER_PROTECTED_ATTR __attribute__((always_inline, flatten))
#endif

#ifdef FORMAT_USERVER_HAS_ATTRIBUTE
#if FORMAT_USERVER_HAS_NODEBUG
#define USERVER_IMPL_NODEBUG __attribute__((__nodebug__))
#define USERVER_IMPL_NODEBUG_INLINE_FUNC __attribute__((__nodebug__, __always_inline__))
#elif FORMAT_USERVER_HAS_ALWAYS_INLINE
// GCC may have no __nodebug__ attribute.
#define USERVER_IMPL_NODEBUG_INLINE_FUNC __attribute__((__always_inline__))
#endif
#endif

#if FORMAT_USERVER_LEGACY_FMT
#define FORMAT_USERVER_CONST
#else
#define FORMAT_USERVER_CONST const
#endif

#if FORMAT_USERVER_HAS_NAMESPACE_ALIAS
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CURL_FORMAT_USERVER_NAMESPACE fixture::
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CURL_FORMAT_USERVER_NAMESPACE
#endif

namespace format_userver_fixture {

#if FORMAT_USERVER_LEGACY_FMT
template <typename S>
const S& LegacyRuntime(const S& s) {
    return s;
}
#endif

#if defined(FORMAT_USERVER_PLATFORM) && __has_include(<format_userver/header.hpp>)
void HasIncludeGuardedFunction() {
    UsePlatformHeader();
}
#else
void HasIncludeGuardedFallback() {
    UseFallbackHeader();
}
#endif

void ConditionalStatementGuard() {
#ifdef SOCK_CLOEXEC
    x &= ~SOCK_CLOEXEC;
#endif
#include "format_userver_statement.inc"
    UseAfterInclude();
}

void ConditionalLocalDeclaration() {
#ifdef FORMAT_USERVER_LIBCPP
    const std::string expected = "libcpp";
#else
    const std::string expected = "stdlib";
#endif
    Use(expected);
}

namespace include_after_comment {}  // namespace include_after_comment

#include "format_userver_after_namespace.inc"

struct ConditionalMembers {
#ifdef FORMAT_USERVER_HAS_FIELD
    int value;
#else
    void Value();
#endif
#ifndef FORMAT_USERVER_DISABLE_METHOD
    void Method();
#endif
};

#if FORMAT_USERVER_HAS_DECLARATION
constexpr int kConditionalDeclaration = 1;
#else
constexpr int kConditionalDeclaration = 0;
#endif

}  // namespace format_userver_fixture

#endif  // FORMAT_IFDEF_FIXTURE_HPP
