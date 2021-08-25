/**
 * Copyright 2004-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unwind.h>
#include <algorithm>
#include <memory>

#include <plthooks/plthooks.h>
#include <plthooks/trampoline.h>

#include <plthooktestdata/meaningoflife.h>
#include <plthooktestdata/target.h>
#include <linker/test/test.h>

#ifdef LINKER_TRAMPOLINE_SUPPORTED_ARCH

static clock_t hook_clock() {
  // Intentionally call CALL_PREV more than once to ensure
  // CALL_PREV actually cares about who the caller is.
  for (int i = 0; i < 5; i++) {
    (void) CALL_PREV(hook_clock);
  }
  return 0xface;
}

struct OneHookTest : public BaseTest {
  OneHookTest(char const* method_name, void* hook)
    : libtarget(loadLibrary("libtarget.so")),
      method_name(method_name),
      hook(hook) { }

  virtual void SetUp() {
    BaseTest::SetUp();
    ASSERT_EQ(0, hook_plt_method("libtarget.so", method_name, hook));
  }

  virtual void TearDown() {
    ASSERT_EQ(0, unhook_plt_method("libtarget.so", method_name, hook));
  }

  std::unique_ptr<LibraryHandle>  libtarget;
  char const* const method_name;
  void* const hook;
};

struct CallClockHookTest : public OneHookTest {
  CallClockHookTest() : OneHookTest("clock", (void*)hook_clock) {}
};

TEST_F(CallClockHookTest, testHook) {
  auto call_clock = libtarget->get_symbol<int()>("call_clock");
  ASSERT_EQ(0xface, call_clock());
}

struct TwoHookTest : public CallClockHookTest {
  TwoHookTest()
      : CallClockHookTest(),
        libsecond_hook(loadLibrary("libsecond_hook.so")),
        perform_hook(libsecond_hook->get_symbol<int()>("perform_hook")),
        cleanup(libsecond_hook->get_symbol<int()>("cleanup")) {}

  virtual void SetUp() {
    OneHookTest::SetUp();
    ASSERT_EQ(1, perform_hook());
  }

  virtual void TearDown() {
    ASSERT_EQ(1, cleanup());
    OneHookTest::TearDown();
  }

  std::unique_ptr<LibraryHandle> libsecond_hook;
  int (*const perform_hook)();
  int (*const cleanup)();
};

TEST_F(TwoHookTest, testDoubleHook) {
  auto call_clock = libtarget->get_symbol<int()>("call_clock");
  ASSERT_EQ(0xfaceb00c, call_clock());
}

struct TargetedHooksTest : public BaseTest {
  TargetedHooksTest()
      : BaseTest(),
        libtarget(loadLibrary("libtarget.so")),
        libownclock(loadLibrary("libownclock.so")) {}

  std::unique_ptr<LibraryHandle> libtarget;
  std::unique_ptr<LibraryHandle> libownclock;
};

TEST_F(TargetedHooksTest, testNonTargetedLibSymbolsAreIgnored) {
  // libtarget uses clock() from libc. We want to
  // be able to hook that.
  // libownclock use clock() from libownclock_impl. We
  // want to ignore that, if the spec specifies it.

  {
    plt_hook_spec spec("libc.so", "clock", (void*)hook_clock);
    // Verify that the linker did the right thing and linked against our own
    // clock() implementation.
    ASSERT_EQ(1, libownclock->get_symbol<clock_t()>("call_clock")());

    ASSERT_EQ(0, hook_single_lib("libownclock.so", &spec, 1));
    ASSERT_EQ(0, spec.hook_result);

    // Since the hook did not succeed, we expect the result to be unhooked.
    ASSERT_EQ(1, libownclock->get_symbol<clock_t()>("call_clock")());
  }

  {
    plt_hook_spec spec("libc.so", "clock", (void*)hook_clock);
    ASSERT_EQ(0, hook_single_lib("libtarget.so", &spec, 1));
    ASSERT_EQ(1, spec.hook_result);
    ASSERT_EQ(0xface, libtarget->get_symbol<clock_t()>("call_clock")());
    ASSERT_EQ(0, unhook_single_lib("libtarget.so", &spec, 1));
  }
}

struct HookUnhookTest : public BaseTest {
  struct Hook {
    Hook(const char* sym, void* hook) : symbol(sym), fn(hook) {
      EXPECT_EQ(hook_plt_method("libtarget.so", symbol, fn), 0)
        << "symbol: " << symbol << " hook:" << fn;
    }

    ~Hook() {
      EXPECT_EQ(unhook_plt_method("libtarget.so", symbol, fn), 0)
        << "symbol: " << symbol << " hook:" << fn;
    }

    const char* symbol;
    void* fn;
  };

  HookUnhookTest() : libtarget(loadLibrary("libtarget.so")) {}

  virtual ~HookUnhookTest() {}

  virtual void SetUp() {
    BaseTest::SetUp();
  }

  static clock_t clock1() {
    return kOne;
  }

  static clock_t clock2() {
    return CALL_PREV(clock2) * kTwo;
  }

  static clock_t clock3() {
    return CALL_PREV(clock3) * kThree;
  }

  static int kOne;
  static int kTwo;
  static int kThree;

  std::unique_ptr<LibraryHandle> libtarget;
};

int HookUnhookTest::kOne = 11;
int HookUnhookTest::kTwo = 13;
int HookUnhookTest::kThree = 17;

TEST_F(HookUnhookTest, testProperStackHookUnhook) {
  auto call_clock = libtarget->get_symbol<clock_t()>("call_clock");
  {
    Hook fst{"clock", (void*) &clock1};
    EXPECT_EQ(call_clock(), kOne);
    {
      Hook snd{"clock", (void*) &clock2};
      EXPECT_EQ(call_clock(), kOne * kTwo);
      {
        Hook trd{"clock", (void*)&clock3};
        EXPECT_EQ(call_clock(), kOne * kTwo * kThree);
      }
      EXPECT_EQ(call_clock(), kOne * kTwo);
    }
    EXPECT_EQ(call_clock(), kOne);
  }
  EXPECT_NE(call_clock(), kOne);
}

TEST_F(HookUnhookTest, testUnhookAllWithUnhookedLib) {
  // This test ensures that unhook_all_libs does not trip up on
  // libraries with symbols that match the hook spec but are not hooked.
  auto call_clock = libtarget->get_symbol<clock_t()>("call_clock");

  plt_hook_spec spec("clock", (void*)&clock1);

  auto hook_return = hook_all_libs(
      &spec, 1, [](const char* name, const char*, void*) {
        // Avoid accidental matches on system libraries by hardcoding our target
        return strcmp(name, "libtarget.so") == 0;
      }, nullptr);

  EXPECT_EQ(hook_return, 0) << "hook_all failed";
  EXPECT_EQ(spec.hook_result, 1) << "must hook exactly 1 library";
  EXPECT_EQ(call_clock(), kOne);

  // Load a second library that has a PLT slot for clock()
  auto other_lib = loadLibrary("libmeaningoflife.so");

  spec.hook_result = 0; // reset after the hook_all operation
  EXPECT_EQ(unhook_all_libs(&spec, 1), 0) << "unhook_all failed";
  EXPECT_EQ(spec.hook_result, 1) << "must unhook exactly 1 library";
}

TEST_F(HookUnhookTest, testUnhookWithMissingHookDoesNotFail) {
  // This test ensures that unhook_single_lib does not fail when the
  // the spec matches an existing hooked slot but the hook function is not
  // registered for that slot.
  auto call_clock = libtarget->get_symbol<clock_t()>("call_clock");

  plt_hook_spec spec("clock", (void*)&clock1);

  auto hook_return = hook_single_lib("libtarget.so", &spec, 1);

  EXPECT_EQ(hook_return, 0) << "hook_single_lib failed";
  EXPECT_EQ(spec.hook_result, 1) << "must hook exactly 1 library";
  EXPECT_EQ(call_clock(), kOne);

  plt_hook_spec unhooked_spec("clock", (void*) &clock2);

  spec.hook_result = 0; // reset after the hook_all operation
  EXPECT_EQ(unhook_single_lib("libtarget.so", &unhooked_spec, 1), 0);
  EXPECT_EQ(spec.hook_result, 0) << "must unhook exactly 0 libraries";

  // Cleanup
  EXPECT_EQ(unhook_single_lib("libtarget.so", &spec, 1), 0);
}

TEST_F(HookUnhookTest, testOutOfOrderHookUnhook) {
  // Test out of order unhooking.
  auto call_clock = libtarget->get_symbol<clock_t()>("call_clock");
  auto fst = std::make_unique<Hook>("clock", (void*) &clock1);
  auto snd = std::make_unique<Hook>("clock", (void*) &clock2);
  auto trd = std::make_unique<Hook>("clock", (void*) &clock3);

  EXPECT_EQ(call_clock(), kOne * kTwo * kThree);

  snd.reset();
  EXPECT_EQ(call_clock(), kOne * kThree);

  trd.reset();
  EXPECT_EQ(call_clock(), kOne);

  fst.reset();
  EXPECT_NE(call_clock(), kOne);
}

TEST_F(HookUnhookTest, testOutOfOrderHookUnhook2) {
  // Test out of order unhooking and hooking sequences.
  auto call_clock = libtarget->get_symbol<clock_t()>("call_clock");
  auto fst = std::make_unique<Hook>("clock", (void*) &clock1);
  auto snd = std::make_unique<Hook>("clock", (void*) &clock2);

  EXPECT_EQ(call_clock(), kOne * kTwo);

  snd.reset();
  EXPECT_EQ(call_clock(), kOne);

  auto trd = std::make_unique<Hook>("clock", (void*) &clock3);
  EXPECT_EQ(call_clock(), kOne * kThree);

  snd = std::make_unique<Hook>("clock", (void*) &clock2);
  EXPECT_EQ(call_clock(), kOne * kTwo * kThree);

  trd.reset();
  EXPECT_EQ(call_clock(), kOne * kTwo);

  auto frt = std::make_unique<Hook>("clock", (void*) &hook_clock);
  EXPECT_EQ(call_clock(), 0xface); // hook_clock overwrites the value

  trd = std::make_unique<Hook>("clock", (void*) &clock3);
  EXPECT_EQ(call_clock(), 0xface * kThree);

  frt.reset();
  snd.reset();
  trd.reset();
  EXPECT_EQ(call_clock(), kOne);
}

static double hook_nice1(int one) {
  return CALL_PREV(hook_nice1, one * 6);
}

struct Nice1Test : public OneHookTest {
  Nice1Test() : OneHookTest("nice1", (void*)hook_nice1) {}
};

TEST_F(Nice1Test, nice1Test) {
  auto call_nice1 = libtarget->get_symbol<double(int)>("call_nice1");
  ASSERT_EQ(-1764.0, call_nice1(7));
}

static int hook_nice2(int one, double two) {
  return CALL_PREV(hook_nice2, one * 6, two);
}

struct Nice2Test : public OneHookTest {
  Nice2Test() : OneHookTest("nice2", (void*)hook_nice2) {}
};

TEST_F(Nice2Test, nice2Test) {
  auto call_nice2 = libtarget->get_symbol<int(int, double)>("call_nice2");
  ASSERT_EQ(1764.0, call_nice2(70, 4.2));
}


#define MUNGE_TRIPLE(x)                       ((x) * 3)
#define MUNGE_MUL17(x)                        ((x) * 17)
#define MUNGE_REPLACESTRING(x)                "world"
#define MUNGE_INCR(x)                         ((x) + 1)
#define MUNGE_ADD3(x)                         ((x) + 3)
#define MUNGE_SUB10(x)                        ((x) - 10)

static constexpr double       kDouble1  =     8102.0827;
static constexpr double       kDouble2  =     -0.000105;
static constexpr double       kDouble3  =     451.0;
static constexpr double       kDouble4  =     -459.67;
static constexpr int          kInt1     =     0x6d3abe0;
static constexpr int          kInt2     =     0x800000;
static constexpr int          kInt3     =     -562;
static constexpr int          kInt4     =     5;
static constexpr int          kInt5     =     0xbeefc0de;
static constexpr char const*  kString1  =     "hello";
static constexpr char const*  kString2  =     "facebook";
static constexpr char         kChar1    =     'f';
static constexpr char         kChar2    =     'm';
static constexpr char         kChar3    =     'l';
static constexpr char         kChar4    =     'z';
static constexpr char         kChar5    =     'u';
static constexpr char         kChar6    =     'c';

static void hook_evil1(struct large one, int two, void (*cb)(struct large*, int, void*), void* unk) {
  cb(&one, two, unk);
  one.a = MUNGE_TRIPLE(one.a);
  one.b = MUNGE_TRIPLE(one.b);
  one.c = MUNGE_TRIPLE(one.c);
  one.d = MUNGE_REPLACESTRING(one.d);
  one.e = MUNGE_INCR(one.e);
  one.f = MUNGE_ADD3(one.f);
  one.g = MUNGE_SUB10(one.g);
  CALL_PREV(hook_evil1, one, MUNGE_MUL17(two), cb, unk);
}

struct Evil1Test : public OneHookTest {
  Evil1Test() : OneHookTest("evil1", (void*)hook_evil1) {}
};

TEST_F(Evil1Test, evil1Test) {
  auto call_evil1 = libtarget->get_symbol<void(struct large, int, void(*)(struct large*, int, void*), void*)>("call_evil1");
  struct large param = {
    .a = kDouble1,
    .b = kInt1,
    .c = kDouble2,
    .d = kString1,
    .e = kChar1,
    .f = kChar2,
    .g = kChar3
  };
  int call_num = 0;
  call_evil1(param, kInt2, +[](struct large* one, int two, void* unk) {
    int* cn = reinterpret_cast<int*>(unk);
    switch (++(*cn)) {
      case 1:
        EXPECT_EQ(kDouble1, one->a);
        EXPECT_EQ(kInt1, one->b);
        EXPECT_EQ(kDouble2, one->c);
        EXPECT_STREQ(kString1, one->d);
        EXPECT_EQ(kChar1, one->e);
        EXPECT_EQ(kChar2, one->f);
        EXPECT_EQ(kChar3, one->g);
        EXPECT_EQ(kInt2, two);
        break;
      case 2:
        EXPECT_EQ(MUNGE_TRIPLE(kDouble1), one->a);
        EXPECT_EQ(MUNGE_TRIPLE(kInt1), one->b);
        EXPECT_EQ(MUNGE_TRIPLE(kDouble2), one->c);
        EXPECT_STREQ(MUNGE_REPLACESTRING(kString1), one->d);
        EXPECT_EQ(MUNGE_INCR(kChar1), one->e);
        EXPECT_EQ(MUNGE_ADD3(kChar2), one->f);
        EXPECT_EQ(MUNGE_SUB10(kChar3), one->g);
        EXPECT_EQ(MUNGE_MUL17(kInt2), two);
        break;
      default:
        FAIL() << "bad call_num";
    }
  }, &call_num);
  EXPECT_EQ(2, call_num);
}

static void* hook_evil2(int one, struct large two, void (*cb)(struct large*, int, void*), void* unk) {
  cb(&two, one, unk);
  two.a = MUNGE_TRIPLE(two.a);
  two.b = MUNGE_TRIPLE(two.b);
  two.c = MUNGE_TRIPLE(two.c);
  two.d = MUNGE_REPLACESTRING(two.d);
  two.e = MUNGE_INCR(two.e);
  two.f = MUNGE_ADD3(two.f);
  two.g = MUNGE_SUB10(two.g);
  return CALL_PREV(hook_evil2, MUNGE_MUL17(one), two, cb, unk);
}

struct Evil2Test : public OneHookTest {
  Evil2Test() : OneHookTest("evil2", (void*)hook_evil2) {}
};

TEST_F(Evil2Test, evil2Test) {
  auto call_evil2 = libtarget->get_symbol<void*(int, struct large, void(*)(struct large*, int, void*), void*)>("call_evil2");
  struct large param = {
    .a = kDouble1,
    .b = kInt1,
    .c = kDouble2,
    .d = kString1,
    .e = kChar1,
    .f = kChar2,
    .g = kChar3
  };
  int call_num = 0;
  void* ret = call_evil2(kInt2, param, +[](struct large* one, int two, void* unk) {
    int* cn = reinterpret_cast<int*>(unk);
    switch (++(*cn)) {
      case 1:
        EXPECT_EQ(kDouble1, one->a);
        EXPECT_EQ(kInt1, one->b);
        EXPECT_EQ(kDouble2, one->c);
        EXPECT_STREQ(kString1, one->d);
        EXPECT_EQ(kChar1, one->e);
        EXPECT_EQ(kChar2, one->f);
        EXPECT_EQ(kChar3, one->g);
        EXPECT_EQ(kInt2, two);
        break;
      case 2:
        EXPECT_EQ(MUNGE_TRIPLE(kDouble1), one->a);
        EXPECT_EQ(MUNGE_TRIPLE(kInt1), one->b);
        EXPECT_EQ(MUNGE_TRIPLE(kDouble2), one->c);
        EXPECT_STREQ(MUNGE_REPLACESTRING(kString1), one->d);
        EXPECT_EQ(MUNGE_INCR(kChar1), one->e);
        EXPECT_EQ(MUNGE_ADD3(kChar2), one->f);
        EXPECT_EQ(MUNGE_SUB10(kChar3), one->g);
        EXPECT_EQ(EVIL2_MUNGE_CALLBACK_INT(MUNGE_MUL17(kInt2)), two);
        break;
      default:
        FAIL() << "bad call_num";
    }
  }, &call_num);
  EXPECT_EQ(2, call_num);
  EXPECT_EQ(&call_num, ret);
}

static struct large hook_evil3(int one, int two, int three, struct large four, void (*cb)(struct large*, int, void*), void* unk) {
  cb(&four, one, unk);
  four.a = MUNGE_TRIPLE(four.a);
  four.b = MUNGE_TRIPLE(four.b);
  four.c = MUNGE_TRIPLE(four.c);
  four.d = MUNGE_REPLACESTRING(four.d);
  four.e = MUNGE_INCR(four.e);
  four.f = MUNGE_ADD3(four.f);
  four.g = MUNGE_SUB10(four.g);
  return CALL_PREV(hook_evil3, one, two, three, four, cb, unk);
}

struct Evil3Test : public OneHookTest {
  Evil3Test() : OneHookTest("evil3", (void*)hook_evil3) { }
};

TEST_F(Evil3Test, evil3Test) {
  auto call_evil3 =
    libtarget->get_symbol<struct large(int, int, int, struct large, void(*)(struct large*, int, void*), void*)>("call_evil3");
  struct large param = {
    .a = kDouble1,
    .b = kInt1,
    .c = kDouble2,
    .d = kString1,
    .e = kChar1,
    .f = kChar2,
    .g = kChar3
  };
  int call_num = 0;
  struct large ret = call_evil3(kInt2, kInt3, kInt4, param, +[](struct large* one, int two, void* unk) {
    int* cn = reinterpret_cast<int*>(unk);
    switch (++(*cn)) {
      case 1:
        EXPECT_EQ(kDouble1, one->a);
        EXPECT_EQ(kInt1, one->b);
        EXPECT_EQ(kDouble2, one->c);
        EXPECT_STREQ(kString1, one->d);
        EXPECT_EQ(kChar1, one->e);
        EXPECT_EQ(kChar2, one->f);
        EXPECT_EQ(kChar3, one->g);
        EXPECT_EQ(kInt2, two);
        break;
      case 2:
        EXPECT_EQ(MUNGE_TRIPLE(kDouble1), one->a);
        EXPECT_EQ(MUNGE_TRIPLE(kInt1), one->b);
        EXPECT_EQ(MUNGE_TRIPLE(kDouble2), one->c);
        EXPECT_STREQ(MUNGE_REPLACESTRING(kString1), one->d);
        EXPECT_EQ(MUNGE_INCR(kChar1), one->e);
        EXPECT_EQ(MUNGE_ADD3(kChar2), one->f);
        EXPECT_EQ(MUNGE_SUB10(kChar3), one->g);
        EXPECT_EQ(EVIL3_MUNGE_CALLBACK_INT(kInt2, kInt3, kInt4), two);

        one->a = kDouble3;
        one->b = kInt5;
        one->c = kDouble4;
        one->d = kString2;
        one->e = kChar4;
        one->f = kChar5;
        one->g = kChar6;
        break;
      default:
        FAIL() << "bad call_num";
    }
  }, &call_num);

  EXPECT_EQ(2, call_num);

  EXPECT_EQ(kDouble3, ret.a);
  EXPECT_EQ(kInt5, ret.b);
  EXPECT_EQ(kDouble4, ret.c);
  EXPECT_STREQ(kString2, ret.d);
  EXPECT_EQ(kChar4, ret.e);
  EXPECT_EQ(kChar5, ret.f);
  EXPECT_EQ(kChar6, ret.g);
}

// TODO: write a varargs test

template <int Ret>
struct AliasTest : public OneHookTest {
  AliasTest(char const* method_name)
    : OneHookTest(method_name, (void*)(+[] { return Ret; })),
      libmeaningoflife(loadLibrary("libmeaningoflife.so")),
      foo(libmeaningoflife->get_symbol<int()>("foo")),
      bar(libmeaningoflife->get_symbol<int()>("bar")) {}

  static constexpr int kExpectedValue = Ret;

  std::unique_ptr<LibraryHandle> libmeaningoflife;
  int (*foo)();
  int (*bar)();
};

struct AliasFooTest : public AliasTest<69> {
  AliasFooTest() : AliasTest("foo") {}
};

TEST_F(AliasFooTest, aliasFooTest) {
  ASSERT_EQ(kExpectedValue + bar(), libtarget->get_symbol<int()>("add_foo_and_bar")());
}

struct AliasBarTest : public AliasTest<101> {
  AliasBarTest() : AliasTest("bar") {}
};

TEST_F(AliasBarTest, aliasBarTest) {
  ASSERT_EQ(kExpectedValue + foo(), libtarget->get_symbol<int()>("add_foo_and_bar")());
}

struct NoChainingTest : public BaseTest {
  NoChainingTest() : libtarget(loadLibrary("libtarget.so")) {}

  std::unique_ptr<LibraryHandle> libtarget;

  // Can't use hook_clock because CALL_PREV will abort in a no-chaining hook.
  static clock_t beef_clock() {
    return 0xbeef;
  }

  // Avoid tail call differences in optimized and non-optimized tests. Ideally
  // we'd always force a tail call instead, but there's no way to do so that I'm
  // aware of.
  __attribute__((optnone))
  static _Unwind_Reason_Code unwind_backtrace_wrapper(_Unwind_Trace_Fn trace, void* arg) {
    return _Unwind_Backtrace(trace, arg);
  }
};

TEST_F(NoChainingTest, testHook) {
  plt_hook_spec spec("clock", (hook_func)NoChainingTest::beef_clock, /*no_chaining=*/true);
  auto failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);
  auto call_clock = libtarget->get_symbol<int()>("call_clock");
  ASSERT_EQ(0xbeef, call_clock());
}

TEST_F(NoChainingTest, testHookingAfterNoChainingHook) {
  plt_hook_spec spec("clock", (hook_func)NoChainingTest::beef_clock, /*no_chaining=*/true);
  auto failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);

  spec.hook_result = 0;
  failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(1, failures);
  ASSERT_EQ(0, spec.hook_result);

  spec.no_chaining = false;
  spec.hook_result = 0;
  failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(1, failures);
  ASSERT_EQ(0, spec.hook_result);
}

TEST_F(NoChainingTest, testNoChainingHookAfterRegularHookAndUnhook) {
  plt_hook_spec spec("clock", (hook_func)NoChainingTest::beef_clock);
  auto failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);

  spec.no_chaining = true;
  spec.hook_result = 0;
  failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(1, failures);
  ASSERT_EQ(0, spec.hook_result);

  failures = unhook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);

  spec.hook_result = 0;
  failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);
}

TEST_F(NoChainingTest, unwindingTest) {
  auto call_unwind_backtrace =
      libtarget->get_symbol<bool(unsigned*)>("call_unwind_backtrace");
  unsigned num_frames_original;
  bool success = call_unwind_backtrace(&num_frames_original);
  ASSERT_TRUE(success);

  std::string target_symbol;

  // Determine dynamically which symbol to use, preferring the wrapped one, if it exists.
  try {
    static constexpr char kWrappedUnwindBacktrace[] = "__wrap__Unwind_Backtrace";
    libtarget->get_symbol<bool(unsigned*)>(kWrappedUnwindBacktrace);
    target_symbol = kWrappedUnwindBacktrace;
  } catch (...) {
    target_symbol = "_Unwind_Backtrace";
  }
  plt_hook_spec spec(
      target_symbol.c_str(),
      (hook_func)&NoChainingTest::unwind_backtrace_wrapper);
  auto failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);

  // The assembly trampoline used by regular hooks can't be unwound through.
  unsigned num_frames_with_regular_hook;
  success = call_unwind_backtrace(&num_frames_with_regular_hook);
  ASSERT_TRUE(success);

  spec.hook_result = 0;
  failures = unhook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);

  spec.no_chaining = true;
  spec.hook_result = 0;
  failures = hook_single_lib("libtarget.so", &spec, 1);
  ASSERT_EQ(0, failures);
  ASSERT_EQ(1, spec.hook_result);

  // No-chaining hooks shouldn't use the trampoline and unwinding should behave
  // as normal.
  unsigned num_frames_with_no_chaining_hook;
  success = call_unwind_backtrace(&num_frames_with_no_chaining_hook);
  ASSERT_TRUE(success);

  // Add 1 to account for the extra unwind_backtrace_wrapper frame, but cap to
  // the max number of frames the callback will traverse.
  unsigned num_frames_adjusted =
      std::min<unsigned>(num_frames_original + 1, MAX_BACKTRACE_FRAMES);
  ASSERT_EQ(num_frames_adjusted, num_frames_with_no_chaining_hook);
  ASSERT_GT(num_frames_with_no_chaining_hook, num_frames_with_regular_hook);
}

#else

TEST(UnsupportedArch, unsupportedArch) {
  auto libtarget = loadLibrary("libtarget.so");
  ASSERT_EQ(1, hook_plt_method("libtarget.so", "call_clock", (void*)(+[]() -> clock_t { return 0; })));
}

#endif
