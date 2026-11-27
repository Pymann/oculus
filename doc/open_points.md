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
- **OC-0369** constant assert: only arguments made of literals, `true`, `false`,
  `!`, `&&`, `||` and parentheses. `assert(sizeof(int) == 4)` is not seen. An
  assert written in a macro body is reported once, at the definition.
