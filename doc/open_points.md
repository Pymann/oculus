# Open points of the rules

Kept up to date with every rules batch. Only rules: each entry says what is open
and what it waits for. Rule numbers are the catalogue ids (Pymann/coding-rules).

## Decisions needed

- **OC-0022 terminated_escape, strict reading.** An octal or hex escape must be
  followed by the end of the literal or by another escape, as the catalogue text
  says. That also reports unambiguous cases such as `"One\0Two"` (imgui combo
  lists) and `"\x1b[0m"` (fmt colours). Those make up most of its findings in the
  real projects. Option: no report when the next character cannot continue the
  escape (not a hex digit after `\x`, not an octal digit after an octal escape).
  This needs a catalogue change.
- **OC-0343 no_unary_plus.** Reports every built-in unary `+`, including the
  deliberate `+1.0f` style in imgui (84 findings). It is correct by the rule text.
  Open: should written literals (`+1.0f`, `+0`) be exempt?

- **OC-0315 macros_at_global_scope.** Every #define or #undef inside a namespace
  counts, including the configuration macros fmt and gtest define between
  their namespace macros (239 findings in fmt, 206 of them in gtest). It is correct
  by the rule text. Open: should headers that only wrap a namespace be treated
  differently?

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
  `ImFormatString(buf, IM_COUNTOF(buf), ...)`) are reported as the rule text says;
  open: a configuration key for such pointer-and-count functions.
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
- **OC-0365**: only `new` is reported. The rule text also reports every
  `delete`, but the catalogue's bad example expects no finding at its
  `delete`; to be decided. A pointer given to a constructor of a class with a
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
