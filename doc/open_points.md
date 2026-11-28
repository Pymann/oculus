# Open points of the rules

Kept up to date with every rules batch. Only rules: each entry says what is open
and what it waits for. Rule numbers are the catalogue ids (Pymann/coding-rules).

## Decisions needed

- **OC-0315 macros_at_global_scope.** Every #define or #undef inside a namespace
  counts, including the configuration macros fmt and gtest define between
  their namespace macros (239 findings in fmt, 206 of them in gtest). It is correct
  by the rule text. Open: should headers that only wrap a namespace be treated
  differently?

- **OC-0451 seeded_prng, test code.** The rule exempts tests that seed with a
  constant on purpose when the project configuration marks the file as test
  code. There is no such key yet, so every constant `srand` seed is reported.
  `rand` without any `srand` and C++ engines are not checked yet (cross-TU).
- **OC-0468 async_safe_signal_handlers, platform.** The POSIX list is always
  used (the platform is assumed to be POSIX). Open: a configuration key for
  non-POSIX targets, where only `_Exit`, `abort`, `quick_exit` and `signal`
  are safe.

## Left out on purpose

- **OC-0386** catch_all_handler_last and **OC-0329**
  no_mixed_prefix_concatenation: Clang rejects such code. A unit that does not
  compile is reported as such and is not checked. (0386 is listed in
  catalog/not_implemented.txt; 0329 is not listed yet.)
- **OC-0189** (_Atomic void): only a compile-error example. Same reason.
- **OC-0084** no_pointer_float_cast: only a compile-error example. Same reason.
- **OC-0122** no_implicit_declaration and **OC-0154** conditional_in_one_file:
  only compile-error examples. Same reason.
- **OC-0142** include_form: an `#include` of anything but `<name>` or `"name"`
  does not compile either.

## Deferred: needs more frontend or IR

- **OC-0316** function-like macros: needs expansion records for the user's own
  macros, not only system macros. Without them a macro used as a constant
  expression cannot be told apart.
- **OC-0094** and **OC-0096**: side effects of calls in initializer lists and
  next to `++`/`--`. These are ambiguous without knowing which functions are pure
  (library model, phase 2).
- **OC-0196**: qualifiers on function types. Clang drops the qualifier before the
  AST, so the IR never sees it.
- **OC-0141** (characters in header names), **OC-0322** (C library headers in
  C++) and **OC-0375** (`std::vector<bool>`): deferred in earlier batches. The
  detailed reasons still need writing up here.

## Scope limits of implemented rules (until phase 2, cross-TU facts)

- **OC-0016** unused parameters: only functions with internal linkage whose
  address is not taken in the unit. Others may be callbacks defined elsewhere.
- **OC-0057** implicit enum values: reported only when an *earlier* enumerator
  has the value. A later explicit alias (`LAST = C`) counts as intended.
- **OC-0063** double initialisation: only lists whose elements are all designated
  and have no ranges. Lists that mix positional and designated elements are not
  checked yet.
- **OC-0286** explicit base construction: classes with dependent bases (templates)
  are not checked, and a base with no data members (an interface that has only a
  vtable) does not count.
- **OC-0337** and **OC-0250** (0/NULL): judged by the implicit conversion above
  the constant, so uses inside templates whose types are not known yet are not
  reported. OC-0250 counts only integral conversions of NULL.
- **OC-0353**: only comparisons where one side is written as `&Class::virtual_fn`;
  two member-pointer variables are not known statically.
- **OC-0273** const member returning a mutable handle: deferred. The exception for
  non-owning views needs a configuration key first (span-like classes return `T*`
  from const members by design).
- **OC-0100** float loop counter: only loops whose whole condition is one
  comparison with the variable as one side, stepped by `++`, `--`, a compound
  assignment or `v = ...v...`. Conditions joined with `&&`/`||` and convergence
  loops such as `while (fabs(x - y) > eps)` are not checked.
- **OC-0255** array decay: only calls of functions not declared in a system header
  (no builtins, no operator calls, no constructors). A `va_list` argument and a
  `?:` that picks between string literals are not reported.
  Project wrappers that take a buffer and its count (imgui's
  `ImFormatString(buf, IM_COUNTOF(buf), ...)`) are reported unless listed in the
  rule's `names` key of oculus.json.
- **OC-0284** and **OC-0049**: a member or definition whose name a macro made with
  `##` (gmock's MOCK_METHOD, Zephyr's K_*_DEFINE buffers) is not reported.
- **OC-0330**: only `T name();` with nothing between the parentheses. A
  parenthesised declarator `T (name);` and the vexing parse with arguments
  (`Widget w(Timer());`) are not reported yet.
- **OC-0078**, **OC-0079**, **OC-0080**, **OC-0081**, **OC-0083**, **OC-0085**:
  explicit casts only (implicit conversions between these pointer types do not
  compile cleanly anyway). A chain such as `(int)(long)p` is reported once, at the
  outermost cast. OC-0080 does not look through common initial sequences:
  `(unixFile *)pFile` in sqlite is reported.
- **OC-0081** and **OC-0082**: the configuration keys of the rule text do not
  exist yet. In C, `uintptr_t` casts are reported, and there is no hardware-access
  header. In C++ only `uintptr_t` is allowed. For OC-0082 no function is marked as
  a callback. Exempt are `malloc`, `calloc`, `realloc`, functions with the
  `malloc` or `alloc_size` attribute, and functions whose name contains `alloc`
  (project wrappers such as `sqlite3_malloc`).
- **OC-0090** explicit_precedence: `&&` against `||`, and shifts or bitwise
  operators against another binary operator. The conditional operator and
  `sizeof` of a non-primary expression are not checked yet. `sizeof *p` is the
  usual idiom, so that part needs a decision first. An operand that one macro
  spells and the outer operator another file position spells is left out.
- **OC-0111** and **OC-0113**: switches with code skipped by `#if` are not judged.
  OC-0113 only knows how a clause ends when the last statement is a jump, a
  throw, a noreturn call, a block or an if/else of such statements. A clause
  ending in a loop, a nested switch or try is not reported. `goto` counts as an
  end. `[[fallthrough]]` is reported: the configuration key does not exist yet.
- **OC-0140**: the `extern "C" {` and `}` around includes do not count as code,
  and neither does code skipped by `#if`. X-macro files cannot be configured yet.
- **OC-0284**: a class whose data members are all public is plain data and is
  not reported, even with a constructor (the rule text's "plain structs whose
  data is all public"; imgui's `ImVec2`).
- **OC-0045**: a literal passed to a function declared in a system header is not
  reported (the configurable list does not exist yet), and neither is a variadic
  argument or a `?:` that picks a literal.
- **OC-0161**: library names only for functions and variables at file scope that
  do not declare again what a system header declares. The list of names is a
  fixed set of common C library functions and objects, not the full library.
  Names that a macro body writes or pastes (Zephyr's `LOG_MODULE_REGISTER`) are
  not reported at the macro's use.
- **OC-0126** use_return_value: the ignorable list of the rule text is not a
  configuration key yet. Built in: the printf family that writes to stdout or to
  memory (`printf`, `sprintf`, `snprintf` and their `v`/wide forms; the
  catalogue reports `fprintf`) and the C functions that return their first
  argument (`memcpy`, `strcpy` and the like). Members and functions of the C++
  standard library count only when marked `[[nodiscard]]` (the catalogue reports
  `empty()` but not `erase()`). A discarded `?:` or comma operand is not checked.
- **OC-0148**: only conditions that are one integer literal or one object-like
  macro whose replacement (through other such macros) is one integer literal.
  Arithmetic conditions such as `#if A + B` are not evaluated.
- **OC-0149**: conditions with `&&`, `||` or `?:` are not checked (their short
  circuit may skip the identifier), nor are the arguments of anything called
  (`__has_include`, `__has_attribute`, function-like macros) or identifiers
  that only a macro of the condition expands to.
- **OC-0153**: GNU line markers (`# 12 "file"`) and the extension directives
  Clang accepts (`#include_next`, `#import`, `#ident`, `#sccs`, `#assert`,
  `#unassert`) count as valid.
- **OC-0369** constant assert: only arguments made of literals, `true`, `false`,
  `!`, `&&`, `||` and parentheses. `assert(sizeof(int) == 4)` is not seen. An
  assert written in a macro body is reported once, at the definition.
- **OC-0061** braced initializers: only the outermost written list is judged,
  and each written list gives at most one finding (at its first sub-object
  without braces). Not checked: lists with designators, the zero initializer,
  anonymous structure or union members, and the members of system-header classes
  (`std::array<int, 3> a = {1, 2, 3}`). A sub-list whose first value is itself a
  braced list is taken as braced.
- **OC-0412** single atomic reference: only `_Atomic` variables named directly
  (not members, not C++ `std::atomic`). A repeat inside a system macro's
  expression (`atomic_store(&a, atomic_load(&a) + 1)`) is not reported, because
  the operand is spelled in the header.
- **OC-0463**: only `pthread_kill` with a constant signal other than 0; `raise`
  and `kill` from a thread function are not checked (no notion of thread
  functions yet).
- **OC-0375**: only written `std::vector<...>` types; an instantiation reached
  only through a template parameter is not reported.
- **OC-0096** lone increment: only assignments, compound assignments and other
  builtin `++`/`--` count as the other side effect; calls (which may have side
  effects) and overloaded `++` on iterators (`*out++ = v`) are not checked.
- **OC-0475**: `signed char` named by a typedef (`int8_t`, sqlite's `i8`,
  imgui's `ImS8`) is a small integer, not a character, and is not reported;
  casts to enum types neither. Arguments are only checked for the ctype
  functions (`strchr(s, c)` converts back to char). Implicit widening in
  arithmetic (`c - '0'`) and in initialisation is not reported.
- **OC-0251**: constant operands (`'+' << 16`, enumerators) are not reported.
  An implicit conversion back to the small type (`std::uint8_t x = ~m;`) is
  reported, as the rule asks for a cast.
- **OC-0338**/**OC-0339**: "may outlive the call" is: returned, assigned to a
  member or static variable, given to `std::thread`, `std::jthread` or
  `std::async`, or added to a member container. Other asynchronous APIs and
  local containers are not recognised (no configuration key yet).
- **OC-0406**: thread creation is judged per unit (phase 2 can use the whole
  program); `getenv` is not reported.
- **OC-0409**: any enclosing loop counts; whether its condition re-tests the
  awaited predicate is not checked. `pthread_cond_wait` is not in the rule text
  and not reported.
- **OC-0094** and **OC-0098** (side effects in initializer lists and after
  `&&`/`||`) are not implemented yet: almost every finding would be a call,
  and the rule texts exempt functions without side effects, which needs a
  configuration key (pure functions, project getters) first.
- **OC-0365**: every `delete` is reported, as the rule text says (decided; the
  catalogue's bad example now expects its `delete` too). A pointer given to a constructor of a class with a
  destructor of its own counts as handed to an owner (`std::locale`); owners
  reached through a function call (`listeners()->Append(new L)`) are not
  recognised, and the resource-manager configuration key does not exist yet.
- **OC-0424**: "known to be aligned" is: an allocation result, a variable or
  field with enough declared alignment, a member whose offset in an aligned
  object is a multiple of the target alignment, and container_of with
  `offsetof`. Byte offsets that are aligned by construction (sqlite's
  `ROUND8` layouts within one allocation) are reported.
- **OC-0296** (explicit instantiation after a specialisation) is not
  implemented: Clang keeps no node for such an instantiation, only a warning.
- **OC-0032**: locals hiding class members, and members hiding namespace-scope
  names, are not checked; names of system headers (`timezone`, `optarg`) do
  not count as hidden globals.
- **OC-0031**: the limit is 63 characters (C99); the C90 limit of 31 and the
  configuration key do not exist yet.
- **OC-0332**: only member functions that override nothing; an override that
  hides other overloads of its name is not reported. fmt's formatters that
  derive from `formatter<string_view>` and declare their own `format` are
  reported, as the rule text says.
- **OC-0334**: volatile objects are not reported (the hardware-register
  configuration does not exist yet), and neither are variables whose name a
  macro body writes or pastes (Zephyr's `K_THREAD_STACK_DEFINE`,
  `LOG_MODULE_DECLARE`).
- **OC-0355**: a private copy or move member that is declared and never defined
  (the pre-C++11 way to forbid copying) is left out like a deleted one.
- **OC-0354**: only the "some but not all five declared" part; inconsistent
  copy and move definitions within a full set are not checked.
- **OC-0233**: only a parameter used twice inside one association. A use
  outside the `_Generic` is not reported, because it may be on another branch of
  the macro (Zephyr's cbprintf packing).
- **OC-0281**: only the "defined twice on one path" part; overriders in two
  sibling bases that share a virtual base are not checked. gtest listeners that
  override `EmptyTestEventListener`'s empty definitions are reported, as the
  rule text says.
- **OC-0290**: only templates whose parameters all take any type
  (`format(format_string<T...>, T&&...)` is not reported). Constraints are
  recognised as requires clauses, type constraints and `enable_if` in the
  spelled types.
- **OC-0347**: references to a volatile local (bound or passed by reference)
  count as leaving the function only through `&` and by-reference lambda
  captures.
- **OC-0293** (dependent base names) is not implemented yet.
- **OC-0294**: only named non-member calls (operators and member calls are not
  checked) to functions of the user's. Customisation points meant to be found
  later (fmt's `format_as`, ADL `begin`/`end`) are reported unless listed in the
  rule's `names` key of oculus.json.
- **OC-0307**: "can throw" is a throw, a new, or a call or construction of
  something not noexcept; destructors of temporaries are not considered.
- **OC-0230**: "a type that cannot match" is not checked, only qualified,
  array, function and atomic association types.
- **OC-0280**: private members of bases do not count.
- **OC-0299** (uniform viable set) is not implemented: the candidate set of
  overload resolution is not kept in the AST.
- **OC-0410**: only within one unit (the `signal` call and the thread creation
  in the same translation unit) until cross-TU facts exist.
- **OC-0420, OC-0470, OC-0471**: handlers are only the functions named directly
  in `atexit`, `at_quick_exit` or `signal` of the same unit; `sigaction`
  handlers are not followed. 0420 looks only at calls written in the handler
  itself.
- **OC-0432**: only the direct form `b[strlen(b) - 1]` on a variable filled by
  `fgets` in the same unit; a length kept in a variable is not followed.
- **OC-0468, OC-0469**: only what a handler named in `signal` does itself;
  functions it calls are not followed, and `sigaction` handlers are not seen.
- **OC-0419, OC-0459**: "on the same path" is the order within one function
  (see `late_group_drop` in doc/ir.md); copies of `envp` are not followed.
- **OC-0452**: only `asctime(&variable)`; any comparison of a member of the
  variable counts as a check.
- **OC-0472**: only local pointers initialised from a literal and never
  changed; literals reaching a pointer otherwise are not followed.
- **OC-0470**: `signal(sig, SIG_DFL)` and `SIG_IGN` in a handler are not
  reported; `handlers_persist: true` in oculus.json turns the rule off for
  platforms that keep handlers installed.
- **OC-0022**: the strict reading stays (decided): an octal or hex escape must
  be followed by the end of the literal or another escape, also where the next
  character could not continue it (`"\x1b[0m"`, `"One\0Two"`).
- **OC-0343**: every built-in unary `+` is reported, written literals such as
  `+1.0f` included (decided).
- **OC-0361, OC-0312, OC-0306**: "can throw" is known only for code with a
  body in the unit and for C++ library functions; calls of the user's
  functions defined in other units are not reported (phase 2). A throw inside a
  try block is never reported, whatever its handlers catch. Destructors of
  static objects are not looked at.
- **OC-0314**: only a local `std::thread` built with a function and never named
  again; the other implicit terminate paths are left to their own rules or not
  checked.
- **OC-0435**: a position counts as filled when the unit has an `fgetpos` of the
  same variable and stream anywhere, not on every path.
- **OC-0462**: only stores into `sin_port`/`sin6_port`; buffers given to
  `send`/`recv` are not followed.
- **OC-0460**: the uid and the gid functions are reported (decided; the
  catalogue's bad example now expects its `setgid` too). A path that goes on
  without testing a stored result is not checked yet.
- **OC-0303**: exceptions of types derived from the standard ones (fmt's
  `system_error`, `assertion_failure`) count as user types: their
  construction is followed like any other.
- **OC-0378**: only the half "a forwarding reference passed on without
  std::forward", and only for a single use outside loops and pack expansions;
  `std::forward` applied to something else is not checked yet.
- **OC-0421**: `strto*` is reported only with a null end pointer; whether the
  end pointer and errno are examined afterwards is not checked.
- **OC-0322**: only includes written in C++ files (.cpp, .cc, .hpp and so on);
  plain `.h` headers may be shared with C and are left alone.
- **OC-0433, OC-0456, OC-0458**: the order of statements in one function
  stands in for paths (see doc/ir.md).
- **OC-0014, OC-0180, OC-0121, OC-0010**: within one unit until phase 2.
  OC-0014 looks only at macros of the main file (headers can be used by other
  units); in an amalgamation such as sqlite3.c the public header text is part
  of the main file, so its unused interface macros are reported (55). OC-0180
  only in C and only for objects of the main file with internal linkage.
  OC-0121 follows only direct calls. OC-0010 only sees statements that follow
  a jump in the same block.
- **OC-0086**: only in C (C++ is OC-0337). sqlite writes 0 for null pointers
  throughout (3937 findings); they are correct by the rule text.
- **OC-0177**: only an `fclose` that runs unconditionally before the use in
  the same function (an fclose in a branch, such as an error path that then
  returns, is not followed); paths through other functions are not checked.
- **OC-0138**: only copies within one named variable at constant offsets;
  copies through two different pointers that may alias are not checked.
- **OC-0135**: returns and stores into static objects; addresses stored into
  heap objects or out parameters are not followed.
- **OC-0194**: within one unit; a function counts as non-returning only when
  the CFG shows it, so calls to functions not declared noreturn keep it
  returning.
- **OC-0182**: an argument that is a single macro name is not expanded and
  not reported (Zephyr passes devicetree macros); a minus sign makes the
  argument an expression and is reported.
- **OC-0046, OC-0122, OC-0142, OC-0154, OC-0189, OC-0302, OC-0329, OC-0386,
  OC-0414**: taken from Clang's diagnostics, so they depend on Clang
  reporting the construct; such a unit is still checked by all rules.
- **OC-0179**: only these obsolescent features: functions without a prototype
  (C before C23), a storage class after a type keyword or qualifier (C, not
  after a struct or typedef name), `register` (C++). Others (ungetc at the
  start of a file, realloc to size 0, and so on) are not checked yet.
- **OC-0084, OC-0311, OC-0342**: taken from Clang's diagnostics like batch 38.
- **OC-0200**: only a string literal argument with a constant size that
  reaches its terminator; prefix compares (size up to the characters) and
  strings in arrays are not reported.
- **OC-0201**: type punning with memcpy (sqlite and yyjson copy between
  doubles and 64-bit integers, 37 findings) is reported; it is what the rule
  text forbids.
- **OC-0195**: sqlite passes function names without & throughout (839).
- **OC-0217, OC-0212**: within one function and on named variables only;
  OC-0212 needs every mtx_init of the variable to name its kind.
- **OC-0033**: the limit is 63 characters (31 before C99); the rule text says
  it is configurable, a configuration key is not there yet. Zephyr's
  generated devicetree header has 490 such names (genuine).
- **OC-0259**: only continue in while and do loops; whether a for loop is
  well-formed is not checked yet.
- **OC-0175, OC-0450**: within one function, on local variables.
- **OC-0030**: within one unit; before C99 (yyjson's compile_ansi.c with
  -ansi) the 6 case-insensitive characters make every yyjson_ name clash (50).
- **OC-0352**: many findings (sqlite 5027, yyjson 1031); a C++ object whose
  class has a trivial default constructor counts as initialised.
- **OC-0297**: fmt specialises formatter (primary in base.h) for standard
  types in chrono.h, std.h and so on (54); reported as the rule text says.
- **OC-0222, OC-0224, OC-0404**: within one function (OC-0224 also one call
  deep); OC-0222 only in functions that also unlock the mutex, so lock
  wrappers are not reported.
- **OC-0054**: C only, objects at file scope of the main file; statics that
  a macro defines (Zephyr's LOG_MODULE_REGISTER and the like) are skipped.
- **OC-0058**: definitions in the main file; a parameter passed on to a
  function that takes a pointer to mutable counts as written.
- **OC-0260**: C++ locals of scalar or pointer type with an initializer;
  record and array objects are not checked yet.
- **OC-0147**: parameters next to `<`, `>`, `&`, `*`, `::` and `:` are not
  reported in C++ (template arguments, declarators), nor a parameter that
  is followed by `*` or `&` as a declarator or cast, the whole right side of
  an assignment or initializer, or the middle operand of `?:`.
- **OC-0211**: only the `__c11_atomic` builtins and C11 fences, as stdatomic.h
  maps them; GNU `__atomic` builtins (sqlite) are not reported.
- **OC-0091**: only constant shift amounts; variable amounts need value
  ranges from data flow.
- **OC-0445**: constant zero divisors, and divisions by a parameter that the
  function never checks (any comparison or test of it counts as a guard);
  other variables need value ranges. `-1` with the most negative value is
  not checked yet.
- **OC-0437**: from Clang's format checks, so only functions declared with
  a format attribute (the C library and wrappers that say so).
