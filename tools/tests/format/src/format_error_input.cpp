namespace format_error_fixture {

void ExpressionFragment() {
    constexpr int kOptmask =
        ARES_OPT_FLAGS | ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES | ARES_OPT_DOMAINS |
#if ARES_VERSION < 0x011400
        ARES_OPT_SOCK_STATE_CB |
#endif
        ARES_OPT_LOOKUPS;
}

extern "C" {
#ifndef FORMAT_USERVER_CLANG
[[gnu::visibility("default")]] [[gnu::externally_visible]]
#endif
int FormatUserverExternAttribute();
}

constexpr utils::StringLiteral kFormatUserverPrefixes[] = {
#ifdef FORMAT_USERVER_PREFIX
    FORMAT_USERVER_STRINGIZE(FORMAT_USERVER_PREFIX),
#endif
#ifdef FORMAT_USERVER_SOURCE_PREFIX
    FORMAT_USERVER_STRINGIZE(FORMAT_USERVER_SOURCE_PREFIX),
#endif
};

template <typename T>
concept FormatUserverConvertible =
    requires(T& value) { FormatUserverConvert(value); } &&
#if FORMAT_USERVER_OLD_LIB
    // Old libraries reject long double here.
    !std::same_as<T, long double>
#else
    true
#endif
    ;

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
        "argument label"
    );
    Open(
#ifdef FORMAT_USERVER_FLAG_A
        kFlagA |
#endif
#ifdef FORMAT_USERVER_FLAG_B
        kFlagB |
#endif
        kBaseFlag
    );
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
    return TimestampToJsonFailureTestParam{
        TimestampMessageData{0, kMaxTimestampNanos + 1},
        PrintErrorCode::kInvalidValue,
        "field1",
        {},
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
        PreprocessorSpecifierFixture(const char* value) noexcept
        : value{value} {}

private:
    const char* value;
};

void IncludeExpressionFragment() {
    int value =
#include "format_userver_value.inc"
        1;
}

}  // namespace format_error_fixture
