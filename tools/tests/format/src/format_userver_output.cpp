#pragma once

// Golden fixture for userver formatting without conditional preprocessing.
// Keep userver examples that do not use #if/#ifdef/#ifndef here. Conditional preprocessor
// examples come only from userver and live in format_ifdef_input.cpp and format_ifdef_output.cpp,
// without "userver" in the file names; place future userver examples by that boundary.
// Mirrors userver .clang-format include setting: IncludeBlocks: Preserve.

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

#define FORMAT_USERVER_DO_WHILE(flag) do { \
        if (flag) { \
            break; \
        } \
        UseFlag(flag); \
    } while (false)
#define USERVER_IMPL_FORCE_INLINE __attribute__((always_inline)) inline
#define LOG_FORMAT_USERVER_LIMITED(logger, level, ...) \
    if (const RateLimiter limiter{[]() -> RateLimitData& { \
            static RateLimitData data; \
            return data; \
        }()}; \
        !limiter.ShouldLog()) \
    { \
    } else \
        LOG_TO((logger), (level), __VA_ARGS__) << limiter
#define FORMAT_USERVER_COMPLEX_OPTION(FUNCTION_NAME, OPTION_TYPE) \
    inline void FUNCTION_NAME(OPTION_TYPE arg) { \
        UseOption(arg, PP_STRINGIZE(FUNCTION_NAME)); \
    }
#define FORMAT_USERVER_HASH_JOIN(FUNCTION_NAME) \
private: \
    inline void FUNCTION_NAME##_impl() {} \
public: \
    static constexpr bool is_##FUNCTION_NAME##_available = true
#define FORMAT_USERVER_EXPECT_TRY(cmd) \
    try { \
        cmd; \
    } catch (const Error& error) { \
        EXPECT_EQ(error.Code(), ErrorCode::kExpected); \
    }
#define FORMAT_USERVER_BENCHMARK_ARGS ->Arg(2)->Arg(4)
#define IMPL_UTEST_FORMAT_USERVER(name) \
    TestLauncher<::testing::Test>::RunTest<name>(); \
    struct FormatUserverForceSemicolon

BENCHMARK_CAPTURE(FormatterBenchmark, Mode, kValue) FORMAT_USERVER_BENCHMARK_ARGS;
BENCHMARK_INSTANTIATE_TEMPLATE_F(FormatterBenchmark, Value, int);
BENCHMARK_DEFINE_TEMPLATE_F(FormatterBenchmark, Value)(benchmark::State& state) {
    UseBenchmarkState(state);
}
BENCHMARK_DEFINE_F(FormatterBenchmark, Inline)(benchmark::State& state) {
    UseBenchmarkState(state);
}

namespace format_userver_fixture {

template <typename Output>
USERVER_IMPL_FORCE_INLINE Output FormatUserverInline(Output output) {
    return output;
}

template <typename Enum>
Flags<Enum> FormatUserverFlagsOr(Flags<Enum> lhs) {
    return Flags<Enum>{lhs} |= lhs;
}

namespace macro_boundary_fixture {}

class MockMethodFixture {
public:
    using typename Base::GetFunc;
    MOCK_METHOD(void, PreSendMessage, (Context&, (const CompletionStatus&)), (const, override));
};

class MacroFieldDeclarationHost {
public:
    MOCK_METHOD(void, SetValue, (std::string_view, std::string&&), (override));
};

UTEST_DEATH(FormatterMacroFixture, KeepsBody) {
    RunDeathTest();
}

UTEST_MT(FormatterMacroFixture, KeepsThreads, 2) {
    RunThreadedTest();
}

TYPED_UTEST_SUITE_P(FormatterTypedFixture);

TYPED_UTEST_P_MT(FormatterTypedFixture, KeepsTypedThreads, 2) {
    RunTypedThreadedTest<TypeParam>();
}

TYPED_UTEST_MT(FormatterTypedFixture, KeepsDirectTypedThreads, 2) {
    RunDirectTypedThreadedTest<TypeParam>();
}

INSTANTIATE_UTEST_SUITE_P(/* no prefix */, FormatterMacroFixture, testing::Values(true));

BENCHMARK_TEMPLATE(FormatterBenchmark, std::string)->Range(1, 8);

void DependentTemplateMemberCall(DependentStorage& storage) {
    storage.template Emplace(tag, MakeValue());
}

void DecltypeBracedSentinel(Iterator it) {
    for (; it != decltype(it){}; ++it) {}
}

void MacroConcatenatedString() {
    throw Error("prefix " FORMAT_USERVER_VERSION " suffix");
}

void QualifiedTemplateCompoundLiteral(Token token, Writer& writer) {
    WriteToStream(
        fixture::chaotic::Primitive<std::string, fixture::chaotic::MinLength<128>, fixture::chaotic::MaxLength<128>>{
            *token
        },
        writer
    );
}

auto BracedLambdaCapture(std::vector<int> inputs, CaptureSettings settings) {
    return [inputs{std::move(inputs)}, settings{std::move(settings)}]() mutable {
        return inputs.size() + settings.count;
    };
}

FORMAT_USERVER_NODEBUG_FUNC inline decltype(auto) PrefixedInlineFunction(Function&& function) {
    return function();
}

class PrefixedAttributeMethodFixture {
public:
    template <typename T>
    FORMAT_USERVER_PROTECTED_ATTR bool Compare(T& value) noexcept {
        return value.Compare();
    }
};

Metric* ContextualRefIdentifier(Storage& storage) {
    auto ref = storage.Find();
    if (ref) {
        return &ref->metric;
    }
    return nullptr;
}

extern template class ExplicitTemplateInstantiation<ExplicitOptions>;
template class ExplicitTemplateInstantiation<RuntimeOptions>;

struct MacroInitializerFixture {
    std::atomic_flag started FORMAT_USERVER_ATOMIC_INIT;
};

static const std::size_t gnu_attribute_value __attribute__((used)) = FormatterTraits::page_size();

void DeclarationMacroArgument(Source& source) {
    UEXPECT_THROW([[maybe_unused]] auto bytes_read = source.ReadSome(kBuffer, kDeadline), IoTimeout);
}

void SplitConstDeclarationMacroArgument(Source& source) {
    EXPECT_THROW(
        [[maybe_unused]] const auto bytes_read =
            source.ReadSome(kVeryLongBufferNameForFormatterFixture, kVeryLongDeadlineNameForFormatterFixture),
        IoTimeout
    );
}

void ThrowExpressionMacroArgument() {
    UEXPECT_THROW(throw ErrorType(), ErrorType);
}

void PlainDeclarationMacroArgument() {
    UEXPECT_THROW(auto future = Client().SayHello(request), std::runtime_error);
    UEXPECT_NO_THROW(const auto stream = Client().ReadMany(request));
}

void StatementSequenceMacroArgument() {
    UEXPECT_THROW(
        crypto::SslCtx context = MakeContext();
        static_cast<void>(UseContext(context)),
        IoException
    );
}

Value ConditionalThrowExpression(bool enabled) {
    return enabled ? throw ErrorType() : Value{};
}

void ThrowFoldExpression(ErrorContext context, std::size_t processed_bytes) {
    throw(IoCancelled(/*bytes_transferred =*/processed_bytes) << ... << context);
}

void QualifiedOperatorCall(Task& task) {
    task.TaskBase::operator=(Task{});
}

void CapitalizedHelperCall() {
    SetHttpProxy(target, channel_args, factory.GetAuthType(), proxy_address);
}

DateParts OperatorConversionCall(DatePartsParts ymd) {
    return {ymd.year().operator int(), ymd.month().operator unsigned int()};
}

template <typename T>
concept HasNonEmptyName = requires {
    requires !std::string_view{T::kName}.empty();
};

template <typename T>
struct DetectedBufferCategory : decltype(DetectBufferCategory<T>()) {};

template <
    typename RedisRequestType,
    typename... Args,
    typename M = RedisRequestType (storages::redis::Client::*)(Args..., const redis::CommandControl&)
>
HedgedRedisRequest<RedisRequestType> MakeHedgedRedisRequestAsync(M method, Args... args);

template <typename StringViews, typename DistanceFunc = std::size_t (*)(std::string_view, std::string_view)>
std::optional<std::string_view> GetNearestString(const StringViews& strings, DistanceFunc distance_func);

template <typename StorageTag>
void TemplateVariableAssignment(std::size_t alignment) {
    data_offset<StorageTag> += (alignment - (data_offset<StorageTag> % alignment)) % alignment;
    count<StorageTag>++;
}

class DeletedConversionOperator {
public:
    /*implicit*/ operator bool() const = delete;
};

void MacroCompoundArgument() {
    UEXPECT_THROW_MSG({
        GetConn()->SetParameter("invalid", "parameter", Scope::kSession);
        auto res = GetConn()->Execute("select 1");
    }, pg::AccessRuleViolation, "invalid parameter");
    UEXPECT_NO_THROW({
        const auto result = coll.Distinct("type", mongo::options::Comment("test distinct operation"));
        EXPECT_EQ(2, result.size());
    });
}

void MacroTypedDeclarationArgument(Pool& pool) {
    UEXPECT_THROW(const pg::detail::ConnectionPtr conn2 = pool.Acquire(MakeDeadline()), pg::PoolError);
    ASSERT_THROW([[maybe_unused]] auto foo = value.foo(), proto_structs::OneofAccessError);
}

void MacroTrailingCommaArgument() {
    TEST_COMMAND("assert_matches('(running.*){1,4}', tasks_list_output)\n", test_in_coredump = True);
}

LockedChannelProxy<AMQP::Channel> GetChannel(engine::Deadline deadline) {
    return DoGetChannel(channel, deadline);
}

LockedChannelProxy<AmqpConnection::ReliableChannel> GetReliableChannel(engine::Deadline deadline) {
    return DoGetChannel(*reliable, deadline);
}

PostgresChaosProxy::PostgresChaosProxy(engine::TaskProcessor& task_processor) :
    task_processor_(task_processor),
    task_storage_() {}

PostgresChaosProxy::~PostgresChaosProxy() {}

ATTRIBUTE_NO_SANITIZE_UNDEFINED std::size_t AttributePrefixedFunction(const BoundsBlock& block, float value) noexcept {
    return block.Find(value);
}

template <typename T>
USERVER_IMPL_NODEBUG T NodebugPrefixedTemplate() {
    return T{};
}

template <IsRange T>
using IteratorType USERVER_IMPL_NODEBUG = decltype(begin(std::declval<T&>()));

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

FORMAT_USERVER_ALWAYS_INLINE_SIMD std::size_t PrefixMacroFunction(const BoundsBlock& block, float value) noexcept {
    return block.Find(value);
}

Value DependentTypenameBraced(Payload payload) {
    return typename Value::Builder{payload.value}.ExtractValue();
}

Value DependentTypenameCall(Item item) {
    return typename Value::Builder(T{item});
}

auto DecltypeValueInitialization(Func& func) {
    return decltype(func())();
}

using MemberFunctionPointerArray = std::array<void (FieldSubparser::*)(), Count>;

using GenericPrepareUnaryCall = std::unique_ptr<grpc::ClientAsyncResponseReader<grpc::ByteBuffer>>
    (grpc::GenericStub::*)(grpc::ClientContext*, const grpc::string&);
using AuthCheckerFactoryFactory = utils::UniqueRef<AuthCheckerFactoryBase> (*)(const components::ComponentContext&);

PrepareUnaryCallProxy(
    GenericPrepareUnaryCall,
    const grpc::string&
) -> PrepareUnaryCallProxy<grpc::GenericStub, grpc::ByteBuffer, grpc::ByteBuffer>;

Data& operator*() & FORMAT_USERVER_LIFETIME_BOUND {
    return data_;
}

using std::chrono::duration, std::chrono::nanoseconds;

enum class [[nodiscard]] FormatStatus : bool {
    kNo = false,
    kYes = true,
};

enum CurlNamespaceStatus {
    kOptional = CURL_FORMAT_USERVER_NAMESPACE kOptionalValue,
};

}  // namespace format_userver_fixture
