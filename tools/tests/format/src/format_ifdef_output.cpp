#ifndef FORMAT_IFDEF_FIXTURE_HPP
#define FORMAT_IFDEF_FIXTURE_HPP

// Golden fixture for conditional preprocessor formatting.
// This project forbids ifdefs, and these examples come only from userver, so the fixture
// is named format_ifdef_* rather than format_userver_ifdef_*. Keep future userver examples
// with #if/#ifdef/#ifndef here; keep non-conditional userver examples in format_userver_*.
// The guarded header shape also exercises include preservation away from the common userver #pragma once path.

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
namespace compat_userver {
template <typename S>
const S& runtime(const S& s) {
return s;
}
}
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

extern "C" {
#ifndef FORMAT_USERVER_CLANG
[[gnu::visibility("default")]] [[gnu::externally_visible]]
#endif
    int FormatUserverExternAttribute();
}

namespace format_userver_fixture {

constexpr utils::StringLiteral kFormatUserverPrefixes[] = {
#ifdef FORMAT_USERVER_PREFIX
    FORMAT_USERVER_STRINGIZE(FORMAT_USERVER_PREFIX),
#endif
#ifdef FORMAT_USERVER_SOURCE_PREFIX
    FORMAT_USERVER_STRINGIZE(FORMAT_USERVER_SOURCE_PREFIX),
#endif
};

template <typename T>
concept FormatUserverConvertible = requires(T& value) {
    FormatUserverConvert(value);
} &&
#if FORMAT_USERVER_OLD_LIB
    // Old libraries reject long double here.
    !std::same_as<T, long double>
#else
    true
#endif
;

#if FORMAT_USERVER_DISABLED_THREADS
UTEST_MT(FormatterMacroFixture, DISABLED_ConditionalThreads, 2) {
#else
UTEST_MT(FormatterMacroFixture, ConditionalThreads, 2) {
#endif
    RunConditionalThreadedTest();
}

#if defined(FORMAT_USERVER_PLATFORM) && __has_include(<format_userver/header.hpp>)
void HasIncludeGuardedFunction() {
    UsePlatformHeader();
}
#else
void HasIncludeGuardedFallback() {
    UseFallbackHeader();
}
#endif

extern int* ConditionalDeclarationSuffix(void)
#ifdef FORMAT_USERVER_THROW
    FORMAT_USERVER_THROW
#endif
;

void ConditionalLocalConstQualifier() {
#if FORMAT_USERVER_OPENSSL_HAS_CONST_SIGNATURE
const
#endif
    ASN1_BIT_STRING* signature = nullptr;
    UseSignature(signature);
}

constexpr int ConditionalExpressionFragment = kFirst |
#if FORMAT_USERVER_HAS_SECOND
    kSecond |
#endif
kThird;

bool ConditionalLogicalFragment(int error_code) {
    if (error_code == kWouldBlock
#if FORMAT_USERVER_HAS_DUPLICATE_WOULD_BLOCK
    || error_code == kAgain
#endif
    ) {
        return true;
    }
    return false;
}

bool ConditionalMultiLineLogicalFragment(Connection* conn) {
    if (conn->xactStatus != kInTransaction
#if FORMAT_USERVER_PIPELINE_STATUS
&& (conn->pipelineStatus == kPipelineOff ||
conn->asyncStatus == kAsyncIdle)
#endif
    ) {
        return true;
    }
    return false;
}

void PreprocessorSelectedIfHeader(Connection* conn) {
#if FORMAT_USERVER_PIPELINE_STATUS
if (conn->pipelineStatus == kPipelineOff)
#else
if (Flush(conn) < 0)
#endif
    goto sendFailed;
    sendFailed:;
}

void PreprocessorSelectedBracedIf(Connection* conn, std::string& status) {
#if FORMAT_USERVER_NEW_MONGO
if (HasReadableServer(conn)) {
#else
if (HasReadableServer(const_cast<Connection*>(conn))) {
#endif
status.append("Secondary AVAILABLE");
} else {
status.append("Secondary UNAVAILABLE");
}
}

bool ConditionalWholeCondition(int error_code) {
    if (
#if FORMAT_USERVER_USE_WOULD_BLOCK
error_code == kWouldBlock
#else
error_code == kAgain
#endif
    ) {
        return true;
    }
    return false;
}

void ConditionalArgumentFragment() {
    Use(
#ifdef FORMAT_USERVER_FAST_ARGUMENT
FastArgument(),
#else
SlowArgument(),
#endif
    "argument label");
    Open(
#ifdef FORMAT_USERVER_FLAG_A
kFlagA |
#endif
#ifdef FORMAT_USERVER_FLAG_B
kFlagB |
#endif
    kBaseFlag);
}

void ConditionalStreamingAssertion() {
#ifndef FORMAT_USERVER_ARCADIA
// Test flaps on external CI.
GTEST_SKIP()
#else
FAIL()
#endif
    << "failed to trigger failures";
}

void PreprocessorSelectedInitializer(DescriptorPool* descriptor_pool, std::string_view file_name) {
    const Descriptor* file_desc =
#if FORMAT_USERVER_PROTOBUF_GE_4022000
descriptor_pool->FindFileByName(file_name);
#else
descriptor_pool->FindFileByName(std::string{file_name});
#endif
    Use(file_desc);
}

auto PreprocessorSelectedListItem() {
    return TimestampToJsonFailureTestParam{TimestampMessageData{
        0,
        kMaxTimestampNanos + 1
    }, PrintErrorCode::kInvalidValue, "field1", {},
#if FORMAT_USERVER_PROTOBUF_GE_6033000
false
#else
true
#endif
    };
}

void PreprocessorEndedConsequence(Status status, Handle& handle, Handle next_handle) {
#if FORMAT_USERVER_HAS_PIPELINING
if (status == Status::kSync) {
HandlePipelineSync();
} else if (status != Status::kAborted)
#endif
handle = std::move(next_handle);
}

const char* ConditionalStringLiteral() {
    return "prefix "
#if FORMAT_USERVER_USE_UTC
"UTC "
#else
"GMT "
#endif
    "suffix";
}

class PreprocessorSpecifierFixture {
public:
#if FORMAT_USERVER_USE_CONSTEXPR
    // Older compilers keep this path constexpr.
    constexpr
#else
    consteval
#endif
    PreprocessorSpecifierFixture(const char* value) noexcept : value{value} {}

private:
    const char* value;
};

}  // namespace format_userver_fixture

#endif  // FORMAT_IFDEF_FIXTURE_HPP
