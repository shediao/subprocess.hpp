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
// run_args_type
// ============================================================================

TEST(RunArgsTypeTest, AcceptsNamedArgumentTypes) {
  static_assert(run_args_type<Env>);
  static_assert(run_args_type<Cwd>);
  static_assert(run_args_type<Timeout>);
  static_assert(run_args_type<StdinRedirector>);
  static_assert(run_args_type<StdoutRedirector>);
  static_assert(run_args_type<StderrRedirector>);
  static_assert(run_args_type<EnvAppend>);
  static_assert(run_args_type<EnvItemAppend>);
}

TEST(RunArgsTypeTest, AcceptsStringTypes) {
  static_assert(run_args_type<std::string>);
  static_assert(run_args_type<std::string_view>);
  static_assert(run_args_type<const char*>);
  static_assert(run_args_type<char*>);
}

TEST(RunArgsTypeTest, RejectsNonStringNonNamedTypes) {
  static_assert(!run_args_type<int>);
  static_assert(!run_args_type<double>);
  static_assert(!run_args_type<std::vector<int>>);
  static_assert(!run_args_type<Env*>);
}

// ============================================================================
// named_argument_for_capture_type
// ============================================================================

TEST(NamedArgumentForCaptureTypeTest, AcceptsNonRedirectorNamedArgs) {
  static_assert(named_argument_for_capture_type<Env>);
  static_assert(named_argument_for_capture_type<Cwd>);
  static_assert(named_argument_for_capture_type<Timeout>);
  static_assert(named_argument_for_capture_type<EnvAppend>);
  static_assert(named_argument_for_capture_type<EnvItemAppend>);
}

TEST(NamedArgumentForCaptureTypeTest, RejectsRedirectorTypes) {
  static_assert(!named_argument_for_capture_type<StdinRedirector>);
  static_assert(!named_argument_for_capture_type<StdoutRedirector>);
  static_assert(!named_argument_for_capture_type<StderrRedirector>);
}

TEST(NamedArgumentForCaptureTypeTest, RejectsConstAndRefRedirectors) {
  static_assert(!named_argument_for_capture_type<const StdinRedirector>);
  static_assert(!named_argument_for_capture_type<StdinRedirector&>);
  static_assert(!named_argument_for_capture_type<StdinRedirector&&>);
  static_assert(!named_argument_for_capture_type<const StdoutRedirector&>);
}

TEST(NamedArgumentForCaptureTypeTest, RejectsNonNamedTypes) {
  static_assert(!named_argument_for_capture_type<int>);
  static_assert(!named_argument_for_capture_type<std::string>);
  static_assert(!named_argument_for_capture_type<const char*>);
}

// ============================================================================
// capture_run_args_type
// ============================================================================

TEST(CaptureRunArgsTypeTest, AcceptsNonRedirectorNamedArgs) {
  static_assert(capture_run_args_type<Env>);
  static_assert(capture_run_args_type<Cwd>);
  static_assert(capture_run_args_type<Timeout>);
  static_assert(capture_run_args_type<EnvAppend>);
  static_assert(capture_run_args_type<EnvItemAppend>);
}

TEST(CaptureRunArgsTypeTest, AcceptsStringTypes) {
  static_assert(capture_run_args_type<std::string>);
  static_assert(capture_run_args_type<std::string_view>);
  static_assert(capture_run_args_type<const char*>);
  static_assert(capture_run_args_type<char*>);
}

TEST(CaptureRunArgsTypeTest, RejectsRedirectorTypes) {
  static_assert(!capture_run_args_type<StdinRedirector>);
  static_assert(!capture_run_args_type<StdoutRedirector>);
  static_assert(!capture_run_args_type<StderrRedirector>);
}

TEST(CaptureRunArgsTypeTest, RejectsNonStringNonNamedTypes) {
  static_assert(!capture_run_args_type<int>);
  static_assert(!capture_run_args_type<double>);
  static_assert(!capture_run_args_type<Env*>);
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

TEST(ConceptRelationshipTest, NamedArgumentTypeIsSubsetOfRunArgsType) {
  // Everything that satisfies named_argument_type also satisfies run_args_type
  static_assert(run_args_type<Env>);
  static_assert(run_args_type<StdinRedirector>);
  static_assert(run_args_type<Timeout>);
}

TEST(ConceptRelationshipTest,
     NamedArgumentForCaptureTypeIsSubsetOfNamedArgumentType) {
  // named_argument_for_capture_type is more restrictive than
  // named_argument_type
  static_assert(!named_argument_for_capture_type<StdinRedirector>);
  static_assert(named_argument_type<StdinRedirector>);

  // But non-redirector named args satisfy both
  static_assert(named_argument_for_capture_type<Env>);
  static_assert(named_argument_type<Env>);
}

TEST(ConceptRelationshipTest,
     NamedArgumentForCaptureTypeIsSubsetOfCaptureRunArgsType) {
  static_assert(capture_run_args_type<Env>);
  static_assert(capture_run_args_type<Timeout>);
  static_assert(capture_run_args_type<Cwd>);
}

TEST(ConceptRelationshipTest, StringLikeTypeSatisfiesRunArgsType) {
  static_assert(run_args_type<std::string>);
  static_assert(run_args_type<std::string_view>);
  static_assert(run_args_type<const char*>);
}

TEST(ConceptRelationshipTest, StringLikeTypeSatisfiesCaptureRunArgsType) {
  static_assert(capture_run_args_type<std::string>);
  static_assert(capture_run_args_type<std::string_view>);
  static_assert(capture_run_args_type<const char*>);
}

// ============================================================================
// partition_args — positive cases
// ============================================================================

TEST(PartitionArgsTest, AllStrings) {
  // Only string-like arguments, no named args
  static_assert(partition_args<std::string>);
  static_assert(partition_args<const char*>);
  static_assert(partition_args<std::string, const char*>);
  static_assert(partition_args<std::string, std::string_view, const char*>);
}

TEST(PartitionArgsTest, AllNamedArgs) {
  // Only named-argument types, no strings
  static_assert(partition_args<Env>);
  static_assert(partition_args<Cwd>);
  static_assert(partition_args<Env, Cwd>);
  static_assert(partition_args<Env, Cwd, Timeout>);
}

TEST(PartitionArgsTest, StringsThenNamedArgs) {
  // Conventional calling pattern: string args first, then named args
  static_assert(partition_args<std::string, Env>);
  static_assert(partition_args<std::string, const char*, Env>);
  static_assert(partition_args<std::string, const char*, Env, Cwd>);
  static_assert(partition_args<std::string, const char*, std::string_view, Env,
                               Cwd, Timeout>);
  static_assert(partition_args<std::string, EnvAppend>);
  static_assert(partition_args<std::string, EnvItemAppend>);
}

TEST(PartitionArgsTest, EmptyPack) { static_assert(partition_args<>); }

TEST(PartitionArgsTest, SingleStringSingleNamed) {
  static_assert(partition_args<std::string, Env>);
  static_assert(partition_args<const char*, Cwd>);
  static_assert(partition_args<std::string, Timeout>);
}

TEST(PartitionArgsTest, AllRedirectorNamedArgs) {
  // Redirectors are also valid named_argument_type
  static_assert(partition_args<std::string, StdinRedirector>);
  static_assert(partition_args<std::string, StdoutRedirector>);
  static_assert(partition_args<std::string, StderrRedirector>);
  static_assert(partition_args<std::string, StdinRedirector, StdoutRedirector,
                               StderrRedirector>);
}

TEST(PartitionArgsTest, WideStringsWithNamedArgs) {
#if defined(_WIN32)
  static_assert(partition_args<wchar_t*, Env>);
  static_assert(partition_args<const wchar_t*, Cwd>);
  static_assert(partition_args<std::wstring, Timeout>);
  static_assert(partition_args<std::wstring_view, Env>);
  static_assert(partition_args<wchar_t*, std::wstring, Env, Cwd>);
#endif
}

TEST(PartitionArgsTest, AllEightNamedArgTypes) {
  static_assert(
      partition_args<std::string, Env, StdinRedirector, StdoutRedirector,
                     StderrRedirector, Cwd, Timeout, EnvAppend, EnvItemAppend>);
}

// ============================================================================
// partition_args — negative cases
// ============================================================================

TEST(PartitionArgsTest, RejectsNonStringLikeInStringPortion) {
  static_assert(!partition_args<int>);
  static_assert(!partition_args<double>);
  static_assert(!partition_args<bool>);
  static_assert(!partition_args<int, Env>);
  static_assert(!partition_args<std::string, int, Env>);
  static_assert(!partition_args<double, const char*, Cwd>);
}

TEST(PartitionArgsTest, RejectsNamedArgsBeforeStrings) {
  static_assert(!partition_args<Env, std::string>);
  static_assert(!partition_args<Cwd, const char*>);
  static_assert(!partition_args<Timeout, std::string, const char*>);
}

TEST(PartitionArgsTest, RejectsInterleaved) {
  static_assert(!partition_args<std::string, Env, const char*>);
  static_assert(!partition_args<std::string, Env, std::string, Cwd>);
  static_assert(!partition_args<const char*, Cwd, std::string, Timeout>);
  static_assert(!partition_args<Env, std::string, Cwd, const char*>);
}

TEST(PartitionArgsTest, RejectsPointerToNamedArgTypes) {
  static_assert(!partition_args<Env*>);
  static_assert(!partition_args<const Cwd*>);
  static_assert(!partition_args<std::string, Env*>);
}

TEST(PartitionArgsTest, RejectsContainerInStringPortion) {
  static_assert(!partition_args<std::vector<int>>);
  static_assert(!partition_args<std::vector<char>, Env>);
  static_assert(!partition_args<std::tuple<>>);
}

TEST(PartitionArgsTest, RejectsReferenceToNamedArgInStringPortion) {
  static_assert(!partition_args<const Env&, std::string>);
  static_assert(!partition_args<Env&, const char*>);
}

// ============================================================================
// partition_args — edge cases
// ============================================================================

TEST(PartitionArgsTest, DecayHandlingForStringTypes) {
  static_assert(partition_args<const std::string&, Env>);
  static_assert(partition_args<std::string&&, Cwd>);
  static_assert(partition_args<const char (&)[6], Env>);
}

TEST(PartitionArgsTest, NamedArgsOnlyWithDecay) {
  static_assert(partition_args<std::string, const Env&>);
  static_assert(partition_args<const char*, Env&&>);
}

TEST(PartitionArgsTest, MultipleStringsMultipleNamed) {
  static_assert(partition_args<std::string, const char*, std::string_view,
                               const char*, Env, Cwd, Timeout, EnvAppend>);
}

TEST(PartitionArgsTest, OnlyOneStringManyNamed) {
  static_assert(partition_args<std::string, Env, Cwd, Timeout, StdinRedirector,
                               StdoutRedirector, StderrRedirector, EnvAppend,
                               EnvItemAppend>);
}

// ============================================================================
// partition_args — relationship with other concepts
// ============================================================================

TEST(PartitionArgsRelationshipTest, SatisfiedByRunArgsType) {
  static_assert(partition_args<std::string, Env>);
  static_assert(run_args_type<std::string>);
  static_assert(run_args_type<Env>);
}

TEST(PartitionArgsRelationshipTest, RunArgsButNotPartitionArgs) {
  static_assert(run_args_type<Env>);
  static_assert(run_args_type<std::string>);
  static_assert(!partition_args<Env, std::string>);

  static_assert(run_args_type<int> == false);
  static_assert(!partition_args<int, Env>);
  static_assert(!partition_args<std::string, int>);
}
