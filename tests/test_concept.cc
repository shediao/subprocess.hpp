#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "subprocess/subprocess.hpp"

using namespace subprocess::detail;

// ============================================================================
// named_argument_type — positive cases
// ============================================================================

TEST(NamedArgumentTypeTest, Env) {
  static_assert(named_argument_type<Env>);
  static_assert(named_argument_type<const Env>);
  static_assert(named_argument_type<Env&>);
  static_assert(named_argument_type<Env&&>);
}

TEST(NamedArgumentTypeTest, StdinRedirector) {
  static_assert(named_argument_type<StdinRedirector>);
  static_assert(named_argument_type<const StdinRedirector>);
  static_assert(named_argument_type<StdinRedirector&>);
  static_assert(named_argument_type<StdinRedirector&&>);
}

TEST(NamedArgumentTypeTest, StdoutRedirector) {
  static_assert(named_argument_type<StdoutRedirector>);
  static_assert(named_argument_type<const StdoutRedirector>);
  static_assert(named_argument_type<StdoutRedirector&>);
  static_assert(named_argument_type<StdoutRedirector&&>);
}

TEST(NamedArgumentTypeTest, StderrRedirector) {
  static_assert(named_argument_type<StderrRedirector>);
  static_assert(named_argument_type<const StderrRedirector>);
  static_assert(named_argument_type<StderrRedirector&>);
  static_assert(named_argument_type<StderrRedirector&&>);
}

TEST(NamedArgumentTypeTest, Cwd) {
  static_assert(named_argument_type<Cwd>);
  static_assert(named_argument_type<const Cwd>);
  static_assert(named_argument_type<Cwd&>);
  static_assert(named_argument_type<Cwd&&>);
}

TEST(NamedArgumentTypeTest, Timeout) {
  static_assert(named_argument_type<Timeout>);
  static_assert(named_argument_type<const Timeout>);
  static_assert(named_argument_type<Timeout&>);
  static_assert(named_argument_type<Timeout&&>);
}

TEST(NamedArgumentTypeTest, EnvAppend) {
  static_assert(named_argument_type<EnvAppend>);
  static_assert(named_argument_type<const EnvAppend>);
  static_assert(named_argument_type<EnvAppend&>);
  static_assert(named_argument_type<EnvAppend&&>);
}

TEST(NamedArgumentTypeTest, EnvItemAppend) {
  static_assert(named_argument_type<EnvItemAppend>);
  static_assert(named_argument_type<const EnvItemAppend>);
  static_assert(named_argument_type<EnvItemAppend&>);
  static_assert(named_argument_type<EnvItemAppend&&>);
}

// ============================================================================
// named_argument_type — negative cases
// ============================================================================

TEST(NamedArgumentTypeTest, RejectsFundamentalTypes) {
  static_assert(!named_argument_type<int>);
  static_assert(!named_argument_type<double>);
  static_assert(!named_argument_type<char>);
  static_assert(!named_argument_type<bool>);
}

TEST(NamedArgumentTypeTest, RejectsStringTypes) {
  static_assert(!named_argument_type<std::string>);
  static_assert(!named_argument_type<std::string_view>);
  static_assert(!named_argument_type<const char*>);
  static_assert(!named_argument_type<char*>);
}

TEST(NamedArgumentTypeTest, RejectsContainerTypes) {
  static_assert(!named_argument_type<std::vector<int>>);
  static_assert(!named_argument_type<std::tuple<>>);
}

TEST(NamedArgumentTypeTest, RejectsPointerTypes) {
  static_assert(!named_argument_type<Env*>);
  static_assert(!named_argument_type<const Cwd*>);
}

// ============================================================================
// string_like_type — positive cases
// ============================================================================

TEST(StringLikeTypeTest, CharPtr) {
  static_assert(string_like_type<char*>);
  static_assert(string_like_type<const char*>);
}

TEST(StringLikeTypeTest, StdString) {
  static_assert(string_like_type<std::string>);
  static_assert(string_like_type<const std::string>);
  static_assert(string_like_type<std::string&>);
  static_assert(string_like_type<std::string&&>);
}

TEST(StringLikeTypeTest, StringView) {
  static_assert(string_like_type<std::string_view>);
  static_assert(string_like_type<const std::string_view>);
  static_assert(string_like_type<std::string_view&>);
}

TEST(StringLikeTypeTest, CArrayDecay) {
  // C-arrays decay to pointers
  static_assert(string_like_type<decltype("hello")>);  // const char(&)[6]
  static_assert(string_like_type<std::decay_t<const char[6]>>);  // const char*
}

#if defined(_WIN32)
TEST(StringLikeTypeTest, WcharPtr) {
  static_assert(string_like_type<wchar_t*>);
  static_assert(string_like_type<const wchar_t*>);
}

TEST(StringLikeTypeTest, Wstring) {
  static_assert(string_like_type<std::wstring>);
  static_assert(string_like_type<const std::wstring>);
  static_assert(string_like_type<std::wstring&>);
  static_assert(string_like_type<std::wstring&&>);
}

TEST(StringLikeTypeTest, WstringView) {
  static_assert(string_like_type<std::wstring_view>);
  static_assert(string_like_type<const std::wstring_view>);
}
#endif  // _WIN32

// ============================================================================
// string_like_type — negative cases
// ============================================================================

TEST(StringLikeTypeTest, RejectsFundamentalTypes) {
  static_assert(!string_like_type<int>);
  static_assert(!string_like_type<double>);
  static_assert(!string_like_type<bool>);
}

TEST(StringLikeTypeTest, RejectsNamedArgumentTypes) {
  static_assert(!string_like_type<Env>);
  static_assert(!string_like_type<Cwd>);
  static_assert(!string_like_type<Timeout>);
}

TEST(StringLikeTypeTest, RejectsContainerTypes) {
  static_assert(!string_like_type<std::vector<char>>);
  static_assert(!string_like_type<std::tuple<>>);
}

// ============================================================================
// run_type
// ============================================================================

TEST(RunTypeTest, EquivalentToNamedArgumentType) {
  // run_type is defined as named_argument_type<T> — verify equivalence
  static_assert(run_type<Env>);
  static_assert(run_type<Cwd>);
  static_assert(run_type<Timeout>);
  static_assert(run_type<StdinRedirector>);
  static_assert(run_type<StdoutRedirector>);
  static_assert(run_type<StderrRedirector>);
  static_assert(run_type<EnvAppend>);
  static_assert(run_type<EnvItemAppend>);

  static_assert(!run_type<int>);
  static_assert(!run_type<std::string>);
  static_assert(!run_type<const char*>);
}

// ============================================================================
// run_with_args_type
// ============================================================================

TEST(RunWithArgsTypeTest, AcceptsNamedArgumentTypes) {
  static_assert(run_with_args_type<Env>);
  static_assert(run_with_args_type<Cwd>);
  static_assert(run_with_args_type<Timeout>);
  static_assert(run_with_args_type<StdinRedirector>);
  static_assert(run_with_args_type<StdoutRedirector>);
  static_assert(run_with_args_type<StderrRedirector>);
  static_assert(run_with_args_type<EnvAppend>);
  static_assert(run_with_args_type<EnvItemAppend>);
}

TEST(RunWithArgsTypeTest, AcceptsStringTypes) {
  static_assert(run_with_args_type<std::string>);
  static_assert(run_with_args_type<std::string_view>);
  static_assert(run_with_args_type<const char*>);
  static_assert(run_with_args_type<char*>);
}

TEST(RunWithArgsTypeTest, RejectsNonStringNonNamedTypes) {
  static_assert(!run_with_args_type<int>);
  static_assert(!run_with_args_type<double>);
  static_assert(!run_with_args_type<std::vector<int>>);
  static_assert(!run_with_args_type<Env*>);
}

// ============================================================================
// capture_run_type
// ============================================================================

TEST(CaptureRunTypeTest, AcceptsNonRedirectorNamedArgs) {
  static_assert(capture_run_type<Env>);
  static_assert(capture_run_type<Cwd>);
  static_assert(capture_run_type<Timeout>);
  static_assert(capture_run_type<EnvAppend>);
  static_assert(capture_run_type<EnvItemAppend>);
}

TEST(CaptureRunTypeTest, RejectsRedirectorTypes) {
  static_assert(!capture_run_type<StdinRedirector>);
  static_assert(!capture_run_type<StdoutRedirector>);
  static_assert(!capture_run_type<StderrRedirector>);
}

TEST(CaptureRunTypeTest, RejectsConstAndRefRedirectors) {
  static_assert(!capture_run_type<const StdinRedirector>);
  static_assert(!capture_run_type<StdinRedirector&>);
  static_assert(!capture_run_type<StdinRedirector&&>);
  static_assert(!capture_run_type<const StdoutRedirector&>);
}

TEST(CaptureRunTypeTest, RejectsNonNamedTypes) {
  static_assert(!capture_run_type<int>);
  static_assert(!capture_run_type<std::string>);
  static_assert(!capture_run_type<const char*>);
}

// ============================================================================
// capture_run_with_args_type
// ============================================================================

TEST(CaptureRunWithArgsTypeTest, AcceptsNonRedirectorNamedArgs) {
  static_assert(capture_run_with_args_type<Env>);
  static_assert(capture_run_with_args_type<Cwd>);
  static_assert(capture_run_with_args_type<Timeout>);
  static_assert(capture_run_with_args_type<EnvAppend>);
  static_assert(capture_run_with_args_type<EnvItemAppend>);
}

TEST(CaptureRunWithArgsTypeTest, AcceptsStringTypes) {
  static_assert(capture_run_with_args_type<std::string>);
  static_assert(capture_run_with_args_type<std::string_view>);
  static_assert(capture_run_with_args_type<const char*>);
  static_assert(capture_run_with_args_type<char*>);
}

TEST(CaptureRunWithArgsTypeTest, RejectsRedirectorTypes) {
  static_assert(!capture_run_with_args_type<StdinRedirector>);
  static_assert(!capture_run_with_args_type<StdoutRedirector>);
  static_assert(!capture_run_with_args_type<StderrRedirector>);
}

TEST(CaptureRunWithArgsTypeTest, RejectsNonStringNonNamedTypes) {
  static_assert(!capture_run_with_args_type<int>);
  static_assert(!capture_run_with_args_type<double>);
  static_assert(!capture_run_with_args_type<Env*>);
}

// ============================================================================
// named_arg_type_list — filtering logic
// ============================================================================

TEST(NamedArgTypeListTest, EmptyPack) {
  using result = named_arg_type_list_t<>;
  static_assert(std::same_as<result, std::tuple<>>);
}

TEST(NamedArgTypeListTest, SingleNamedArg) {
  using result = named_arg_type_list_t<Env>;
  static_assert(std::same_as<result, std::tuple<Env>>);
}

TEST(NamedArgTypeListTest, SingleNonNamedArg) {
  using result = named_arg_type_list_t<int>;
  static_assert(std::same_as<result, std::tuple<>>);
}

TEST(NamedArgTypeListTest, SingleStringArg) {
  using result = named_arg_type_list_t<std::string>;
  static_assert(std::same_as<result, std::tuple<>>);
}

TEST(NamedArgTypeListTest, AllNamedArgs) {
  using result = named_arg_type_list_t<Env, Cwd, Timeout>;
  static_assert(std::same_as<result, std::tuple<Env, Cwd, Timeout>>);
}

TEST(NamedArgTypeListTest, AllNonNamedArgs) {
  using result = named_arg_type_list_t<int, double, std::string>;
  static_assert(std::same_as<result, std::tuple<>>);
}

TEST(NamedArgTypeListTest, StringsThenNamedArgs) {
  // The conventional calling pattern: string args first, then named args
  using result = named_arg_type_list_t<std::string, const char*, Env, Timeout>;
  static_assert(std::same_as<result, std::tuple<Env, Timeout>>);
}

TEST(NamedArgTypeListTest, NamedArgsThenStrings) {
  // Unconventional order (caught by static_assert in run(), but filter works)
  using result = named_arg_type_list_t<Env, std::string, Timeout>;
  static_assert(std::same_as<result, std::tuple<Env, Timeout>>);
}

TEST(NamedArgTypeListTest, Interleaved) {
  // Interleaved: the previously-buggy case
  using result = named_arg_type_list_t<std::string, Env, std::string, Cwd>;
  static_assert(std::same_as<result, std::tuple<Env, Cwd>>);
}

TEST(NamedArgTypeListTest, AllNamedArgTypes) {
  using result = named_arg_type_list_t<StdinRedirector, Env, StdoutRedirector,
                                       Cwd, Timeout, EnvAppend,
                                       StderrRedirector, EnvItemAppend>;
  static_assert(
      std::same_as<result, std::tuple<StdinRedirector, Env, StdoutRedirector,
                                      Cwd, Timeout, EnvAppend, StderrRedirector,
                                      EnvItemAppend>>);
}

TEST(NamedArgTypeListTest, MixWithConstAndRef) {
  // Decay happens before filtering in the run() function
  using result =
      named_arg_type_list_t<std::decay_t<const Env&>, std::string, Cwd>;
  static_assert(std::same_as<result, std::tuple<Env, Cwd>>);
}

TEST(NamedArgTypeListTest, PreservesOrderAmongNamedArgs) {
  using result = named_arg_type_list_t<std::string, Cwd, const char*, Env,
                                       std::string, Timeout>;
  static_assert(std::same_as<result, std::tuple<Cwd, Env, Timeout>>);
}

TEST(NamedArgTypeListTest, EmptyPackProducesEmptyTuple) {
  using result = named_arg_type_list_t<>;
  static_assert(std::tuple_size_v<result> == 0);
}

// ============================================================================
// Concept subsumption / relationship checks
// ============================================================================

TEST(ConceptRelationshipTest, RunTypeIsSubsetOfRunWithArgsType) {
  // Everything that satisfies run_type also satisfies run_with_args_type
  static_assert(run_with_args_type<Env>);
  static_assert(run_with_args_type<StdinRedirector>);
  static_assert(run_with_args_type<Timeout>);
}

TEST(ConceptRelationshipTest, CaptureRunTypeIsSubsetOfRunType) {
  // capture_run_type is more restrictive than run_type
  static_assert(!capture_run_type<StdinRedirector>);
  static_assert(run_type<StdinRedirector>);

  // But non-redirector named args satisfy both
  static_assert(capture_run_type<Env>);
  static_assert(run_type<Env>);
}

TEST(ConceptRelationshipTest, CaptureRunTypeIsSubsetOfCaptureRunWithArgsType) {
  static_assert(capture_run_with_args_type<Env>);
  static_assert(capture_run_with_args_type<Timeout>);
  static_assert(capture_run_with_args_type<Cwd>);
}

TEST(ConceptRelationshipTest, StringLikeTypeSatisfiesRunWithArgsType) {
  static_assert(run_with_args_type<std::string>);
  static_assert(run_with_args_type<std::string_view>);
  static_assert(run_with_args_type<const char*>);
}

TEST(ConceptRelationshipTest, StringLikeTypeSatisfiesCaptureRunWithArgsType) {
  static_assert(capture_run_with_args_type<std::string>);
  static_assert(capture_run_with_args_type<std::string_view>);
  static_assert(capture_run_with_args_type<const char*>);
}
