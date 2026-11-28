// The Clang side of Oculus, shared by both ways of reaching it: the JSON worker
// `oculus-clang` (oculus_clang.cpp) and the in-process shim liboculus_clang.so
// (oculus_shim.cpp, doc/inprocess_frontend.md).
//
// parse_unit() parses one translation unit with LibTooling and returns its
// RuleInformation as plain records (files, types with their sugar chain,
// declarations with position, origin and the written type specifiers).  The AST
// is gone when it returns: nothing of Clang leaves this header, ids are Oculus'
// own dense numbers.

#pragma once

#include <cstring>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Analysis/Analyses/ExprMutationAnalyzer.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticLex.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroArgs.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#ifndef OCULUS_RESOURCE_DIR
#define OCULUS_RESOURCE_DIR ""
#endif

using namespace clang;

namespace oculus {


struct Pos {
    int file = -1;
    unsigned line = 0;
    unsigned column = 0;
};

struct SrcPos {
    Pos spelling;
    Pos expansion;
    bool from_macro = false;
};

struct Origin {
    bool in_system_header = false;
    bool from_macro = false;
    bool from_macro_arg = false;
    bool implicit = false;
    bool instantiated = false;
};

struct FileRec {
    int uid = 0;
    std::string path;
    std::string kind;   // source | header | system_header
    bool is_main = false;
    // headers: protected against a second inclusion (#ifndef guard or #pragma
    // once, as Clang's multiple-include optimisation sees it), and where the
    // first token after the leading comments is
    bool include_guarded = true;
    unsigned first_line = 0;
    unsigned first_column = 0;
    // headers: the #include sits inside a declaration (an enum, an initializer, a
    // namespace): a data or X-macro file, meant to be included more than once
    bool included_in_declaration = false;
    // source files and headers: one character per physical line, as Oculus' own
    // line counting sees it: b blank, c comment only, x code only, m code and comment
    std::string line_classes;
    // source files and headers: lexical marks, "code:line:col;" each (comment_open,
    // comment_splice, trigraph, open_escape, odd_escape, digraph)
    std::string lexical_marks;
};

// One #define, #undef or #pragma (or _Pragma) outside system headers, in source
// order.  Recorded while the preprocessor runs; only directives of active code.
struct MacroRec {
    int uid = 0;
    std::string kind;           // define | undef | pragma | include | expansion | condition
    std::string name;           // the macro; for a pragma ""
    std::string body;           // define: replacement tokens, spaced; pragma: its text
    Pos pos;                    // the # of the directive (pragma: # or _Pragma)
    Pos name_pos;
    bool function_like = false;
    bool variadic = false;
    int param_count = 0;
    int stringize_count = 0;    // # operators (function-like macros only)
    int paste_count = 0;        // ## operators
    Pos first_operator;         // ## : its left operand; # : the # itself
    Pos second_operator;        // the second # or ## operator
    Pos stringize_paste;        // a ## right after `#param`
    bool uses_call_site = false;  // __FILE__, __LINE__, __func__ ... or a macro that does
    int line_count = 1;         // lines of the definition (backslash continuations)
    Pos splice_pos;             // define: the first backslash that continues the line
    int defined_file_uid = -1;  // undef: file of the definition it removes; -2 none in a file;
                                // include: the file included; expansion: file of the definition
    std::string scope;          // define, undef: function | class | namespace | "" (global)
    Pos argument_pos;           // expansion of a one-argument macro: where its argument is spelled (body: the tokens)
    SourceLocation loc;         // the name, while the AST is alive (not written out)
};

struct TypeRec {
    int uid = 0;
    std::string kind;
    std::string name;
    int canonical_uid = -1;
    int sugar_uid = -1;
    int decl_uid = -1;
    bool is_typedef = false;
    bool is_builtin = false;
    bool is_bool = false;
    bool is_integer = false;
    bool int_signed = false;
    int int_width = 0;
    bool plain_char = false;
    bool is_enum = false;
    bool is_record = false;
    bool is_dependent = false;
    int enum_underlying_uid = -1;
    std::string array_kind;     // "" | constant | incomplete | variable | dependent
    int array_size = -1;        // element count of a constant array
    int element_uid = -1;       // element type of an array, pointee of a pointer
    bool is_pointer = false;
};

struct DeclRec {
    int uid = 0;
    std::string kind;
    std::string name;
    SrcPos pos;
    Pos name_pos;
    Origin origin;
    Pos instantiation_pos;   // where the enclosing template was instantiated
    int type_uid = -1;
    int parent_uid = -1;
    bool is_definition = false;
    bool is_bit_field = false;
    int bit_width = -1;
    bool written_builtin = false;
    std::string sign_written;   // "" | signed | unsigned
    std::string tag;            // struct | union | class | enum, for tag declarations
    std::string storage;        // written storage class: "" | static | extern | register | auto
    bool is_inline = false;     // `inline` written
    bool is_local = false;      // declared inside a function body
    bool is_param = false;
    int param_count = -1;       // functions
    int virtual_bases = 0;      // records: base specifiers written with `virtual`
    bool is_scoped = false;     // enum class / enum struct
    bool is_fixed = false;      // enum with a written underlying type
    bool has_init = false;      // variable, enumerator or field with an initializer
    bool constant_init = false; // const variable whose initializer is a constant
    int overrides = 0;          // methods: number of overridden methods
    bool override_written = false;  // `override` or `final` written
    bool is_noreturn = false;
    bool returns_void = false;
    bool explicit_specialization = false;
    bool has_restrict = false;  // restrict anywhere in the declared type
    bool in_extern_c = false;
    bool is_main = false;
    bool is_first_decl = true;      // the first declaration of the entity in the TU
    bool virtual_written = false;   // methods: `virtual` written on this declaration
    bool template_specialization = false;  // an explicit specialisation of a function template
    bool external_linkage = false;  // functions and variables
    bool internal_linkage = false;  // functions and variables
    bool first_decl_static = false; // the first declaration of the entity says static
    bool odr_definition = false;    // a definition with external linkage that each includer repeats
    bool paren_declarator = false;  // a local `T (name);`: the name alone in parentheses, no initializer
    bool is_explicit = false;       // constructors and conversion functions: explicit (also explicit(true))
    bool is_deleted = false;        // functions: = delete
    bool is_defaulted = false;      // functions: = default (written or implied)
    int min_args = -1;              // functions: arguments a call needs (default arguments not counted)
    Pos type_pos;                   // where the written type starts (functions: the return type)
    Pos restrict_pos;               // the first restrict keyword of the declarator
    Pos declarator_pos;             // where the declarator starts: `*p` at the *, else the name
    // function definitions (not lambdas): the first token of the definition (its
    // template head, if any) and the closing brace of the body, both at the
    // expansion (a function made by a macro lies on the macro call), and the
    // preprocessing tokens of the body as written, braces included (-1 if unknown)
    Pos def_begin;
    Pos body_end;
    int body_tokens = -1;
    // parameters: written with array syntax (`int items[16]`), and the `static`
    // inside its brackets (`int items[static 16]`; invalid when there is none)
    bool array_param = false;
    Pos static_array_pos;
    bool copy_or_move = false;      // constructors: a copy or move constructor
    Pos pointer_pos;                // the * of a declarator whose written type is a pointer
    std::string ref_qualifier;      // member functions: "", "&" or "&&"
    std::string returns_kind;       // functions: "reference" or "pointer" when the return type is one
    bool is_referenced = false;     // variables and parameters: named somewhere (Clang's isReferenced)
    bool maybe_unused = false;      // [[maybe_unused]] or __attribute__((unused))
    // parameters of a definition: the name is written in the body as a token
    // (also in a macro argument, in code that #if skipped, in an inactive assert)
    bool named_in_body = false;
    // local variables: how often the name is written in the function body (the
    // declaration included), and whether it is a handler or range-for variable, a
    // structured binding or of class type (RAII objects)
    int name_count = -1;
    bool special_local = false;
    // class definitions: abstract with a public, not deleted copy assignment; a
    // one-argument constructor template and no user-declared copy constructor; a
    // one-parameter operator= template and no user-declared copy assignment
    bool abstract_public_copy = false;
    bool template_ctor_no_copy = false;
    bool template_assign_no_copy = false;
    // functions that must not throw (move members, swap, std::hash call operator)
    // and are not noexcept; destructors declared noexcept(false)
    bool may_throw = false;
    bool pure_overrides_impl = false;   // pure virtual, overriding a function that is not pure
    Pos default_arg_pos;                // overriders: a default argument the overridden function differs in
    std::string enum_value;             // enumerators: the value, decimal ("" if dependent)
    bool designated_array = false;      // variables: written `[]` size, initialised with array designators
    bool missing_base_init = false;     // user-provided constructors: a non-empty base not in the init list
    // float_loop_counter (a floating variable declared in a for header and used as
    // its counter), exposed_data (a public or protected field of a non-trivial
    // class), vexing_function (a block-scope function declaration written `T f();`),
    // pasted_name (functions and variables whose name a macro made with ##)
    std::string shape;
    // alignment specifiers written on this declaration (alignas, _Alignas; not
    // inherited from an earlier one): how many, where the second one is, and
    // where an alignment that evaluates to zero is written
    int align_count = 0;
    Pos second_align_pos;
    Pos zero_align_pos;
    int group_uid = -1;         // a variable or field: uid of the first declarator of its declaration
    int field_index = -1;       // position in its list: field in the record,
                                // enumerator in the enum, parameter in the function
    int field_count = -1;       // records: number of fields
    std::string target;         // using directive: the nominated namespace
    bool in_instantiation = false;  // inside a template instantiation (any enclosing context)
    bool array_size_literal = false;  // declared array bound is a plain integer literal
    bool is_mutated = false;    // static local: modified, or its address escapes, in its function
};

// One statement or expression of a function body or initializer, outside system
// headers and outside template instantiations (the template itself is recorded).
// `kind` is Clang's class name without the namespace (GotoStmt, BinaryOperator,
// CXXReinterpretCastExpr ...), `detail` the operator, cast or label spelling.
struct NodeRec {
    int uid = 0;
    std::string kind;
    std::string detail;
    SrcPos pos;
    bool in_system_header = false;  // spelled in a system header (a system macro)
    bool from_macro = false;
    int parent_uid = -1;
    int function_uid = -1;          // the function whose body holds it
    int ref_uid = -1;               // the declaration it names (DeclRefExpr, label ...)
    int child_count = 0;
    int type_uid = -1;              // expressions: their type
    bool is_dependent = false;      // expressions: type- or value-dependent (in a template)
    int label_count = -1;           // switch: case and default labels
    int clause_count = -1;          // switch: label groups that start a clause
    // switch, if and function bodies: the preprocessor skipped code inside it
    // (#if 0, an #ifdef that is not taken).  Another configuration may compile more.
    bool has_skipped_code = false;
    std::string spelling;           // literals: the characters as written (spelling file)
    std::string role;               // then | else | body: the sub-statement of its parent
    std::string before;             // NullStmt: the character before it on its line, "" if none
    // its key token (the keyword, a binary operator, the ? of ?:) is written in a
    // macro body, not in the source or a macro argument
    bool macro_body = false;
    // sizeof and alignof of an expression: the operand has a side effect (calls
    // not counted) and is not evaluated, because its type is not variably modified
    bool side_effect = false;
    // =, compound assignment, ++ and -- of errno with a value other than 0 or one
    // saved from errno
    bool errno_write = false;
    // initializers: partial (InitListExpr of an array with fewer elements and no
    // designators, not {0}), override (a DesignatedInitExpr that initialises an
    // element again), undesignated (a positional element of a list with chained
    // designators); the argument of std::move: move_const, move_rvalue; C++:
    // null_pointer (0 or NULL used as a null pointer), null_integer (NULL used as
    // an integer), polymorphic_downcast (a cast from a polymorphic base to a
    // derived class), virtual_base_cast (the operand of a reinterpret or C-style
    // cast from a virtual base to a derived class), virtual_member_compare (== or
    // != of the address of a virtual member function with anything but a null
    // pointer constant)
    std::string shape;
};

// Diagnostics that rules report (batch 38): the consumer keeps the ones listed
// in kept_diagnostic, the tables turn them into marks at the end of the unit.
struct DiagSeen {
    unsigned id = 0;
    SourceLocation loc;
    bool atomic_void = false;   // err_atomic_specifier_bad_type on void
    bool try_scope = false;     // a jump whose notes name a try block or handler
    bool odd_name = false;      // err_pp_file_not_found of a name with ' \\ " /* or //
    bool error = false;         // reported as an error (not a warning)
};
inline thread_local std::vector<DiagSeen> *diag_sink = nullptr;

// The kept errors that a rule reports as a finding (batch 38).  They do not
// make the unit unclean: Clang recovers from them, and the finding says what
// is wrong.  An unterminated #if counts only when an #endif or #else without
// #if was seen too (the conditional spans files, OC-0154).
inline int rule_errors(const std::vector<DiagSeen> &seen) {
    bool orphan = false;
    for (const DiagSeen &one : seen) {
        if (one.id == diag::err_pp_endif_without_if || one.id == diag::pp_err_else_without_if || one.id == diag::pp_err_elif_without_if) orphan = true;
    }
    int count = 0;
    for (const DiagSeen &one : seen) {
        if (!one.error) continue;
        bool reported = true;
        if (one.id == diag::err_atomic_specifier_bad_type) reported = one.atomic_void;
        if (one.id == diag::err_goto_into_protected_scope || one.id == diag::err_switch_into_protected_scope) reported = one.try_scope;
        if (one.id == diag::err_pp_file_not_found) reported = one.odd_name;
        if (one.id == diag::err_pp_unterminated_conditional) reported = orphan;
        if (reported) ++count;
    }
    return count;
}

inline bool kept_diagnostic(unsigned id) {
    static const unsigned kept[] = {
        diag::ext_missing_type_specifier, diag::ext_implicit_function_decl_c99,
        diag::warn_implicit_function_decl, diag::ext_implicit_lib_function_decl,
        diag::ext_implicit_lib_function_decl_c99, diag::err_pp_expects_filename,
        diag::err_pp_endif_without_if, diag::pp_err_else_without_if, diag::pp_err_elif_without_if,
        diag::err_atomic_specifier_bad_type, diag::err_goto_into_protected_scope,
        diag::err_switch_into_protected_scope, diag::err_early_catch_all,
        diag::err_unsupported_string_concat, diag::err_static_non_static, diag::err_non_static_static,
        diag::err_pp_file_not_found, diag::err_pp_unterminated_conditional,
        diag::err_cast_pointer_to_non_pointer_int, diag::err_cast_pointer_from_non_pointer_int,
        diag::warn_atomic_member_access, diag::err_missing_exception_specification,
        diag::err_mismatched_exception_spec, diag::ext_missing_exception_specification,
        diag::ext_mismatched_exception_spec, diag::ext_register_storage_class, diag::warn_deprecated_register,
        diag::warn_cannot_pass_non_pod_arg_to_vararg, diag::warn_non_pod_vararg_with_format_string,
        diag::err_cannot_pass_to_vararg, diag::err_cannot_pass_to_vararg_format,
        diag::err_cannot_pass_non_trivial_c_struct_to_vararg};
    for (unsigned one : kept) {
        if (one == id) return true;
    }
    return false;
}

// The tables of one TU, filled while the AST is alive.
class Tables {
public:
    explicit Tables(ASTContext &context) : context_(context), sm_(context.getSourceManager()) {}

    int file_of(SourceLocation loc) {
        // A token made by ## lives in the scratch buffer, not in a file.
        if (loc.isInvalid() || sm_.isWrittenInScratchSpace(loc)) {
            return -1;
        }
        FileID fid = sm_.getFileID(loc);
        auto known = file_ids_.find(fid);
        if (known != file_ids_.end()) {
            return known->second;
        }
        FileRec rec;
        rec.uid = static_cast<int>(files.size());
        // "./x.h" (a header found next to its includer) is named "x.h"
        llvm::SmallString<256> path(sm_.getFilename(loc));
        llvm::sys::path::remove_dots(path, false);
        rec.path = std::string(path.str());
        rec.is_main = fid == sm_.getMainFileID();
        SrcMgr::CharacteristicKind kind = sm_.getFileCharacteristic(loc);
        if (rec.is_main) {
            rec.kind = "source";
        } else if (kind == SrcMgr::C_System || kind == SrcMgr::C_ExternCSystem) {
            rec.kind = "system_header";
        } else {
            rec.kind = "header";
        }
        files.push_back(rec);
        file_ids_[fid] = rec.uid;
        return rec.uid;
    }

    Pos pos_of(SourceLocation file_loc) {
        Pos pos;
        if (file_loc.isInvalid()) {
            return pos;
        }
        pos.file = file_of(file_loc);
        pos.line = sm_.getSpellingLineNumber(file_loc);
        pos.column = sm_.getSpellingColumnNumber(file_loc);
        return pos;
    }

    SrcPos src_pos_of(SourceLocation loc) {
        SrcPos pos;
        pos.from_macro = loc.isMacroID();
        pos.spelling = pos_of(sm_.getSpellingLoc(loc));
        pos.expansion = pos_of(sm_.getExpansionLoc(loc));
        return pos;
    }

    Origin origin_of(const Decl *decl) {
        Origin origin;
        SourceLocation loc = decl->getBeginLoc();
        if (loc.isValid()) {
            origin.from_macro = loc.isMacroID();
            origin.from_macro_arg = loc.isMacroID() && sm_.isMacroArgExpansion(loc);
            origin.in_system_header = sm_.isInSystemHeader(sm_.getExpansionLoc(loc));
        }
        origin.implicit = decl->isImplicit();
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            origin.instantiated = fn->isTemplateInstantiation();
        } else if (const auto *record = dyn_cast<CXXRecordDecl>(decl)) {
            origin.instantiated = record->getTemplateSpecializationKind() == TSK_ImplicitInstantiation;
        }
        return origin;
    }

    int type_of(QualType type) {
        if (type.isNull()) {
            return -1;
        }
        const Type *ptr = type.getTypePtr();
        auto known = type_ids_.find(ptr);
        if (known != type_ids_.end()) {
            return known->second;
        }
        int uid = static_cast<int>(types.size());
        types.emplace_back();
        type_ids_[ptr] = uid;
        TypeRec rec;
        rec.uid = uid;
        rec.kind = ptr->getTypeClassName();
        rec.name = QualType(ptr, 0).getAsString(context_.getPrintingPolicy());
        QualType canonical = ptr->getCanonicalTypeInternal();
        QualType next = QualType(ptr, 0).getSingleStepDesugaredType(context_);
        if (next.getTypePtr() != ptr) {
            rec.sugar_uid = type_of(next);
        }
        rec.canonical_uid = canonical.getTypePtr() == ptr ? uid : type_of(canonical);
        rec.is_dependent = ptr->isDependentType();
        if (const auto *alias = dyn_cast<TypedefType>(ptr)) {
            rec.is_typedef = true;
            rec.decl_uid = decl_of(alias->getDecl());
        }
        if (const auto *builtin = dyn_cast<BuiltinType>(ptr)) {
            rec.is_builtin = true;
            rec.is_bool = builtin->getKind() == BuiltinType::Bool;
            rec.is_integer = builtin->isInteger() && !rec.is_bool;
            rec.plain_char = builtin->getKind() == BuiltinType::Char_S || builtin->getKind() == BuiltinType::Char_U;
            if (rec.is_integer) {
                rec.int_signed = builtin->isSignedInteger();
                rec.int_width = static_cast<int>(context_.getIntWidth(QualType(ptr, 0)));
            }
        }
        // getAsTagDecl() rather than EnumType/RecordType::getDecl(): it keeps working
        // across the type-sugar changes of newer Clang releases.
        if (isa<EnumType>(ptr)) {
            rec.is_enum = true;
            if (const auto *enum_decl = dyn_cast_or_null<EnumDecl>(ptr->getAsTagDecl())) {
                rec.decl_uid = decl_of(enum_decl);
                rec.enum_underlying_uid = type_of(enum_decl->getIntegerType());
            }
        }
        if (isa<RecordType>(ptr)) {
            rec.is_record = true;
            rec.decl_uid = decl_of(ptr->getAsTagDecl());
        }
        if (const auto *array = dyn_cast<ArrayType>(ptr)) {
            rec.element_uid = type_of(array->getElementType());
            if (const auto *sized = dyn_cast<ConstantArrayType>(array)) {
                rec.array_kind = "constant";
                rec.array_size = static_cast<int>(sized->getSize().getLimitedValue(INT32_MAX));
            } else if (isa<IncompleteArrayType>(array)) {
                rec.array_kind = "incomplete";
            } else if (isa<VariableArrayType>(array)) {
                rec.array_kind = "variable";
            } else {
                rec.array_kind = "dependent";
            }
        }
        if (const auto *pointer = dyn_cast<PointerType>(ptr)) {
            rec.is_pointer = true;
            rec.element_uid = type_of(pointer->getPointeeType());
        }
        types[uid] = rec;
        return uid;
    }

    // Interns a declaration.  Declarations of system headers are only added when a
    // type refers to them (a typedef or enum of a system header).
    int decl_of(const NamedDecl *decl) {
        if (decl == nullptr) {
            return -1;
        }
        auto known = decl_ids_.find(decl);
        if (known != decl_ids_.end()) {
            return known->second;
        }
        int uid = static_cast<int>(decls.size());
        decls.emplace_back();
        decl_ids_[decl] = uid;
        DeclRec rec;
        rec.uid = uid;
        rec.kind = decl->getDeclKindName();
        rec.name = decl->getNameAsString();
        rec.pos = src_pos_of(decl->getBeginLoc());
        rec.name_pos = pos_of(sm_.getExpansionLoc(decl->getLocation()));
        rec.origin = origin_of(decl);
        if (const auto *spec = dyn_cast<ClassTemplateSpecializationDecl>(decl->getDeclContext())) {
            if (spec->getTemplateSpecializationKind() == TSK_ImplicitInstantiation ||
                spec->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDefinition) {
                rec.origin.instantiated = true;
                rec.instantiation_pos = pos_of(sm_.getExpansionLoc(spec->getPointOfInstantiation()));
            }
        }
        if (const auto *parent = dyn_cast_or_null<NamedDecl>(decl->getDeclContext() ? Decl::castFromDeclContext(decl->getDeclContext()) : nullptr)) {
            if (!isa<TranslationUnitDecl>(parent)) {
                rec.parent_uid = decl_of(parent);
            }
        }
        TypeSourceInfo *written = nullptr;
        if (const auto *declarator = dyn_cast<DeclaratorDecl>(decl)) {
            written = declarator->getTypeSourceInfo();
            rec.type_uid = type_of(declarator->getType());
        } else if (const auto *alias = dyn_cast<TypedefNameDecl>(decl)) {
            written = alias->getTypeSourceInfo();
            rec.type_uid = type_of(alias->getUnderlyingType());
        }
        if (written != nullptr) {
            note_written(written->getTypeLoc(), rec);
            if (isa<ParmVarDecl>(decl)) note_array_param(written->getTypeLoc(), rec);
            note_pointer(written->getTypeLoc(), rec);
        }
        note_kind_details(decl, rec);
        if (const auto *field = dyn_cast<FieldDecl>(decl)) {
            rec.is_bit_field = field->isBitField();
            if (rec.is_bit_field && !field->getBitWidth()->isValueDependent()) {
#if CLANG_VERSION_MAJOR >= 20
                rec.bit_width = static_cast<int>(field->getBitWidthValue());
#else
                rec.bit_width = static_cast<int>(field->getBitWidthValue(context_));
#endif
            }
        }
        if (const auto *tag = dyn_cast<TagDecl>(decl)) {
            rec.is_definition = tag->isThisDeclarationADefinition();
        } else if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            rec.is_definition = fn->isThisDeclarationADefinition();
        } else if (const auto *var = dyn_cast<VarDecl>(decl)) {
            rec.is_definition = var->isThisDeclarationADefinition() != VarDecl::DeclarationOnly;
        }
        decls[uid] = rec;
        return uid;
    }

    std::vector<FileRec> files;
    std::vector<TypeRec> types;
    std::vector<DeclRec> decls;

    // Records, by declaration kind, what the rules on declarations read.
    void note_kind_details(const NamedDecl *decl, DeclRec &rec) {
        rec.in_instantiation = inside_instantiation(decl);
        const DeclContext *lexical = decl->getLexicalDeclContext();
        rec.is_local = lexical != nullptr && lexical->isFunctionOrMethod();
        // a tag first named inside an extern "C" block (`typedef struct x x;`) belongs
        // to the enclosing namespace semantically, but is written in the block
        rec.in_extern_c = (decl->getDeclContext() != nullptr && decl->getDeclContext()->isExternCContext()) ||
                          (decl->getLexicalDeclContext() != nullptr && decl->getLexicalDeclContext()->isExternCContext());
        if (const auto *tag = dyn_cast<TagDecl>(decl)) {
            rec.tag = tag->getKindName().str();
        }
        if (const auto *directive = dyn_cast<UsingDirectiveDecl>(decl)) {
            if (const NamespaceDecl *nominated = directive->getNominatedNamespace()) {
                rec.target = nominated->getQualifiedNameAsString();
            }
        }
        if (const auto *record = dyn_cast<RecordDecl>(decl)) {
            int count = 0;
            for (auto it = record->field_begin(); it != record->field_end(); ++it) ++count;
            rec.field_count = count;
        }
        if (const auto *klass = dyn_cast<CXXRecordDecl>(decl)) {
            if (klass->hasDefinition() && klass->isThisDeclarationADefinition()) {
                for (const CXXBaseSpecifier &base : klass->bases()) {
                    if (base.isVirtual()) ++rec.virtual_bases;
                }
            }
        }
        if (const auto *enum_decl = dyn_cast<EnumDecl>(decl)) {
            rec.is_scoped = enum_decl->isScoped();
            rec.is_fixed = enum_decl->getIntegerTypeSourceInfo() != nullptr;   // written, not implied
        }
        if (const auto *constant = dyn_cast<EnumConstantDecl>(decl)) {
            rec.has_init = constant->getInitExpr() != nullptr;
        }
        rec.is_first_decl = decl->isFirstDecl();
        if (const auto *value = dyn_cast<ValueDecl>(decl)) {
            bool linked = isa<FunctionDecl>(value) || isa<VarDecl>(value);
            rec.external_linkage = linked && value->hasExternalFormalLinkage();
            rec.internal_linkage = linked && value->getFormalLinkage() == Linkage::Internal;
            // static at namespace scope; a static class member is something else
            if (const auto *fn = dyn_cast<FunctionDecl>(value)) {
                const FunctionDecl *first = fn->getFirstDecl();
                rec.first_decl_static = first->getStorageClass() == SC_Static && !first->isCXXClassMember() &&
                                        first->getDeclContext()->isFileContext();
            } else if (const auto *var = dyn_cast<VarDecl>(value)) {
                const VarDecl *first = var->getFirstDecl();
                rec.first_decl_static = first->getStorageClass() == SC_Static && !first->isStaticDataMember() &&
                                        first->getDeclContext()->isFileContext();
            }
        }
        if (const auto *constant = dyn_cast<EnumConstantDecl>(decl)) {
            const auto *owner = dyn_cast<EnumDecl>(constant->getDeclContext());
            if (owner != nullptr) {
                int index = 0;
                for (const EnumConstantDecl *item : owner->enumerators()) {
                    if (item == constant) {
                        rec.field_index = index;
                        break;
                    }
                    ++index;
                }
            }
        }
        if (const auto *param = dyn_cast<ParmVarDecl>(decl)) {
            // parameters of a function type inside a declarator (a callback) are
            // not the function's own
            if (param->getFunctionScopeDepth() == 0) {
                rec.field_index = static_cast<int>(param->getFunctionScopeIndex());
            }
        }
        if (const auto *field = dyn_cast<FieldDecl>(decl)) {
            rec.field_index = static_cast<int>(field->getFieldIndex());
            rec.has_init = field->hasInClassInitializer();
        }
        if (const auto *declarator = dyn_cast<DeclaratorDecl>(decl)) {
            rec.has_restrict = has_restrict(declarator->getType());
            // `int *restrict p` as a parameter: the function type drops the
            // top-level qualifier of a parameter, its declaration keeps it
            if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
                for (const ParmVarDecl *param : fn->parameters()) {
                    if (param != nullptr && has_restrict(param->getType())) rec.has_restrict = true;
                }
            }
        } else if (const auto *alias = dyn_cast<TypedefNameDecl>(decl)) {
            rec.has_restrict = has_restrict(alias->getUnderlyingType());
        }
        if (const auto *declarator = dyn_cast<DeclaratorDecl>(decl)) {
            rec.array_size_literal = literal_array_bound(declarator);
        }
        if (const auto *var = dyn_cast<VarDecl>(decl)) {
            if (var->isStaticLocal() && !rec.in_instantiation) {
                rec.is_mutated = mutated_in_function(var);
            }
            rec.storage = storage_name(var->getStorageClass());
            rec.is_param = isa<ParmVarDecl>(var);
            rec.has_init = var->hasInit();
            if (var->isConstexpr()) {
                rec.constant_init = true;
            } else if (var->getType().isConstQualified() && var->hasInit() && !var->getType()->isDependentType()) {
                const Expr *init = var->getInit();
                rec.constant_init = var->isConstexpr() ||
                    (!init->isValueDependent() && init->isConstantInitializer(context_, false));
            }
        }
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            rec.storage = storage_name(fn->getStorageClass());
            rec.is_inline = fn->isInlineSpecified();
            rec.param_count = static_cast<int>(fn->getNumParams());
            rec.is_noreturn = fn->hasAttr<C11NoReturnAttr>() || fn->hasAttr<CXX11NoReturnAttr>() ||
                              fn->hasAttr<NoReturnAttr>();
            rec.returns_void = fn->getReturnType()->isVoidType();
            rec.explicit_specialization = fn->getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
            rec.template_specialization = rec.explicit_specialization && fn->getPrimaryTemplate() != nullptr;
            rec.is_main = fn->isMain();
            SourceRange returned = fn->getReturnTypeSourceRange();
            if (returned.isValid()) rec.type_pos = pos_of(sm_.getExpansionLoc(returned.getBegin()));
        }
        if (const auto *method = dyn_cast<CXXMethodDecl>(decl)) {
            rec.overrides = static_cast<int>(method->size_overridden_methods());
            rec.override_written = method->hasAttr<OverrideAttr>() || method->hasAttr<FinalAttr>();
            rec.virtual_written = method->isVirtualAsWritten();
        }
        if (rec.has_restrict) {
            rec.restrict_pos = restrict_token(decl);
        }
        if (const auto *declarator = dyn_cast<DeclaratorDecl>(decl)) {
            rec.declarator_pos = pos_of(sm_.getExpansionLoc(declarator_start(declarator)));
            // The declarators of one declaration share its begin location; every
            // macro expansion has its own locations, so separate expansions differ.
            auto key = std::make_pair(static_cast<const void *>(decl->getDeclContext()), decl->getBeginLoc().getRawEncoding());
            auto known = groups_.emplace(key, rec.uid).first;
            rec.group_uid = known->second;
        }
        rec.odr_definition = odr_definition(decl);
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            rec.is_deleted = fn->isDeleted();
            rec.is_defaulted = fn->isDefaulted();
            rec.min_args = static_cast<int>(fn->getMinRequiredArguments());
            QualType result = fn->getReturnType();
            rec.returns_kind = result.isNull() ? "" : result->isReferenceType() ? "reference" : result->isPointerType() ? "pointer" : "";
            if (const auto *method = dyn_cast<CXXMethodDecl>(fn)) {
                rec.ref_qualifier = method->getRefQualifier() == RQ_LValue ? "&"
                                  : method->getRefQualifier() == RQ_RValue ? "&&" : "";
            }
            if (const auto *ctor = dyn_cast<CXXConstructorDecl>(fn)) {
                rec.is_explicit = ctor->isExplicit();
                rec.copy_or_move = ctor->isCopyOrMoveConstructor();
            }
            if (const auto *conv = dyn_cast<CXXConversionDecl>(fn)) rec.is_explicit = conv->isExplicit();
            if (fn->doesThisDeclarationHaveABody() && fn->getBody() != nullptr && !(isa<CXXMethodDecl>(fn) && cast<CXXMethodDecl>(fn)->getParent()->isLambda())) {
                measure_body(fn, rec);
            }
        }
        if (const auto *var = dyn_cast<VarDecl>(decl)) {
            rec.paren_declarator = paren_declarator(var);
        }
        note_alignment(decl, rec);
        rec.maybe_unused = decl->hasAttr<UnusedAttr>();
        if (isa<VarDecl>(decl)) rec.is_referenced = decl->isReferenced();
        if (const auto *param = dyn_cast<ParmVarDecl>(decl)) {
            const auto *owner = dyn_cast_or_null<FunctionDecl>(param->getDeclContext());
            if (owner != nullptr && owner->doesThisDeclarationHaveABody() && owner->getBody() != nullptr &&
                !param->getName().empty()) {
                rec.named_in_body = body_names(owner).count(param->getName().str()) > 0;
            }
        } else if (const auto *local = dyn_cast<VarDecl>(decl)) {
            const auto *owner = dyn_cast_or_null<FunctionDecl>(local->getDeclContext());
            if (local->isLocalVarDecl() && owner != nullptr && owner->doesThisDeclarationHaveABody() &&
                owner->getBody() != nullptr && local->getIdentifier() != nullptr) {
                const auto &names = body_names(owner);
                auto found = names.find(local->getName().str());
                rec.name_count = found != names.end() ? found->second : 0;
            }
            rec.special_local = local->isExceptionVariable() || local->isCXXForRangeDecl() ||
                                isa<DecompositionDecl>(local);
            QualType held = context_.getBaseElementType(local->getType());
            rec.special_local = rec.special_local || held->isRecordType() || held->isDependentType();
        }
        if (const auto *constant = dyn_cast<EnumConstantDecl>(decl)) {
            if (!constant->getDeclContext()->isDependentContext())
                rec.enum_value = toString(constant->getInitVal(), 10);
        }
        if (const auto *var = dyn_cast<VarDecl>(decl)) rec.designated_array = designated_array(var);
        if (const auto *ctor = dyn_cast<CXXConstructorDecl>(decl)) rec.missing_base_init = missing_base_init(ctor);
        bool pasted = pasted_name(decl);
        if (pasted && (isa<FunctionDecl>(decl) || isa<VarDecl>(decl))) rec.shape = "pasted_name";
        if (redeclares_system(decl)) rec.shape = rec.shape.empty() ? "redeclares_system" : rec.shape + ",redeclares_system";
        SourceLocation named = decl->getLocation();
        if (!pasted && named.isMacroID() && !sm_.isMacroArgExpansion(named)) {
            rec.shape = rec.shape.empty() ? "macro_body_name" : rec.shape + ",macro_body_name";
        }
        if (const auto *field = dyn_cast<FieldDecl>(decl)) {
            if (!pasted && exposed_data(field)) rec.shape = "exposed_data";
        }
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            if (vexing_function(fn)) rec.shape = "vexing_function";
        }
        if (flexible_object(decl)) add_shape(rec, "flexible_object");
        if (shadows_outer(decl)) add_shape(rec, "shadowing");
        if (long_name_clash(decl)) add_shape(rec, "long_name_clash");
        if (type_name_clash(decl)) add_shape(rec, "type_clash");
        if (hides_base_function(decl)) add_shape(rec, "hides_base");
        if (odd_special_signature(decl)) add_shape(rec, "odd_special_signature");
        if (global_variable(decl)) add_shape(rec, "global_variable");
        if (partial_special_members(decl)) add_shape(rec, "partial_special_members");
        if (mixed_virtual_base(decl)) add_shape(rec, "mixed_virtual_base");
        if (repeated_overrider(decl)) add_shape(rec, "repeated_overrider");
        if (open_function_template(decl)) add_shape(rec, "open_template");
        if (pointless_volatile(decl)) add_shape(rec, "pointless_volatile");
        if (ambiguous_inherited(decl)) add_shape(rec, "ambiguous_inherited");
        if (unguarded_main(decl)) add_shape(rec, "unguarded_main");
        if (straddled_by_using(decl)) add_shape(rec, "straddled_using");
        note_late_calls(decl);
        note_throwing_bodies(decl);
        note_noreturn_returns(decl);
        note_function_paths(decl);
        note_obsolescent(decl);
        if (decl->getLocation().isValid()) decl_begins_[decl->getLocation().getRawEncoding()] = decl->getBeginLoc();
        note_graph_function(decl);
        if (unused_static(decl)) add_shape(rec, "unused_static");
        if (throwing_static(decl)) add_shape(rec, "throwing_static");
        if (unjoined_thread(decl)) add_shape(rec, "unjoined_thread");
        if (qualified_function(decl)) add_shape(rec, "qualified_function");
        note_class_shape(decl, rec);
        note_must_not_throw(decl, rec);
        note_override_defaults(decl, rec);
    }

    // A variable written with `[]` whose initializer list uses array designators.
    static bool designated_array(const VarDecl *var) {
        const TypeSourceInfo *written = var->getTypeSourceInfo();
        if (written == nullptr || !var->hasInit()) return false;
        TypeLoc loc = written->getTypeLoc().getUnqualifiedLoc();
        if (!loc.getAs<IncompleteArrayTypeLoc>()) return false;
        const auto *list = dyn_cast<InitListExpr>(var->getInit()->IgnoreImplicit());
        if (list == nullptr) return false;
        if (list->isSemanticForm() && list->getSyntacticForm() != nullptr) list = list->getSyntacticForm();
        for (const Expr *item : list->inits()) {
            const auto *designated = dyn_cast_or_null<DesignatedInitExpr>(item);
            if (designated == nullptr || designated->size() == 0) continue;
            const DesignatedInitExpr::Designator *first = designated->getDesignator(0);
            if (first->isArrayDesignator() || first->isArrayRangeDesignator()) return true;
        }
        return false;
    }

    // A function or variable that an earlier declaration in a system header
    // declares already (a library function declared again by the user).
    bool redeclares_system(const Decl *decl) const {
        if (!isa<FunctionDecl>(decl) && !isa<VarDecl>(decl)) return false;
        for (const Decl *earlier = decl->getPreviousDecl(); earlier != nullptr; earlier = earlier->getPreviousDecl()) {
            SourceLocation spelled = sm_.getSpellingLoc(earlier->getLocation());
            if (spelled.isValid() && sm_.isInSystemHeader(spelled)) return true;
        }
        return false;
    }

    // The name is made by a macro with ## (gmock's mockers, Zephyr's K_*_DEFINE
    // buffers): the user did not write it.
    bool pasted_name(const Decl *decl) const {
        SourceLocation loc = decl->getLocation();
        return loc.isMacroID() && sm_.isWrittenInScratchSpace(sm_.getSpellingLoc(loc));
    }

    // A named public or protected field of a C++ class that is not an aggregate,
    // has a user-provided constructor or destructor, a virtual function, a base or a
    // private field, and does not keep all its data public.
    static bool exposed_data(const FieldDecl *field) {
        if (field->isImplicit() || field->getIdentifier() == nullptr) return false;
        if (field->getAccess() != AS_public && field->getAccess() != AS_protected) return false;
        const auto *record = dyn_cast<CXXRecordDecl>(field->getParent());
        if (record == nullptr || record->isUnion() || record->isLambda() || !record->hasDefinition()) return false;
        record = record->getDefinition();
        if (record->isAggregate()) return false;
        bool invariant = record->isPolymorphic() || record->getNumBases() > 0 || record->getNumVBases() > 0;
        if (const CXXDestructorDecl *dtor = record->getDestructor()) invariant = invariant || dtor->isUserProvided();
        for (const CXXConstructorDecl *ctor : record->ctors()) invariant = invariant || ctor->isUserProvided();
        for (const Decl *member : record->decls()) {
            if (const auto *tmpl = dyn_cast<FunctionTemplateDecl>(member)) {
                invariant = invariant || isa<CXXConstructorDecl>(tmpl->getTemplatedDecl());
            }
        }
        bool hidden = false;
        for (const FieldDecl *other : record->fields()) {
            if (other->getAccess() != AS_public) hidden = true;
            invariant = invariant || other->getAccess() == AS_private;
        }
        // a struct whose data is all public is plain data, even with a constructor
        return invariant && hidden;
    }

    // A block-scope function declaration with nothing between its parentheses
    // (`Widget widget();`, which reads like an object with no arguments).
    bool vexing_function(const FunctionDecl *fn) const {
        if (!context_.getLangOpts().CPlusPlus || !fn->isLocalExternDecl() || fn->getTypeSourceInfo() == nullptr) return false;
        if (fn->getNumParams() != 0) return false;
        auto proto = fn->getTypeSourceInfo()->getTypeLoc().IgnoreParens().getAs<FunctionProtoTypeLoc>();
        if (!proto) return false;
        SourceLocation open = proto.getLParenLoc();
        SourceLocation close = proto.getRParenLoc();
        if (open.isInvalid() || close.isInvalid() || open.isMacroID() || close.isMacroID()) return false;
        bool bad = false;
        StringRef text = Lexer::getSourceText(CharSourceRange::getCharRange(open.getLocWithOffset(1), close),
                                              sm_, context_.getLangOpts(), &bad);
        return !bad && text.trim().empty();
    }

    // Does the class or one of its bases have a non-static data member (a vtable
    // pointer does not count: interfaces have no data)?
    static bool has_data(const CXXRecordDecl *record) {
        if (record == nullptr || !record->hasDefinition()) return false;
        record = record->getDefinition();
        if (!record->field_empty()) return true;
        for (const CXXBaseSpecifier &base : record->bases()) {
            if (has_data(base.getType()->getAsCXXRecordDecl())) return true;
        }
        return false;
    }

    // A user-provided, non-delegating constructor definition that leaves a direct or
    // virtual base with data out of its member initialiser list.
    static bool missing_base_init(const CXXConstructorDecl *ctor) {
        if (!ctor->isThisDeclarationADefinition() || !ctor->isUserProvided() || ctor->isDelegatingConstructor() ||
            ctor->isImplicit() || ctor->getParent()->isDependentContext() || ctor->isDependentContext()) {
            return false;
        }
        const CXXRecordDecl *owner = ctor->getParent();
        std::vector<const CXXRecordDecl *> bases;
        auto want = [&bases](const CXXBaseSpecifier &base) {
            const CXXRecordDecl *record = base.getType()->getAsCXXRecordDecl();
            if (record == nullptr || !record->hasDefinition()) return false;
            record = record->getDefinition();
            if (has_data(record)) bases.push_back(record);
            return true;
        };
        for (const CXXBaseSpecifier &base : owner->bases()) {
            if (base.getType()->isDependentType() || !want(base)) return false;
        }
        for (const CXXBaseSpecifier &base : owner->vbases()) {
            if (!want(base)) return false;
        }
        for (const CXXRecordDecl *base : bases) {
            bool named = false;
            for (const CXXCtorInitializer *init : ctor->inits()) {
                if (!init->isWritten() || !init->isBaseInitializer()) continue;
                const CXXRecordDecl *record = init->getBaseClass()->getAsCXXRecordDecl();
                named = named || (record != nullptr && record->getCanonicalDecl() == base->getCanonicalDecl());
            }
            if (!named) return true;
        }
        return false;
    }

    static const Expr *without_implicit_casts(const Expr *expr) {
        while (const auto *cast = dyn_cast_or_null<ImplicitCastExpr>(expr)) expr = cast->getSubExpr();
        return expr;
    }

    // {0}, {{0}}, {} and the like: the zero initializer.
    bool zero_init(const Expr *expr) const {
        expr = without_implicit_casts(expr);
        if (const auto *list = dyn_cast<InitListExpr>(expr)) {
            if (list->getNumInits() == 0) return true;
            return list->getNumInits() == 1 && zero_init(list->getInit(0));
        }
        if (expr->isValueDependent()) return false;
        if (expr->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull) return true;
        Expr::EvalResult result;
        if (!expr->EvaluateAsRValue(result, context_)) return false;
        if (result.Val.isInt()) return result.Val.getInt() == 0;
        if (result.Val.isFloat()) return result.Val.getFloat().isZero();
        return false;
    }

    // The shapes of a written initializer list (NodeRec.shape): partial array lists,
    // elements initialised twice, positional elements next to chained designators.
    // The traversal records the semantic form and walks the written elements.
    void note_init_list(const InitListExpr *semantic, NodeRec &rec) {
        const InitListExpr *list = semantic;
        if (list->isSemanticForm() && list->getSyntacticForm() != nullptr) list = list->getSyntacticForm();
        if (semantic->getType().isNull() || semantic->getType()->isDependentType()) return;
        bool designated = false, chained = false, ranged = false;
        for (const Expr *item : list->inits()) {
            const auto *one = dyn_cast_or_null<DesignatedInitExpr>(item);
            if (one == nullptr) continue;
            designated = true;
            chained = chained || one->size() > 1;
            for (const DesignatedInitExpr::Designator &part : one->designators()) ranged = ranged || part.isArrayRangeDesignator();
        }
        if (const auto *array = context_.getAsConstantArrayType(semantic->getType())) {
            uint64_t size = array->getSize().getZExtValue();
            unsigned count = list->getNumInits();
            bool text = count == 1 && isa<StringLiteral>(without_implicit_casts(list->getInit(0)));
            if (!designated && count > 0 && count < size && !text && !zero_init(list)) rec.shape = "partial";
        }
        note_brace_elision(semantic, list);
        if (chained) {
            for (const Expr *item : list->inits()) {
                if (item != nullptr && !isa<DesignatedInitExpr>(item)) pending_shape_[without_implicit_casts(item)] = "undesignated";
            }
        }
        if (!designated || ranged) return;
        std::vector<std::string> keys;
        for (const Expr *item : list->inits()) {
            const auto *one = dyn_cast_or_null<DesignatedInitExpr>(item);
            if (one == nullptr) return;
            std::string key;
            for (unsigned at = 0; at < one->size(); ++at) {
                const DesignatedInitExpr::Designator *part = one->getDesignator(at);
                if (part->isFieldDesignator()) {
                    const IdentifierInfo *name = part->getFieldName();
                    if (name == nullptr) return;
                    key += "." + name->getName().str();
                } else {
                    const Expr *index = one->getArrayIndex(*part);
                    Expr::EvalResult value;
                    if (index == nullptr || index->isValueDependent() || !index->EvaluateAsInt(value, context_)) return;
                    key += "[" + toString(value.Val.getInt(), 10) + "]";
                }
            }
            keys.push_back(key);
        }
        auto covers = [](const std::string &outer, const std::string &inner) {
            return inner.compare(0, outer.size(), outer) == 0 &&
                   (inner.size() == outer.size() || inner[outer.size()] == '.' || inner[outer.size()] == '[');
        };
        for (size_t later = 1; later < keys.size(); ++later) {
            for (size_t former = 0; former < later; ++former) {
                if (covers(keys[former], keys[later]) || covers(keys[later], keys[former])) {
                    pending_shape_[list->getInit(static_cast<unsigned>(later))] = "override";
                    break;
                }
            }
        }
    }

    // A written list or designator below this syntactic list.
    static bool has_designator(const InitListExpr *list) {
        for (const Expr *item : list->inits()) {
            if (item == nullptr) continue;
            if (isa<DesignatedInitExpr>(item)) return true;
            if (const auto *inner = dyn_cast<InitListExpr>(item->IgnoreImplicit())) {
                if (has_designator(inner)) return true;
            }
        }
        return false;
    }

    // The source character at a location (spelled), or 0.
    char char_at(SourceLocation loc) const {
        SourceLocation spelled = sm_.getSpellingLoc(loc);
        if (spelled.isInvalid() || sm_.isWrittenInScratchSpace(spelled)) return 0;
        bool invalid = false;
        const char *data = sm_.getCharacterData(spelled, &invalid);
        return invalid || data == nullptr ? 0 : *data;
    }

    static bool system_record(const SourceManager &sm, QualType type) {
        const auto *record = type.isNull() ? nullptr : type->getAsRecordDecl();
        if (record == nullptr) return false;
        SourceLocation spelled = sm.getSpellingLoc(record->getLocation());
        return spelled.isValid() && sm.isInSystemHeader(spelled);
    }

    // Brace elision (shape brace_elision on the first element of the first
    // implicit sub-list of each written list): a sub-aggregate of the semantic form whose list starts at no
    // written brace.  Only the outermost written list is judged; lists with
    // designators, the zero initializer and members of system-header classes
    // (std::array) are left out.
    void note_brace_elision(const InitListExpr *visited, const InitListExpr *written) {
        const InitListExpr *semantic = visited;
        if (semantic->isSyntacticForm() && semantic->getSemanticForm() != nullptr) semantic = semantic->getSemanticForm();
        auto parents = context_.getParents(*visited);
        if (parents.size() != 1 || parents[0].get<InitListExpr>() != nullptr || parents[0].get<DesignatedInitExpr>() != nullptr) return;
        if (zero_init(written) || has_designator(written)) return;
        elided_lists(semantic);
    }

    void elided_lists(const InitListExpr *semantic) {
        if (semantic->getType().isNull() || semantic->getType()->isDependentType()) return;
        bool system_parent = system_record(sm_, semantic->getType());
        bool reported = false;
        for (const Expr *item : semantic->inits()) {
            const auto *inner = dyn_cast_or_null<InitListExpr>(item == nullptr ? nullptr : item->IgnoreImplicit());
            if (inner == nullptr || inner->getNumInits() == 0) continue;
            QualType held = inner->getType();
            if (held.isNull() || !(held->isArrayType() || held->isRecordType())) continue;
            const RecordDecl *member = held->getAsRecordDecl();
            if (member != nullptr && member->isAnonymousStructOrUnion()) continue;
            if (char_at(inner->getLBraceLoc()) == '{' || system_parent) {
                elided_lists(inner);
                continue;
            }
            const Expr *first = inner->getInit(0);
            while (const auto *nested = dyn_cast_or_null<InitListExpr>(first == nullptr ? nullptr : first->IgnoreImplicit())) {
                if (nested->getNumInits() == 0) break;
                first = nested->getInit(0);
            }
            if (first == nullptr || isa<ImplicitValueInitExpr>(first) || first->getBeginLoc().isInvalid()) continue;
            if (reported) continue;
            add_pending(without_implicit_casts(first), "brace_elision");
            reported = true;
        }
    }

    // Arrays passed as arguments of a call to a function of the user's (not a
    // builtin, not declared in a system header): the decayed operand is marked.
    // String literals, __func__ (also chosen by ?:) and va_list do not count.
    void note_array_decay(const CallExpr *call) {
        if (isa<CXXOperatorCallExpr>(call) || isa<UserDefinedLiteral>(call)) return;
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getBuiltinID() != 0) return;
        for (const FunctionDecl *seen : callee->redecls()) {
            SourceLocation spelled = sm_.getSpellingLoc(seen->getLocation());
            if (spelled.isInvalid() || sm_.isInSystemHeader(spelled)) return;
        }
        for (const Expr *arg : call->arguments()) {
            if (arg == nullptr || isa<CXXDefaultArgExpr>(arg)) continue;
            const Expr *walk = arg;
            const ImplicitCastExpr *decay = nullptr;
            while (const auto *cast = dyn_cast<ImplicitCastExpr>(walk)) {
                if (cast->getCastKind() == CK_ArrayToPointerDecay) decay = cast;
                walk = cast->getSubExpr();
            }
            if (decay == nullptr) continue;
            if (text_literal(decay->getSubExpr())) continue;
            QualType held = decay->getSubExpr()->getType();
            if (context_.hasSameUnqualifiedType(held, context_.getBuiltinVaListType())) continue;
            pending_shape_[without_implicit_casts(decay->getSubExpr())] = "array_decay";
        }
    }

    // A string literal, __func__, or a choice between such (`ok ? "yes" : "no"`).
    static bool text_literal(const Expr *expr) {
        expr = expr->IgnoreParenImpCasts();
        if (isa<StringLiteral>(expr) || isa<PredefinedExpr>(expr)) return true;
        if (const auto *choice = dyn_cast<ConditionalOperator>(expr)) {
            return text_literal(choice->getTrueExpr()) && text_literal(choice->getFalseExpr());
        }
        return false;
    }

    // The variable compared directly by a comparison that is the whole loop
    // condition, if it has a real floating type.
    static const DeclRefExpr *compared_float(const Expr *condition) {
        if (condition == nullptr) return nullptr;
        const auto *compare = dyn_cast<BinaryOperator>(condition->IgnoreParenImpCasts());
        if (compare == nullptr || !compare->isComparisonOp()) return nullptr;
        for (const Expr *side : {compare->getLHS(), compare->getRHS()}) {
            const auto *ref = dyn_cast<DeclRefExpr>(side->IgnoreParenImpCasts());
            const auto *var = ref == nullptr ? nullptr : dyn_cast<VarDecl>(ref->getDecl());
            if (var != nullptr && !var->getType().isNull() && var->getType()->isRealFloatingType()) return ref;
        }
        return nullptr;
    }

    static bool names_var(const Stmt *stmt, const VarDecl *var) {
        if (stmt == nullptr) return false;
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) return ref->getDecl() == var;
        for (const Stmt *child : stmt->children()) {
            if (names_var(child, var)) return true;
        }
        return false;
    }

    static bool is_var(const Expr *expr, const VarDecl *var) {
        const auto *ref = dyn_cast<DeclRefExpr>(expr->IgnoreParenImpCasts());
        return ref != nullptr && ref->getDecl() == var;
    }

    // Is the variable stepped: ++, --, a compound assignment, or `v = ...v...`?
    // Lambdas inside do not count.
    static bool stepped(const Stmt *stmt, const VarDecl *var) {
        if (stmt == nullptr || isa<LambdaExpr>(stmt)) return false;
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->isIncrementDecrementOp() && is_var(unary->getSubExpr(), var)) return true;
        }
        if (const auto *compound = dyn_cast<CompoundAssignOperator>(stmt)) {
            if (is_var(compound->getLHS(), var)) return true;
        } else if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (binary->getOpcode() == BO_Assign && is_var(binary->getLHS(), var) && names_var(binary->getRHS(), var)) return true;
        }
        for (const Stmt *child : stmt->children()) {
            if (stepped(child, var)) return true;
        }
        return false;
    }

    // A loop whose condition compares a floating variable that the increment or
    // the body steps.  Declared in the for header: the declaration is marked,
    // otherwise the reference in the condition.
    void note_float_counter(const Stmt *stmt) {
        const Expr *condition = nullptr;
        const Stmt *step = nullptr;
        const Stmt *body = nullptr;
        const Stmt *init = nullptr;
        if (const auto *loop = dyn_cast<ForStmt>(stmt)) {
            condition = loop->getCond();
            step = loop->getInc();
            body = loop->getBody();
            init = loop->getInit();
        } else if (const auto *loop = dyn_cast<WhileStmt>(stmt)) {
            condition = loop->getCond();
            body = loop->getBody();
        } else if (const auto *loop = dyn_cast<DoStmt>(stmt)) {
            condition = loop->getCond();
            body = loop->getBody();
        } else {
            return;
        }
        const DeclRefExpr *ref = compared_float(condition);
        if (ref == nullptr) return;
        const auto *var = cast<VarDecl>(ref->getDecl());
        if (!stepped(step, var) && !stepped(body, var)) return;
        if (const auto *declared = dyn_cast_or_null<DeclStmt>(init)) {
            for (const Decl *item : declared->decls()) {
                if (item == var) {
                    decls[static_cast<size_t>(decl_of(var))].shape = "float_loop_counter";
                    return;
                }
            }
        }
        pending_shape_[ref] = "float_loop_counter";
    }

    // C library functions whose result may be ignored: the printf family that
    // writes to stdout or to memory (the catalogue reports fprintf) and the
    // functions that return their first argument.
    static bool ignorable_result(const FunctionDecl *callee, const SourceManager &sm) {
        if (callee->getIdentifier() == nullptr) return false;
        const DeclContext *owner = callee->getDeclContext()->getRedeclContext();
        if (!owner->isTranslationUnit() && !callee->isInStdNamespace()) return false;
        SourceLocation spelled = sm.getSpellingLoc(callee->getFirstDecl()->getLocation());
        if (spelled.isInvalid() || !sm.isInSystemHeader(spelled)) return false;
        static const char *const names[] = {
            "printf", "vprintf", "wprintf", "vwprintf", "sprintf", "snprintf", "vsprintf", "vsnprintf",
            "swprintf", "vswprintf",
            "memcpy", "memmove", "memset", "strcpy", "strncpy", "strcat", "strncat", "wmemcpy",
            "wmemmove", "wmemset", "wcscpy", "wcsncpy", "wcscat", "wcsncat"};
        StringRef name = callee->getName();
        for (const char *known : names) {
            if (name == known) return true;
        }
        return false;
    }

    // A call whose non-void result is thrown away: the call is an expression
    // statement (also as the body of if, while, for, case, a label, and the
    // first clause of a for header; not the third clause, not the last
    // statement of a GNU statement expression).  Overloaded operators, the
    // ignorable C library functions and builtins that are no library functions
    // are left out.
    bool unused_result(const CallExpr *call) {
        if (isa<CXXOperatorCallExpr>(call) || isa<UserDefinedLiteral>(call) || call->isTypeDependent()) return false;
        if (call->getType().isNull() || call->getType()->isVoidType()) return false;
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee != nullptr) {
            unsigned builtin = callee->getBuiltinID();
            if (builtin != 0 && !context_.BuiltinInfo.isLibFunction(builtin) && !context_.BuiltinInfo.isPredefinedLibFunction(builtin)) return false;
            if (ignorable_result(callee, sm_)) return false;
            // The C++ standard library: only what it marks [[nodiscard]] (empty(),
            // not erase()).
            SourceLocation declared = sm_.getSpellingLoc(callee->getFirstDecl()->getLocation());
            bool library = declared.isValid() && sm_.isInSystemHeader(declared);
            const DeclContext *owner = callee->getDeclContext();
            bool cxx_library = library && context_.getLangOpts().CPlusPlus &&
                               (callee->isInStdNamespace() || isa<CXXMethodDecl>(callee) ||
                                !owner->getRedeclContext()->isTranslationUnit());
            if (cxx_library && !callee->hasAttr<WarnUnusedResultAttr>() &&
                (!isa<CXXMethodDecl>(callee) || !cast<CXXMethodDecl>(callee)->getParent()->hasAttr<WarnUnusedResultAttr>())) {
                return false;
            }
        }
        DynTypedNode node = DynTypedNode::create(*static_cast<const Stmt *>(call));
        for (int depth = 0; depth < 8; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return false;
            const Stmt *parent = parents[0].get<Stmt>();
            const Stmt *self = node.get<Stmt>();
            if (parent == nullptr || self == nullptr) return false;
            if (isa<ParenExpr>(parent) || isa<ExprWithCleanups>(parent) || isa<CXXBindTemporaryExpr>(parent) ||
                isa<MaterializeTemporaryExpr>(parent) || isa<ConstantExpr>(parent)) {
                node = parents[0];
                continue;
            }
            if (const auto *block = dyn_cast<CompoundStmt>(parent)) {
                auto above = context_.getParents(*block);
                bool value_of_block = above.size() == 1 && above[0].get<StmtExpr>() != nullptr &&
                                      !block->body_empty() && block->body_back() == self;
                return !value_of_block;
            }
            if (const auto *branch = dyn_cast<IfStmt>(parent)) return branch->getThen() == self || branch->getElse() == self;
            if (const auto *loop = dyn_cast<WhileStmt>(parent)) return loop->getBody() == self;
            if (const auto *loop = dyn_cast<DoStmt>(parent)) return loop->getBody() == self;
            if (const auto *loop = dyn_cast<ForStmt>(parent)) return loop->getBody() == self || loop->getInit() == self;
            if (const auto *loop = dyn_cast<CXXForRangeStmt>(parent)) return loop->getBody() == self;
            if (const auto *label = dyn_cast<CaseStmt>(parent)) return label->getSubStmt() == self;
            if (const auto *label = dyn_cast<DefaultStmt>(parent)) return label->getSubStmt() == self;
            if (const auto *label = dyn_cast<LabelStmt>(parent)) return label->getSubStmt() == self;
            if (const auto *attributed = dyn_cast<AttributedStmt>(parent)) return attributed->getSubStmt() == self;
            return false;
        }
        return false;
    }

    static bool bit_operator(BinaryOperatorKind op) {
        return op == BO_Shl || op == BO_Shr || op == BO_And || op == BO_Or || op == BO_Xor;
    }

    // OC-0090: an operand of a binary operator that is itself a binary operator
    // written without parentheses, where && meets ||, or a shift or bitwise
    // operator meets a different binary operator (assignments and the comma
    // operator do not count).  The operand is marked mixed_precedence.
    void note_precedence(const BinaryOperator *outer) {
        BinaryOperatorKind op = outer->getOpcode();
        if (outer->isAssignmentOp() || op == BO_Comma || outer->isTypeDependent()) return;
        for (const Expr *side : {outer->getLHS(), outer->getRHS()}) {
            const auto *inner = dyn_cast<BinaryOperator>(side->IgnoreImpCasts());
            if (inner == nullptr || inner->isAssignmentOp() || inner->getOpcode() == BO_Comma) continue;
            BinaryOperatorKind sub = inner->getOpcode();
            if (sub == op) continue;
            bool logic = (op == BO_LAnd && sub == BO_LOr) || (op == BO_LOr && sub == BO_LAnd);
            bool bits = bit_operator(op) || bit_operator(sub);
            if (!logic && !bits) continue;
            // a macro argument or body may hold the operand: only report what
            // one file spells in one piece
            if (inner->getBeginLoc().isMacroID() != outer->getBeginLoc().isMacroID()) continue;
            add_pending(inner, "mixed_precedence");
        }
    }

    void add_pending(const Stmt *stmt, const char *shape) {
        std::string &pending = pending_shape_[stmt];
        pending = pending.empty() ? shape : pending + "," + shape;
    }

    // Shapes of one node are a comma-separated list.
    template <typename Rec> static void add_shape(Rec &rec, const std::string &shape) {
        if (shape.empty()) return;
        std::string list = "," + rec.shape + ",";
        if (list.find("," + shape + ",") != std::string::npos) return;
        rec.shape = rec.shape.empty() ? shape : rec.shape + "," + shape;
    }

    bool null_constant(const Expr *expr) {
        return expr->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull;
    }

    // A call of an allocator: malloc, calloc, realloc, a function marked with
    // the malloc or alloc_size attribute, or one whose name says alloc (the
    // project's wrappers).
    static bool allocation(const Expr *expr) {
        const auto *call = dyn_cast<CallExpr>(expr->IgnoreParenImpCasts());
        const FunctionDecl *callee = call == nullptr ? nullptr : call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return false;
        StringRef name = callee->getName();
        if (callee->hasAttr<AllocSizeAttr>() || callee->hasAttr<RestrictAttr>()) return true;
        // allocator wrappers (sqlite3_malloc, sqlite3DbMallocZero, k_malloc ...)
        return name.contains_insensitive("alloc");
    }

    // Does `part` lie at offset 0 of `whole`: its first member (any member of a
    // union), also nested?
    static bool leading_member(QualType whole, QualType part, int depth) {
        if (depth > 8) return false;
        const RecordDecl *record = whole->getAsRecordDecl();
        if (record == nullptr || record->getDefinition() == nullptr) return false;
        record = record->getDefinition();
        for (const FieldDecl *field : record->fields()) {
            QualType held = field->getType().getCanonicalType().getUnqualifiedType();
            if (held == part || leading_member(held, part, depth + 1)) return true;
            if (!record->isUnion()) break;
        }
        return false;
    }

    // Is the type, as written, the typedef uintptr_t?
    static bool written_uintptr(QualType type) {
        while (const auto *alias = type->getAs<TypedefType>()) {
            if (alias->getDecl()->getName() == "uintptr_t") return true;
            type = alias->desugar();
        }
        return false;
    }

    // What one explicit cast does with pointers (several shapes can apply).
    void cast_shapes(const ExplicitCastExpr *cast, std::vector<std::string> &shapes) {
        if (cast->isTypeDependent() || cast->isValueDependent() || cast->getSubExpr()->isTypeDependent()) return;
        const Expr *operand = cast->getSubExprAsWritten();
        CastKind kind = cast->getCastKind();
        if (kind == CK_ToVoid || kind == CK_PointerToBoolean || null_constant(operand)) return;
        bool cxx = context_.getLangOpts().CPlusPlus;
        QualType from = operand->getType();
        QualType written = cast->getTypeAsWritten();
        if (from.isNull() || written.isNull() || from->isDependentType() || written->isDependentType()) return;
        if (from->isFunctionType() || from->isArrayType()) from = context_.getDecayedType(from);
        if (written->isReferenceType()) {
            QualType target = written->getPointeeType();
            if ((from.isConstQualified() && !target.isConstQualified()) ||
                (from.isVolatileQualified() && !target.isVolatileQualified())) {
                shapes.push_back("drop_qualifier");
            }
            return;
        }
        bool fn_from = from->isFunctionPointerType();
        bool fn_to = written->isFunctionPointerType();
        if (fn_from || fn_to) {
            if (context_.getCanonicalType(from).getUnqualifiedType() != context_.getCanonicalType(written).getUnqualifiedType()) {
                shapes.push_back("function_pointer_cast");
            }
            return;
        }
        if (from->isPointerType() && written->isPointerType()) {
            QualType source = from->getPointeeType();
            QualType target = written->getPointeeType();
            if ((source.isConstQualified() && !target.isConstQualified()) ||
                (source.isVolatileQualified() && !target.isVolatileQualified())) {
                shapes.push_back("drop_qualifier");
            }
            QualType plain_source = context_.getCanonicalType(source).getUnqualifiedType();
            QualType plain_target = context_.getCanonicalType(target).getUnqualifiedType();
            if (plain_source == plain_target) return;
            if (plain_source->isVoidType() || plain_target->isVoidType()) {
                if (!cxx && plain_source->isVoidType() && !allocation(operand)) shapes.push_back("void_to_object");
                return;
            }
            bool incomplete = (plain_source->isIncompleteType() && plain_source->isRecordType()) ||
                              (plain_target->isIncompleteType() && plain_target->isRecordType());
            if (incomplete) {
                if (!cxx) shapes.push_back("incomplete_cast");
                return;
            }
            if (kind != CK_BitCast) return;
            if (plain_target->isCharType() || plain_target->isStdByteType()) return;
            if (leading_member(plain_source, plain_target, 0) || leading_member(plain_target, plain_source, 0)) return;
            shapes.push_back("object_cast");
            return;
        }
        if (kind == CK_PointerToIntegral) {
            if (from->getPointeeType()->isVoidType()) {
                if (!cxx) shapes.push_back("void_arithmetic");
            } else if (!(cxx && written_uintptr(written))) {
                shapes.push_back("pointer_integer");
            }
        } else if (kind == CK_IntegralToPointer) {
            if (written->getPointeeType()->isVoidType()) {
                if (!cxx) shapes.push_back("void_arithmetic");
            } else if (!(cxx && written_uintptr(operand->IgnoreParens()->getType()))) {
                shapes.push_back("pointer_integer");
            }
        }
    }

    // A chain of explicit casts, `(int)(long)pointer`, is reported at its
    // outermost cast: the casts inside add their shapes there.
    void note_pointer_casts(const ExplicitCastExpr *cast, NodeRec &rec) {
        auto parents = context_.getParents(*cast);
        while (parents.size() == 1 && parents[0].get<ParenExpr>() != nullptr) parents = context_.getParents(parents[0]);
        if (parents.size() == 1 && parents[0].get<ExplicitCastExpr>() != nullptr) return;
        std::vector<std::string> shapes;
        for (const ExplicitCastExpr *link = cast; link != nullptr;
             link = dyn_cast<ExplicitCastExpr>(link->getSubExprAsWritten()->IgnoreParens())) {
            cast_shapes(link, shapes);
        }
        for (const std::string &shape : shapes) add_shape(rec, shape);
    }

    // C: a string literal stored through a pointer to non-const char: assigned,
    // initialised (also in a braced list), passed (not to a function declared in a
    // system header) or returned.  The literal is marked literal_to_mutable.
    void note_literal_to_mutable(const ImplicitCastExpr *cast) {
        if (context_.getLangOpts().CPlusPlus || cast->getCastKind() != CK_ArrayToPointerDecay) return;
        const auto *literal = dyn_cast<StringLiteral>(cast->getSubExpr()->IgnoreParens());
        if (literal == nullptr) return;
        DynTypedNode node = DynTypedNode::create(*static_cast<const Stmt *>(cast));
        const Expr *top = cast;
        for (int depth = 0; depth < 8; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return;
            const auto *above = parents[0].get<ImplicitCastExpr>();
            if (above == nullptr) break;
            top = above;
            node = parents[0];
        }
        QualType held = top->getType();
        if (!held->isPointerType() || held->getPointeeType().isConstQualified()) return;
        auto parents = context_.getParents(node);
        if (parents.size() != 1) return;
        bool stored = false;
        if (const auto *binary = parents[0].get<BinaryOperator>()) {
            stored = binary->getOpcode() == BO_Assign && binary->getRHS() == top;
        } else if (parents[0].get<VarDecl>() != nullptr || parents[0].get<InitListExpr>() != nullptr ||
                   parents[0].get<ReturnStmt>() != nullptr) {
            stored = true;
        } else if (const auto *call = parents[0].get<CallExpr>()) {
            const FunctionDecl *callee = call->getDirectCallee();
            stored = callee != nullptr && call->getCallee() != top;
            if (stored) {
                SourceLocation declared = sm_.getSpellingLoc(callee->getFirstDecl()->getLocation());
                if (declared.isInvalid() || sm_.isInSystemHeader(declared)) stored = false;
            }
            // a variadic argument keeps char *: the callee's format decides
            if (stored) {
                unsigned index = 0;
                for (; index < call->getNumArgs() && call->getArg(index) != top; ++index) {}
                if (index >= callee->getNumParams()) stored = false;
            }
        }
        if (stored) add_pending(literal, "literal_to_mutable");
    }

    // C: an implicit conversion of a void pointer to an object pointer (not of a
    // null pointer constant, not of malloc, calloc or realloc); the operand is marked.
    void note_implicit_cast(const ImplicitCastExpr *cast) {
        note_literal_to_mutable(cast);
        note_literal_null(cast);
        if (!context_.getLangOpts().CPlusPlus && cast->getCastKind() == CK_ArrayToPointerDecay &&
            isa<MemberExpr>(cast->getSubExpr()->IgnoreParens()) && temporary_base(cast->getSubExpr())) {
            add_pending(cast->getSubExpr()->IgnoreParens(), "temporary_decay");
        }
        if (context_.getLangOpts().CPlusPlus || cast->getCastKind() != CK_BitCast || cast->isTypeDependent()) return;
        QualType from = cast->getSubExpr()->getType();
        QualType to = cast->getType();
        if (!from->isPointerType() || !to->isPointerType() || to->isFunctionPointerType()) return;
        if (!from->getPointeeType()->isVoidType() || to->getPointeeType()->isVoidType()) return;
        if (null_constant(cast->getSubExpr()) || allocation(cast->getSubExpr())) return;
        const Expr *operand = without_implicit_casts(cast);
        std::string &pending = pending_shape_[operand];
        pending = pending.empty() ? "void_to_object" : pending + ",void_to_object";
    }

    // std::move (one argument) of a const object or of a temporary.
    void note_move(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || call->getNumArgs() != 1 || callee->getIdentifier() == nullptr ||
            callee->getName() != "move" || !callee->isInStdNamespace()) {
            return;
        }
        const Expr *arg = call->getArg(0);
        if (arg->isTypeDependent()) return;
        const Expr *inner = without_implicit_casts(arg);
        if (isa<MaterializeTemporaryExpr>(inner)) {
            pending_shape_[inner] = "move_rvalue";
        } else if (arg->getType().isConstQualified()) {
            pending_shape_[inner] = "move_const";
        }
    }

    // A #if or #elif whose condition uses a macro that expands to `defined`
    // (MacroRec kind condition).
    bool expands_to_defined(Preprocessor &pp, const IdentifierInfo *name, int depth,
                            std::set<const IdentifierInfo *> &seen) const {
        if (name == nullptr || depth > 16 || !seen.insert(name).second) return false;
        const MacroInfo *info = pp.getMacroInfo(name);
        if (info == nullptr) return false;
        for (const Token &tok : info->tokens()) {
            const IdentifierInfo *word = tok.getIdentifierInfo();
            if (word == nullptr) continue;
            if (word->getName() == "defined") return true;
            if (expands_to_defined(pp, word, depth + 1, seen)) return true;
        }
        return false;
    }

    void note_condition(Preprocessor &pp, SourceLocation loc, SourceRange condition) {
        if (!user_file_loc(loc) || condition.isInvalid()) return;
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(sm_.getExpansionLoc(condition.getBegin()));
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(at.first, &invalid);
        if (invalid || at.second > buffer.size()) return;
        size_t end = at.second;
        while (end < buffer.size() && (buffer[end] != '\n' || (end > 0 && buffer[end - 1] == '\\'))) ++end;
        // The raw lexer needs a NUL after its end: lex a copy, placed at the condition.
        std::string text = buffer.substr(at.second, end - at.second).str();
        Lexer raw(sm_.getExpansionLoc(condition.getBegin()), pp.getLangOpts(), text.c_str(), text.c_str(),
                  text.c_str() + text.size());
        raw.SetCommentRetentionState(false);
        std::vector<Token> tokens;
        Token tok;
        while (true) {
            raw.LexFromRawLexer(tok);
            if (tok.is(tok::eof)) break;
            tokens.push_back(tok);
        }
        note_defined_macro(pp, tokens);
        note_undefined_identifier(pp, tokens);
        note_number_condition(pp, tokens);
    }

    // A macro used in the condition that expands to `defined` (kind condition).
    void note_defined_macro(Preprocessor &pp, const std::vector<Token> &tokens) {
        int after_defined = 0;
        for (const Token &tok : tokens) {
            if (tok.is(tok::raw_identifier)) {
                llvm::StringRef word = tok.getRawIdentifier();
                if (word == "defined") { after_defined = 2; continue; }
                if (after_defined == 0) {
                    std::set<const IdentifierInfo *> seen;
                    const IdentifierInfo *name = pp.getIdentifierInfo(word);
                    if (expands_to_defined(pp, name, 0, seen)) {
                        MacroRec rec = directive_rec("condition", pp, tok);
                        rec.name = word.str();
                        rec.loc = SourceLocation();
                        macros.push_back(rec);
                        return;
                    }
                }
                after_defined = 0;
                continue;
            }
            after_defined = tok.is(tok::l_paren) && after_defined == 2 ? 1 : 0;
        }
    }

    static bool operator_word(llvm::StringRef word, const LangOptions &lang) {
        if (lang.CPlusPlus || lang.C23) {
            if (word == "true" || word == "false") return true;
        }
        if (!lang.CPlusPlus) return false;
        static const char *const words[] = {"and", "and_eq", "bitand", "bitor", "compl", "not",
                                            "not_eq", "or", "or_eq", "xor", "xor_eq"};
        for (const char *known : words) {
            if (word == known) return true;
        }
        return false;
    }

    // An identifier of the condition that is no macro and not the operand of
    // `defined` (kind undefined_condition, the first one).  Conditions with &&, ||
    // or ?: are left alone: their short circuit may never evaluate it.  The
    // arguments of anything called (__has_include, __has_attribute, a
    // function-like macro) are skipped.
    void note_undefined_identifier(Preprocessor &pp, const std::vector<Token> &tokens) {
        const LangOptions &lang = pp.getLangOpts();
        for (const Token &tok : tokens) {
            if (tok.isOneOf(tok::ampamp, tok::pipepipe, tok::question)) return;
            if (tok.is(tok::raw_identifier)) {
                llvm::StringRef word = tok.getRawIdentifier();
                if (lang.CPlusPlus && (word == "and" || word == "or")) return;
            }
        }
        for (size_t idx = 0; idx < tokens.size(); ++idx) {
            const Token &tok = tokens[idx];
            if (!tok.is(tok::raw_identifier)) continue;
            llvm::StringRef word = tok.getRawIdentifier();
            if (word == "defined") {
                ++idx;
                if (idx < tokens.size() && tokens[idx].is(tok::l_paren)) ++idx;
                continue;
            }
            if (idx + 1 < tokens.size() && tokens[idx + 1].is(tok::l_paren)) {
                int depth = 0;
                for (++idx; idx < tokens.size(); ++idx) {
                    if (tokens[idx].is(tok::l_paren)) ++depth;
                    if (tokens[idx].is(tok::r_paren) && --depth == 0) break;
                }
                continue;
            }
            if (operator_word(word, lang) || pp.isMacroDefined(word)) continue;
            MacroRec rec = directive_rec("undefined_condition", pp, tok);
            rec.name = word.str();
            rec.loc = SourceLocation();
            macros.push_back(rec);
            return;
        }
    }

    // The value of an integer literal token, -1 if it is not one.
    static long long literal_value(llvm::StringRef text) {
        while (!text.empty() && std::strchr("uUlL", text.back()) != nullptr) text = text.drop_back();
        unsigned long long value = 0;
        if (text.empty() || text.getAsInteger(0, value) || value > (1ULL << 62)) return -1;
        return static_cast<long long>(value);
    }

    // A condition that is one integer literal, or one object-like macro whose
    // replacement is (through other such macros) one integer literal, with a
    // value other than 0 or 1 (kind number_condition, name the literal).
    void note_number_condition(Preprocessor &pp, const std::vector<Token> &tokens) {
        size_t first = 0;
        size_t last = tokens.size();
        while (last >= first + 3 && tokens[first].is(tok::l_paren) && tokens[last - 1].is(tok::r_paren)) {
            ++first;
            --last;
        }
        if (last != first + 1) return;
        const Token &only = tokens[first];
        std::string spelled;
        if (only.is(tok::numeric_constant)) {
            spelled = std::string(only.getLiteralData(), only.getLength());
        } else if (only.is(tok::raw_identifier)) {
            const IdentifierInfo *name = pp.getIdentifierInfo(only.getRawIdentifier());
            for (int depth = 0; depth < 8 && spelled.empty(); ++depth) {
                const MacroInfo *info = name == nullptr ? nullptr : pp.getMacroInfo(name);
                if (info == nullptr || info->isFunctionLike() || info->getNumTokens() != 1) return;
                const Token &body = info->getReplacementToken(0);
                if (body.is(tok::numeric_constant)) {
                    spelled = pp.getSpelling(body);
                } else if (body.is(tok::identifier)) {
                    name = body.getIdentifierInfo();
                } else {
                    return;
                }
            }
        } else {
            return;
        }
        long long value = literal_value(spelled);
        if (value < 0 || value == 0 || value == 1) return;
        MacroRec rec = directive_rec("number_condition", pp, only);
        rec.name = spelled;
        rec.loc = SourceLocation();
        macros.push_back(rec);
    }

    // Is `loc` (or a macro it comes from) the expansion of the macro errno?
    bool from_errno(SourceLocation loc) const { return from_macro_named(loc, "errno"); }

    bool from_macro_named(SourceLocation loc, llvm::StringRef name) const {
        while (loc.isMacroID()) {
            if (Lexer::getImmediateMacroName(loc, sm_, context_.getLangOpts()) == name) return true;
            loc = sm_.getImmediateMacroCallerLoc(loc);
        }
        return false;
    }

    // 0 or NULL in C++: the first implicit conversion above it (parentheses
    // skipped) tells a null pointer from an integer use (an integral conversion).
    const char *null_shape(const Expr *expr) {
        if (!context_.getLangOpts().CPlusPlus) return nullptr;
        bool null_macro = isa<GNUNullExpr>(expr) || from_macro_named(expr->getBeginLoc(), "NULL");
        const auto *literal = dyn_cast<IntegerLiteral>(expr);
        if (!null_macro && (literal == nullptr || literal->getValue() != 0)) return nullptr;
        DynTypedNode node = DynTypedNode::create(*expr);
        for (int depth = 0; depth < 8; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return nullptr;
            node = parents[0];
            if (node.get<ParenExpr>() != nullptr) continue;
            if (const auto *cast = node.get<ImplicitCastExpr>()) {
                if (cast->getCastKind() == CK_NullToPointer || cast->getCastKind() == CK_NullToMemberPointer) {
                    return "null_pointer";
                }
                CastKind kind = cast->getCastKind();
                bool integral = kind == CK_IntegralCast || kind == CK_IntegralToBoolean ||
                                kind == CK_IntegralToFloating;
                return null_macro && integral ? "null_integer" : nullptr;
            }
            break;
        }
        // No conversion: an integer of NULL's own type, or a dependent use in a
        // template whose type is not known; neither is reported.
        return nullptr;
    }

    // The class a pointer or reference type refers to.
    static const CXXRecordDecl *pointed_class(QualType type) {
        if (type.isNull() || type->isDependentType()) return nullptr;
        if (type->isPointerType()) type = type->getPointeeType();
        else type = type.getNonReferenceType();
        const CXXRecordDecl *record = type->getAsCXXRecordDecl();
        return record != nullptr && record->hasDefinition() ? record->getDefinition() : nullptr;
    }

    // Downcasts (0253) and casts from a virtual base (0252).
    void note_cast(const ExplicitCastExpr *cast, NodeRec &rec) {
        if (cast->isTypeDependent() || cast->getSubExpr()->isTypeDependent()) return;
        const Expr *operand = cast->getSubExprAsWritten();
        QualType from = operand->getType();
        QualType to = cast->getTypeAsWritten();
        if (to->isPointerType() != from->isPointerType() && !to->isReferenceType()) return;
        const CXXRecordDecl *base = pointed_class(from);
        const CXXRecordDecl *derived = pointed_class(to);
        if (base == nullptr || derived == nullptr || base == derived || !derived->isDerivedFrom(base)) return;
        bool reinterpret = isa<CXXReinterpretCastExpr>(cast);
        if (reinterpret || isa<CStyleCastExpr>(cast)) {
            if (derived->isVirtuallyDerivedFrom(base)) {
                pending_shape_[without_implicit_casts(operand)] = "virtual_base_cast";
                return;
            }
        }
        if (!reinterpret && base->isPolymorphic() &&
            (isa<CXXStaticCastExpr>(cast) || isa<CXXDynamicCastExpr>(cast) || isa<CStyleCastExpr>(cast))) {
            rec.shape = "polymorphic_downcast";
        }
    }

    // == or != with the address of a virtual member function on one side.
    void note_member_compare(const BinaryOperator *binary, NodeRec &rec) {
        if (!binary->isEqualityOp() || binary->isTypeDependent()) return;
        auto virtual_address = [](const Expr *side) {
            const auto *address = dyn_cast<UnaryOperator>(side->IgnoreParenImpCasts());
            if (address == nullptr || address->getOpcode() != UO_AddrOf) return false;
            const auto *ref = dyn_cast<DeclRefExpr>(address->getSubExpr()->IgnoreParens());
            const auto *method = ref != nullptr ? dyn_cast<CXXMethodDecl>(ref->getDecl()) : nullptr;
            return method != nullptr && method->isVirtual();
        };
        auto null_constant = [this](const Expr *side) {
            return !side->isValueDependent() &&
                   side->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull;
        };
        const Expr *left = binary->getLHS();
        const Expr *right = binary->getRHS();
        if ((virtual_address(left) && !null_constant(right)) || (virtual_address(right) && !null_constant(left))) {
            rec.shape = "virtual_member_compare";
        }
    }

    bool is_errno(const Expr *expr) const {
        return expr != nullptr && from_errno(expr->IgnoreParenImpCasts()->getBeginLoc()) &&
               from_errno(expr->getBeginLoc());
    }

    // The writes of errno (NodeRec.errno_write); variables saved from errno are kept
    // so that restoring them is no finding.
    bool errno_write(const Stmt *stmt) {
        if (const auto *declared = dyn_cast<DeclStmt>(stmt)) {
            for (const Decl *one : declared->decls()) {
                const auto *var = dyn_cast<VarDecl>(one);
                if (var != nullptr && var->getInit() != nullptr && is_errno(var->getInit())) errno_saved_.insert(var);
            }
            return false;
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            return unary->isIncrementDecrementOp() && is_errno(unary->getSubExpr());
        }
        const auto *binary = dyn_cast<BinaryOperator>(stmt);
        if (binary == nullptr || !binary->isAssignmentOp()) return false;
        const Expr *value = binary->getRHS()->IgnoreParenImpCasts();
        if (!is_errno(binary->getLHS())) {
            const auto *target = dyn_cast<DeclRefExpr>(binary->getLHS()->IgnoreParenImpCasts());
            if (binary->getOpcode() == BO_Assign && target != nullptr && is_errno(binary->getRHS())) {
                if (const auto *var = dyn_cast<VarDecl>(target->getDecl())) errno_saved_.insert(var);
            }
            return false;
        }
        if (binary->isCompoundAssignmentOp()) return true;
        if (value->isValueDependent() || is_errno(binary->getRHS())) return false;
        Expr::EvalResult result;
        if (value->EvaluateAsInt(result, context_) && result.Val.getInt() == 0) return false;
        const auto *source = dyn_cast<DeclRefExpr>(value);
        if (source != nullptr) {
            const auto *var = dyn_cast<VarDecl>(source->getDecl());
            if (var != nullptr && errno_saved_.count(var) > 0) return false;
        }
        return true;
    }

    // The class type a handler catches, or the class a caught pointer points to.
    static const CXXRecordDecl *caught_class(QualType type, bool &pointer) {
        if (type.isNull() || type->isDependentType()) return nullptr;
        type = type.getNonReferenceType();
        pointer = type->isPointerType();
        if (pointer) type = type->getPointeeType();
        const CXXRecordDecl *record = type->getAsCXXRecordDecl();
        return record != nullptr && record->hasDefinition() ? record->getDefinition() : nullptr;
    }

    // The handlers of a try block that a former handler of a public, unambiguous
    // base class makes unreachable.
    void note_shadowed_handlers(const CXXTryStmt *attempt) {
        for (unsigned later = 1; later < attempt->getNumHandlers(); ++later) {
            bool later_pointer = false;
            const CXXRecordDecl *derived = caught_class(attempt->getHandler(later)->getCaughtType(), later_pointer);
            if (derived == nullptr) continue;
            for (unsigned former = 0; former < later; ++former) {
                bool former_pointer = false;
                const CXXRecordDecl *base = caught_class(attempt->getHandler(former)->getCaughtType(), former_pointer);
                if (base == nullptr || former_pointer != later_pointer || base == derived) continue;
                CXXBasePaths paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true, /*DetectVirtual=*/false);
                if (!derived->isDerivedFrom(base, paths)) continue;
#if CLANG_VERSION_MAJOR >= 22
                CanQualType base_type = context_.getCanonicalTagType(base);
#else
                CanQualType base_type = context_.getCanonicalType(context_.getRecordType(base));
#endif
                if (paths.isAmbiguous(base_type)) continue;
                bool open = false;
                for (const CXXBasePath &path : paths) open = open || path.Access == AS_public;
                if (!open) continue;
                shadowed_handlers_.insert(attempt->getHandler(later));
                break;
            }
        }
    }

    // Where the class name of a caught type is written.
    SourceLocation caught_name_loc(const VarDecl *caught) const {
        const TypeSourceInfo *written = caught->getTypeSourceInfo();
        if (written == nullptr) return SourceLocation();
        TypeLoc loc = written->getTypeLoc();
        while (true) {
            if (auto qualified = loc.getAs<QualifiedTypeLoc>()) { loc = qualified.getUnqualifiedLoc(); continue; }
            if (auto reference = loc.getAs<ReferenceTypeLoc>()) { loc = reference.getPointeeLoc(); continue; }
            if (auto pointer = loc.getAs<PointerTypeLoc>()) { loc = pointer.getPointeeLoc(); continue; }
#if CLANG_VERSION_MAJOR < 22
            if (auto elaborated = loc.getAs<ElaboratedTypeLoc>()) { loc = elaborated.getNamedTypeLoc(); continue; }
#endif
            break;
        }
        if (auto tag = loc.getAs<TagTypeLoc>()) return tag.getNameLoc();
        if (auto special = loc.getAs<TemplateSpecializationTypeLoc>()) return special.getTemplateNameLoc();
        return loc.getBeginLoc();
    }

    // The scope a #define or #undef sits in: the innermost function body, class or
    // namespace whose braces enclose it (expansion positions, one file).
    void note_scope(const Decl *decl) {
        SourceRange range;
        const char *kind = nullptr;
        if (decl->isImplicit()) return;
        if (const auto *space = dyn_cast<NamespaceDecl>(decl)) {
            range = SourceRange(space->getBeginLoc(), space->getRBraceLoc());
            kind = "namespace";
        } else if (const auto *record = dyn_cast<RecordDecl>(decl)) {
            if (!record->isThisDeclarationADefinition()) return;
            range = record->getBraceRange();
            kind = "class";
        } else if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            if (!fn->doesThisDeclarationHaveABody() || fn->getBody() == nullptr) return;
            range = fn->getBody()->getSourceRange();
            kind = "function";
        } else {
            return;
        }
        SourceLocation first = sm_.getExpansionLoc(range.getBegin());
        SourceLocation last = sm_.getExpansionLoc(range.getEnd());
        if (first.isInvalid() || last.isInvalid()) return;
        std::pair<FileID, unsigned> from = sm_.getDecomposedLoc(first);
        std::pair<FileID, unsigned> upto = sm_.getDecomposedLoc(last);
        if (from.first != upto.first || upto.second <= from.second) return;
        scopes_.push_back({from.first, from.second, upto.second, kind});
    }

    void finish_scopes() {
        for (MacroRec &rec : macros) {
            if ((rec.kind != "define" && rec.kind != "undef") || rec.loc.isInvalid()) continue;
            std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(sm_.getExpansionLoc(rec.loc));
            unsigned best = ~0u;
            for (const ScopeRange &scope : scopes_) {
                if (scope.fid != at.first || at.second <= scope.begin || at.second >= scope.end) continue;
                if (scope.end - scope.begin < best) {
                    best = scope.end - scope.begin;
                    rec.scope = scope.kind;
                }
            }
        }
    }

    // The alignas specifiers written on the declaration, in source order.
    void note_alignment(const NamedDecl *decl, DeclRec &rec) {
        std::vector<const AlignedAttr *> written;
        for (const auto *attr : decl->specific_attrs<AlignedAttr>()) {
            if (attr->isInherited() || attr->isImplicit() || !attr->isAlignas()) continue;
            written.push_back(attr);
        }
        std::sort(written.begin(), written.end(), [this](const AlignedAttr *left, const AlignedAttr *right) {
            return sm_.isBeforeInTranslationUnit(left->getLocation(), right->getLocation());
        });
        rec.align_count = static_cast<int>(written.size());
        if (written.size() > 1) rec.second_align_pos = pos_of(sm_.getExpansionLoc(written[1]->getLocation()));
        for (const AlignedAttr *attr : written) {
            if (!attr->isAlignmentExpr() || attr->getAlignmentExpr() == nullptr) continue;
            const Expr *value = attr->getAlignmentExpr();
            if (value->isValueDependent()) continue;
            std::optional<llvm::APSInt> known = value->getIntegerConstantExpr(context_);
            if (known && known->isZero()) {
                rec.zero_align_pos = pos_of(sm_.getExpansionLoc(value->getBeginLoc()));
                break;
            }
        }
    }

    // Where a function definition starts and ends, and the tokens of its body as
    // written (the source text between the braces, macros not expanded).
    void measure_body(const FunctionDecl *fn, DeclRec &rec) {
        SourceLocation begin = fn->getOuterLocStart();
        if (const FunctionTemplateDecl *pattern = fn->getDescribedFunctionTemplate()) {
            if (pattern->getBeginLoc().isValid()) begin = pattern->getBeginLoc();
        }
        const Stmt *body = fn->getBody();
        SourceLocation open = sm_.getExpansionLoc(body->getBeginLoc());
        SourceLocation close = sm_.getExpansionRange(body->getEndLoc()).getEnd();
        rec.def_begin = pos_of(sm_.getExpansionLoc(begin));
        rec.body_end = pos_of(close);
        if (open.isInvalid() || close.isInvalid()) return;
        std::pair<FileID, unsigned> first = sm_.getDecomposedLoc(open);
        std::pair<FileID, unsigned> last = sm_.getDecomposedLoc(close);
        if (first.first != last.first || first.second > last.second) return;
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(first.first, &invalid);
        if (invalid) return;
        Lexer lexer(sm_.getLocForStartOfFile(first.first), context_.getLangOpts(), buffer.begin(),
                    buffer.begin() + first.second, buffer.end());
        int count = 0;
        Token tok;
        while (true) {
            lexer.LexFromRawLexer(tok);
            if (tok.is(tok::eof)) break;
            if (sm_.getFileOffset(tok.getLocation()) > last.second) break;
            ++count;
        }
        rec.body_tokens = count;
    }

    // A definition that every translation unit including it would repeat: external
    // linkage, not inline, not a template or inside one, not deleted or defaulted,
    // not weak, not a C tentative definition, not a template specialization.
    static bool odr_definition(const NamedDecl *decl) {
        if (decl->isTemplated() || decl->hasAttr<WeakAttr>() || decl->hasAttr<SelectAnyAttr>()) return false;
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            if (!fn->isThisDeclarationADefinition() || fn->isInlined() || fn->isDeleted() || fn->isDefaulted()) return false;
            if (fn->getTemplateSpecializationKind() != TSK_Undeclared || isa<CXXDeductionGuideDecl>(fn)) return false;
            if (fn->hasAttr<GNUInlineAttr>()) return false;
            return fn->hasExternalFormalLinkage();
        }
        if (const auto *var = dyn_cast<VarDecl>(decl)) {
            if (isa<ParmVarDecl>(var) || var->isLocalVarDeclOrParm()) return false;
            if (var->isThisDeclarationADefinition() != VarDecl::Definition) return false;
            if (var->isInline() || var->isConstexpr()) return false;
            if (var->getTemplateSpecializationKind() != TSK_Undeclared) return false;
            return var->hasExternalFormalLinkage();
        }
        return false;
    }

    // `T (name);` in a function: parsed as a declaration of `name`, although it
    // reads as a conversion of `name` to T.
    static bool paren_declarator(const VarDecl *var) {
        if (isa<ParmVarDecl>(var) || !var->isLocalVarDecl()) return false;
        TypeSourceInfo *info = var->getTypeSourceInfo();
        if (info == nullptr) return false;
        TypeLoc loc = info->getTypeLoc();
        if (auto qualified = loc.getAs<QualifiedTypeLoc>()) loc = qualified.getUnqualifiedLoc();
        auto paren = loc.getAs<ParenTypeLoc>();
        if (!paren) return false;
        TypeLoc inner = paren.getInnerLoc();
        if (auto qualified = inner.getAs<QualifiedTypeLoc>()) inner = qualified.getUnqualifiedLoc();
        if (inner.getAs<PointerTypeLoc>() || inner.getAs<ReferenceTypeLoc>() || inner.getAs<MemberPointerTypeLoc>() ||
            inner.getAs<ParenTypeLoc>() || inner.getAs<ArrayTypeLoc>() || inner.getAs<FunctionTypeLoc>() ||
            inner.getAs<BlockPointerTypeLoc>()) {
            return false;
        }
        const Expr *init = var->getInit();
        if (init == nullptr) return true;
        const auto *construct = dyn_cast<CXXConstructExpr>(init);
        return construct != nullptr && construct->getNumArgs() == 0 && construct->getParenOrBraceRange().isInvalid();
    }

    // The sub-statements of a statement, by the node they will get: an implicit
    // cast is not recorded, so its operand stands for it.
    void note_roles(const Stmt *stmt) {
        auto mark = [&](const Stmt *body, const char *role) {
            if (body == nullptr) return;
            if (const auto *cast = dyn_cast<ImplicitCastExpr>(body)) body = cast->IgnoreImpCasts();
            roles_[body] = role;
        };
        if (const auto *branch = dyn_cast<IfStmt>(stmt)) {
            mark(branch->getThen(), "then");
            mark(branch->getElse(), "else");
        } else if (const auto *loop = dyn_cast<WhileStmt>(stmt)) {
            mark(loop->getBody(), "body");
        } else if (const auto *loop = dyn_cast<DoStmt>(stmt)) {
            mark(loop->getBody(), "body");
        } else if (const auto *loop = dyn_cast<ForStmt>(stmt)) {
            mark(loop->getBody(), "body");
            mark(loop->getInit(), "init");
            mark(loop->getInc(), "step");
        } else if (const auto *loop = dyn_cast<CXXForRangeStmt>(stmt)) {
            mark(loop->getBody(), "body");
            mark(loop->getInit(), "init");
        } else if (const auto *choice = dyn_cast<SwitchStmt>(stmt)) {
            mark(choice->getBody(), "body");
            mark(choice->getInit(), "init");
        }
        if (const auto *branch = dyn_cast<IfStmt>(stmt)) mark(branch->getInit(), "init");
    }

    // The first backslash-newline after `from` on the same logical line.
    Pos first_splice(SourceLocation from) {
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(from);
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(at.first, &invalid);
        if (invalid) return Pos();
        for (unsigned offset = at.second; offset < buffer.size(); ++offset) {
            char ch = buffer[offset];
            if (ch == '\n') break;
            if (ch != '\\') continue;
            unsigned next = offset + 1;
            while (next < buffer.size() && (buffer[next] == ' ' || buffer[next] == '\t' || buffer[next] == '\r')) ++next;
            if (next < buffer.size() && buffer[next] == '\n') return pos_of(sm_.getComposedLoc(at.first, offset));
        }
        return Pos();
    }

    // A literal as written: the characters from its first to its last token in the
    // file that spells them; "" when they are not in one file (a token made by ##,
    // pieces of a string from different macros).
    std::string literal_spelling(const Stmt *stmt) const {
        SourceLocation begin = sm_.getSpellingLoc(stmt->getBeginLoc());
        SourceLocation end = sm_.getSpellingLoc(stmt->getEndLoc());
        if (begin.isInvalid() || end.isInvalid() || sm_.isWrittenInScratchSpace(begin)) return "";
        if (sm_.getFileID(begin) != sm_.getFileID(end)) return "";
        unsigned first = sm_.getFileOffset(begin);
        unsigned last = sm_.getFileOffset(end) + Lexer::MeasureTokenLength(end, sm_, context_.getLangOpts());
        if (last < first || last - first > 4096) return "";
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(sm_.getFileID(begin), &invalid);
        if (invalid || last > buffer.size()) return "";
        return buffer.slice(first, last).str();
    }

    // The character before `loc` on its line (spaces skipped), "" at the line start.
    std::string char_before(SourceLocation loc) const {
        SourceLocation spelled = sm_.getSpellingLoc(loc);
        if (spelled.isInvalid() || sm_.isWrittenInScratchSpace(spelled)) return "";
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(spelled);
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(at.first, &invalid);
        if (invalid) return "";
        for (unsigned back = std::min<unsigned>(at.second, static_cast<unsigned>(buffer.size())); back > 0; --back) {
            char ch = buffer[back - 1];
            if (ch == '\n') return "";
            if (ch != ' ' && ch != '\t' && ch != '\r') return std::string(1, ch);
        }
        return "";
    }

    // The leftmost token of the declarator: the first *, &, ( or the name.
    SourceLocation declarator_start(const DeclaratorDecl *decl) const {
        SourceLocation start = decl->getLocation();
        TypeSourceInfo *info = decl->getTypeSourceInfo();
        if (info == nullptr || start.isInvalid()) return start;
        auto earlier = [&](SourceLocation loc) {
            if (loc.isValid() && loc.isFileID() == start.isFileID() &&
                sm_.isBeforeInTranslationUnit(loc, start) &&
                sm_.isBeforeInTranslationUnit(decl->getBeginLoc(), loc)) {
                start = loc;
            }
        };
        TypeLoc loc = info->getTypeLoc();
        for (int depth = 0; depth < 32 && !loc.isNull(); ++depth) {
            if (auto qualified = loc.getAs<QualifiedTypeLoc>()) {
                loc = qualified.getUnqualifiedLoc();
            } else if (auto pointer = loc.getAs<PointerTypeLoc>()) {
                earlier(pointer.getStarLoc());
                loc = pointer.getPointeeLoc();
            } else if (auto reference = loc.getAs<ReferenceTypeLoc>()) {
                earlier(reference.getSigilLoc());
                loc = reference.getPointeeLoc();
            } else if (auto member = loc.getAs<MemberPointerTypeLoc>()) {
                earlier(member.getStarLoc());
                loc = member.getPointeeLoc();
            } else if (auto paren = loc.getAs<ParenTypeLoc>()) {
                earlier(paren.getLParenLoc());
                loc = paren.getInnerLoc();
            } else if (auto array = loc.getAs<ArrayTypeLoc>()) {
                loc = array.getElementLoc();
            } else if (auto function = loc.getAs<FunctionTypeLoc>()) {
                loc = function.getReturnLoc();
            } else {
                break;
            }
        }
        return start;
    }

    // The first `restrict` (or __restrict, __restrict__) written in the declaration,
    // up to a function body or an initializer.  Spelled through a macro: the place
    // of the macro; not found: the name.
    Pos restrict_token(const NamedDecl *decl) {
        SourceLocation first = sm_.getExpansionLoc(decl->getBeginLoc());
        SourceLocation last = sm_.getExpansionLoc(decl->getEndLoc());
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            if (const Stmt *body = fn->getBody()) last = sm_.getExpansionLoc(body->getBeginLoc());
        } else if (const auto *var = dyn_cast<VarDecl>(decl)) {
            if (const Expr *init = var->getInit()) last = sm_.getExpansionLoc(init->getBeginLoc());
        }
        Pos fallback = pos_of(sm_.getExpansionLoc(decl->getLocation()));
        if (first.isInvalid() || last.isInvalid() || sm_.getFileID(first) != sm_.getFileID(last)) return fallback;
        FileID file = sm_.getFileID(first);
        unsigned upto = sm_.getFileOffset(last);
        llvm::StringRef buffer = sm_.getBufferData(file);
        Lexer lexer(sm_.getLocForStartOfFile(file), context_.getLangOpts(), buffer.begin(),
                    sm_.getCharacterData(first), buffer.end());
        Token tok;
        for (;;) {
            lexer.LexFromRawLexer(tok);
            if (tok.is(tok::eof) || sm_.getFileOffset(tok.getLocation()) > upto) break;
            if (tok.is(tok::raw_identifier)) {
                llvm::StringRef word = tok.getRawIdentifier();
                if (word == "restrict" || word == "__restrict" || word == "__restrict__") {
                    return pos_of(tok.getLocation());
                }
            }
        }
        return fallback;
    }

    // Is the declaration inside a template instantiation: an enclosing function
    // or class that is an instantiation (not the template itself)?
    static bool inside_instantiation(const Decl *decl) {
        for (const DeclContext *ctx = decl->getDeclContext(); ctx != nullptr; ctx = ctx->getParent()) {
            if (const auto *fn = dyn_cast<FunctionDecl>(ctx)) {
                if (fn->isTemplateInstantiation()) return true;
            } else if (const auto *record = dyn_cast<CXXRecordDecl>(ctx)) {
                TemplateSpecializationKind kind = record->getTemplateSpecializationKind();
                if (kind == TSK_ImplicitInstantiation || kind == TSK_ExplicitInstantiationDefinition ||
                    kind == TSK_ExplicitInstantiationDeclaration) return true;
            }
        }
        return false;
    }

    // Is the declared array bound written as a plain integer literal (not a
    // macro, not an expression)?
    static bool literal_array_bound(const DeclaratorDecl *decl) {
        TypeSourceInfo *info = decl->getTypeSourceInfo();
        if (info == nullptr) return false;
        TypeLoc loc = info->getTypeLoc();
        if (auto qualified = loc.getAs<QualifiedTypeLoc>()) loc = qualified.getUnqualifiedLoc();
        auto array = loc.getAs<ConstantArrayTypeLoc>();
        if (!array) return false;
        const Expr *size = array.getSizeExpr();
        if (size == nullptr) return false;
        size = size->IgnoreParenImpCasts();
        return isa<IntegerLiteral>(size) && !size->getBeginLoc().isMacroID();
    }

    // Does the function holding this static local modify it, or let its address
    // escape (Clang's ExprMutationAnalyzer, conservative)?
    bool mutated_in_function(const VarDecl *var) {
        const auto *fn = dyn_cast_or_null<FunctionDecl>(var->getParentFunctionOrMethod());
        if (fn == nullptr || fn->getBody() == nullptr) {
            return true;
        }
        ExprMutationAnalyzer analyzer(*fn->getBody(), context_);
        return analyzer.isMutated(var);
    }

    static std::string storage_name(StorageClass storage) {
        switch (storage) {
        case SC_Static: return "static";
        case SC_Extern: return "extern";
        case SC_Register: return "register";
        case SC_Auto: return "auto";
        default: return "";
        }
    }

    // Is `restrict` written anywhere in this type: on it, a pointee, an array
    // element, a parameter or the result of a function type?
    static bool has_restrict(QualType type) {
        for (int depth = 0; depth < 16 && !type.isNull(); ++depth) {
            if (type.isRestrictQualified()) {
                return true;
            }
            const Type *ptr = type.getTypePtr();
            if (const auto *pointer = dyn_cast<PointerType>(ptr)) {
                type = pointer->getPointeeType();
            } else if (const auto *array = dyn_cast<ArrayType>(ptr)) {
                type = array->getElementType();
            } else if (const auto *proto = dyn_cast<FunctionProtoType>(ptr)) {
                for (QualType param : proto->getParamTypes()) {
                    if (has_restrict(param)) return true;
                }
                type = proto->getReturnType();
            } else if (const auto *paren = dyn_cast<ParenType>(ptr)) {
                type = paren->getInnerType();
            } else {
                return false;
            }
        }
        return false;
    }

    // A range the preprocessor skipped (#if not taken), kept by file as start
    // offsets; sorted by finish_files.
    void note_skipped(SourceRange range) {
        SourceLocation start = range.getBegin();
        if (start.isInvalid() || !start.isFileID()) return;
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(start);
        unsigned length = sm_.getFileOffset(range.getEnd()) >= at.second
                              ? sm_.getFileOffset(range.getEnd()) - at.second : 0;
        skipped_[at.first].push_back({at.second, at.second + length});
    }

    // Is the offset inside a skipped range of the file?
    bool offset_skipped(FileID fid, unsigned offset) const {
        auto known = skipped_.find(fid);
        if (known == skipped_.end()) return false;
        auto next = std::upper_bound(known->second.begin(), known->second.end(),
                                     std::make_pair(offset, ~0u));
        if (next == known->second.begin()) return false;
        --next;
        return offset >= next->first && offset <= next->second;
    }

    // Does the preprocessor skip any code between the two ends of `range`?
    bool skips_inside(SourceRange range) const {
        SourceLocation first = sm_.getExpansionLoc(range.getBegin());
        SourceLocation last = sm_.getExpansionLoc(range.getEnd());
        if (first.isInvalid() || last.isInvalid()) return false;
        std::pair<FileID, unsigned> from = sm_.getDecomposedLoc(first);
        std::pair<FileID, unsigned> upto = sm_.getDecomposedLoc(last);
        if (from.first != upto.first) return false;
        auto known = skipped_.find(from.first);
        if (known == skipped_.end()) return false;
        auto next = std::lower_bound(known->second.begin(), known->second.end(),
                                     std::make_pair(from.second, 0u));
        return next != known->second.end() && next->first <= upto.second;
    }

    // A file the preprocessor enters, so that headers with nothing else recorded
    // are known too.
    void note_entered(SourceLocation loc) {
        if (loc.isValid() && loc.isFileID()) {
            file_of(loc);
            note_include(loc);
        }
    }

    // An #include written by the user: name and body hold the header name as
    // written (`setjmp.h`, `<setjmp.h>`), pos the #, name_pos the < or ", defined_file_uid the file
    // entered.  A header name made by a macro is not recorded.
    void note_include(SourceLocation entered) {
        SourceLocation include = sm_.getIncludeLoc(sm_.getFileID(entered));
        if (include.isInvalid()) return;
        include = sm_.getExpansionLoc(include);
        if (!user_file_loc(include)) return;
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(include);
        llvm::StringRef buffer = sm_.getBufferData(at.first);
        unsigned start = std::min<unsigned>(at.second, static_cast<unsigned>(buffer.size()));
        while (start > 0 && buffer[start - 1] != '\n') --start;
        unsigned end = start;
        while (end < buffer.size() && buffer[end] != '\n') ++end;
        llvm::StringRef line = buffer.slice(start, end);
        size_t hash = line.find_first_not_of(" \t");
        if (hash == llvm::StringRef::npos || line[hash] != '#') return;
        llvm::StringRef rest = line.substr(hash + 1).ltrim(" \t");
        if (!rest.consume_front("include") && !rest.consume_front("import")) return;
        rest.consume_front("_next");
        rest = rest.ltrim(" \t");
        if (rest.empty() || (rest[0] != '<' && rest[0] != '"')) return;
        size_t stop = rest.find(rest[0] == '<' ? '>' : '"', 1);
        if (stop == llvm::StringRef::npos) return;
        MacroRec rec;
        rec.uid = static_cast<int>(macros.size());
        rec.kind = "include";
        rec.name = rest.substr(1, stop - 1).str();
        rec.body = rest.substr(0, stop + 1).str();
        rec.pos = pos_of(sm_.getComposedLoc(at.first, start + static_cast<unsigned>(hash)));
        rec.name_pos = pos_of(sm_.getComposedLoc(at.first, static_cast<unsigned>(rest.data() - buffer.data())));
        rec.defined_file_uid = file_of(entered);
        macros.push_back(rec);
    }

    // A macro of a system header expanded in the user's code: pos is where its name
    // is spelled (in a macro body of the user: that body), name_pos the expansion in
    // the file.  Macros of the user are not recorded; there are too many.
    void note_expansion(Preprocessor &pp, const Token &name, const MacroDefinition &definition,
                        const MacroArgs *args) {
        const MacroInfo *info = definition.getMacroInfo();
        if (info == nullptr) return;
        SourceLocation defined = info->getDefinitionLoc();
        if (defined.isInvalid() || !defined.isFileID() || !sm_.isInSystemHeader(defined)) return;
        SourceLocation spelled = sm_.getSpellingLoc(name.getLocation());
        if (!user_file_loc(spelled)) return;
        MacroRec rec;
        rec.uid = static_cast<int>(macros.size());
        rec.kind = "expansion";
        rec.name = pp.getSpelling(name);
        rec.pos = pos_of(spelled);
        rec.name_pos = pos_of(sm_.getExpansionLoc(name.getLocation()));
        rec.function_like = info->isFunctionLike();
        rec.defined_file_uid = file_of(defined);
        static const llvm::Regex constant_macro("^U?INT(8|16|32|64|MAX)_C$");
        if (args != nullptr && info->getNumParams() == 1 && args->getNumMacroArguments() == 1 && constant_macro.match(rec.name)) {
            note_constant_macro(pp, rec.name, args);
        }
        if (args != nullptr && info->getNumParams() == 1 && args->getNumMacroArguments() == 1) {
            const Token *tok = args->getUnexpArgument(0);
            if (tok != nullptr && tok->isNot(tok::eof)) rec.argument_pos = pos_of(sm_.getSpellingLoc(tok->getLocation()));
            for (; tok != nullptr && tok->isNot(tok::eof); ++tok) {
                if (!rec.body.empty()) rec.body += " ";
                rec.body += pp.getSpelling(*tok);
            }
        }
        macros.push_back(rec);
    }

    // Is `loc` in a real file of the user (not a system header, not the command
    // line or the predefines)?
    bool user_file_loc(SourceLocation loc) const {
        if (loc.isInvalid() || !loc.isFileID()) return false;
        if (!sm_.getFileEntryRefForID(sm_.getFileID(loc))) return false;
        return !sm_.isInSystemHeader(loc);
    }

    void note_define(Preprocessor &pp, const Token &name, const MacroDirective *directive) {
        SourceLocation loc = name.getLocation();
        const MacroInfo *info = directive != nullptr ? directive->getMacroInfo() : nullptr;
        if (info == nullptr || !user_file_loc(loc)) return;
        MacroRec rec = directive_rec("define", pp, name);
        rec.function_like = info->isFunctionLike();
        rec.variadic = info->isVariadic();
        rec.param_count = static_cast<int>(info->getNumParams());
        rec.line_count = static_cast<int>(sm_.getSpellingLineNumber(info->getDefinitionEndLoc()) -
                                          sm_.getSpellingLineNumber(info->getDefinitionLoc())) + 1;
        if (rec.line_count > 1) rec.splice_pos = first_splice(info->getDefinitionLoc());
        llvm::ArrayRef<Token> tokens = info->tokens();
        int operators = 0;
        for (size_t at = 0; at < tokens.size(); ++at) {
            const Token &tok = tokens[at];
            bool stringize = tok.is(tok::hash) && rec.function_like;
            bool paste = tok.is(tok::hashhash);
            if (stringize || paste) {
                ++operators;
                if (stringize) ++rec.stringize_count; else ++rec.paste_count;
                if (operators == 1) {
                    rec.first_operator = pos_of(paste && at > 0 ? tokens[at - 1].getLocation() : tok.getLocation());
                } else if (operators == 2) {
                    rec.second_operator = pos_of(tok.getLocation());
                }
                if (stringize && at + 2 < tokens.size() && tokens[at + 2].is(tok::hashhash) &&
                    rec.stringize_paste.line == 0) {
                    rec.stringize_paste = pos_of(tokens[at + 2].getLocation());
                }
            }
            if (const IdentifierInfo *ident = tok.getIdentifierInfo()) {
                llvm::StringRef word = ident->getName();
                if (word == "__FILE__" || word == "__LINE__" || word == "__func__" ||
                    word == "__FUNCTION__" || word == "__PRETTY_FUNCTION__" ||
                    word == "__FILE_NAME__" || call_site_macros_.count(word.str()) > 0) {
                    rec.uses_call_site = true;
                }
            }
            if (rec.body.size() < 4096) {
                if (!rec.body.empty()) rec.body += ' ';
                rec.body += pp.getSpelling(tok);
            }
        }
        if (rec.uses_call_site) call_site_macros_.insert(rec.name);
        macros.push_back(rec);
        if (rec.function_like) note_mixed_params(info);
        if (rec.function_like) note_generic_reuse(info);
        note_ucn_paste(info, pp);
    }

    // A backslash and u or U (with hex digits) pasted by ## into something
    // that would be a universal character name: mark ucn_paste at the
    // backslash.
    void note_ucn_paste(const MacroInfo *info, Preprocessor &pp) {
        llvm::ArrayRef<Token> tokens = info->tokens();
        for (size_t at = 2; at < tokens.size(); ++at) {
            if (!tokens[at].is(tok::hashhash)) continue;
            const Token &word = tokens[at - 1];
            const Token &slash = tokens[at - 2];
            const IdentifierInfo *ident = word.getIdentifierInfo();
            if (ident == nullptr || word.hasLeadingSpace() || !slash.is(tok::unknown) || pp.getSpelling(slash) != "\\") continue;
            llvm::StringRef name = ident->getName();
            if (name.empty() || (name[0] != 'u' && name[0] != 'U')) continue;
            bool hex = true;
            for (char letter : name.drop_front()) hex = hex && llvm::isHexDigit(letter);
            if (hex) add_spelled_mark("ucn_paste", slash.getLocation());
        }
    }

    // A parameter that is an operand of # or ## and also used where it is
    // expanded: kind mixed_param at the first expanded use (name the parameter).
    // A function-like macro whose _Generic association uses a parameter twice:
    // mark generic_reuse at the association's first token.
    void note_generic_reuse(const MacroInfo *info) {
        llvm::ArrayRef<Token> tokens = info->tokens();
        for (size_t start = 0; start < tokens.size(); ++start) {
            const IdentifierInfo *word = tokens[start].getIdentifierInfo();
            if (word == nullptr || word->getName() != "_Generic" || start + 1 >= tokens.size() || !tokens[start + 1].is(tok::l_paren)) continue;
            // find the parts: controlling expression, then associations
            std::vector<std::pair<size_t, size_t>> parts;
            size_t from = start + 2, at = start + 2;
            int depth = 0;
            for (; at < tokens.size(); ++at) {
                if (tokens[at].isOneOf(tok::l_paren, tok::l_square, tok::l_brace)) ++depth;
                if (tokens[at].isOneOf(tok::r_paren, tok::r_square, tok::r_brace)) {
                    if (depth == 0) break;
                    --depth;
                }
                if (depth == 0 && tokens[at].is(tok::comma)) {
                    parts.push_back({from, at});
                    from = at + 1;
                }
            }
            if (at >= tokens.size()) return;
            parts.push_back({from, at});
            size_t end = at;
            for (const IdentifierInfo *param : info->params()) {
                int outside = 0;
                for (size_t one = 0; one < tokens.size(); ++one) {
                    if ((one < start || one > end) && tokens[one].getIdentifierInfo() == param) ++outside;
                }
                for (size_t part = 1; part < parts.size(); ++part) {
                    size_t colon = parts[part].first;
                    int nest = 0;
                    while (colon < parts[part].second) {
                        if (tokens[colon].is(tok::l_paren)) ++nest;
                        if (tokens[colon].is(tok::r_paren)) --nest;
                        if (nest == 0 && tokens[colon].is(tok::colon)) break;
                        ++colon;
                    }
                    if (colon + 1 >= parts[part].second) continue;
                    int inside = 0;
                    for (size_t one = colon + 1; one < parts[part].second; ++one) {
                        if (tokens[one].getIdentifierInfo() == param) ++inside;
                    }
                    (void)outside;   // a use outside may sit on another branch of the macro
                    if (inside >= 2) {
                        SourceLocation loc = tokens[colon + 1].getLocation();
                        if (loc.isFileID()) add_ast_mark("generic_reuse", loc);
                    }
                }
            }
            start = end;
        }
    }

    void note_mixed_params(const MacroInfo *info) {
        llvm::ArrayRef<Token> tokens = info->tokens();
        for (const IdentifierInfo *param : info->params()) {
            bool operand = false;
            int plain = -1;
            for (size_t at = 0; at < tokens.size(); ++at) {
                if (tokens[at].getIdentifierInfo() != param) continue;
                bool stringized = at > 0 && tokens[at - 1].is(tok::hash);
                bool pasted = (at > 0 && tokens[at - 1].is(tok::hashhash)) ||
                              (at + 1 < tokens.size() && tokens[at + 1].is(tok::hashhash));
                if (stringized || pasted) {
                    operand = true;
                } else if (plain < 0) {
                    plain = static_cast<int>(at);
                }
            }
            if (!operand || plain < 0) continue;
            SourceLocation loc = tokens[static_cast<size_t>(plain)].getLocation();
            if (!loc.isFileID()) continue;
            MacroRec rec;
            rec.uid = static_cast<int>(macros.size());
            rec.kind = "mixed_param";
            rec.name = param->getName().str();
            rec.pos = pos_of(loc);
            rec.name_pos = rec.pos;
            macros.push_back(rec);
        }
    }

    void note_undef(Preprocessor &pp, const Token &name, const MacroDefinition &definition) {
        if (!user_file_loc(name.getLocation())) return;
        MacroRec rec = directive_rec("undef", pp, name);
        if (const MacroInfo *info = definition.getMacroInfo()) {
            SourceLocation defined = info->getDefinitionLoc();
            bool in_file = defined.isValid() && defined.isFileID() &&
                           sm_.getFileEntryRefForID(sm_.getFileID(defined));
            rec.defined_file_uid = in_file ? file_of(defined) : -2;
        }
        macros.push_back(rec);
    }

    // #pragma and _Pragma.  A _Pragma written in a macro body is placed at the
    // #define (its spelling); one from a system macro is not recorded.
    void note_pragma(SourceLocation loc) {
        SourceLocation spelled = sm_.getSpellingLoc(loc);
        if (!user_file_loc(spelled)) return;
        MacroRec rec;
        rec.uid = static_cast<int>(macros.size());
        rec.kind = "pragma";
        rec.pos = pos_of(spelled);
        rec.name_pos = rec.pos;
        bool invalid = false;
        const char *text = sm_.getCharacterData(spelled, &invalid);
        if (!invalid && text != nullptr) {
            for (const char *at = text; *at != '\0' && *at != '\n' && rec.body.size() < 512; ++at) {
                rec.body += *at;
            }
        }
        macros.push_back(rec);
    }

    // After the preprocessor is done: the include guards of the headers, and the
    // skipped ranges in order.  A header counts as protected when Clang's
    // multiple-include optimisation sees a guard or #pragma once, or when every
    // piece of code in it sits in some guard block (several guarded blocks one
    // after the other, as in an amalgamated header); a header of directives only
    // is harmless when included again.  `first_line`/`first_column`: the first
    // token of code outside any guard.
    void finish_files(Preprocessor &pp) {
        for (auto &entry : skipped_) std::sort(entry.second.begin(), entry.second.end());
        HeaderSearch &search = pp.getHeaderSearchInfo();
        for (const auto &entry : file_ids_) {
            FileRec &rec = files[static_cast<size_t>(entry.second)];
            if (rec.kind != "header") continue;
            OptionalFileEntryRef file = sm_.getFileEntryRefForID(entry.first);
            if (!file) continue;
            if (search.isFileMultipleIncludeGuarded(*file)) continue;
            SourceLocation unguarded = unguarded_code(entry.first, pp.getLangOpts());
            if (unguarded.isInvalid()) continue;
            rec.include_guarded = false;
            rec.first_line = sm_.getSpellingLineNumber(unguarded);
            rec.first_column = sm_.getSpellingColumnNumber(unguarded);
            rec.included_in_declaration = included_in_declaration(sm_.getIncludeLoc(entry.first));
        }
    }

    // The line classes of every user file seen, after all records are in.
    void finish_lines(const LangOptions &lang) {
        finish_signals();
        finish_process_calls();
        finish_positions();
        finish_batch35();
        finish_batch36();
        finish_batch37();
        finish_diagnostics();
        if (creates_threads_) {
            for (SourceLocation call : unsafe_calls_) add_ast_mark("unsafe_in_threads", call);
        }
        for (const auto &entry : file_ids_) {
            FileRec &rec = files[static_cast<size_t>(entry.second)];
            if (rec.kind == "system_header" || !sm_.getFileEntryRefForID(entry.first)) continue;
            rec.line_classes = line_classes(entry.first, lang);
            rec.lexical_marks = lexical_marks(entry.first, lang);
            auto ast = ast_marks_.find(entry.first);
            if (ast != ast_marks_.end()) rec.lexical_marks += ast->second;
        }
    }

    // One lexical mark at a file offset.
    void add_mark(std::string &out, const char *code, FileID fid, unsigned offset) const {
        out += code;
        out += ":" + std::to_string(sm_.getLineNumber(fid, offset)) + ":" +
               std::to_string(sm_.getColumnNumber(fid, offset)) + ";";
    }

    // The escapes of one string or character literal token: an octal or hex
    // escape not followed by another escape or the closing quote (open_escape, at
    // the literal), an escape the standard does not define (odd_escape, at the
    // backslash).  Raw strings have none.
    void literal_marks(std::string &out, FileID fid, unsigned start, llvm::StringRef text) const {
        size_t quote = text.find_first_of("'\"");
        if (quote == llvm::StringRef::npos || text.take_front(quote).contains('R')) return;
        if (text.size() < quote + 2) return;
        size_t close = text.size() - 1;
        bool open_seen = false;
        size_t idx = quote + 1;
        while (idx < close) {
            if (text[idx] != '\\') { ++idx; continue; }
            if (idx + 1 >= close) break;
            char kind = text[idx + 1];
            size_t next = idx + 2;
            bool numeric = false;
            if (kind == 'x' && next < close && text[next] == '{') {
                while (next < close && text[next] != '}') ++next;
                ++next;
            } else if (kind == 'x') {
                while (next < close && llvm::isHexDigit(text[next])) ++next;
                numeric = next > idx + 2;
            } else if (kind >= '0' && kind <= '7') {
                while (next < close && next < idx + 4 && text[next] >= '0' && text[next] <= '7') ++next;
                numeric = true;
            } else if ((kind == 'o' || kind == 'N' || kind == 'u' || kind == 'U') && next < close &&
                       text[next] == '{') {
                while (next < close && text[next] != '}') ++next;
                ++next;
            } else if (kind == '\n' || kind == '\r' || std::strchr("'\"?\\abfnrtvuU", kind) == nullptr) {
                if (kind != '\n' && kind != '\r') add_mark(out, "odd_escape", fid, start + static_cast<unsigned>(idx));
            }
            if (numeric && !open_seen && next < close && text[next] != '\\') {
                add_mark(out, "open_escape", fid, start);
                open_seen = true;
            }
            idx = next;
        }
    }

    // The argument list of a function-like macro written in a file (the name to
    // the closing parenthesis), for directive_in_args.
    void note_argument_range(const MacroDefinition &definition, SourceRange range) {
        const MacroInfo *info = definition.getMacroInfo();
        if (info == nullptr || !info->isFunctionLike()) return;
        SourceLocation begin = range.getBegin();
        SourceLocation end = range.getEnd();
        if (!begin.isFileID() || !end.isFileID()) return;
        std::pair<FileID, unsigned> from = sm_.getDecomposedLoc(begin);
        std::pair<FileID, unsigned> upto = sm_.getDecomposedLoc(end);
        if (from.first != upto.first || upto.second <= from.second) return;
        argument_ranges_[from.first].push_back({from.second, upto.second});
    }

    // The index of the argument range of the file that holds `offset`, -1 if none.
    int argument_range_of(FileID fid, unsigned offset) const {
        auto found = argument_ranges_.find(fid);
        if (found == argument_ranges_.end()) return -1;
        for (size_t idx = 0; idx < found->second.size(); ++idx) {
            const auto &range = found->second[idx];
            if (offset > range.first && offset < range.second) return static_cast<int>(idx);
        }
        return -1;
    }

    // OC-0141: a header name of an #include that holds ', \\, /* or // (and "
    // between < and >): mark header_name_chars at the opening " or <.
    void note_header_name(std::string &out, FileID fid, llvm::StringRef buffer, unsigned from) const {
        size_t eol = buffer.find('\n', from);
        if (eol == llvm::StringRef::npos) eol = buffer.size();
        llvm::StringRef rest = buffer.slice(from, eol).ltrim(" \t");
        if (rest.empty() || (rest[0] != '"' && rest[0] != '<')) return;
        size_t stop = rest.find(rest[0] == '<' ? '>' : '"', 1);
        if (stop == llvm::StringRef::npos) return;
        llvm::StringRef name = rest.substr(1, stop - 1);
        bool odd = name.contains('\'') || name.contains('\\') || name.contains("/*") || name.contains("//") ||
                   (rest[0] == '<' && name.contains('"'));
        if (odd) add_mark(out, "header_name_chars", fid, static_cast<unsigned>(rest.data() - buffer.data()));
    }

    // OC-0182: the argument of INTn_C, UINTn_C, INTMAX_C or UINTMAX_C that is
    // not one unsuffixed integer literal within the range of the type.
    void note_constant_macro(Preprocessor &pp, llvm::StringRef macro, const MacroArgs *args) {
        const Token *tok = args->getUnexpArgument(0);
        if (tok == nullptr || tok->is(tok::eof)) return;
        // A macro name as the whole argument: what it expands to is not looked at.
        if (tok->isAnyIdentifier() && (tok + 1)->is(tok::eof)) return;
        bool plain = tok->is(tok::numeric_constant) && (tok + 1)->is(tok::eof);
        if (plain) {
            std::string spelling = pp.getSpelling(*tok);
            static const llvm::Regex digits("^(0[xX][0-9a-fA-F]+|[0-9]+)$");
            unsigned long long value = 0;
            plain = digits.match(spelling) && !llvm::StringRef(spelling).getAsInteger(0, value);
            bool wide = macro.contains("MAX");
            unsigned bits = 64;
            if (!wide) {
                llvm::StringRef width = macro.drop_front(macro.starts_with("U") ? 4 : 3);
                width = width.drop_back(2);
                if (width.getAsInteger(10, bits)) bits = 64;
            }
            bool is_unsigned = macro.starts_with("U");
            unsigned long long limit = is_unsigned ? (bits >= 64 ? ~0ULL : (1ULL << bits) - 1) : (1ULL << (bits - 1)) - 1;
            plain = plain && value <= limit;
        }
        if (!plain) add_ast_mark("constant_macro_arg", tok->getLocation());
    }

    static bool known_directive(llvm::StringRef name) {
        static const char *const names[] = {"if", "ifdef", "ifndef", "elif", "elifdef", "elifndef", "else",
                                            "endif", "define", "undef", "include", "include_next", "import",
                                            "line", "error", "warning", "pragma", "ident", "sccs", "assert",
                                            "unassert", "embed"};
        for (const char *known : names) {
            if (name == known) return true;
        }
        return false;
    }

    // The lexical marks of a file (FileRec.lexical_marks).  The raw tokens, comments
    // kept; literals in skipped code and on #include, #error and #warning lines
    // are not looked at.
    std::string lexical_marks(FileID fid, const LangOptions &lang) const {
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(fid, &invalid);
        if (invalid) return "";
        std::string out;
        for (size_t at = 0; at + 2 < buffer.size(); ++at) {
            if (buffer[at] == '?' && buffer[at + 1] == '?' &&
                std::strchr("=(/)'<!>-", buffer[at + 2]) != nullptr && buffer[at + 2] != 0) {
                add_mark(out, "trigraph", fid, static_cast<unsigned>(at));
                at += 2;
            }
        }
        Lexer lexer(fid, sm_.getBufferOrFake(fid), sm_, lang);
        lexer.SetCommentRetentionState(true);
        Token tok;
        bool directive_quiet = false;
        bool after_hash = false;
        unsigned hash_offset = 0;
        std::set<int> reported_ranges;
        // OC-0140: has code (not a directive, a comment, skipped code, or the
        // `extern "C" {` and `}` around includes) come before an #include?
        bool in_directive = false;
        bool code_seen = false;
        int extern_stage = 0;
        int extern_open = 0;
        unsigned digraph_end = ~0u;
        while (true) {
            lexer.LexFromRawLexer(tok);
            if (tok.is(tok::eof)) break;
            if (tok.getLength() == 0) continue;
            unsigned offset = sm_.getFileOffset(tok.getLocation());
            if (offset + tok.getLength() > buffer.size()) break;
            llvm::StringRef text = buffer.substr(offset, tok.getLength());
            // A token right after a backslash continuation starts at the backslash.
            while (text.starts_with("\\")) {
                size_t rest = 1;
                while (rest < text.size() && (text[rest] == ' ' || text[rest] == '\t')) ++rest;
                if (rest < text.size() && text[rest] == '\r') ++rest;
                if (rest >= text.size() || text[rest] != '\n') break;
                text = text.drop_front(rest + 1);
                offset += static_cast<unsigned>(rest + 1);
            }
            if (text.empty()) continue;
            if (tok.isAtStartOfLine()) {
                directive_quiet = false;
                after_hash = tok.is(tok::hash);
                in_directive = after_hash;
                if (after_hash) {
                    hash_offset = offset;
                    int range = argument_range_of(fid, offset);
                    if (range >= 0 && reported_ranges.insert(range).second) add_mark(out, "directive_in_args", fid, offset);
                }
            } else if (after_hash) {
                after_hash = false;
                if (tok.is(tok::raw_identifier)) {
                    llvm::StringRef name = tok.getRawIdentifier();
                    directive_quiet = name == "include" || name == "include_next" || name == "import" ||
                                      name == "error" || name == "warning";
                    if (!known_directive(name)) add_mark(out, "invalid_directive", fid, hash_offset);
                    bool include = name == "include" || name == "include_next" || name == "import";
                    if (include && code_seen && !offset_skipped(fid, hash_offset)) add_mark(out, "late_include", fid, hash_offset);
                    if (include && !offset_skipped(fid, hash_offset)) note_header_name(out, fid, buffer, offset + static_cast<unsigned>(text.size()));
                } else if (!tok.isOneOf(tok::numeric_constant, tok::comment)) {
                    add_mark(out, "invalid_directive", fid, hash_offset);
                }
            }
            if (!in_directive && !code_seen && !tok.is(tok::comment) && !offset_skipped(fid, offset)) {
                if (extern_stage == 0 && tok.is(tok::raw_identifier) && tok.getRawIdentifier() == "extern") {
                    extern_stage = 1;
                } else if (extern_stage == 1 && (text == "\"C\"" || text == "\"C++\"")) {
                    extern_stage = 2;
                } else if (extern_stage == 2 && tok.is(tok::l_brace)) {
                    extern_stage = 0;
                    ++extern_open;
                } else if (extern_stage == 0 && extern_open > 0 && tok.is(tok::r_brace)) {
                    --extern_open;
                } else {
                    code_seen = true;
                }
            }
            if (tok.is(tok::comment)) {
                if (text.starts_with("/*")) {
                    size_t inner = text.find("/*", 2);
                    if (inner != llvm::StringRef::npos)
                        add_mark(out, "comment_open", fid, offset + static_cast<unsigned>(inner));
                } else if (text.starts_with("//")) {
                    size_t slash = text.find('\\');
                    while (slash != llvm::StringRef::npos) {
                        size_t rest = slash + 1;
                        while (rest < text.size() && (text[rest] == ' ' || text[rest] == '\t')) ++rest;
                        if (rest < text.size() && (text[rest] == '\n' || text[rest] == '\r')) {
                            add_mark(out, "comment_splice", fid, offset + static_cast<unsigned>(slash));
                            break;
                        }
                        slash = text.find('\\', slash + 1);
                    }
                }
                continue;
            }
            if (tok.isOneOf(tok::l_square, tok::r_square, tok::l_brace, tok::r_brace, tok::hash,
                            tok::hashhash) && (text[0] == '<' || text[0] == ':' || text[0] == '%')) {
                // one mark for adjacent digraphs such as <::>
                if (offset != digraph_end) add_mark(out, "digraph", fid, offset);
                digraph_end = offset + tok.getLength();
            }
            bool literal = tok::isStringLiteral(tok.getKind()) || tok.isOneOf(tok::char_constant,
                tok::wide_char_constant, tok::utf8_char_constant, tok::utf16_char_constant,
                tok::utf32_char_constant);
            if (literal && !directive_quiet && !offset_skipped(fid, offset)) literal_marks(out, fid, offset, text);
        }
        return out;
    }

    // Oculus' line counting: the raw tokens of the file, comments kept; a line
    // touched by a comment token is a comment line, by any other token a code
    // line, by both a mixed line, by none a blank line.  Code in #if 0 counts.
    std::string line_classes(FileID fid, const LangOptions &lang) const {
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(fid, &invalid);
        if (invalid) return "";
        size_t lines = static_cast<size_t>(buffer.count('\n'));
        if (!buffer.empty() && buffer.back() != '\n') ++lines;
        std::vector<unsigned char> marks(lines, 0);
        Lexer lexer(fid, sm_.getBufferOrFake(fid), sm_, lang);
        lexer.SetCommentRetentionState(true);
        Token tok;
        while (true) {
            lexer.LexFromRawLexer(tok);
            if (tok.is(tok::eof)) break;
            if (tok.getLength() == 0) continue;
            unsigned offset = sm_.getFileOffset(tok.getLocation());
            unsigned first = sm_.getLineNumber(fid, offset);
            unsigned last = sm_.getLineNumber(fid, offset + tok.getLength() - 1);
            unsigned char bit = tok.is(tok::comment) ? 2 : 1;
            for (unsigned line = first; line <= last && line >= 1 && line <= lines; ++line) {
                marks[line - 1] |= bit;
            }
        }
        std::string classes(lines, 'b');
        for (size_t at = 0; at < lines; ++at) {
            classes[at] = marks[at] == 1 ? 'x' : marks[at] == 2 ? 'c' : marks[at] == 3 ? 'm' : 'b';
        }
        return classes;
    }

    // A mark from the AST (FileRec.lexical_marks): at the expansion of `loc`, once.
    void add_ast_mark(const char *code, SourceLocation loc) {
        SourceLocation at = sm_.getExpansionLoc(loc);
        if (at.isInvalid() || sm_.isInSystemHeader(at)) return;
        std::pair<FileID, unsigned> place = sm_.getDecomposedLoc(at);
        if (!ast_mark_seen_.insert(place).second) return;
        add_mark(ast_marks_[place.first], code, place.first, place.second);
    }

    // add_ast_mark with a name as fourth part (code:line:col:name), which
    // rules match against configured names.
    void add_named_mark(const char *code, SourceLocation loc, llvm::StringRef name) {
        SourceLocation at = sm_.getExpansionLoc(loc);
        if (at.isInvalid() || sm_.isInSystemHeader(at)) return;
        std::pair<FileID, unsigned> place = sm_.getDecomposedLoc(at);
        if (!ast_mark_seen_.insert(place).second) return;
        std::string &out = ast_marks_[place.first];
        add_mark(out, code, place.first, place.second);
        out.pop_back();
        out += ":" + name.str() + ";";
    }

    // A function declared in a system header with this name.
    bool system_function(const FunctionDecl *callee, const char *name) const {
        if (callee == nullptr || callee->getIdentifier() == nullptr || callee->getName() != name) return false;
        SourceLocation spelled = sm_.getSpellingLoc(callee->getLocation());
        return spelled.isValid() && sm_.isInSystemHeader(spelled);
    }

    // The source text of an expression at its expansion.
    std::string expansion_text(const Expr *expr) const {
        CharSourceRange range = sm_.getExpansionRange(expr->getSourceRange());
        return Lexer::getSourceText(range, sm_, context_.getLangOpts()).str();
    }

    // pthread_kill with a signal that is a constant other than 0 (mark
    // thread_signal at the signal), pthread_setcanceltype with
    // PTHREAD_CANCEL_ASYNCHRONOUS (mark async_cancel at the argument).
    void note_thread_calls(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (call->getNumArgs() == 2 && system_function(callee, "pthread_kill")) {
            const Expr *signal = call->getArg(1);
            Expr::EvalResult value;
            if (!signal->isValueDependent() && signal->EvaluateAsInt(value, context_) && value.Val.getInt() != 0) {
                add_ast_mark("thread_signal", signal->getBeginLoc());
            }
        }
        if (call->getNumArgs() == 2 && system_function(callee, "pthread_setcanceltype")) {
            const Expr *kind = call->getArg(0);
            const auto *ref = dyn_cast<DeclRefExpr>(kind->IgnoreParenImpCasts());
            bool named = ref != nullptr && ref->getDecl()->getIdentifier() != nullptr &&
                         ref->getDecl()->getName() == "PTHREAD_CANCEL_ASYNCHRONOUS";
            if (named || expansion_text(kind) == "PTHREAD_CANCEL_ASYNCHRONOUS") add_ast_mark("async_cancel", kind->getBeginLoc());
        }
    }

    // A written std::vector<bool> (mark vector_bool at the argument).
    void note_vector_bool(TemplateSpecializationTypeLoc loc) {
        const auto *type = loc.getTypePtr();
        if (type == nullptr || loc.getNumArgs() < 1) return;
        const TemplateDecl *tmpl = type->getTemplateName().getAsTemplateDecl();
        if (tmpl == nullptr || tmpl->getIdentifier() == nullptr || tmpl->getName() != "vector" || !tmpl->isInStdNamespace()) return;
        TemplateArgumentLoc first = loc.getArgLoc(0);
        const TemplateArgument &arg = first.getArgument();
        if (arg.getKind() != TemplateArgument::Type || arg.getAsType().isNull() || arg.getAsType()->isDependentType()) return;
        if (!arg.getAsType().getCanonicalType()->isBooleanType()) return;
        add_ast_mark("vector_bool", first.getLocation());
    }

    static const RecordDecl *flexible_record(QualType type) {
        if (type.isNull()) return nullptr;
        const auto *record = type->getAsRecordDecl();
        if (record == nullptr || record->getDefinition() == nullptr) return nullptr;
        return record->getDefinition()->hasFlexibleArrayMember() ? record : nullptr;
    }

    // C: `a = b` of a structure with a flexible array member.
    bool flexible_copy(const BinaryOperator *binary) const {
        if (context_.getLangOpts().CPlusPlus || binary->getOpcode() != BO_Assign) return false;
        return flexible_record(binary->getLHS()->getType()) != nullptr;
    }

    // C: an object, parameter or return type by value of a structure with a
    // flexible array member (not an extern declaration, not a pointer).
    bool flexible_object(const Decl *decl) const {
        if (context_.getLangOpts().CPlusPlus) return false;
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) return flexible_record(fn->getReturnType()) != nullptr;
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || flexible_record(var->getType()) == nullptr) return false;
        if (isa<ParmVarDecl>(var)) return true;
        return var->isThisDeclarationADefinition() != VarDecl::DeclarationOnly;
    }

    // The expression a reference belongs to: up through parent expressions.
    const Expr *full_expression(const Expr *expr) const {
        const Expr *top = expr;
        while (true) {
            auto parents = context_.getParents(*top);
            if (parents.size() != 1) return top;
            const auto *outer = parents[0].get<Expr>();
            if (outer == nullptr) return top;
            top = outer;
        }
    }

    static void refs_to(const Stmt *stmt, const ValueDecl *decl, std::vector<const DeclRefExpr *> &out) {
        if (stmt == nullptr) return;
        if (const auto *unary = dyn_cast<UnaryExprOrTypeTraitExpr>(stmt)) {
            (void)unary;
            return;
        }
        if (isa<GenericSelectionExpr>(stmt)) return;
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            if (ref->getDecl() == decl) out.push_back(ref);
        }
        for (const Stmt *child : stmt->children()) refs_to(child, decl, out);
    }

    static bool contains(const Stmt *stmt, const Stmt *part) {
        if (stmt == nullptr) return false;
        if (stmt == part) return true;
        for (const Stmt *child : stmt->children()) {
            if (contains(child, part)) return true;
        }
        return false;
    }

    // C: at the first reference to an atomic variable in its full expression,
    // each later reference marks (shape atomic_again) the operand of the
    // innermost expression holding both that holds the later one.
    void note_atomic_refs(const DeclRefExpr *ref) {
        if (context_.getLangOpts().CPlusPlus) return;
        const auto *var = dyn_cast<VarDecl>(ref->getDecl());
        if (var == nullptr || var->getType().isNull() || !var->getType()->isAtomicType()) return;
        std::vector<const DeclRefExpr *> refs;
        refs_to(full_expression(ref), var, refs);
        if (refs.size() < 2 || refs.front() != ref) return;
        for (size_t later = 1; later < refs.size(); ++later) {
            const Stmt *below = refs[later];
            while (true) {
                auto parents = context_.getParents(*below);
                if (parents.size() != 1) break;
                const auto *outer = parents[0].get<Expr>();
                if (outer == nullptr) break;
                if (contains(outer, ref)) {
                    const Stmt *operand = isa<Expr>(below) ? without_implicit_casts(cast<Expr>(below)) : below;
                    std::string &pending = pending_shape_[operand];
                    if (pending.find("atomic_again") == std::string::npos) add_pending(operand, "atomic_again");
                    break;
                }
                below = outer;
            }
        }
    }

    // A qualifier written on a function type taken from a typedef
    // (`const handler process;`): Clang drops it, the tokens keep it.
    bool qualified_function(const Decl *decl) const {
        if (context_.getLangOpts().CPlusPlus) return false;
        const TypeSourceInfo *written = nullptr;
        QualType held;
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            written = fn->getTypeSourceInfo();
            held = fn->getType();
        } else if (const auto *alias = dyn_cast<TypedefNameDecl>(decl)) {
            written = alias->getTypeSourceInfo();
            held = alias->getUnderlyingType();
        }
        if (written == nullptr || held.isNull() || !held->isFunctionType()) return false;
        TypeLoc loc = written->getTypeLoc().getUnqualifiedLoc();
        if (loc.getAs<FunctionTypeLoc>() || loc.getAs<ParenTypeLoc>() || loc.getAs<AttributedTypeLoc>()) return false;
        SourceLocation begin = decl->getBeginLoc(), name = decl->getLocation();
        if (begin.isMacroID() || name.isMacroID()) return false;
        std::pair<FileID, unsigned> from = sm_.getDecomposedLoc(begin), to = sm_.getDecomposedLoc(name);
        if (from.first != to.first || to.second <= from.second) return false;
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(from.first, &invalid);
        if (invalid) return false;
        Lexer lexer(sm_.getLocForStartOfFile(from.first), context_.getLangOpts(), buffer.begin(), buffer.begin() + from.second, buffer.end());
        Token token;
        int depth = 0;
        while (!lexer.LexFromRawLexer(token) && sm_.getFileOffset(token.getLocation()) < to.second) {
            if (token.is(tok::l_paren)) ++depth;
            if (token.is(tok::r_paren)) --depth;
            if (depth != 0 || !token.is(tok::raw_identifier)) continue;
            llvm::StringRef word = token.getRawIdentifier();
            if (word == "const" || word == "volatile" || word == "restrict" || word == "_Atomic") return true;
        }
        return false;
    }

    static bool std_class(QualType type, const char *name) {
        if (type.isNull()) return false;
        const auto *record = type->getAsCXXRecordDecl();
        return record != nullptr && record->getIdentifier() != nullptr && record->getName() == name &&
               record->isInStdNamespace();
    }

    // Inside a while, do or for statement of the same function.
    bool inside_loop(const Stmt *stmt) const {
        DynTypedNode node = DynTypedNode::create(*stmt);
        while (true) {
            auto parents = context_.getParents(node);
            if (parents.empty()) return false;
            node = parents[0];
            if (node.get<WhileStmt>() || node.get<DoStmt>() || node.get<ForStmt>() || node.get<CXXForRangeStmt>()) return true;
            if (node.get<FunctionDecl>() || node.get<LambdaExpr>() || node.get<BlockDecl>()) return false;
        }
    }

    static bool float_inside(const ASTContext &context, QualType type, int depth) {
        if (type.isNull() || depth > 8) return false;
        type = context.getBaseElementType(type.getCanonicalType());
        if (type->isRealFloatingType()) return true;
        const auto *record = type->getAsRecordDecl();
        if (record == nullptr || record->getDefinition() == nullptr) return false;
        for (const FieldDecl *field : record->getDefinition()->fields()) {
            if (float_inside(context, field->getType(), depth + 1)) return true;
        }
        return false;
    }

    // Library calls: memcmp of floating objects (mark float_memcmp at the
    // argument), cnd_wait and cnd_timedwait outside a loop (unlooped_wait), the
    // functions with hidden static state (unsafe_in_threads, when the unit
    // creates threads), and ctype functions given a plain or signed char.
    void note_library_calls(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return;
        SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
        if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) return;
        llvm::StringRef name = callee->getName();
        if ((name == "memcmp" || name == "bcmp") && call->getNumArgs() == 3) {
            for (unsigned at = 0; at < 2; ++at) {
                QualType held = without_implicit_casts(call->getArg(at))->getType();
                if (held->isPointerType() && float_inside(context_, held->getPointeeType(), 0)) {
                    add_ast_mark("float_memcmp", call->getArg(at)->getBeginLoc());
                    break;
                }
            }
        }
        if ((name == "cnd_wait" || name == "cnd_timedwait") && !inside_loop(call)) add_ast_mark("unlooped_wait", call->getBeginLoc());
        if (name == "thrd_create" || name == "pthread_create") creates_threads_ = true;
        static const std::set<std::string> unsafe = {"strtok", "rand", "asctime", "ctime", "gmtime", "localtime",
                                                     "localeconv", "setlocale", "strerror", "tmpnam", "strsignal"};
        if (unsafe.count(name.str()) > 0) unsafe_calls_.push_back(call->getBeginLoc());
    }

    // The calls in a statement, not those in nested lambdas and blocks.
    static void calls_in(const Stmt *stmt, std::vector<const CallExpr *> &out) {
        if (stmt == nullptr || isa<LambdaExpr>(stmt) || isa<BlockExpr>(stmt)) return;
        if (const auto *call = dyn_cast<CallExpr>(stmt)) out.push_back(call);
        for (const Stmt *child : stmt->children()) calls_in(child, out);
    }

    // The function a call argument names, if it names one directly.
    static const FunctionDecl *named_function(const Expr *expr) {
        expr = expr->IgnoreParenImpCasts();
        if (const auto *address = dyn_cast<UnaryOperator>(expr)) {
            if (address->getOpcode() == UO_AddrOf) expr = address->getSubExpr()->IgnoreParenImpCasts();
        }
        const auto *ref = dyn_cast<DeclRefExpr>(expr);
        return ref != nullptr ? dyn_cast<FunctionDecl>(ref->getDecl()) : nullptr;
    }

    // A loop that never ends by its condition (for (;;), while (1)).
    bool endless_loop(const Stmt *stmt) const {
        const Expr *cond = nullptr;
        if (const auto *loop = dyn_cast<ForStmt>(stmt)) {
            if (loop->getCond() == nullptr) return true;
            cond = loop->getCond();
        } else if (const auto *loop = dyn_cast<WhileStmt>(stmt)) {
            cond = loop->getCond();
        } else if (const auto *loop = dyn_cast<DoStmt>(stmt)) {
            cond = loop->getCond();
        }
        if (cond == nullptr || cond->isValueDependent()) return false;
        Expr::EvalResult value;
        return cond->EvaluateAsInt(value, context_) && value.Val.getInt() != 0;
    }

    // Whether a statement may end without returning: a call of a noreturn or
    // jump function, an endless loop, or a call of a function whose body is not
    // seen (or that does so itself, one level deep).
    bool may_not_return(const Stmt *stmt, int depth) const {
        if (stmt == nullptr || isa<LambdaExpr>(stmt) || isa<BlockExpr>(stmt)) return false;
        if (endless_loop(stmt)) return true;
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            const FunctionDecl *callee = call->getDirectCallee();
            if (callee == nullptr) return true;
            if (callee->isNoReturn()) return true;
            if (callee->getIdentifier() != nullptr) {
                llvm::StringRef name = callee->getName();
                if (name == "longjmp" || name == "siglongjmp" || name == "_longjmp" || name == "raise" || name == "kill") return true;
            }
            SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
            bool library = declared.isValid() && sm_.isInSystemHeader(declared);
            if (!library) {
                const FunctionDecl *defined = nullptr;
                if (!callee->hasBody(defined) || depth >= 1) return true;
                if (may_not_return(defined->getBody(), depth + 1)) return true;
            }
        }
        for (const Stmt *child : stmt->children()) {
            if (may_not_return(child, depth)) return true;
        }
        return false;
    }

    // signal, atexit, at_quick_exit, getc/putc, putenv and the uid functions:
    // collects signal calls and handlers for finish_signals, marks
    // stream_side_effect at a getc/putc stream argument with a side effect,
    // putenv_automatic at a putenv argument that is a local array, and
    // unchecked_setuid at a uid or gid change whose result is thrown away.
    void note_signal_calls(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return;
        SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
        if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) return;
        llvm::StringRef name = callee->getName();
        if (name == "signal" && call->getNumArgs() == 2) {
            signal_calls_.push_back(call->getBeginLoc());
            if (const FunctionDecl *handler = named_function(call->getArg(1))) {
                signal_handlers_.insert(handler->getCanonicalDecl());
                std::string number = expansion_text(call->getArg(0));
                if (number == "SIGFPE" || number == "SIGILL" || number == "SIGSEGV" || number == "SIGBUS") {
                    fault_handlers_.insert(handler->getCanonicalDecl());
                }
            }
        }
        if ((name == "atexit" || name == "at_quick_exit") && call->getNumArgs() == 1) {
            if (const FunctionDecl *handler = named_function(call->getArg(0))) exit_handlers_.insert(handler->getCanonicalDecl());
        }
        int stream = -1;
        if ((name == "getc" || name == "getwc") && call->getNumArgs() == 1) stream = 0;
        if ((name == "putc" || name == "putwc") && call->getNumArgs() == 2) stream = 1;
        if (stream >= 0) {
            const Expr *arg = call->getArg(static_cast<unsigned>(stream));
            if (!arg->isValueDependent() && arg->HasSideEffects(context_)) add_ast_mark("stream_side_effect", sm_.getFileLoc(arg->getBeginLoc()));
        }
        if (name == "putenv" && call->getNumArgs() == 1) {
            const Expr *arg = call->getArg(0)->IgnoreParenImpCasts();
            if (const auto *address = dyn_cast<UnaryOperator>(arg)) {
                if (address->getOpcode() == UO_AddrOf) {
                    if (const auto *element = dyn_cast<ArraySubscriptExpr>(address->getSubExpr()->IgnoreParens())) arg = element->getBase()->IgnoreParenImpCasts();
                }
            }
            const auto *ref = dyn_cast<DeclRefExpr>(arg);
            const auto *var = ref != nullptr ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
            if (var != nullptr && var->hasLocalStorage() && var->getType()->isArrayType()) {
                add_ast_mark("putenv_automatic", sm_.getFileLoc(call->getArg(0)->getBeginLoc()));
            }
        }
        static const std::set<std::string> drops = {"setuid", "seteuid", "setreuid", "setresuid", "setgid", "setegid", "setregid", "setresgid", "setgroups"};
        if (drops.count(name.str()) > 0 && result_dropped(call)) {
            add_ast_mark("unchecked_setuid", call->getBeginLoc());
        }
    }

    // A call whose value is cast to void or used as a statement.
    bool result_dropped(const CallExpr *call) const {
        DynTypedNode node = DynTypedNode::create(*static_cast<const Stmt *>(call));
        for (int depth = 0; depth < 6; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return false;
            const Stmt *parent = parents[0].get<Stmt>();
            if (parent == nullptr) return false;
            if (isa<ParenExpr>(parent) || isa<ImplicitCastExpr>(parent)) {
                node = parents[0];
                continue;
            }
            if (const auto *cast = dyn_cast<ExplicitCastExpr>(parent)) return cast->getType()->isVoidType();
            return isa<CompoundStmt>(parent);
        }
        return false;
    }

    // buffer[strlen(buffer) - 1] on a buffer filled by fgets (collected for
    // finish_signals), and the reads that check the length first.
    void note_fgets_index(const Stmt *stmt) {
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            const FunctionDecl *callee = call->getDirectCallee();
            if (system_function(callee, "fgets") && call->getNumArgs() == 3) {
                if (const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts())) fgets_buffers_.insert(ref->getDecl());
            }
            return;
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (binary->isComparisonOp() || binary->isLogicalOp()) {
                for (const Expr *side : {binary->getLHS(), binary->getRHS()}) {
                    if (const ValueDecl *decl = strlen_of(side)) length_checked_.insert(decl);
                }
            }
            return;
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->getOpcode() == UO_Deref || unary->getOpcode() == UO_LNot) {
                const auto *ref = dyn_cast<DeclRefExpr>(unary->getSubExpr()->IgnoreParenImpCasts());
                if (ref != nullptr) length_checked_.insert(ref->getDecl());
                if (const ValueDecl *decl = strlen_of(unary->getSubExpr())) length_checked_.insert(decl);
            }
            return;
        }
        const Expr *cond = nullptr;
        if (const auto *branch = dyn_cast<IfStmt>(stmt)) cond = branch->getCond();
        if (const auto *loop = dyn_cast<WhileStmt>(stmt)) cond = loop->getCond();
        if (const auto *loop = dyn_cast<ForStmt>(stmt)) cond = loop->getCond();
        if (const auto *choice = dyn_cast<ConditionalOperator>(stmt)) cond = choice->getCond();
        if (cond != nullptr) {
            if (const ValueDecl *decl = strlen_of(cond)) length_checked_.insert(decl);
            return;
        }
        const auto *element = dyn_cast<ArraySubscriptExpr>(stmt);
        if (element == nullptr) return;
        const auto *base = dyn_cast<DeclRefExpr>(element->getBase()->IgnoreParenImpCasts());
        if (base == nullptr) return;
        const Expr *index = element->getIdx()->IgnoreParenImpCasts();
        Expr::EvalResult value;
        if (!index->isValueDependent() && index->EvaluateAsInt(value, context_)) {
            if (value.Val.getInt() == 0) length_checked_.insert(base->getDecl());
            return;
        }
        const auto *minus = dyn_cast<BinaryOperator>(index);
        if (minus == nullptr || minus->getOpcode() != BO_Sub || strlen_of(minus->getLHS()) != base->getDecl()) return;
        const Expr *one = minus->getRHS();
        if (one->isValueDependent() || !one->EvaluateAsInt(value, context_) || value.Val.getInt() != 1) return;
        fgets_indexes_.push_back({base->getDecl(), element->getIdx()->getBeginLoc()});
    }

    // The variable of strlen(variable), or null.
    const ValueDecl *strlen_of(const Expr *expr) const {
        const auto *call = dyn_cast<CallExpr>(expr->IgnoreParenImpCasts());
        if (call == nullptr || call->getNumArgs() != 1 || !system_function(call->getDirectCallee(), "strlen")) return nullptr;
        const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
        return ref != nullptr ? ref->getDecl() : nullptr;
    }

    // At the end of the unit: signal_in_threads at signal calls when the unit
    // creates threads; exit_in_handler at exit and jumps in atexit handlers;
    // signal_in_handler at a signal call in a handler that installs a function;
    // fault_handler_returns at the closing brace of a SIGFPE/SIGILL/SIGSEGV/SIGBUS
    // handler that always returns; fgets_last_char at unchecked
    // buffer[strlen(buffer) - 1].
    void finish_signals() {
        if (creates_threads_) {
            for (SourceLocation call : signal_calls_) add_ast_mark("signal_in_threads", call);
        }
        for (const FunctionDecl *handler : exit_handlers_) {
            const FunctionDecl *defined = nullptr;
            if (!handler->hasBody(defined)) continue;
            std::vector<const CallExpr *> calls;
            calls_in(defined->getBody(), calls);
            for (const CallExpr *call : calls) {
                const FunctionDecl *callee = call->getDirectCallee();
                for (const char *name : {"exit", "quick_exit", "longjmp", "siglongjmp", "_longjmp"}) {
                    if (system_function(callee, name)) add_ast_mark("exit_in_handler", call->getBeginLoc());
                }
            }
        }
        for (const FunctionDecl *handler : signal_handlers_) {
            const FunctionDecl *defined = nullptr;
            if (!handler->hasBody(defined)) continue;
            std::vector<const CallExpr *> calls;
            calls_in(defined->getBody(), calls);
            for (const CallExpr *call : calls) {
                if (call->getNumArgs() == 2 && system_function(call->getDirectCallee(), "signal") && named_function(call->getArg(1)) != nullptr) {
                    add_ast_mark("signal_in_handler", call->getBeginLoc());
                }
            }
        }
        for (const FunctionDecl *handler : fault_handlers_) {
            const FunctionDecl *defined = nullptr;
            if (!handler->hasBody(defined)) continue;
            const auto *body = dyn_cast<CompoundStmt>(defined->getBody());
            if (body == nullptr || may_not_return(body, 0)) continue;
            add_ast_mark("fault_handler_returns", body->getRBracLoc());
        }
        for (const auto &entry : fgets_indexes_) {
            if (fgets_buffers_.count(entry.first) > 0 && length_checked_.count(entry.first) == 0) add_ast_mark("fgets_last_char", entry.second);
        }
    }

    // A statement and its ancestors up to the function body.
    std::vector<const Stmt *> stmt_chain(const Stmt *stmt) const {
        std::vector<const Stmt *> chain{stmt};
        DynTypedNode node = DynTypedNode::create(*stmt);
        for (int depth = 0; depth < 256; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) break;
            const Stmt *parent = parents[0].get<Stmt>();
            if (parent == nullptr) break;
            chain.push_back(parent);
            node = parents[0];
        }
        return chain;
    }

    // Whether first runs before second on one path of the same function: they
    // meet in a block (first in an earlier statement), in the left side of &&,
    // || or comma, or first in the condition of an if whose branch holds second.
    bool sequenced_before(const Stmt *first, const Stmt *second) const {
        std::vector<const Stmt *> one = stmt_chain(first);
        std::vector<const Stmt *> two = stmt_chain(second);
        for (size_t at = 1; at < one.size(); ++at) {
            auto found = std::find(two.begin(), two.end(), one[at]);
            if (found == two.end()) continue;
            if (found == two.begin()) return false;
            const Stmt *meet = one[at];
            const Stmt *left = one[at - 1];
            const Stmt *right = *(found - 1);
            if (left == right) return false;
            if (const auto *block = dyn_cast<CompoundStmt>(meet)) {
                for (const Stmt *child : block->body()) {
                    if (child == left) return true;
                    if (child == right) return false;
                }
                return false;
            }
            if (const auto *binary = dyn_cast<BinaryOperator>(meet)) {
                bool ordered = binary->getOpcode() == BO_LAnd || binary->getOpcode() == BO_LOr || binary->getOpcode() == BO_Comma;
                return ordered && left == binary->getLHS() && right == binary->getRHS();
            }
            if (const auto *branch = dyn_cast<IfStmt>(meet)) {
                return left == branch->getCond() && (right == branch->getThen() || right == branch->getElse());
            }
            return false;
        }
        return false;
    }

    // A library function by name that this unit only declares (also when the
    // user's code redeclares it).
    bool declared_library(const FunctionDecl *callee, const std::set<std::string> &names) const {
        if (callee == nullptr || callee->getIdentifier() == nullptr || names.count(callee->getName().str()) == 0) return false;
        if (callee->hasBody() || isa<CXXMethodDecl>(callee)) return false;
        return callee->getDeclContext()->getRedeclContext()->isTranslationUnit() || callee->isInStdNamespace();
    }

    // Batch 33 facts: privilege changes (uid_calls_, gid_calls_), environment
    // changes and envp uses, constant srand seeds (constant_seed), asctime of a
    // local struct tm, writes through pointers set from a string literal.
    void note_process_calls(const Stmt *stmt) {
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            const auto *param = dyn_cast<ParmVarDecl>(ref->getDecl());
            const auto *owner = param != nullptr ? dyn_cast<FunctionDecl>(param->getDeclContext()) : nullptr;
            if (owner != nullptr && owner->isMain() && param->getFunctionScopeIndex() == 2) envp_refs_.push_back(ref);
            return;
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (binary->isComparisonOp()) {
                for (const Expr *side : {binary->getLHS(), binary->getRHS()}) {
                    const auto *member = dyn_cast<MemberExpr>(side->IgnoreParenImpCasts());
                    const auto *base = member != nullptr ? dyn_cast<DeclRefExpr>(member->getBase()->IgnoreParenImpCasts()) : nullptr;
                    if (base != nullptr) fields_checked_.insert(base->getDecl());
                }
            }
            if (binary->isAssignmentOp()) note_literal_write(binary->getLHS(), binary->getLHS());
            return;
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->isIncrementDecrementOp()) note_literal_write(unary->getSubExpr(), unary);
            if (unary->getOpcode() == UO_AddrOf) {
                if (const auto *ref = dyn_cast<DeclRefExpr>(unary->getSubExpr()->IgnoreParenImpCasts())) literal_unsure_.insert(ref->getDecl());
            }
            return;
        }
        if (const auto *var = dyn_cast<DeclStmt>(stmt)) {
            for (const Decl *decl : var->decls()) {
                const auto *local = dyn_cast<VarDecl>(decl);
                if (local == nullptr || !local->hasLocalStorage() || !local->getType()->isPointerType() || local->getInit() == nullptr) continue;
                if (isa<StringLiteral>(local->getInit()->IgnoreParenCasts())) literal_pointers_.insert(local);
            }
            return;
        }
        const auto *call = dyn_cast<CallExpr>(stmt);
        if (call == nullptr) return;
        const FunctionDecl *callee = call->getDirectCallee();
        static const std::set<std::string> uid = {"setuid", "seteuid", "setreuid", "setresuid"};
        static const std::set<std::string> gid = {"setgid", "setegid", "setregid", "setresgid", "setgroups"};
        static const std::set<std::string> env = {"setenv", "putenv", "unsetenv", "clearenv"};
        static const std::set<std::string> writers = {"strcpy", "strncpy", "strcat", "strncat", "strtok", "memset", "memcpy", "memmove", "sprintf", "snprintf", "gets", "fgets"};
        if (declared_library(callee, uid)) uid_calls_.push_back(call);
        if (declared_library(callee, gid)) gid_calls_.push_back(call);
        if (declared_library(callee, env)) env_calls_.push_back(call);
        if (call->getNumArgs() == 1 && system_function(callee, "srand")) {
            const Expr *seed = call->getArg(0);
            Expr::EvalResult value;
            if (!seed->isValueDependent() && seed->EvaluateAsInt(value, context_)) add_ast_mark("constant_seed", sm_.getFileLoc(seed->getBeginLoc()));
        }
        if (call->getNumArgs() == 1 && system_function(callee, "asctime")) {
            const auto *address = dyn_cast<UnaryOperator>(call->getArg(0)->IgnoreParenImpCasts());
            const auto *ref = address != nullptr && address->getOpcode() == UO_AddrOf ? dyn_cast<DeclRefExpr>(address->getSubExpr()->IgnoreParenImpCasts()) : nullptr;
            if (ref != nullptr && isa<VarDecl>(ref->getDecl())) asctime_args_.push_back({ref->getDecl(), call->getArg(0)->getBeginLoc()});
        }
        if (call->getNumArgs() >= 1 && callee != nullptr && callee->getIdentifier() != nullptr && writers.count(callee->getName().str()) > 0 &&
            system_function(callee, callee->getName().str().c_str())) {
            const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
            if (ref != nullptr) literal_writes_.push_back({ref->getDecl(), call->getArg(0)->getBeginLoc()});
        }
    }

    // A write to p[i] or *p (at where), or a new value for p itself.
    void note_literal_write(const Expr *target, const Expr *where) {
        target = target->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(target)) {
            literal_unsure_.insert(ref->getDecl());
            return;
        }
        const Expr *base = nullptr;
        if (const auto *element = dyn_cast<ArraySubscriptExpr>(target)) base = element->getBase();
        if (const auto *unary = dyn_cast<UnaryOperator>(target)) {
            if (unary->getOpcode() == UO_Deref) base = unary->getSubExpr();
        }
        const auto *ref = base != nullptr ? dyn_cast<DeclRefExpr>(base->IgnoreParenImpCasts()) : nullptr;
        if (ref != nullptr) literal_writes_.push_back({ref->getDecl(), where->getBeginLoc()});
    }

    // An object a signal handler may touch: volatile sig_atomic_t, an atomic
    // type, or a const object.
    static bool handler_safe_object(QualType type) {
        if (type.isConstQualified() || type->isAtomicType()) return true;
        if (std_class(type, "atomic") || std_class(type, "atomic_flag")) return true;
        if (!type.isVolatileQualified()) return false;
        for (QualType step = type; !step.isNull();) {
            const auto *named = step->getAs<TypedefType>();
            if (named == nullptr) break;
            if (named->getDecl()->getName() == "sig_atomic_t") return true;
            step = named->getDecl()->getUnderlyingType();
        }
        return false;
    }

    // The references and calls of a statement, outside lambdas, blocks and
    // unevaluated operands.
    static void refs_and_calls(const Stmt *stmt, std::vector<const DeclRefExpr *> &refs, std::vector<const CallExpr *> &calls) {
        if (stmt == nullptr || isa<LambdaExpr>(stmt) || isa<BlockExpr>(stmt) || isa<UnaryExprOrTypeTraitExpr>(stmt)) return;
        if (const auto *address = dyn_cast<UnaryOperator>(stmt)) {
            if (address->getOpcode() == UO_AddrOf && isa<DeclRefExpr>(address->getSubExpr()->IgnoreParens())) return;
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) refs.push_back(ref);
        if (const auto *call = dyn_cast<CallExpr>(stmt)) calls.push_back(call);
        for (const Stmt *child : stmt->children()) refs_and_calls(child, refs, calls);
    }

    // The C and POSIX async-signal-safe functions.
    static bool async_safe(llvm::StringRef name) {
        static const std::set<std::string> safe = {
            "_Exit", "_exit", "abort", "quick_exit", "signal", "accept", "access", "aio_error", "aio_return", "aio_suspend",
            "alarm", "bind", "cfgetispeed", "cfgetospeed", "cfsetispeed", "cfsetospeed", "chdir", "chmod", "chown",
            "clock_gettime", "close", "connect", "creat", "dup", "dup2", "dup3", "execl", "execle", "execv", "execve",
            "faccessat", "fchdir", "fchmod", "fchmodat", "fchown", "fchownat", "fcntl", "fdatasync", "fexecve", "ffs",
            "fork", "_Fork", "fstat", "fstatat", "fsync", "ftruncate", "futimens", "getegid", "geteuid", "getgid",
            "getgroups", "getpeername", "getpgrp", "getpid", "getppid", "getsockname", "getsockopt", "getuid", "htonl",
            "htons", "kill", "link", "linkat", "listen", "longjmp", "lseek", "lstat", "memccpy", "memchr", "memcmp",
            "memcpy", "memmove", "memset", "mkdir", "mkdirat", "mkfifo", "mkfifoat", "mknod", "mknodat", "ntohl",
            "ntohs", "open", "openat", "pause", "pipe", "pipe2", "poll", "pselect", "pthread_kill", "pthread_self",
            "pthread_sigmask", "raise", "read", "readlink", "readlinkat", "recv", "recvfrom", "recvmsg", "rename",
            "renameat", "rmdir", "select", "sem_post", "send", "sendmsg", "sendto", "setgid", "setpgid", "setsid",
            "setsockopt", "setuid", "shutdown", "sigaction", "sigaddset", "sigdelset", "sigemptyset", "sigfillset",
            "sigismember", "siglongjmp", "sigpause", "sigpending", "sigprocmask", "sigqueue", "sigset", "sigsuspend",
            "sleep", "sockatmark", "socket", "socketpair", "stat", "stpcpy", "stpncpy", "strcat", "strchr", "strcmp",
            "strcpy", "strcspn", "strlen", "strncat", "strncmp", "strncpy", "strnlen", "strpbrk", "strrchr", "strspn",
            "strstr", "strtok_r", "symlink", "symlinkat", "tcdrain", "tcflow", "tcflush", "tcgetattr", "tcgetpgrp",
            "tcsendbreak", "tcsetattr", "tcsetpgrp", "time", "timer_getoverrun", "timer_gettime", "timer_settime",
            "times", "umask", "uname", "unlink", "unlinkat", "utime", "utimensat", "utimes", "wait", "waitpid",
            "wcpcpy", "wcpncpy", "wcscat", "wcschr", "wcscmp", "wcscpy", "wcscspn", "wcslen", "wcsncat", "wcsncmp",
            "wcsncpy", "wcsnlen", "wcspbrk", "wcsrchr", "wcsspn", "wcsstr", "wcstok", "wmemchr", "wmemcmp", "wmemcpy",
            "wmemmove", "wmemset", "write", "__errno_location", "__error", "_longjmp", "__sigsetjmp"};
        return safe.count(name.str()) > 0;
    }

    // At the end of the unit: privilege order, envp after an environment
    // change, asctime of unchecked fields, writes into string literals, and what
    // signal handlers call and touch.
    void finish_process_calls() {
        for (const CallExpr *group : gid_calls_) {
            for (const CallExpr *user : uid_calls_) {
                if (sequenced_before(user, group)) {
                    add_ast_mark("late_group_drop", group->getBeginLoc());
                    break;
                }
            }
        }
        for (const DeclRefExpr *ref : envp_refs_) {
            for (const CallExpr *change : env_calls_) {
                if (sequenced_before(change, ref)) {
                    add_ast_mark("stale_envp", ref->getBeginLoc());
                    break;
                }
            }
        }
        for (const auto &entry : asctime_args_) {
            if (fields_checked_.count(entry.first) == 0) add_ast_mark("unchecked_asctime", entry.second);
        }
        for (const auto &entry : literal_writes_) {
            const auto *var = dyn_cast<VarDecl>(entry.first);
            if (var != nullptr && literal_pointers_.count(var) > 0 && literal_unsure_.count(var) == 0) add_ast_mark("literal_write", entry.second);
        }
        for (const FunctionDecl *handler : signal_handlers_) {
            const FunctionDecl *defined = nullptr;
            if (!handler->hasBody(defined)) continue;
            std::vector<const DeclRefExpr *> refs;
            std::vector<const CallExpr *> calls;
            refs_and_calls(defined->getBody(), refs, calls);
            for (const DeclRefExpr *ref : refs) {
                const auto *var = dyn_cast<VarDecl>(ref->getDecl());
                if (var != nullptr && var->hasGlobalStorage() && !handler_safe_object(var->getType())) add_ast_mark("handler_shared_object", ref->getBeginLoc());
            }
            for (const CallExpr *call : calls) {
                const FunctionDecl *callee = call->getDirectCallee();
                if (callee == nullptr || callee->getIdentifier() == nullptr || isa<CXXMethodDecl>(callee)) continue;
                unsigned builtin = callee->getBuiltinID();
                if (builtin != 0 && !context_.BuiltinInfo.isLibFunction(builtin) && !context_.BuiltinInfo.isPredefinedLibFunction(builtin)) continue;
                SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
                if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) continue;
                if (!async_safe(callee->getName())) add_ast_mark("unsafe_in_handler", call->getBeginLoc());
            }
        }
    }

    // Whether a function is known to let an exception out: it is not nothrow
    // and its body (looked at up to a depth) has a throwing point, or it is a
    // library function of C++ (not extern "C") with no body in the unit.
    // Functions of the user's without a body here are not known (phase 2).
    bool known_throwing(const FunctionDecl *fn, int depth) {
        if (fn == nullptr) return false;
        const auto *proto = fn->getType()->getAs<FunctionProtoType>();
        if (proto != nullptr && !isUnresolvedExceptionSpec(proto->getExceptionSpecType()) && proto->isNothrow()) return false;
        if (fn->getBuiltinID() != 0) return false;
        const FunctionDecl *canonical = fn->getCanonicalDecl();
        auto known = throwing_memo_.find(canonical);
        if (known != throwing_memo_.end()) return known->second;
        const FunctionDecl *defined = nullptr;
        bool result = false;
        if (fn->hasBody(defined) && defined->getBody() != nullptr) {
            if (depth >= 10) return false;
            throwing_memo_[canonical] = false;
            std::vector<const Stmt *> points;
            throwing_points(defined->getBody(), depth + 1, points, true);
            if (const auto *ctor = dyn_cast<CXXConstructorDecl>(defined)) {
                for (const CXXCtorInitializer *init : ctor->inits()) {
                    if (points.empty() && init->getInit() != nullptr && init->isWritten()) throwing_points(init->getInit(), depth + 1, points, true);
                }
            }
            result = !points.empty();
        } else {
            SourceLocation declared = sm_.getSpellingLoc(fn->getLocation());
            bool library = declared.isValid() && sm_.isInSystemHeader(declared);
            result = library && !fn->isExternC() && !fn->isInExternCContext();
        }
        throwing_memo_[canonical] = result;
        return result;
    }

    // The outermost points of a statement that are known to throw: a throw, a
    // new that is not nothrow, a call or construction of a known throwing
    // function.  Try blocks (with any handler), lambdas and unevaluated
    // operands are skipped.  With first_only it stops at the first point.
    void throwing_points(const Stmt *stmt, int depth, std::vector<const Stmt *> &out, bool first_only) {
        if (stmt == nullptr || (first_only && !out.empty())) return;
        if (isa<LambdaExpr>(stmt) || isa<BlockExpr>(stmt) || isa<UnaryExprOrTypeTraitExpr>(stmt) || isa<CXXNoexceptExpr>(stmt) ||
            isa<CXXTypeidExpr>(stmt) || isa<DeclRefExpr>(stmt)) {
            return;
        }
        if (const auto *attempt = dyn_cast<CXXTryStmt>(stmt)) {
            for (unsigned at = 0; at < attempt->getNumHandlers(); ++at) throwing_points(attempt->getHandler(at)->getHandlerBlock(), depth, out, first_only);
            return;
        }
        bool point = false;
        if (isa<CXXThrowExpr>(stmt)) point = true;
        if (const auto *fresh = dyn_cast<CXXNewExpr>(stmt)) {
            const FunctionDecl *op = fresh->getOperatorNew();
            point = op != nullptr && !fresh->shouldNullCheckAllocation() && known_throwing(op, depth);
        }
        if (const auto *call = dyn_cast<CallExpr>(stmt)) point = known_throwing(call->getDirectCallee(), depth);
        if (const auto *make = dyn_cast<CXXConstructExpr>(stmt)) {
            if (!make->getConstructor()->isTrivial()) point = known_throwing(make->getConstructor(), depth);
        }
        if (point) {
            out.push_back(stmt);
            return;
        }
        for (const Stmt *child : stmt->children()) throwing_points(child, depth, out, first_only);
    }

    // C++ functions written noexcept (or throw()) with a throwing point
    // (noexcept_throw at each), and destructors with one (destructor_throw).
    void note_throwing_bodies(const Decl *decl) {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !context_.getLangOpts().CPlusPlus || !context_.getLangOpts().CXXExceptions) return;
        if (!fn->doesThisDeclarationHaveABody() || fn->isDependentContext() || fn->isImplicit() || fn->isDefaulted()) return;
        if (fn->isTemplateInstantiation() || !user_location(sm_, fn->getLocation())) return;
        const auto *proto = fn->getType()->getAs<FunctionProtoType>();
        if (proto == nullptr) return;
        const char *code = nullptr;
        if (isa<CXXDestructorDecl>(fn)) {
            code = "destructor_throw";
        } else {
            ExceptionSpecificationType spec = proto->getExceptionSpecType();
            if (spec == EST_BasicNoexcept || spec == EST_NoexceptTrue || spec == EST_DynamicNone) code = "noexcept_throw";
        }
        if (code == nullptr) return;
        std::vector<const Stmt *> points;
        throwing_points(fn->getBody(), 0, points, false);
        for (const Stmt *point : points) add_ast_mark(code, point->getBeginLoc());
    }

    // A variable of static storage outside functions whose initialiser can
    // throw before main (not constant-initialised, not thread_local).
    bool throwing_static(const Decl *decl) {
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || !context_.getLangOpts().CPlusPlus || !context_.getLangOpts().CXXExceptions) return false;
        if (!var->hasGlobalStorage() || var->isLocalVarDecl() || var->getTLSKind() != VarDecl::TLS_None || var->isConstexpr()) return false;
        if (!var->isThisDeclarationADefinition() || var->getInit() == nullptr || var->getType()->isDependentType()) return false;
        if (var->isTemplated() || isa<VarTemplateSpecializationDecl>(var) || var->isStaticDataMember() && var->getTemplateInstantiationPattern() != nullptr) return false;
        if (!user_location(sm_, var->getLocation())) return false;
        if (var->hasConstantInitialization()) return false;
        std::vector<const Stmt *> points;
        throwing_points(var->getInit(), 0, points, true);
        return !points.empty();
    }

    // A local std::thread that is never named again: it is destroyed while
    // joinable.
    bool unjoined_thread(const Decl *decl) const {
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || !var->isLocalVarDecl() || var->isStaticLocal() || var->isReferenced()) return false;
        if (!std_class(var->getType(), "thread") || var->getType()->isReferenceType()) return false;
        const auto *make = var->getInit() != nullptr ? dyn_cast<CXXConstructExpr>(var->getInit()->IgnoreImplicit()) : nullptr;
        if (make == nullptr || make->getNumArgs() == 0 || make->getConstructor()->isDefaultConstructor()) return false;
        return user_location(sm_, var->getLocation());
    }

    // fsetpos and fgetpos positions (finish_process_calls), and sin_port or
    // sin6_port given a host-order value: a non-zero constant or a parameter
    // not of type in_port_t (mark host_order_port at the value).
    void note_stream_positions(const Stmt *stmt) {
        if (const auto *assign = dyn_cast<BinaryOperator>(stmt)) {
            if (assign->getOpcode() != BO_Assign) return;
            const auto *member = dyn_cast<MemberExpr>(assign->getLHS()->IgnoreParenImpCasts());
            const auto *field = member != nullptr ? dyn_cast<FieldDecl>(member->getMemberDecl()) : nullptr;
            if (field == nullptr || field->getIdentifier() == nullptr || (field->getName() != "sin_port" && field->getName() != "sin6_port")) return;
            SourceLocation declared = sm_.getSpellingLoc(field->getLocation());
            if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) return;
            const Expr *value = assign->getRHS()->IgnoreParenImpCasts();
            if (isa<CallExpr>(value) || expansion_text(assign->getRHS()).find("hton") != std::string::npos) return;
            bool host = false;
            Expr::EvalResult constant;
            if (!value->isValueDependent() && value->EvaluateAsInt(constant, context_)) {
                host = constant.Val.getInt() != 0;
            } else if (const auto *ref = dyn_cast<DeclRefExpr>(value)) {
                const auto *param = dyn_cast<ParmVarDecl>(ref->getDecl());
                bool network = false;
                if (param != nullptr) {
                    for (QualType step = param->getType(); !step.isNull();) {
                        const auto *named = step->getAs<TypedefType>();
                        if (named == nullptr) break;
                        if (named->getDecl()->getName() == "in_port_t") network = true;
                        step = named->getDecl()->getUnderlyingType();
                    }
                }
                host = param != nullptr && !network;
            }
            if (host) add_ast_mark("host_order_port", assign->getRHS()->getBeginLoc());
            return;
        }
        const auto *call = dyn_cast<CallExpr>(stmt);
        if (call == nullptr || call->getNumArgs() != 2) return;
        const FunctionDecl *callee = call->getDirectCallee();
        bool set = system_function(callee, "fsetpos");
        bool get = system_function(callee, "fgetpos");
        if (!set && !get) return;
        const auto *address = dyn_cast<UnaryOperator>(call->getArg(1)->IgnoreParenImpCasts());
        const auto *place = address != nullptr && address->getOpcode() == UO_AddrOf ? dyn_cast<DeclRefExpr>(address->getSubExpr()->IgnoreParenImpCasts()) : nullptr;
        if (place == nullptr) return;
        const auto *stream = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
        const ValueDecl *from = stream != nullptr ? stream->getDecl() : nullptr;
        if (set) position_sets_.push_back({place->getDecl(), from, call->getArg(1)->getBeginLoc()});
        if (get) position_gets_.push_back({place->getDecl(), from});
    }

    // fsetpos of a position that no fgetpos of the same stream filled.
    void finish_positions() {
        for (const PositionUse &use : position_sets_) {
            bool filled = false;
            for (const auto &get : position_gets_) {
                if (get.first != use.position) continue;
                if (get.second == nullptr || use.stream == nullptr || get.second == use.stream) filled = true;
            }
            if (!filled) add_ast_mark("foreign_position", use.where);
        }
    }

    // The parameter of a function template (or generic lambda) declared T&&
    // with T a template type parameter of that same function: a forwarding
    // reference.
    static bool forwarding_param(const ValueDecl *decl) {
        const auto *param = dyn_cast_or_null<ParmVarDecl>(decl);
        if (param == nullptr) return false;
        const auto *ref = param->getType()->getAs<RValueReferenceType>();
        if (ref == nullptr) return false;
        QualType pointee = ref->getPointeeTypeAsWritten();
        if (pointee.hasQualifiers()) return false;
        const auto *parm_type = pointee->getAs<TemplateTypeParmType>();
        if (parm_type == nullptr) return false;
        const auto *fn = dyn_cast<FunctionDecl>(param->getDeclContext());
        const FunctionTemplateDecl *tmpl = fn != nullptr ? fn->getDescribedFunctionTemplate() : nullptr;
        if (tmpl == nullptr) return false;
        const TemplateParameterList *own = tmpl->getTemplateParameters();
        return parm_type->getDepth() == own->getDepth() && parm_type->getIndex() < own->size() &&
               isa<TemplateTypeParmDecl>(own->getParam(parm_type->getIndex()));
    }

    // Batch 35 statement facts: a forwarding reference passed on without
    // std::forward (mark unforwarded), throw operands that can throw
    // (throwing_operand), atoi-style conversions and strto* without an end
    // pointer (unchecked_conversion), device names opened (device_path),
    // narrow/wide strings cast into the other family (wrong_width_string),
    // and the facts of readlink, stat/open and fgets failure.
    void note_batch35(const Stmt *stmt) {
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            if (!forwarding_param(ref->getDecl())) return;
            auto parents = context_.getParents(*ref);
            if (parents.size() != 1) return;
            const auto *call = parents[0].get<CallExpr>();
            const auto *make = parents[0].get<CXXConstructExpr>();
            const auto *unresolved = parents[0].get<CXXUnresolvedConstructExpr>();
            bool argument = false;
            if (call != nullptr && !isa<CXXOperatorCallExpr>(call) && call->getCallee() != ref) {
                for (const Expr *arg : call->arguments()) argument = argument || arg == ref;
                std::string callee = expansion_text(call->getCallee());
                if (callee.find("forward") != std::string::npos || callee.find("move") != std::string::npos) argument = false;
            }
            if (make != nullptr && make->getNumArgs() > 0 && !make->isElidable()) {
                for (const Expr *arg : make->arguments()) argument = argument || arg == ref;
            }
            if (unresolved != nullptr) {
                for (const Expr *arg : unresolved->arguments()) argument = argument || arg == ref;
            }
            if (argument && single_plain_use(ref)) add_ast_mark("unforwarded", ref->getBeginLoc());
            return;
        }
        if (const auto *raise = dyn_cast<CXXThrowExpr>(stmt)) {
            if (!context_.getLangOpts().CXXExceptions || raise->getSubExpr() == nullptr || raise->isTypeDependent()) return;
            const Expr *operand = raise->getSubExpr()->IgnoreImplicit();
            while (true) {
                if (const auto *cast = dyn_cast<CXXFunctionalCastExpr>(operand)) { operand = cast->getSubExpr()->IgnoreImplicit(); continue; }
                if (const auto *bind = dyn_cast<CXXBindTemporaryExpr>(operand)) { operand = bind->getSubExpr()->IgnoreImplicit(); continue; }
                if (const auto *temp = dyn_cast<MaterializeTemporaryExpr>(operand)) { operand = temp->getSubExpr()->IgnoreImplicit(); continue; }
                break;
            }
            std::vector<const Stmt *> points;
            const auto *make = dyn_cast<CXXConstructExpr>(operand);
            const CXXRecordDecl *record = make != nullptr ? make->getConstructor()->getParent() : nullptr;
            if (record != nullptr && record->isInStdNamespace()) {
                for (const Expr *arg : make->arguments()) throwing_points(arg, 0, points, true);
            } else {
                throwing_points(operand, 0, points, true);
            }
            if (!points.empty()) add_ast_mark("throwing_operand", raise->getSubExpr()->getBeginLoc());
            return;
        }
        if (const auto *assign = dyn_cast<BinaryOperator>(stmt)) {
            if (!assign->isAssignmentOp()) return;
            const Expr *target = assign->getLHS()->IgnoreParenImpCasts();
            const Expr *base = nullptr;
            if (const auto *element = dyn_cast<ArraySubscriptExpr>(target)) base = element->getBase();
            if (const auto *deref = dyn_cast<UnaryOperator>(target)) {
                if (deref->getOpcode() == UO_Deref) base = deref->getSubExpr();
            }
            const auto *ref = base != nullptr ? dyn_cast<DeclRefExpr>(base->IgnoreParenImpCasts()) : nullptr;
            if (ref != nullptr) buffer_writes_.push_back({ref->getDecl(), assign});
            return;
        }
        if (const auto *branch = dyn_cast<IfStmt>(stmt)) {
            note_fgets_failure(branch);
            return;
        }
        const auto *call = dyn_cast<CallExpr>(stmt);
        if (call == nullptr) return;
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return;
        SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
        if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) return;
        llvm::StringRef name = callee->getName();
        if (name == "atoi" || name == "atol" || name == "atoll" || name == "atof") add_ast_mark("unchecked_conversion", call->getBeginLoc());
        static const std::set<std::string> strto = {"strtol", "strtoll", "strtoul", "strtoull", "strtod", "strtof", "strtold", "strtoimax", "strtoumax",
                                                    "wcstol", "wcstoll", "wcstoul", "wcstoull", "wcstod", "wcstof", "wcstold"};
        if (strto.count(name.str()) > 0 && call->getNumArgs() >= 2) {
            const Expr *end = call->getArg(1);
            if (end->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull) add_ast_mark("unchecked_conversion", call->getBeginLoc());
        }
        if ((name == "fopen" || name == "open" || name == "freopen" || name == "open64" || name == "fopen64") && call->getNumArgs() >= 1) {
            const auto *text = dyn_cast<StringLiteral>(call->getArg(0)->IgnoreParenImpCasts());
            if (text != nullptr && text->getCharByteWidth() == 1 && device_name(text->getString())) add_ast_mark("device_path", call->getArg(0)->getBeginLoc());
        }
        bool narrow = name.starts_with("str");
        bool wide = name.starts_with("wcs");
        if (narrow || wide) {
            for (unsigned at = 0; at < call->getNumArgs(); ++at) {
                const auto *cast = dyn_cast<ExplicitCastExpr>(call->getArg(at)->IgnoreParenImpCasts());
                if (cast == nullptr || !cast->getType()->isPointerType()) continue;
                QualType from = cast->getSubExpr()->IgnoreParenImpCasts()->getType();
                if (!from->isPointerType()) continue;
                QualType held = from->getPointeeType().getCanonicalType().getUnqualifiedType();
                QualType made = cast->getType()->getPointeeType().getCanonicalType().getUnqualifiedType();
                bool held_wide = held->isWideCharType() || held->isChar16Type() || held->isChar32Type() || wide_typedef(from->getPointeeType());
                bool made_narrow = made->isCharType();
                bool held_narrow = held->isCharType();
                bool made_wide = made->isWideCharType() || wide_typedef(cast->getType()->getPointeeType());
                if ((narrow && made_narrow && held_wide) || (wide && made_wide && held_narrow)) add_ast_mark("wrong_width_string", cast->getSubExpr()->getBeginLoc());
            }
        }
        if (name == "readlink" && call->getNumArgs() == 3) {
            const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(1)->IgnoreParenImpCasts());
            if (ref != nullptr) readlinks_.push_back({ref->getDecl(), call});
        }
        if ((name == "stat" || name == "lstat") && call->getNumArgs() == 2) {
            const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
            if (ref != nullptr) path_checks_.push_back({ref->getDecl(), call});
        }
        if ((name == "open" || name == "fopen" || name == "open64" || name == "fopen64") && call->getNumArgs() >= 2) {
            const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
            bool nofollow = name.starts_with("open") && expansion_text(call->getArg(1)).find("O_NOFOLLOW") != std::string::npos;
            if (ref != nullptr && !nofollow) path_opens_.push_back({ref->getDecl(), call});
        }
        // a const char * argument: the buffer read as a string
        for (unsigned at = 0; at < call->getNumArgs() && at < callee->getNumParams(); ++at) {
            QualType param = callee->getParamDecl(at)->getType();
            if (!param->isPointerType() || !param->getPointeeType().isConstQualified() || !param->getPointeeType()->isCharType()) continue;
            const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(at)->IgnoreParenImpCasts());
            if (ref != nullptr) string_reads_.push_back({ref->getDecl(), ref});
        }
    }

    // A type spelled through the typedef wchar_t, char16_t or char32_t (C).
    static bool wide_typedef(QualType type) {
        for (QualType step = type; !step.isNull();) {
            const auto *named = step->getAs<TypedefType>();
            if (named == nullptr) return false;
            llvm::StringRef name = named->getDecl()->getName();
            if (name == "wchar_t" || name == "char16_t" || name == "char32_t") return true;
            step = named->getDecl()->getUnderlyingType();
        }
        return false;
    }

    // The only reference to its parameter in the function, not inside a loop
    // or a pack expansion (where forwarding would move the object twice).
    bool single_plain_use(const DeclRefExpr *ref) const {
        const auto *param = cast<ParmVarDecl>(ref->getDecl());
        const auto *fn = dyn_cast<FunctionDecl>(param->getDeclContext());
        if (fn == nullptr || fn->getBody() == nullptr || inside_loop(ref)) return false;
        std::vector<const Stmt *> work{fn->getBody()};
        int uses = 0;
        while (!work.empty()) {
            const Stmt *here = work.back();
            work.pop_back();
            if (here == nullptr) continue;
            if (const auto *other = dyn_cast<DeclRefExpr>(here)) {
                if (other->getDecl() == param) ++uses;
            }
            for (const Stmt *child : here->children()) work.push_back(child);
        }
        if (uses != 1) return false;
        DynTypedNode node = DynTypedNode::create(*static_cast<const Stmt *>(ref));
        for (int depth = 0; depth < 64; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return true;
            node = parents[0];
            if (node.get<PackExpansionExpr>() != nullptr || node.get<CXXFoldExpr>() != nullptr) return false;
            if (node.get<FunctionDecl>() != nullptr) return true;
        }
        return true;
    }

    // Literal names of devices: /dev/..., and the reserved DOS names.
    static bool device_name(llvm::StringRef path) {
        if (path.starts_with("/dev/")) return true;
        std::string lower = path.lower();
        size_t dot = lower.find('.');
        std::string stem = dot == std::string::npos ? lower : lower.substr(0, dot);
        static const std::set<std::string> dos = {"con", "nul", "prn", "aux", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3"};
        return dos.count(stem) > 0;
    }

    // if (fgets(b, ...) == NULL) or !fgets(...): the first use of b in the
    // failure branch before b is written (mark failed_fgets_buffer).
    void note_fgets_failure(const IfStmt *branch) {
        const Expr *cond = branch->getCond()->IgnoreParenImpCasts();
        const CallExpr *call = nullptr;
        bool failure_then = true;
        if (const auto *not_op = dyn_cast<UnaryOperator>(cond)) {
            if (not_op->getOpcode() == UO_LNot) call = dyn_cast<CallExpr>(not_op->getSubExpr()->IgnoreParenImpCasts());
        } else if (const auto *compare = dyn_cast<BinaryOperator>(cond)) {
            if (compare->getOpcode() == BO_EQ || compare->getOpcode() == BO_NE) {
                failure_then = compare->getOpcode() == BO_EQ;
                const Expr *left = compare->getLHS()->IgnoreParenImpCasts();
                const Expr *right = compare->getRHS()->IgnoreParenImpCasts();
                if (right->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull) call = dyn_cast<CallExpr>(left);
                else if (left->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull) call = dyn_cast<CallExpr>(right);
            }
        } else {
            call = dyn_cast<CallExpr>(cond);
            failure_then = false;
        }
        if (call == nullptr || call->getNumArgs() != 3) return;
        const FunctionDecl *callee = call->getDirectCallee();
        if (!system_function(callee, "fgets") && !system_function(callee, "fgetws")) return;
        const auto *buffer = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
        if (buffer == nullptr) return;
        const Stmt *failed = failure_then ? branch->getThen() : branch->getElse();
        if (failed == nullptr) return;
        std::vector<const Stmt *> steps;
        if (const auto *block = dyn_cast<CompoundStmt>(failed)) {
            for (const Stmt *child : block->body()) steps.push_back(child);
        } else {
            steps.push_back(failed);
        }
        for (const Stmt *step : steps) {
            std::vector<const DeclRefExpr *> refs;
            std::vector<const CallExpr *> calls;
            refs_and_calls(step, refs, calls);
            const DeclRefExpr *use = nullptr;
            for (const DeclRefExpr *ref : refs) {
                if (ref->getDecl() == buffer->getDecl()) {
                    use = ref;
                    break;
                }
            }
            if (use == nullptr) continue;
            if (const auto *assign = dyn_cast<BinaryOperator>(step->IgnoreContainers())) {
                if (assign->isAssignmentOp()) return;
            }
            for (const CallExpr *inner : calls) {
                const FunctionDecl *fn = inner->getDirectCallee();
                if (fn == nullptr || fn->getIdentifier() == nullptr || inner->getNumArgs() == 0) continue;
                static const std::set<std::string> writers = {"memset", "strcpy", "strncpy", "snprintf", "sprintf", "memcpy", "fgets", "strcat"};
                const auto *first = dyn_cast<DeclRefExpr>(inner->getArg(0)->IgnoreParenImpCasts());
                if (writers.count(fn->getName().str()) > 0 && first != nullptr && first->getDecl() == buffer->getDecl()) return;
            }
            add_ast_mark("failed_fgets_buffer", use->getBeginLoc());
            return;
        }
    }

    // readlink results read as strings without a write in between
    // (unterminated_link), opens of a path that stat/lstat looked at first
    // (checked_then_opened).
    void finish_batch35() {
        for (const auto &read : string_reads_) {
            for (const auto &link : readlinks_) {
                if (link.first != read.first || !sequenced_before(link.second, read.second)) continue;
                bool written = false;
                for (const auto &write : buffer_writes_) {
                    if (write.first == read.first && sequenced_before(link.second, write.second) && sequenced_before(write.second, read.second)) written = true;
                }
                if (!written) {
                    add_ast_mark("unterminated_link", read.second->getBeginLoc());
                    break;
                }
            }
        }
        for (const auto &opened : path_opens_) {
            for (const auto &checked : path_checks_) {
                if (checked.first == opened.first && sequenced_before(checked.second, opened.second)) {
                    add_ast_mark("checked_then_opened", opened.second->getBeginLoc());
                    break;
                }
            }
        }
    }

    // Macros defined in the main file, and the macros referenced anywhere
    // (expanded, tested, undefined): unused_macro for the rest at the end.
    void note_main_define(const Token &name, const MacroInfo *info) {
        if (info == nullptr || !sm_.isInMainFile(name.getLocation())) return;
        if (const IdentifierInfo *ident = name.getIdentifierInfo()) {
            if (guard_candidates_.count(ident->getName().str()) > 0) return;
        }
        main_macros_.push_back({info, name.getLocation()});
    }
    void note_macro_use(const MacroDefinition &definition) {
        if (const MacroInfo *info = definition.getMacroInfo()) used_macros_.insert(info);
    }
    void note_ifndef(const Token &name, const MacroDefinition &definition) {
        if (definition.getMacroInfo() == nullptr) {
            if (const IdentifierInfo *ident = name.getIdentifierInfo()) guard_candidates_.insert(ident->getName().str());
        }
        note_macro_use(definition);
    }

    // A statement after return, break, continue, goto, throw or a call of a
    // noreturn function in the same block (mark dead_statement at it), unless
    // it holds a label, case or default, or either comes from a macro.
    void note_dead_statements(const CompoundStmt *block) {
        const Stmt *previous = nullptr;
        for (const Stmt *child : block->body()) {
            if (previous != nullptr && ends_flow(previous) && !holds_label(child) && !child->getBeginLoc().isMacroID() &&
                !previous->getBeginLoc().isMacroID() && !isa<NullStmt>(child) && !directive_between(previous, child)) {
                add_ast_mark("dead_statement", child->getBeginLoc());
                return;
            }
            previous = child;
        }
    }
    // A preprocessor directive between two statements (the second may be
    // reachable in another configuration).
    bool directive_between(const Stmt *first, const Stmt *second) const {
        SourceLocation from = first->getEndLoc();
        SourceLocation to = second->getBeginLoc();
        if (from.isMacroID() || to.isMacroID() || sm_.getFileID(from) != sm_.getFileID(to)) return true;
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(sm_.getFileID(from), &invalid);
        if (invalid) return true;
        unsigned start = sm_.getFileOffset(from);
        unsigned end = sm_.getFileOffset(to);
        if (end <= start || end > buffer.size()) return true;
        llvm::StringRef gap = buffer.slice(start, end);
        size_t line = gap.find('\n');
        while (line != llvm::StringRef::npos) {
            llvm::StringRef rest = gap.drop_front(line + 1).ltrim(" \t");
            if (rest.starts_with("#")) return true;
            line = gap.find('\n', line + 1);
        }
        return false;
    }
    static bool ends_flow(const Stmt *stmt) {
        if (isa<ReturnStmt>(stmt) || isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt) || isa<GotoStmt>(stmt)) return true;
        const auto *expr = dyn_cast<Expr>(stmt);
        if (expr == nullptr) return false;
        expr = expr->IgnoreParenImpCasts();
        if (const auto *voided = dyn_cast<CStyleCastExpr>(expr)) expr = voided->getSubExpr()->IgnoreParenImpCasts();
        if (isa<CXXThrowExpr>(expr)) return true;
        const auto *call = dyn_cast<CallExpr>(expr);
        return call != nullptr && call->getDirectCallee() != nullptr && call->getDirectCallee()->isNoReturn();
    }
    static bool holds_label(const Stmt *stmt) {
        if (stmt == nullptr) return false;
        if (isa<LabelStmt>(stmt) || isa<SwitchCase>(stmt)) return true;
        if (isa<SwitchStmt>(stmt)) return false;
        for (const Stmt *child : stmt->children()) {
            if (holds_label(child)) return true;
        }
        return false;
    }

    // A comparison of EOF with a char operand (mark eof_char at the operand).
    void note_eof_compare(const BinaryOperator *compare) {
        if (compare->getOpcode() != BO_EQ && compare->getOpcode() != BO_NE) return;
        for (int side = 0; side < 2; ++side) {
            const Expr *eof = side == 0 ? compare->getLHS() : compare->getRHS();
            const Expr *other = side == 0 ? compare->getRHS() : compare->getLHS();
            if (expansion_text(eof) != "EOF") continue;
            const Expr *bare = other->IgnoreParenImpCasts();
            if (bare->getType()->isCharType()) add_ast_mark("eof_char", other->getBeginLoc());
        }
    }

    // C: a literal 0 or (void *)0 used as a null pointer, not spelled NULL
    // (mark literal_null at it).
    void note_literal_null(const ImplicitCastExpr *cast) {
        if (context_.getLangOpts().CPlusPlus || cast->getCastKind() != CK_NullToPointer) return;
        const Expr *inner = cast->getSubExpr()->IgnoreParens();
        if (const auto *voided = dyn_cast<CStyleCastExpr>(inner)) inner = voided->getSubExpr()->IgnoreParenImpCasts();
        const auto *literal = dyn_cast<IntegerLiteral>(inner);
        if (literal == nullptr || literal->getValue() != 0) return;
        SourceLocation at = cast->getSubExpr()->getBeginLoc();
        if (at.isMacroID() && !sm_.isMacroArgExpansion(at)) return;
        auto up = context_.getParents(*cast);
        if (up.size() == 1) {
            if (const auto *list = up[0].get<InitListExpr>()) {
                const InitListExpr *written = list->isSemanticForm() && list->getSyntacticForm() != nullptr ? list->getSyntacticForm() : list;
                if (written->getNumInits() == 1) return;   // {0}
            }
        }
        for (SourceLocation walk = at; walk.isMacroID(); walk = sm_.getImmediateMacroCallerLoc(walk)) {
            if (Lexer::getImmediateMacroName(walk, sm_, context_.getLangOpts()) == "NULL") return;
        }
        add_ast_mark("literal_null", at);
    }

    // C: a static object (file scope with internal linkage, or a static local)
    // that nothing refers to.
    bool unused_static(const Decl *decl) const {
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || context_.getLangOpts().CPlusPlus || !var->hasGlobalStorage() || var->isReferenced()) return false;
        if (var->hasExternalStorage() || var->getType().isVolatileQualified() || var->getLocation().isMacroID()) return false;
        if (var->hasAttr<UnusedAttr>() || var->hasAttr<UsedAttr>() || var->hasAttr<SectionAttr>() || var->hasAttr<AliasAttr>()) return false;
        bool internal = var->isStaticLocal() || (var->isFileVarDecl() && var->getFormalLinkage() == Linkage::Internal);
        if (!internal || !var->isThisDeclarationADefinition()) return false;
        for (const VarDecl *other : var->redecls()) {
            if (other->isReferenced()) return false;
        }
        return user_location(sm_, var->getLocation()) && sm_.isInMainFile(sm_.getExpansionLoc(var->getLocation()));
    }

    // A function declared noreturn whose end or a return is reachable (mark
    // noreturn_returns at the closing brace).
    void note_noreturn_returns(const Decl *decl) {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !fn->doesThisDeclarationHaveABody() || fn->isDependentContext() || fn->isTemplateInstantiation()) return;
        if (!fn->hasAttr<C11NoReturnAttr>() && !fn->hasAttr<CXX11NoReturnAttr>() && !fn->hasAttr<NoReturnAttr>()) return;
        if (!user_location(sm_, fn->getLocation())) return;
        const auto *body = dyn_cast<CompoundStmt>(fn->getBody());
        if (body == nullptr) return;
        CFG::BuildOptions options;
        std::unique_ptr<CFG> graph = CFG::buildCFG(fn, const_cast<CompoundStmt *>(body), const_cast<ASTContext *>(&context_), options);
        if (!graph) return;
        std::set<const CFGBlock *> seen;
        std::vector<const CFGBlock *> work{&graph->getEntry()};
        while (!work.empty()) {
            const CFGBlock *block = work.back();
            work.pop_back();
            if (block == nullptr || !seen.insert(block).second) continue;
            for (const CFGBlock::AdjacentBlock &next : block->succs()) {
                if (next.getReachableBlock() != nullptr) work.push_back(next.getReachableBlock());
            }
        }
        const CFGBlock &exit = graph->getExit();
        for (const CFGBlock::AdjacentBlock &before : exit.preds()) {
            const CFGBlock *block = before.getReachableBlock();
            if (block == nullptr || seen.count(block) == 0 || block->hasNoReturnElement()) continue;
            bool throws = false;
            for (const CFGElement &element : *block) {
                if (std::optional<CFGStmt> item = element.getAs<CFGStmt>()) {
                    if (isa<CXXThrowExpr>(item->getStmt())) throws = true;
                }
            }
            if (throws) continue;
            add_ast_mark("noreturn_returns", body->getRBracLoc());
            return;
        }
    }

    // Functions of the unit with a body, for the call graph (recursion_call).
    void note_graph_function(const Decl *decl) {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !fn->doesThisDeclarationHaveABody() || fn->isDependentContext() || fn->isTemplateInstantiation() || fn->isConstexpr()) return;
        if (!user_location(sm_, fn->getLocation())) return;
        graph_functions_.push_back(fn);
    }

    // Each cycle of direct calls once, at the call that closes it in a depth
    // first walk from the functions in source order.
    void finish_recursion() {
        std::map<const FunctionDecl *, std::vector<const CallExpr *>> calls;
        std::set<const FunctionDecl *> known;
        for (const FunctionDecl *fn : graph_functions_) known.insert(fn->getCanonicalDecl());
        for (const FunctionDecl *fn : graph_functions_) {
            std::vector<const CallExpr *> found;
            calls_in(fn->getBody(), found);
            for (const CallExpr *call : found) {
                const FunctionDecl *callee = call->getDirectCallee();
                if (callee != nullptr && known.count(callee->getCanonicalDecl()) > 0) calls[fn->getCanonicalDecl()].push_back(call);
            }
        }
        std::map<const FunctionDecl *, int> state;   // 1 on the stack, 2 done
        for (const FunctionDecl *fn : graph_functions_) {
            if (state[fn->getCanonicalDecl()] == 0) recursion_walk(fn->getCanonicalDecl(), calls, state);
        }
    }
    void recursion_walk(const FunctionDecl *fn, std::map<const FunctionDecl *, std::vector<const CallExpr *>> &calls,
                        std::map<const FunctionDecl *, int> &state) {
        state[fn] = 1;
        for (const CallExpr *call : calls[fn]) {
            const FunctionDecl *callee = call->getDirectCallee()->getCanonicalDecl();
            if (state[callee] == 1) {
                add_ast_mark("recursion_call", call->getBeginLoc());
            } else if (state[callee] == 0) {
                recursion_walk(callee, calls, state);
            }
        }
        state[fn] = 2;
    }

    void finish_batch36() {
        // a name written more than once in the main file (also in skipped
        // code or another configuration) counts as used
        std::map<std::string, int> words;
        if (!main_macros_.empty()) {
            bool invalid = false;
            llvm::StringRef buffer = sm_.getBufferData(sm_.getMainFileID(), &invalid);
            size_t at = 0;
            while (!invalid && at < buffer.size()) {
                char letter = buffer[at];
                if (llvm::isAlpha(letter) || letter == '_') {
                    size_t start = at;
                    while (at < buffer.size() && (llvm::isAlnum(buffer[at]) || buffer[at] == '_')) ++at;
                    ++words[buffer.slice(start, at).str()];
                } else if (llvm::isDigit(letter)) {
                    while (at < buffer.size() && (llvm::isAlnum(buffer[at]) || buffer[at] == '_')) ++at;
                } else {
                    ++at;
                }
            }
        }
        for (const auto &entry : main_macros_) {
            if (used_macros_.count(entry.first) > 0) continue;
            bool invalid = false;
            const char *start = sm_.getCharacterData(entry.second, &invalid);
            if (invalid) continue;
            std::string name;
            while (*start != 0 && (llvm::isAlnum(*start) || *start == '_')) name += *start++;
            if (words[name] > 1) continue;
            add_spelled_mark("unused_macro", entry.second);
        }
        finish_recursion();
    }

    // The blocks of a CFG reachable from its entry; with stop_noreturn the
    // edges out of blocks with a noreturn call are not followed.
    static std::set<const CFGBlock *> reachable_blocks(const CFG &graph, bool stop_noreturn) {
        std::set<const CFGBlock *> seen;
        std::vector<const CFGBlock *> work{&graph.getEntry()};
        while (!work.empty()) {
            const CFGBlock *block = work.back();
            work.pop_back();
            if (block == nullptr || !seen.insert(block).second) continue;
            if (stop_noreturn && block->hasNoReturnElement()) continue;
            for (const CFGBlock::AdjacentBlock &next : block->succs()) {
                if (next.getReachableBlock() != nullptr) work.push_back(next.getReachableBlock());
            }
        }
        return seen;
    }
    static const Stmt *last_stmt(const CFGBlock &block) {
        const Stmt *last = nullptr;
        for (const CFGElement &element : block) {
            if (std::optional<CFGStmt> item = element.getAs<CFGStmt>()) last = item->getStmt();
        }
        return last;
    }

    // Function bodies by the CFG: missing_return (a non-void function whose
    // end can be reached without a return, at the closing brace) and
    // never_returns (a function that is not declared noreturn and whose exit
    // no path reaches, at its name).
    void note_function_paths(const Decl *decl) {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !fn->doesThisDeclarationHaveABody() || fn->isDependentContext() || fn->isTemplateInstantiation()) return;
        if (fn->isMain() || fn->isNoReturn() || fn->isImplicit() || fn->isDefaulted() || !user_location(sm_, fn->getLocation())) return;
        if (fn->getLocation().isMacroID()) return;
        const auto *body = dyn_cast<CompoundStmt>(fn->getBody());
        if (body == nullptr || isa<CoroutineBodyStmt>(fn->getBody())) return;
        CFG::BuildOptions options;
        std::unique_ptr<CFG> graph = CFG::buildCFG(fn, const_cast<CompoundStmt *>(body), const_cast<ASTContext *>(&context_), options);
        if (!graph) return;
        std::set<const CFGBlock *> seen = reachable_blocks(*graph, false);
        const CFGBlock &exit = graph->getExit();
        if (!fn->getReturnType()->isVoidType() && !fn->getReturnType()->isUndeducedType()) {
            for (const CFGBlock::AdjacentBlock &before : exit.preds()) {
                const CFGBlock *block = before.getReachableBlock();
                if (block == nullptr || seen.count(block) == 0 || block->hasNoReturnElement()) continue;
                const Stmt *last = last_stmt(*block);
                if (last != nullptr && (isa<ReturnStmt>(last) || isa<CXXThrowExpr>(last))) continue;
                add_ast_mark("missing_return", body->getRBracLoc());
                break;
            }
        }
        const auto *method = dyn_cast<CXXMethodDecl>(fn);
        if (method != nullptr && method->isVirtual()) return;
        std::set<const CFGBlock *> live = reachable_blocks(*graph, true);
        if (live.count(&exit) > 0 || body->body_empty()) return;
        bool throws = false;
        for (const CFGBlock *block : live) {
            const Stmt *last = last_stmt(*block);
            if (last != nullptr && isa<CXXThrowExpr>(last)) throws = true;
        }
        if (throws || directive_inside(body)) return;
        add_ast_mark("never_returns", fn->getLocation());
    }

    // A preprocessor directive inside a statement's text.
    bool directive_inside(const Stmt *stmt) const {
        SourceLocation from = stmt->getBeginLoc();
        SourceLocation to = stmt->getEndLoc();
        if (from.isMacroID() || to.isMacroID() || sm_.getFileID(from) != sm_.getFileID(to)) return true;
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(sm_.getFileID(from), &invalid);
        if (invalid) return true;
        llvm::StringRef text = buffer.slice(sm_.getFileOffset(from), sm_.getFileOffset(to));
        size_t line = text.find('\n');
        while (line != llvm::StringRef::npos) {
            if (text.drop_front(line + 1).ltrim(" \t").starts_with("#")) return true;
            line = text.find('\n', line + 1);
        }
        return false;
    }

    // A local (automatic) object named by an expression: &local, a local
    // array that decays, or (want_ref) the local itself.
    const VarDecl *local_address(const Expr *expr, bool want_ref) const {
        const Expr *bare = expr->IgnoreParens();
        while (const auto *cast = dyn_cast<ImplicitCastExpr>(bare)) {
            CastKind kind = cast->getCastKind();
            bool plain = kind == CK_NoOp || kind == CK_DerivedToBase || kind == CK_UncheckedDerivedToBase || kind == CK_LValueToRValue || kind == CK_BitCast;
            if (!plain && !(kind == CK_ArrayToPointerDecay && !want_ref)) return nullptr;
            bare = cast->getSubExpr()->IgnoreParens();
        }
        if (const auto *address = dyn_cast<UnaryOperator>(bare)) {
            if (address->getOpcode() == UO_AddrOf) bare = address->getSubExpr()->IgnoreParenImpCasts();
            else return nullptr;
        } else if (!want_ref) {
            const auto *decay = dyn_cast<ImplicitCastExpr>(expr->IgnoreParens());
            if (decay == nullptr || decay->getCastKind() != CK_ArrayToPointerDecay) return nullptr;
        }
        const auto *ref = dyn_cast<DeclRefExpr>(bare);
        const auto *var = ref != nullptr ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        if (var == nullptr || !var->hasLocalStorage() || var->getType()->isReferenceType()) return nullptr;
        if (isa<ParmVarDecl>(var) && var->getType()->isArrayType()) return nullptr;
        return var;
    }

    // Batch 37 statement facts: local addresses stored in longer-lived
    // objects or returned (escaping_address), overlapping copies
    // (overlapping_copy), free of something not allocated (free_unallocated),
    // FILE objects dereferenced (file_dereference), fclose and later uses.
    void note_batch37(const Stmt *stmt) {
        if (const auto *back = dyn_cast<ReturnStmt>(stmt)) {
            const Expr *value = back->getRetValue();
            const auto *fn = value != nullptr ? dyn_cast_or_null<FunctionDecl>(enclosing_function_decl(back)) : nullptr;
            if (fn == nullptr || fn->isDependentContext()) return;
            bool by_ref = fn->getReturnType()->isReferenceType();
            if (!by_ref && !fn->getReturnType()->isPointerType()) return;
            if (local_address(value, by_ref) != nullptr) add_ast_mark("escaping_address", value->getBeginLoc());
            return;
        }
        if (const auto *assign = dyn_cast<BinaryOperator>(stmt)) {
            if (assign->getOpcode() == BO_Assign) {
                const Expr *target = assign->getLHS()->IgnoreParenImpCasts();
                bool longer = false;
                if (const auto *ref = dyn_cast<DeclRefExpr>(target)) {
                    const auto *var = dyn_cast<VarDecl>(ref->getDecl());
                    longer = var != nullptr && var->hasGlobalStorage();
                } else if (const auto *deref = dyn_cast<UnaryOperator>(target)) {
                    const auto *ref = deref->getOpcode() == UO_Deref ? dyn_cast<DeclRefExpr>(deref->getSubExpr()->IgnoreParenImpCasts()) : nullptr;
                    longer = ref != nullptr && isa<ParmVarDecl>(ref->getDecl());
                }
                if (longer && local_address(assign->getRHS(), false) != nullptr) add_ast_mark("escaping_address", assign->getRHS()->getBeginLoc());
                const auto *ref = dyn_cast<DeclRefExpr>(target);
                if (ref != nullptr) stream_writes_.push_back({ref->getDecl(), assign});
            }
            return;
        }
        if (const auto *deref = dyn_cast<UnaryOperator>(stmt)) {
            if (deref->getOpcode() == UO_Deref && file_pointer(deref->getSubExpr()->getType()) && !system_spelled(deref->getBeginLoc())) {
                add_ast_mark("file_dereference", deref->getBeginLoc());
            }
            return;
        }
        if (const auto *member = dyn_cast<MemberExpr>(stmt)) {
            if (member->isArrow() && file_pointer(member->getBase()->getType()) && !system_spelled(member->getBeginLoc())) {
                add_ast_mark("file_dereference", member->getBeginLoc());
            }
            return;
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            if (file_pointer(ref->getType())) stream_uses_.push_back({ref->getDecl(), ref});
            return;
        }
        const auto *call = dyn_cast<CallExpr>(stmt);
        if (call == nullptr) return;
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return;
        SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
        if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) return;
        llvm::StringRef name = callee->getName();
        if ((name == "memcpy" || name == "strcpy" || name == "strncpy" || name == "strcat" || name == "strncat") && call->getNumArgs() >= 2) {
            long long first_offset = 0, second_offset = 0;
            bool first_known = true, second_known = true;
            const ValueDecl *first = copy_base(call->getArg(0), first_offset, first_known);
            const ValueDecl *second = copy_base(call->getArg(1), second_offset, second_known);
            bool apart = false;
            if (first_known && second_known && call->getNumArgs() == 3 && name == "memcpy") {
                Expr::EvalResult size;
                if (!call->getArg(2)->isValueDependent() && call->getArg(2)->EvaluateAsInt(size, context_)) {
                    long long gap = first_offset > second_offset ? first_offset - second_offset : second_offset - first_offset;
                    apart = gap >= size.Val.getInt().getExtValue();
                }
            }
            if (first != nullptr && first == second && first_known && second_known && !apart && !(first_known && second_known && first_offset == second_offset && name == "memcpy")) {
                add_ast_mark("overlapping_copy", call->getBeginLoc());
            }
        }
        if ((name == "free" || name == "realloc") && call->getNumArgs() >= 1) {
            const Expr *arg = call->getArg(0)->IgnoreParenCasts();
            bool bad = isa<StringLiteral>(arg);
            const auto *decay = dyn_cast<ImplicitCastExpr>(call->getArg(0)->IgnoreParens());
            while (decay != nullptr && decay->getCastKind() != CK_ArrayToPointerDecay) decay = dyn_cast<ImplicitCastExpr>(decay->getSubExpr()->IgnoreParens());
            if (decay != nullptr) {
                const auto *ref = dyn_cast<DeclRefExpr>(decay->getSubExpr()->IgnoreParenImpCasts());
                const auto *var = ref != nullptr ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
                bad = bad || (var != nullptr && !isa<ParmVarDecl>(var) && var->getType()->isArrayType());
            }
            if (const auto *address = dyn_cast<UnaryOperator>(arg)) {
                if (address->getOpcode() == UO_AddrOf) {
                    const auto *ref = dyn_cast<DeclRefExpr>(address->getSubExpr()->IgnoreParenImpCasts());
                    bad = bad || (ref != nullptr && isa<VarDecl>(ref->getDecl()) && !ref->getType()->isPointerType());
                }
            }
            if (bad) add_ast_mark("free_unallocated", call->getArg(0)->getBeginLoc());
        }
        if (name == "fclose" && call->getNumArgs() == 1) {
            const auto *ref = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
            if (ref != nullptr) stream_closes_.push_back({ref->getDecl(), call});
        }
    }

    // The variable a copy argument points into, and its constant offset.
    const ValueDecl *copy_base(const Expr *expr, long long &offset, bool &known) const {
        expr = expr->IgnoreParenCasts();
        if (const auto *sum = dyn_cast<BinaryOperator>(expr)) {
            if (sum->getOpcode() == BO_Add || sum->getOpcode() == BO_Sub) {
                Expr::EvalResult value;
                if (!sum->getRHS()->isValueDependent() && sum->getRHS()->EvaluateAsInt(value, context_)) {
                    offset += (sum->getOpcode() == BO_Add ? 1 : -1) * value.Val.getInt().getExtValue();
                } else {
                    known = false;
                }
                return copy_base(sum->getLHS(), offset, known);
            }
            return nullptr;
        }
        if (const auto *address = dyn_cast<UnaryOperator>(expr)) {
            if (address->getOpcode() != UO_AddrOf) return nullptr;
            const auto *element = dyn_cast<ArraySubscriptExpr>(address->getSubExpr()->IgnoreParens());
            if (element == nullptr) return nullptr;
            Expr::EvalResult value;
            if (!element->getIdx()->isValueDependent() && element->getIdx()->EvaluateAsInt(value, context_)) {
                offset += value.Val.getInt().getExtValue();
            } else {
                known = false;
            }
            return copy_base(element->getBase(), offset, known);
        }
        const auto *ref = dyn_cast<DeclRefExpr>(expr);
        return ref != nullptr ? ref->getDecl() : nullptr;
    }

    // A pointer to the library's FILE.
    static bool file_pointer(QualType type) {
        if (type.isNull() || !type->isPointerType()) return false;
        for (QualType step = type->getPointeeType(); !step.isNull();) {
            const auto *named = step->getAs<TypedefType>();
            if (named == nullptr) break;
            if (named->getDecl()->getName() == "FILE") return true;
            step = named->getDecl()->getUnderlyingType();
        }
        return false;
    }
    bool system_spelled(SourceLocation loc) const {
        SourceLocation spelled = sm_.getSpellingLoc(loc);
        return spelled.isValid() && sm_.isInSystemHeader(spelled);
    }

    // The function that holds a statement.
    const Decl *enclosing_function_decl(const Stmt *stmt) const {
        DynTypedNode node = DynTypedNode::create(*stmt);
        for (int depth = 0; depth < 256; ++depth) {
            auto parents = context_.getParents(node);
            if (parents.empty()) return nullptr;
            node = parents[0];
            if (node.get<LambdaExpr>() != nullptr) return nullptr;
            if (const auto *fn = node.get<FunctionDecl>()) return fn;
        }
        return nullptr;
    }

    // Uses of a FILE pointer that fclose ran before on one path, with no new
    // value in between (closed_stream_use).
    // True when nothing conditional (a branch, loop, switch, ?: or && / ||)
    // stands between first and the statement it shares with second, so first
    // runs whenever that shared statement goes on to second.
    bool unconditional_below(const Stmt *first, const Stmt *second) const {
        std::vector<const Stmt *> one = stmt_chain(first);
        std::vector<const Stmt *> two = stmt_chain(second);
        for (size_t at = 1; at < one.size(); ++at) {
            const Stmt *step = one[at];
            if (std::find(two.begin(), two.end(), step) != two.end()) return true;
            if (isa<IfStmt>(step) || isa<SwitchStmt>(step) || isa<WhileStmt>(step) || isa<DoStmt>(step) || isa<ForStmt>(step)) return false;
            if (isa<CXXForRangeStmt>(step) || isa<ConditionalOperator>(step) || isa<BinaryConditionalOperator>(step) || isa<SwitchCase>(step)) return false;
            if (const auto *binary = dyn_cast<BinaryOperator>(step)) {
                if (binary->getOpcode() == BO_LAnd || binary->getOpcode() == BO_LOr) return false;
            }
        }
        return false;
    }

    // Batch 38 marks from diagnostics (DiagSeen): implicit_int,
    // undeclared_call, include_form, orphan_conditional, atomic_void,
    // jump_into_try, early_catch_all, mixed_string_prefix, linkage_conflict.
    void finish_diagnostics() {
        if (diag_sink == nullptr) return;
        for (const DiagSeen &one : *diag_sink) {
            unsigned id = one.id;
            if (one.loc.isInvalid()) continue;
            if (id == diag::ext_missing_type_specifier) add_ast_mark("implicit_int", declaration_begin(one.loc));
            if (id == diag::err_static_non_static || id == diag::err_non_static_static) add_ast_mark("linkage_conflict", declaration_begin(one.loc));
            if (id == diag::ext_implicit_function_decl_c99 || id == diag::warn_implicit_function_decl || id == diag::ext_implicit_lib_function_decl || id == diag::ext_implicit_lib_function_decl_c99) {
                add_ast_mark("undeclared_call", one.loc);
            }
            if (id == diag::err_pp_expects_filename) add_ast_mark("include_form", one.loc);
            if (id == diag::err_pp_endif_without_if || id == diag::pp_err_else_without_if || id == diag::pp_err_elif_without_if) add_ast_mark("orphan_conditional", directive_hash(one.loc));
            if (id == diag::err_atomic_specifier_bad_type && one.atomic_void) add_ast_mark("atomic_void", one.loc);
            if ((id == diag::err_goto_into_protected_scope || id == diag::err_switch_into_protected_scope) && one.try_scope) add_ast_mark("jump_into_try", one.loc);
            if (id == diag::err_early_catch_all) add_ast_mark("early_catch_all", one.loc);
            if (id == diag::err_unsupported_string_concat) add_ast_mark("mixed_string_prefix", one.loc);
            if (id == diag::err_cast_pointer_to_non_pointer_int || id == diag::err_cast_pointer_from_non_pointer_int) add_ast_mark("float_pointer_cast", cast_open(one.loc));
            if (id == diag::err_missing_exception_specification || id == diag::err_mismatched_exception_spec || id == diag::ext_missing_exception_specification || id == diag::ext_mismatched_exception_spec) {
                add_ast_mark("exception_spec_mismatch", one.loc);
            }
            if (id == diag::ext_register_storage_class || id == diag::warn_deprecated_register) add_ast_mark("obsolescent", one.loc);
            if (id == diag::warn_cannot_pass_non_pod_arg_to_vararg || id == diag::warn_non_pod_vararg_with_format_string || id == diag::err_cannot_pass_to_vararg || id == diag::err_cannot_pass_to_vararg_format || id == diag::err_cannot_pass_non_trivial_c_struct_to_vararg) {
                add_ast_mark("vararg_type", one.loc);
            }
        }
    }

    // The ( of the C-style cast whose operand starts at `loc`, else `loc`.
    SourceLocation cast_open(SourceLocation loc) const {
        if (!loc.isFileID()) return loc;
        std::pair<FileID, unsigned> place = sm_.getDecomposedLoc(loc);
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(place.first, &invalid);
        if (invalid || place.second > buffer.size()) return loc;
        size_t at = place.second;
        while (at > 0 && (buffer[at - 1] == ' ' || buffer[at - 1] == '\t')) --at;
        if (at == 0 || buffer[at - 1] != ')') return loc;
        int depth = 0;
        while (at > 0) {
            --at;
            if (buffer[at] == '\n') return loc;
            if (buffer[at] == ')') ++depth;
            if (buffer[at] == '(' && --depth == 0) return sm_.getComposedLoc(place.first, static_cast<unsigned>(at));
        }
        return loc;
    }

    // Batch 39: atomic_member (a member of an _Atomic structure or union
    // accessed directly).
    void note_batch39(const Stmt *stmt) {
        const auto *member = dyn_cast<MemberExpr>(stmt);
        if (member == nullptr) return;
        QualType base = member->getBase()->IgnoreParenImpCasts()->getType();
        if (member->isArrow() && base->isPointerType()) base = base->getPointeeType();
        if (!base.isNull() && base->isAtomicType()) add_ast_mark("atomic_member", member->getBeginLoc());
    }

    // OC-0179 in C: obsolescent (a function declared or defined without a
    // prototype, before C23; a storage class written after a type keyword or
    // qualifier).
    void note_obsolescent(const Decl *decl) {
        const LangOptions &lang = context_.getLangOpts();
        if (lang.CPlusPlus || decl->isImplicit() || !user_location(sm_, decl->getLocation()) || decl->getLocation().isMacroID()) return;
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn != nullptr && !lang.requiresStrictPrototypes() && !fn->hasWrittenPrototype() && fn->getBuiltinID() == 0) {
            add_ast_mark("obsolescent", fn->getLocation());
            return;
        }
        const auto *var = dyn_cast<VarDecl>(decl);
        bool storage = isa<TypedefDecl>(decl) || (fn != nullptr && fn->getStorageClass() != SC_None) ||
                       (var != nullptr && (var->getStorageClass() != SC_None || var->getTSCSpec() != TSCS_unspecified));
        if (!storage || isa<ParmVarDecl>(decl) || decl->getBeginLoc().isMacroID()) return;
        static const std::set<std::string> storage_words = {"static", "extern", "typedef", "_Thread_local", "thread_local", "register", "auto"};
        static const std::set<std::string> type_words = {"int", "char", "short", "long", "signed", "unsigned", "float", "double",
                                                         "void", "_Bool", "bool", "const", "volatile", "restrict", "_Atomic", "_Complex"};
        SourceLocation at = decl->getBeginLoc();
        unsigned stop = sm_.getFileOffset(decl->getLocation());
        FileID fid = sm_.getFileID(at);
        bool typed = false;
        for (int step = 0; step < 64 && at.isValid() && sm_.getFileID(at) == fid && sm_.getFileOffset(at) < stop; ++step) {
            Token tok;
            if (Lexer::getRawToken(at, tok, sm_, lang, true)) break;
            if (!tok.is(tok::raw_identifier)) break;
            std::string word = tok.getRawIdentifier().str();
            if (storage_words.count(word) > 0 && typed) {
                add_ast_mark("obsolescent", tok.getLocation());
                break;
            }
            if (type_words.count(word) == 0 && storage_words.count(word) == 0) break;
            if (type_words.count(word) > 0) typed = true;
            at = tok.getEndLoc();
        }
    }

    // The start of the declaration whose name is at `loc`, else `loc`.
    SourceLocation declaration_begin(SourceLocation loc) const {
        auto found = decl_begins_.find(loc.getRawEncoding());
        return found != decl_begins_.end() && found->second.isValid() ? found->second : loc;
    }

    // The # of the directive whose name is at `loc` (same line), else `loc`.
    SourceLocation directive_hash(SourceLocation loc) const {
        SourceLocation at = sm_.getExpansionLoc(loc);
        std::pair<FileID, unsigned> place = sm_.getDecomposedLoc(at);
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(place.first, &invalid);
        if (invalid || place.second > buffer.size()) return loc;
        unsigned offset = place.second;
        while (offset > 0 && buffer[offset - 1] != '\n' && buffer[offset - 1] != '#') --offset;
        if (offset == 0 || buffer[offset - 1] != '#') return loc;
        return sm_.getComposedLoc(place.first, offset - 1);
    }

    void finish_batch37() {
        for (const auto &use : stream_uses_) {
            for (const auto &close : stream_closes_) {
                if (close.first != use.first || !sequenced_before(close.second, use.second) || !unconditional_below(close.second, use.second)) continue;
                bool renewed = false;
                for (const auto &write : stream_writes_) {
                    if (write.first == use.first && sequenced_before(close.second, write.second) && !sequenced_before(use.second, write.second)) renewed = true;
                }
                if (!renewed) {
                    add_ast_mark("closed_stream_use", use.second->getBeginLoc());
                    break;
                }
            }
        }
    }

    // ~ or << of an operand narrower than int (promoted), not cast at once to an
    // integer type narrower than int (C++).
    bool small_bitwise(const Expr *expr) const {
        if (!context_.getLangOpts().CPlusPlus) return false;
        {
            const Expr *bare = expr->IgnoreParens();
            const auto *bare_unary = dyn_cast<UnaryOperator>(bare);
            const auto *bare_binary = dyn_cast<BinaryOperator>(bare);
            if (!(bare_unary != nullptr && bare_unary->getOpcode() == UO_Not) && !(bare_binary != nullptr && bare_binary->getOpcode() == BO_Shl)) return false;
            auto up = context_.getParents(*expr);
            if (up.size() == 1 && up[0].get<ParenExpr>() != nullptr) return false;
        }
        const Expr *whole = expr;
        expr = expr->IgnoreParens();
        const Expr *operand = nullptr;
        if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            if (unary->getOpcode() == UO_Not) operand = unary->getSubExpr();
        } else if (const auto *binary = dyn_cast<BinaryOperator>(expr)) {
            if (binary->getOpcode() == BO_Shl) operand = binary->getLHS();
        }
        if (operand == nullptr || expr->isValueDependent() || expr->getType()->isDependentType()) return false;
        QualType held = without_implicit_casts(operand)->getType();
        if (held.isNull() || !held->isIntegerType() || held->isBooleanType() || held->isDependentType()) return false;
        if (context_.getTypeSize(held) >= context_.getTypeSize(context_.IntTy)) return false;
        if (operand->isIntegerConstantExpr(context_)) return false;
        DynTypedNode node = DynTypedNode::create(*whole);
        while (true) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return true;
            node = parents[0];
            if (node.get<ParenExpr>() != nullptr || node.get<ImplicitCastExpr>() != nullptr) continue;
            if (const auto *cast = node.get<ExplicitCastExpr>()) {
                QualType target = cast->getType();
                return !(target->isIntegerType() && context_.getTypeSize(target) < context_.getTypeSize(context_.IntTy));
            }
            return true;
        }
    }

    // A plain or signed char value widened for use as a number: an explicit cast
    // to a wider integer type, an array index, or the argument of a ctype
    // function (on the char expression).  Constants and a value cast to signed
    // char on purpose are left out.
    bool char_widened(const Expr *expr) const {
        if (isa<ImplicitCastExpr>(expr) || isa<ParenExpr>(expr) || !character_value(expr)) return false;
        DynTypedNode node = DynTypedNode::create(*expr);
        const Stmt *below = expr;
        while (true) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return false;
            node = parents[0];
            if (const auto *paren = node.get<ParenExpr>()) { below = paren; continue; }
            if (const auto *cast = node.get<ImplicitCastExpr>()) {
                if (cast->getCastKind() == CK_LValueToRValue || cast->getCastKind() == CK_IntegralCast) { below = cast; continue; }
                return false;
            }
            break;
        }
        if (node.get<ExplicitCastExpr>() != nullptr) return false;
        if (const auto *index = node.get<ArraySubscriptExpr>()) return index->getIdx() == below;
        if (const auto *call = node.get<CallExpr>()) {
            const FunctionDecl *callee = call->getDirectCallee();
            if (callee == nullptr || callee->getIdentifier() == nullptr || call->getNumArgs() != 1 || call->getArg(0) != below) return false;
            SourceLocation declared = sm_.getSpellingLoc(callee->getLocation());
            if (declared.isInvalid() || !sm_.isInSystemHeader(declared)) return false;
            static const std::set<std::string> ctype = {"isalnum", "isalpha", "isblank", "iscntrl", "isdigit", "isgraph",
                                                        "islower", "isprint", "ispunct", "isspace", "isupper",
                                                        "isxdigit", "tolower", "toupper"};
            return ctype.count(callee->getName().str()) > 0;
        }
        return false;
    }

    // A character value: plain char, or signed char not named by a typedef
    // (int8_t, ImS8 and the like are small integers).  Constants, character
    // literals, deliberate casts and ?: between constants are left out.
    bool character_value(const Expr *operand) const {
        QualType held = operand->getType();
        if (held.isNull() || operand->isValueDependent()) return false;
        QualType canonical = held.getCanonicalType();
        bool plain = canonical->isSpecificBuiltinType(BuiltinType::Char_S);
        bool written_signed = canonical->isSpecificBuiltinType(BuiltinType::SChar) && !isa<TypedefType>(held.getTypePtr()) &&
                              held->getAs<TypedefType>() == nullptr;
        if (!plain && !written_signed) return false;
        if (isa<CharacterLiteral>(operand) || isa<ExplicitCastExpr>(operand)) return false;
        if (const auto *choice = dyn_cast<ConditionalOperator>(operand)) {
            const Expr *yes = choice->getTrueExpr()->IgnoreParenImpCasts(), *no = choice->getFalseExpr()->IgnoreParenImpCasts();
            if (yes->isIntegerConstantExpr(context_) && no->isIntegerConstantExpr(context_)) return false;
        }
        return !operand->isIntegerConstantExpr(context_);
    }

    // An explicit cast of a plain or signed char value to a wider integer type.
    bool char_cast_widened(const ExplicitCastExpr *cast) const {
        QualType target = cast->getType();
        if (target.isNull() || target->isDependentType() || !target->isIntegerType() || target->isBooleanType() ||
            target->isAnyCharacterType() || context_.getTypeSize(target) <= context_.getTypeSize(context_.CharTy)) return false;
        if (target->isEnumeralType()) return false;
        return character_value(cast->getSubExpr()->IgnoreParenImpCasts());
    }

    // Side effects other than `self` in a full expression: assignments, compound
    // assignments and other builtin ++/--.  Calls are not counted.  Unevaluated
    // operands, statement expressions and lambdas are not entered.
    static bool other_effect(const Stmt *stmt, const Stmt *self) {
        if (stmt == nullptr) return false;
        if (isa<UnaryExprOrTypeTraitExpr>(stmt) || isa<StmtExpr>(stmt) || isa<LambdaExpr>(stmt) ||
            isa<GenericSelectionExpr>(stmt) || isa<CXXTypeidExpr>(stmt) || isa<CXXNoexceptExpr>(stmt) ||
            isa<DeclRefExpr>(stmt)) return false;
        if (stmt != self) {
            if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
                if (unary->isIncrementDecrementOp()) return true;
            }
            if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
                if (binary->isAssignmentOp()) return true;
            }
            if (const auto *op = dyn_cast<CXXOperatorCallExpr>(stmt)) {
                if (op->isAssignmentOp()) return true;
            }
        }
        for (const Stmt *child : stmt->children()) {
            if (other_effect(child, self)) return true;
        }
        return false;
    }

    // A builtin ++/-- whose full expression has another side effect (not in the
    // third clause of a for header, not in a macro of a system header).
    bool increment_with_effects(const UnaryOperator *unary) const {
        if (unary->isTypeDependent() || unary->isValueDependent()) return false;
        const Expr *top = full_expression(unary);
        auto parents = context_.getParents(*top);
        if (parents.size() == 1) {
            if (const auto *loop = parents[0].get<ForStmt>()) {
                if (loop->getInc() == top) return false;
            }
        }
        return other_effect(top, unary);
    }

    // A lambda that leaves the call: returned, assigned to a member or a static
    // variable, given to std::thread, std::jthread or std::async, or added to a
    // member container.  Shapes: escaping_default (written [=] or [&]),
    // escaping_this (this captured implicitly).
    void note_lambda(const LambdaExpr *lambda, NodeRec &rec) {
        if (lambda->getCaptureDefault() == LCD_None) return;
        DynTypedNode node = DynTypedNode::create(*static_cast<const Expr *>(lambda));
        const Expr *below = lambda;
        bool escapes = false;
        while (true) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) break;
            node = parents[0];
            const Expr *outer = node.get<Expr>();
            if (outer != nullptr && (isa<ImplicitCastExpr>(outer) || isa<MaterializeTemporaryExpr>(outer) ||
                                     isa<CXXBindTemporaryExpr>(outer) || isa<ExprWithCleanups>(outer) ||
                                     isa<ParenExpr>(outer) || isa<CXXFunctionalCastExpr>(outer))) {
                below = outer;
                continue;
            }
            if (const auto *make = dyn_cast_or_null<CXXConstructExpr>(outer)) {
                if (std_class(make->getType(), "thread") || std_class(make->getType(), "jthread")) { escapes = true; break; }
                if (make->getNumArgs() >= 1 && make->getArg(0) == below) { below = outer; continue; }
                break;
            }
            if (node.get<ReturnStmt>() != nullptr) { escapes = true; break; }
            if (const auto *var = node.get<VarDecl>()) { escapes = var->hasGlobalStorage() && !var->isConstexpr(); break; }
            if (const auto *op = dyn_cast_or_null<CXXOperatorCallExpr>(outer)) {
                escapes = op->isAssignmentOp() && op->getNumArgs() == 2 && op->getArg(1) == below && stored_far(op->getArg(0));
                break;
            }
            if (const auto *binary = dyn_cast_or_null<BinaryOperator>(outer)) {
                escapes = binary->isAssignmentOp() && binary->getRHS() == below && stored_far(binary->getLHS());
                break;
            }
            if (const auto *member_call = dyn_cast_or_null<CXXMemberCallExpr>(outer)) {
                const CXXMethodDecl *method = member_call->getMethodDecl();
                static const std::set<std::string> adders = {"push_back", "emplace_back", "push_front", "emplace_front",
                                                             "insert", "emplace", "push"};
                escapes = method != nullptr && method->getIdentifier() != nullptr && adders.count(method->getName().str()) > 0 &&
                          stored_far(member_call->getImplicitObjectArgument());
                break;
            }
            if (const auto *call = dyn_cast_or_null<CallExpr>(outer)) {
                const FunctionDecl *callee = call->getDirectCallee();
                escapes = callee != nullptr && callee->getIdentifier() != nullptr && callee->getName() == "async" && callee->isInStdNamespace();
                break;
            }
            break;
        }
        if (!escapes) return;
        add_shape(rec, "escaping_default");
        for (const LambdaCapture &capture : lambda->captures()) {
            if (capture.capturesThis() && capture.isImplicit()) add_shape(rec, "escaping_this");
        }
    }

    // A member (of this or another object) or a variable with static storage.
    static bool stored_far(const Expr *target) {
        if (target == nullptr) return false;
        target = target->IgnoreParenImpCasts();
        if (isa<MemberExpr>(target)) return true;
        if (const auto *ref = dyn_cast<DeclRefExpr>(target)) {
            const auto *var = dyn_cast<VarDecl>(ref->getDecl());
            return var != nullptr && var->hasGlobalStorage();
        }
        return false;
    }

    static bool sizeof_operand(const Expr *expr) {
        const auto *size = dyn_cast<UnaryExprOrTypeTraitExpr>(expr->IgnoreParenImpCasts());
        return size != nullptr && size->getKind() == UETT_SizeOf;
    }

    // Pointer + or - (also += and -=) with an offset `n * sizeof ...` while the
    // pointee is larger than one byte (shape scaled_offset on the product).
    void note_scaled_offset(const BinaryOperator *binary) {
        BinaryOperatorKind kind = binary->getOpcode();
        if (kind != BO_Add && kind != BO_Sub && kind != BO_AddAssign && kind != BO_SubAssign) return;
        if (binary->isTypeDependent() || binary->isValueDependent()) return;
        const Expr *pointer = binary->getLHS(), *offset = binary->getRHS();
        if (!pointer->getType()->isPointerType()) std::swap(pointer, offset);
        if (!pointer->getType()->isPointerType() || !offset->getType()->isIntegerType()) return;
        QualType pointee = pointer->getType()->getPointeeType();
        if (pointee.isNull() || pointee->isVoidType() || pointee->isIncompleteType() || pointee->isFunctionType() ||
            pointee->isDependentType() || context_.getTypeSizeInChars(pointee).getQuantity() <= 1) return;
        const auto *product = dyn_cast<BinaryOperator>(offset->IgnoreParenImpCasts());
        if (product == nullptr || product->getOpcode() != BO_Mul) return;
        if (!sizeof_operand(product->getLHS()) && !sizeof_operand(product->getRHS())) return;
        add_pending(product, "scaled_offset");
    }

    // The alignment known for what a pointer expression points to: the declared
    // alignment of a variable or field it names (array decay or &), else the
    // alignment of the pointee type.
    CharUnits known_alignment(const Expr *source) const {
        const Expr *bare = source->IgnoreParenImpCasts();
        if (const auto *address = dyn_cast<UnaryOperator>(bare)) {
            if (address->getOpcode() == UO_AddrOf) bare = address->getSubExpr()->IgnoreParens();
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(bare)) {
            if (isa<VarDecl>(ref->getDecl())) return context_.getDeclAlign(ref->getDecl());
        }
        if (const auto *member = dyn_cast<MemberExpr>(bare)) {
            if (isa<FieldDecl>(member->getMemberDecl())) return member_alignment(member);
        }
        return context_.getTypeAlignInChars(source->getType()->getPointeeType());
    }

    // The alignment of a member object: what its offset in an aligned enclosing
    // object guarantees, at least the field's own alignment.
    CharUnits member_alignment(const MemberExpr *member) const {
        const auto *field = cast<FieldDecl>(member->getMemberDecl());
        CharUnits own = context_.getDeclAlign(field);
        const RecordDecl *record = field->getParent();
        if (record == nullptr || record->isInvalidDecl() || record->getDefinition() == nullptr || record->isDependentType()) return own;
        const Expr *base = member->getBase()->IgnoreParenImpCasts();
        CharUnits outer;
        if (member->isArrow()) {
            outer = context_.getTypeAlignInChars(base->getType()->getPointeeType());
        } else if (const auto *inner = dyn_cast<MemberExpr>(base)) {
            outer = isa<FieldDecl>(inner->getMemberDecl()) ? member_alignment(inner) : context_.getTypeAlignInChars(base->getType());
        } else if (const auto *ref = dyn_cast<DeclRefExpr>(base)) {
            outer = std::max(context_.getTypeAlignInChars(base->getType()),
                             isa<VarDecl>(ref->getDecl()) && !ref->getDecl()->getType()->isReferenceType() ? context_.getDeclAlign(ref->getDecl()) : CharUnits::One());
        } else {
            outer = context_.getTypeAlignInChars(base->getType());
        }
        uint64_t offset = context_.toCharUnitsFromBits(context_.getFieldOffset(field)).getQuantity();
        uint64_t guaranteed = static_cast<uint64_t>(outer.getQuantity());
        while (guaranteed > 1 && offset % guaranteed != 0) guaranteed /= 2;
        return std::max(own, CharUnits::fromQuantity(static_cast<int64_t>(guaranteed)));
    }

    // container_of: `(type *)((char *)ptr - offsetof(type, field))`.
    static bool container_of(const Expr *source) {
        const auto *minus = dyn_cast<BinaryOperator>(source->IgnoreParenImpCasts());
        if (minus == nullptr || minus->getOpcode() != BO_Sub) return false;
        const Expr *offset = minus->getRHS()->IgnoreParenImpCasts();
        return isa<OffsetOfExpr>(offset);
    }

    // An explicit cast between object pointers that raises the alignment the
    // pointee needs (not from void pointers, not of allocation results).
    bool alignment_increase(const ExplicitCastExpr *cast) const {
        if (isa<CXXStaticCastExpr>(cast) || isa<CXXConstCastExpr>(cast) || isa<CXXDynamicCastExpr>(cast)) return false;
        QualType target = cast->getType();
        const Expr *source = cast->getSubExpr()->IgnoreParenImpCasts();
        QualType held = source->getType();
        if (target.isNull() || held.isNull() || !target->isPointerType() || !held->isPointerType()) return false;
        QualType to = target->getPointeeType(), from = held->getPointeeType();
        for (QualType one : {to, from}) {
            if (one->isVoidType() || one->isIncompleteType() || one->isFunctionType() || one->isDependentType() ||
                one->isSizelessType()) return false;
        }
        if (allocation(source) || container_of(source)) return false;
        return context_.getTypeAlignInChars(to) > known_alignment(source);
    }

    // A non-placement new whose result is not handed at once to a smart pointer
    // (constructor or reset).
    bool unmanaged_new(const CXXNewExpr *fresh) const {
        if (fresh->getNumPlacementArgs() > 0 || fresh->isTypeDependent()) return false;
        if (fresh->getOperatorNew() != nullptr && fresh->getOperatorNew()->getNumParams() > 1 && !fresh->passAlignment()) {
            // a user operator new with extra parameters (an arena) is a placement form
            return false;
        }
        DynTypedNode node = DynTypedNode::create(*static_cast<const Expr *>(fresh));
        while (true) {
            auto parents = context_.getParents(node);
            if (parents.size() != 1) return true;
            node = parents[0];
            const Expr *outer = node.get<Expr>();
            if (outer != nullptr && (isa<ImplicitCastExpr>(outer) || isa<ParenExpr>(outer) || isa<ExprWithCleanups>(outer))) continue;
            if (const auto *make = dyn_cast_or_null<CXXConstructExpr>(outer)) {
                if (std_class(make->getType(), "unique_ptr") || std_class(make->getType(), "shared_ptr")) return false;
                // std::locale(loc, new facet) and other owners: a class with a
                // destructor of its own whose constructor takes the pointer
                const CXXRecordDecl *owner = make->getType()->getAsCXXRecordDecl();
                return !(owner != nullptr && owner->hasDefinition() && owner->getDestructor() != nullptr &&
                         !owner->getDestructor()->isTrivial() && make->getNumArgs() >= 1);
            }
            if (const auto *member_call = dyn_cast_or_null<CXXMemberCallExpr>(outer)) {
                const CXXMethodDecl *method = member_call->getMethodDecl();
                QualType object = member_call->getImplicitObjectArgument() == nullptr ? QualType() : member_call->getImplicitObjectArgument()->getType();
                bool smart = std_class(object, "unique_ptr") || std_class(object, "shared_ptr");
                return !(smart && method != nullptr && method->getIdentifier() != nullptr && method->getName() == "reset");
            }
            return true;
        }
    }

    // C: a prvalue structure (the result of a call) whose member array is
    // reached through `.` chains.
    static const Expr *without_temporaries(const Expr *expr) {
        while (true) {
            expr = expr->IgnoreParenImpCasts();
            if (const auto *held = dyn_cast<MaterializeTemporaryExpr>(expr)) { expr = held->getSubExpr(); continue; }
            return expr;
        }
    }

    static bool temporary_base(const Expr *expr) {
        expr = without_temporaries(expr);
        while (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->isArrow()) return false;
            expr = without_temporaries(member->getBase());
        }
        const auto *call = dyn_cast<CallExpr>(expr);
        return call != nullptr && call->getType()->isRecordType() && !call->isLValue();
    }

    // C: an array member of a temporary that decays (shape temporary_decay on the
    // member) and a write to such an array's element or to a temporary's member
    // (shape temporary_write on the assignment or ++/--).
    void note_temporary_use(const Expr *expr, NodeRec &rec) {
        if (context_.getLangOpts().CPlusPlus) return;
        const Expr *target = nullptr;
        if (const auto *binary = dyn_cast<BinaryOperator>(expr)) {
            if (binary->isAssignmentOp()) target = binary->getLHS();
        } else if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            if (unary->isIncrementDecrementOp()) target = unary->getSubExpr();
        }
        if (target == nullptr) return;
        target = without_temporaries(target);
        if (const auto *element = dyn_cast<ArraySubscriptExpr>(target)) {
            const Expr *base = without_temporaries(element->getBase());
            if (isa<MemberExpr>(base) && temporary_base(base)) add_shape(rec, "temporary_write");
        } else if (isa<MemberExpr>(target) && temporary_base(target)) {
            add_shape(rec, "temporary_write");
        }
    }

    // C++: a non-static member or member function of the object, used through
    // this inside a handler of a constructor's or destructor's function-try-block.
    bool member_in_ctor_handler(const MemberExpr *member) const {
        const ValueDecl *used = member->getMemberDecl();
        bool instance = isa<FieldDecl>(used) || isa<IndirectFieldDecl>(used);
        if (const auto *method = dyn_cast<CXXMethodDecl>(used)) instance = !method->isStatic();
        if (!instance || !isa<CXXThisExpr>(member->getBase()->IgnoreParenImpCasts())) return false;
        DynTypedNode node = DynTypedNode::create(*static_cast<const Expr *>(member));
        bool in_handler = false;
        while (true) {
            auto parents = context_.getParents(node);
            if (parents.empty()) return false;
            node = parents[0];
            if (node.get<CXXCatchStmt>() != nullptr) in_handler = true;
            if (node.get<LambdaExpr>() != nullptr) return false;
            if (const auto *attempt = node.get<CXXTryStmt>()) {
                if (!in_handler) return false;
                auto up = context_.getParents(*attempt);
                if (up.size() != 1) return false;
                const auto *fn = up[0].get<FunctionDecl>();
                return fn != nullptr && (isa<CXXConstructorDecl>(fn) || isa<CXXDestructorDecl>(fn)) && fn->getBody() == attempt;
            }
            if (node.get<FunctionDecl>() != nullptr) return false;
        }
    }

    static bool user_location(const SourceManager &sm, SourceLocation loc) {
        SourceLocation spelled = sm.getSpellingLoc(loc);
        return spelled.isValid() && !sm.isInSystemHeader(spelled);
    }

    // Variables declared directly in `stmt` (a DeclStmt) with this name.
    static const VarDecl *declares(const Stmt *stmt, const IdentifierInfo *name) {
        const auto *group = dyn_cast_or_null<DeclStmt>(stmt);
        if (group == nullptr) return nullptr;
        for (const Decl *one : group->decls()) {
            const auto *var = dyn_cast<VarDecl>(one);
            if (var != nullptr && var->getIdentifier() == name) return var;
        }
        return nullptr;
    }

    // A local or parameter of a function definition that hides a local of an
    // enclosing block, a parameter, or a variable at file or namespace scope of
    // the user's (not in macro expansions, not across lambdas).
    bool shadows_outer(const Decl *decl) const {
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || var->getIdentifier() == nullptr || var->isImplicit() || var->hasExternalStorage()) return false;
        if (var->getLocation().isMacroID() || !user_location(sm_, var->getLocation())) return false;
        const IdentifierInfo *name = var->getIdentifier();
        const auto *fn = dyn_cast<FunctionDecl>(var->getDeclContext());
        if (fn == nullptr || !fn->doesThisDeclarationHaveABody()) return false;
        if (!isa<ParmVarDecl>(var)) {
            if (!var->isLocalVarDecl() && !var->isExceptionVariable()) return false;
            DynTypedNode node = DynTypedNode::create(*static_cast<const Decl *>(var));
            const Stmt *came = nullptr;
            while (true) {
                auto parents = context_.getParents(node);
                if (parents.size() != 1) return false;
                node = parents[0];
                if (node.get<FunctionDecl>() != nullptr) break;
                if (node.get<LambdaExpr>() != nullptr || node.get<BlockDecl>() != nullptr || node.get<CXXRecordDecl>() != nullptr) return false;
                const Stmt *here = node.get<Stmt>();
                if (here == nullptr) continue;
                if (const auto *block = dyn_cast<CompoundStmt>(here)) {
                    for (const Stmt *item : block->body()) {
                        if (item == came) break;
                        if (declares(item, name) != nullptr) return true;
                    }
                } else if (const auto *loop = dyn_cast<ForStmt>(here)) {
                    if (came != loop->getInit() && declares(loop->getInit(), name) != nullptr) return true;
                    if (loop->getConditionVariable() != nullptr && loop->getConditionVariable()->getIdentifier() == name && came != loop->getConditionVariableDeclStmt()) return true;
                } else if (const auto *branch = dyn_cast<IfStmt>(here)) {
                    if (came != branch->getInit() && declares(branch->getInit(), name) != nullptr) return true;
                    const VarDecl *cond = branch->getConditionVariable();
                    if (cond != nullptr && cond != var && cond->getIdentifier() == name && came != branch->getConditionVariableDeclStmt()) return true;
                } else if (const auto *again = dyn_cast<WhileStmt>(here)) {
                    const VarDecl *cond = again->getConditionVariable();
                    if (cond != nullptr && cond != var && cond->getIdentifier() == name && came != again->getConditionVariableDeclStmt()) return true;
                } else if (const auto *choice = dyn_cast<SwitchStmt>(here)) {
                    if (came != choice->getInit() && declares(choice->getInit(), name) != nullptr) return true;
                    const VarDecl *cond = choice->getConditionVariable();
                    if (cond != nullptr && cond != var && cond->getIdentifier() == name && came != choice->getConditionVariableDeclStmt()) return true;
                } else if (const auto *range = dyn_cast<CXXForRangeStmt>(here)) {
                    const VarDecl *item = range->getLoopVariable();
                    if (item != nullptr && item != var && item->getIdentifier() == name && came == range->getBody()) return true;
                    if (came != range->getInit() && declares(range->getInit(), name) != nullptr) return true;
                } else if (const auto *handler = dyn_cast<CXXCatchStmt>(here)) {
                    const VarDecl *caught = handler->getExceptionDecl();
                    if (caught != nullptr && caught != var && caught->getIdentifier() == name) return true;
                }
                came = here;
            }
            for (const ParmVarDecl *param : fn->parameters()) {
                if (param->getIdentifier() == name) return true;
            }
        }
        // a variable at file or namespace scope (the enclosing namespaces)
        const DeclContext *scope = fn->getDeclContext();
        while (scope != nullptr) {
            if (scope->isFileContext()) {
                for (const NamedDecl *found : scope->lookup(DeclarationName(name))) {
                    const auto *global = dyn_cast<VarDecl>(found);
                    if (global != nullptr && global != var && user_location(sm_, global->getLocation()) &&
                        sm_.isBeforeInTranslationUnit(global->getLocation(), var->getLocation())) return true;
                }
            }
            if (scope->isRecord()) return false;
            scope = scope->getParent();
        }
        return false;
    }

    // Two identifiers of one scope and name space that agree in the first 63
    // characters (the later one is marked).  Scopes: a file, namespace or
    // record; for locals, the statement that holds their declaration.
    bool long_name_clash(const Decl *decl) {
        const auto *named = dyn_cast<NamedDecl>(decl);
        if (named == nullptr || named->getIdentifier() == nullptr || named->isImplicit()) return false;
        llvm::StringRef name = named->getName();
        if (name.size() <= 63 || !user_location(sm_, named->getLocation())) return false;
        if (isa<TemplateDecl>(named) || named->isTemplated()) return false;
        const void *scope = named->getDeclContext();
        if (const auto *var = dyn_cast<VarDecl>(named)) {
            if (var->isLocalVarDecl()) {
                auto parents = context_.getParents(*var);
                if (parents.size() != 1) return false;
                auto above = context_.getParents(parents[0]);
                if (above.size() != 1 || above[0].get<Stmt>() == nullptr) return false;
                scope = above[0].get<Stmt>();
            }
        }
        if (isa<FunctionDecl>(named) && named->getDeclContext()->isFunctionOrMethod()) return false;
        std::string space = isa<TagDecl>(named) ? "tag:" : isa<LabelDecl>(named) ? "label:" : "name:";
        std::string key = space + name.substr(0, 63).str();
        // redeclarations of one entity are no clash
        const Decl *first = named->getCanonicalDecl();
        if (first != decl) return false;
        if (long_names_.count({scope, key}) > 0) {
            return !long_names_.count({scope, "full:" + name.str()});
        }
        long_names_.insert({scope, key});
        long_names_.insert({scope, "full:" + name.str()});
        return false;
    }

    // C++: a function, variable or enumerator named like a class or enum of the
    // same scope (both the user's).
    bool type_name_clash(const Decl *decl) const {
        if (!context_.getLangOpts().CPlusPlus) return false;
        const auto *named = dyn_cast<NamedDecl>(decl);
        if (named == nullptr || named->getIdentifier() == nullptr || named->isImplicit()) return false;
        if (!(isa<FunctionDecl>(named) || isa<VarDecl>(named) || isa<EnumConstantDecl>(named)) || isa<CXXMethodDecl>(named)) return false;
        if (isa<ParmVarDecl>(named) || named->getDeclContext()->isFunctionOrMethod()) return false;
        if (!user_location(sm_, named->getLocation()) || named->getCanonicalDecl() != decl) return false;
        const DeclContext *scope = named->getDeclContext();
        if (isa<EnumDecl>(scope)) scope = scope->getParent();
        for (const NamedDecl *found : scope->lookup(named->getDeclName())) {
            const auto *tag = dyn_cast<TagDecl>(found);
            if (tag != nullptr && !tag->isImplicit() && user_location(sm_, tag->getLocation())) return true;
        }
        return false;
    }

    static bool same_signature(const CXXMethodDecl *one, const CXXMethodDecl *other) {
        if (one->getNumParams() != other->getNumParams()) return false;
        if (one->getMethodQualifiers() != other->getMethodQualifiers() || one->getRefQualifier() != other->getRefQualifier()) return false;
        for (unsigned at = 0; at < one->getNumParams(); ++at) {
            QualType left = one->getParamDecl(at)->getType().getCanonicalType().getUnqualifiedType();
            QualType right = other->getParamDecl(at)->getType().getCanonicalType().getUnqualifiedType();
            if (left != right) return false;
        }
        return true;
    }

    // Is there an accessible function `name` with another signature in a base
    // reached through public or protected inheritance?
    static bool base_overload(const CXXRecordDecl *record, const CXXMethodDecl *method, int depth) {
        if (depth > 16) return false;
        for (const CXXBaseSpecifier &base : record->bases()) {
            if (base.getAccessSpecifier() == AS_private) continue;
            const CXXRecordDecl *parent = base.getType()->getAsCXXRecordDecl();
            if (parent == nullptr || !parent->hasDefinition()) return false;
            parent = parent->getDefinition();
            bool found = false;
            for (const CXXMethodDecl *other : parent->methods()) {
                if (other->getDeclName() != method->getDeclName() || other->isImplicit() || other->getAccess() == AS_private) continue;
                found = true;
                if (!same_signature(other, method)) return true;
            }
            for (const Decl *inner : parent->decls()) {
                if (const auto *shared = dyn_cast<FunctionTemplateDecl>(inner)) {
                    if (shared->getDeclName() == method->getDeclName() && shared->getAccess() != AS_private) return true;
                }
            }
            if (!found && base_overload(parent, method, depth + 1)) return true;
        }
        return false;
    }

    // C++: a member function that overrides nothing and hides a base function of
    // the same name with another signature (no using-declaration for the name).
    bool hides_base_function(const Decl *decl) const {
        const auto *method = dyn_cast<CXXMethodDecl>(decl);
        if (method == nullptr || method->isImplicit() || method->getIdentifier() == nullptr || isa<CXXConstructorDecl>(method) ||
            isa<CXXDestructorDecl>(method) || isa<CXXConversionDecl>(method) || method->size_overridden_methods() > 0) return false;
        if (method->getCanonicalDecl() != method || method->isDependentContext()) return false;
        const CXXRecordDecl *record = method->getParent();
        if (record == nullptr || !record->hasDefinition() || record->isLambda()) return false;
        if (!user_location(sm_, method->getLocation())) return false;
        for (const CXXBaseSpecifier &base : record->bases()) {
            if (base.getType()->isDependentType()) return false;
        }
        for (const Decl *inner : record->decls()) {
            if (const auto *brought = dyn_cast<UsingDecl>(inner)) {
                if (brought->getDeclName() == method->getDeclName()) return false;
            }
        }
        return base_overload(record, method, 0);
    }

    // C++: a user-provided copy constructor not taking const T&, a copy
    // assignment (not by value) not taking const T& or not returning T&, a move
    // constructor or assignment that is not noexcept or takes const T&&, a move
    // assignment not returning T&.  Defaulted and deleted ones are left out.
    bool odd_special_signature(const Decl *decl) const {
        const auto *method = dyn_cast<CXXMethodDecl>(decl);
        if (method == nullptr || method->isImplicit() || method->isDeleted() || method->isDefaulted() ||
            method->isDependentContext() || method->getCanonicalDecl() != method) return false;
        if (method->getNumParams() < 1 || !user_location(sm_, method->getLocation())) return false;
        // the pre-C++11 idiom of a private member declared and never defined
        if (method->getAccess() == AS_private && !method->isDefined()) return false;
        const CXXRecordDecl *record = method->getParent();
        QualType param = method->getParamDecl(0)->getType();
        auto nothrow = [&]() {
            const auto *proto = method->getType()->getAs<FunctionProtoType>();
            return proto != nullptr && !isUnresolvedExceptionSpec(proto->getExceptionSpecType()) && proto->isNothrow();
        };
        auto returns_self = [&]() {
            QualType result = method->getReturnType();
            const CXXRecordDecl *target = result->isLValueReferenceType() ? result->getPointeeType()->getAsCXXRecordDecl() : nullptr;
            return target != nullptr && target->getCanonicalDecl() == record->getCanonicalDecl() &&
                   !result->getPointeeType().isConstQualified();
        };
        if (const auto *ctor = dyn_cast<CXXConstructorDecl>(method)) {
            if (ctor->isCopyConstructor()) return !(param->isLValueReferenceType() && param->getPointeeType().isConstQualified());
            if (ctor->isMoveConstructor()) return !nothrow() || param->getPointeeType().isConstQualified();
            return false;
        }
        if (method->isCopyAssignmentOperator()) {
            if (!param->isReferenceType()) return false;   // copy-and-swap
            return !param->getPointeeType().isConstQualified() || !returns_self();
        }
        if (method->isMoveAssignmentOperator()) {
            return !nothrow() || param->getPointeeType().isConstQualified() || !returns_self();
        }
        return false;
    }

    // A modifiable variable with static storage at file or namespace scope (not
    // const, not volatile, not an extern declaration).
    bool global_variable(const Decl *decl) const {
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || var->isImplicit() || !var->getDeclContext()->getRedeclContext()->isFileContext()) return false;
        if (isa<ParmVarDecl>(var) || var->isConstexpr() || var->isThisDeclarationADefinition() == VarDecl::DeclarationOnly) return false;
        if (var->isTemplated() || var->getType()->isDependentType() || isa<VarTemplateSpecializationDecl>(var)) return false;
        QualType held = context_.getBaseElementType(var->getType());
        if (held.isConstQualified() || held.isVolatileQualified() || var->getType()->isReferenceType()) return false;
        return user_location(sm_, var->getLocation());
    }

    // C++: a class that declares some, not all, of copy constructor, copy
    // assignment, move constructor, move assignment and destructor.
    bool partial_special_members(const Decl *decl) const {
        const auto *record = dyn_cast<CXXRecordDecl>(decl);
        if (record == nullptr || !record->isThisDeclarationADefinition() || record->isLambda() || record->isUnion() ||
            record->isImplicit() || isa<ClassTemplateSpecializationDecl>(record) || !user_location(sm_, record->getLocation())) return false;
        bool copy_ctor = false, move_ctor = false, copy_assign = false, move_assign = false;
        for (const CXXConstructorDecl *ctor : record->ctors()) {
            if (ctor->isImplicit()) continue;
            copy_ctor = copy_ctor || ctor->isCopyConstructor();
            move_ctor = move_ctor || ctor->isMoveConstructor();
        }
        for (const CXXMethodDecl *method : record->methods()) {
            if (method->isImplicit()) continue;
            copy_assign = copy_assign || method->isCopyAssignmentOperator();
            move_assign = move_assign || method->isMoveAssignmentOperator();
        }
        const CXXDestructorDecl *dtor = record->getDestructor();
        bool destructor = dtor != nullptr && !dtor->isImplicit();
        int count = copy_ctor + move_ctor + copy_assign + move_assign + destructor;
        return count > 0 && count < 5;
    }

    // C++: a member function (not operator[], *, -> or an assignment) that
    // returns a non-const pointer or reference to a data member of its object
    // (shape member_handle on the returned expression).
    void note_member_handle(const ReturnStmt *back) {
        if (!context_.getLangOpts().CPlusPlus || back->getRetValue() == nullptr) return;
        auto parents = context_.getParents(*back);
        DynTypedNode node = parents.empty() ? DynTypedNode() : parents[0];
        const CXXMethodDecl *method = nullptr;
        while (!parents.empty()) {
            if (node.get<LambdaExpr>() != nullptr) return;
            if (const auto *fn = node.get<FunctionDecl>()) { method = dyn_cast<CXXMethodDecl>(fn); break; }
            parents = context_.getParents(node);
            if (parents.empty()) return;
            node = parents[0];
        }
        if (method == nullptr || method->isStatic() || method->isDependentContext()) return;
        OverloadedOperatorKind op = method->getOverloadedOperator();
        if (op == OO_Subscript || op == OO_Star || op == OO_Arrow || op == OO_Equal || method->isCopyAssignmentOperator() ||
            method->isMoveAssignmentOperator()) return;
        QualType result = method->getReturnType();
        bool reference = result->isReferenceType(), pointer = result->isPointerType();
        if (!reference && !pointer) return;
        if (result->getPointeeType().isConstQualified() || result->getPointeeType()->isFunctionType()) return;
        const Expr *value = back->getRetValue()->IgnoreParenImpCasts();
        if (pointer) {
            const auto *address = dyn_cast<UnaryOperator>(value);
            if (address != nullptr && address->getOpcode() == UO_AddrOf) {
                value = address->getSubExpr()->IgnoreParens();
            } else if (!(value->getType()->isArrayType() || isa<MemberExpr>(value))) {
                return;
            } else if (isa<MemberExpr>(value) && !cast<MemberExpr>(value)->getType()->isArrayType()) {
                return;   // a pointer member's value is not the member itself
            }
        }
        const auto *member = dyn_cast<MemberExpr>(value);
        if (member == nullptr || !isa<FieldDecl>(member->getMemberDecl())) return;
        if (!isa<CXXThisExpr>(member->getBase()->IgnoreParenImpCasts())) return;
        if (cast<FieldDecl>(member->getMemberDecl())->getParent() != method->getParent()) return;
        add_pending(back->getRetValue()->IgnoreParenImpCasts(), "member_handle");
    }

    // Bases reached through accessible paths: virtual, non-virtual or both.
    static void collect_bases(const CXXRecordDecl *record, std::map<const CXXRecordDecl *, int> &seen, int depth) {
        if (depth > 32) return;
        for (const CXXBaseSpecifier &base : record->bases()) {
            if (base.getAccessSpecifier() == AS_private) continue;
            const CXXRecordDecl *parent = base.getType()->getAsCXXRecordDecl();
            if (parent == nullptr || !parent->hasDefinition()) continue;
            parent = parent->getDefinition()->getCanonicalDecl();
            seen[parent] |= base.isVirtual() ? 1 : 2;
            collect_bases(parent->getDefinition(), seen, depth + 1);
        }
    }

    static bool has_mixed_base(const CXXRecordDecl *record) {
        std::map<const CXXRecordDecl *, int> seen;
        collect_bases(record, seen, 0);
        for (const auto &entry : seen) {
            if (entry.second == 3) return true;
        }
        return false;
    }

    // C++: a class whose hierarchy has an accessible base both as virtual and
    // as non-virtual base (where it first comes together: no base has it).
    bool mixed_virtual_base(const Decl *decl) const {
        const auto *record = dyn_cast<CXXRecordDecl>(decl);
        if (record == nullptr || !record->isThisDeclarationADefinition() || record->isDependentType() ||
            !user_location(sm_, record->getLocation())) return false;
        if (!has_mixed_base(record)) return false;
        for (const CXXBaseSpecifier &base : record->bases()) {
            const CXXRecordDecl *parent = base.getType()->getAsCXXRecordDecl();
            if (parent != nullptr && parent->hasDefinition() && has_mixed_base(parent->getDefinition())) return false;
        }
        return true;
    }

    // C++: a virtual function defined the second time (or later) on one path
    // below its first declaration (destructors left out).
    bool repeated_overrider(const Decl *decl) const {
        const auto *method = dyn_cast<CXXMethodDecl>(decl);
        if (method == nullptr || isa<CXXDestructorDecl>(method) || method->isPureVirtual() || method->isImplicit() ||
            method->size_overridden_methods() == 0 || method->getCanonicalDecl() != method || method->isDependentContext()) return false;
        if (!user_location(sm_, method->getLocation())) return false;
        int defined = 1;
        const CXXMethodDecl *walk = *method->begin_overridden_methods();
        while (walk != nullptr && walk->size_overridden_methods() > 0) {
            if (!walk->isPureVirtual()) ++defined;
            walk = *walk->begin_overridden_methods();
        }
        return defined >= 2;
    }

    // C++: a non-member function template of a named namespace (not detail,
    // internal or impl) that declares a class or enum, whose parameters are all
    // bare template type parameters (T, T&, const T&, T&&, packs of them), with
    // no constraint.
    bool open_function_template(const Decl *decl) const {
        const auto *shared = dyn_cast<FunctionTemplateDecl>(decl);
        if (shared == nullptr || !shared->isThisDeclarationADefinition() || !user_location(sm_, shared->getLocation())) return false;
        const FunctionDecl *fn = shared->getTemplatedDecl();
        if (fn == nullptr || isa<CXXMethodDecl>(fn) || fn->getFriendObjectKind() != Decl::FOK_None) return false;
        const auto *space = dyn_cast<NamespaceDecl>(fn->getDeclContext());
        if (space == nullptr || space->isAnonymousNamespace()) return false;
        for (const DeclContext *up = space; up != nullptr; up = up->getParent()) {
            if (const auto *named = dyn_cast<NamespaceDecl>(up)) {
                llvm::StringRef word = named->getName();
                if (word == "detail" || word == "details" || word == "internal" || word == "impl") return false;
            }
        }
        bool types = false;
        for (const Decl *inner : space->decls()) {
            if (const auto *tag = dyn_cast<TagDecl>(inner)) types = types || (!tag->isImplicit() && user_location(sm_, tag->getLocation()));
            if (isa<ClassTemplateDecl>(inner)) types = true;
        }
        if (!types) return false;
        if (fn->getTrailingRequiresClause() || shared->getTemplateParameters()->getRequiresClause() != nullptr) return false;
        for (const NamedDecl *param : *shared->getTemplateParameters()) {
            if (const auto *kind = dyn_cast<TemplateTypeParmDecl>(param)) {
                if (kind->hasTypeConstraint()) return false;
            } else if (const auto *value = dyn_cast<NonTypeTemplateParmDecl>(param)) {
                std::string spelled = value->getType().getAsString();
                if (spelled.find("enable_if") != std::string::npos) return false;
            }
        }
        std::string result = fn->getReturnType().getAsString();
        if (result.find("enable_if") != std::string::npos || result.find("decltype") != std::string::npos) return false;
        bool open = false;
        for (const ParmVarDecl *param : fn->parameters()) {
            std::string spelled = param->getType().getAsString();
            if (spelled.find("enable_if") != std::string::npos) return false;
            QualType bare = param->getType().getNonReferenceType().getUnqualifiedType();
            if (const auto *expansion = bare->getAs<PackExpansionType>()) bare = expansion->getPattern().getNonReferenceType().getUnqualifiedType();
            const auto *type_param = bare->getAs<TemplateTypeParmType>();
            if (type_param == nullptr || type_param->getDepth() != shared->getTemplateParameters()->getDepth()) return false;
            open = true;
        }
        return open;
    }

    // C++: volatile on a parameter or a return type (marked on the function), or
    // on a non-static local whose address is never taken.
    bool pointless_volatile(const Decl *decl) const {
        if (!context_.getLangOpts().CPlusPlus || !user_location(sm_, decl->getLocation())) return false;
        if (const auto *fn = dyn_cast<FunctionDecl>(decl)) {
            if (fn->getCanonicalDecl() != fn || fn->isImplicit()) return false;
            if (fn->getReturnType().isVolatileQualified()) return true;
            for (const ParmVarDecl *param : fn->parameters()) {
                if (param->getType().isVolatileQualified()) return true;
            }
            return false;
        }
        const auto *var = dyn_cast<VarDecl>(decl);
        if (var == nullptr || isa<ParmVarDecl>(var) || !var->isLocalVarDecl() || var->isStaticLocal() ||
            !var->getType().isVolatileQualified()) return false;
        const auto *owner = dyn_cast<FunctionDecl>(var->getDeclContext());
        if (owner == nullptr || owner->getBody() == nullptr) return false;
        return !address_taken(owner->getBody(), var);
    }

    static bool address_taken(const Stmt *stmt, const VarDecl *var) {
        if (stmt == nullptr) return false;
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->getOpcode() == UO_AddrOf) {
                const auto *ref = dyn_cast<DeclRefExpr>(unary->getSubExpr()->IgnoreParens());
                if (ref != nullptr && ref->getDecl() == var) return true;
            }
        }
        if (const auto *lambda = dyn_cast<LambdaExpr>(stmt)) {
            for (const LambdaCapture &capture : lambda->captures()) {
                if (capture.capturesVariable() && capture.getCapturedVar() == var && capture.getCaptureKind() == LCK_ByRef) return true;
            }
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            // bound to a reference or passed to a reference parameter
            (void)ref;
        }
        for (const Stmt *child : stmt->children()) {
            if (address_taken(child, var)) return true;
        }
        return false;
    }

    // The start of qualifier keywords written right before `loc` (`const int`:
    // the const), at the spelling.
    SourceLocation leading_qualifiers(SourceLocation loc) const {
        SourceLocation at = sm_.getSpellingLoc(loc);
        if (at.isInvalid()) return loc;
        std::pair<FileID, unsigned> place = sm_.getDecomposedLoc(at);
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(place.first, &invalid);
        if (invalid) return loc;
        unsigned start = place.second;
        while (true) {
            unsigned back = start;
            while (back > 0 && (buffer[back - 1] == ' ' || buffer[back - 1] == '\t')) --back;
            unsigned word_end = back;
            while (back > 0 && (isalnum(static_cast<unsigned char>(buffer[back - 1])) || buffer[back - 1] == '_')) --back;
            llvm::StringRef word = buffer.substr(back, word_end - back);
            if (word != "const" && word != "volatile" && word != "restrict" && word != "_Atomic") break;
            start = back;
        }
        return sm_.getComposedLoc(place.first, start);
    }

    // A mark at the spelling of `loc` (a macro body's own line), once.
    void add_spelled_mark(const char *code, SourceLocation loc) {
        SourceLocation at = sm_.getSpellingLoc(loc);
        if (at.isInvalid() || sm_.isInSystemHeader(at) || sm_.isWrittenInScratchSpace(at)) return;
        std::pair<FileID, unsigned> place = sm_.getDecomposedLoc(at);
        if (!ast_mark_seen_.insert(place).second) return;
        add_mark(ast_marks_[place.first], code, place.first, place.second);
    }

    // _Generic: an association type that is qualified, an array, a function or
    // atomic (generic_type at the type), and a pointer controlling expression
    // that reaches `default` although an association takes it by implicit
    // conversion (generic_pointer at the _Generic).
    void note_generic_types(const GenericSelectionExpr *generic) {
        if (generic->isResultDependent() || context_.getLangOpts().CPlusPlus) return;
        bool default_picked = false;
        for (const auto association : generic->associations()) {
            const TypeSourceInfo *written = association.getTypeSourceInfo();
            if (written == nullptr) {
                default_picked = association.isSelected();
                continue;
            }
            QualType type = written->getType();
            if (type.isNull() || type->isDependentType()) continue;
            if (type.hasQualifiers() || type->isArrayType() || type->isFunctionType() || type->isAtomicType()) {
                add_spelled_mark("generic_type", leading_qualifiers(written->getTypeLoc().getBeginLoc()));
            }
        }
        if (!default_picked || !generic->isExprPredicate() || generic->getControllingExpr() == nullptr) return;
        QualType held = generic->getControllingExpr()->getType();
        if (held.isNull() || !held->isPointerType()) return;
        QualType from = held->getPointeeType();
        unsigned have = from.getCVRQualifiers();
        for (const auto association : generic->associations()) {
            const TypeSourceInfo *written = association.getTypeSourceInfo();
            if (written == nullptr || !written->getType()->isPointerType()) continue;
            QualType to = written->getType()->getPointeeType();
            unsigned want = to.getCVRQualifiers();
            bool keeps = (want & have) == have;
            bool to_void = to->isVoidType() && !from->isVoidType() && keeps;
            bool more_qualified = context_.hasSameUnqualifiedType(to, from) && keeps && want != have;
            if (to_void || more_qualified) {
                add_ast_mark("generic_pointer", generic->getBeginLoc());
                return;
            }
        }
    }

    // sizeof(...) * CHAR_BIT (either order) of an integer type.
    bool width_from_size(const BinaryOperator *product) const {
        if (product->getOpcode() != BO_Mul) return false;
        for (int side = 0; side < 2; ++side) {
            const Expr *size = (side == 0 ? product->getLHS() : product->getRHS())->IgnoreParenImpCasts();
            const Expr *bits = (side == 0 ? product->getRHS() : product->getLHS())->IgnoreParenImpCasts();
            const auto *of = dyn_cast<UnaryExprOrTypeTraitExpr>(size);
            if (of == nullptr || of->getKind() != UETT_SizeOf) continue;
            QualType measured = of->getTypeOfArgument();
            if (measured.isNull() || !measured->isIntegerType()) continue;
            if (!bits->getBeginLoc().isMacroID()) continue;
            std::string spelled = expansion_text(bits);
            if (spelled == "CHAR_BIT" || spelled == "__CHAR_BIT__") return true;
        }
        return false;
    }

    // Member names a base brings, with the record that declares each (virtual
    // bases by their canonical record, so a shared one is one origin).
    static void inherited_names(const CXXRecordDecl *record, std::map<std::string, std::set<const void *>> &names,
                                std::set<const void *> &origin_of_path, int depth) {
        if (depth > 16 || record == nullptr || !record->hasDefinition()) return;
        record = record->getDefinition();
        for (const Decl *inner : record->decls()) {
            const auto *named = dyn_cast<NamedDecl>(inner);
            if (named == nullptr || named->isImplicit() || named->getIdentifier() == nullptr) continue;
            if (!(isa<FieldDecl>(named) || isa<CXXMethodDecl>(named) || isa<FunctionTemplateDecl>(named) || isa<VarDecl>(named))) continue;
            if (isa<CXXConstructorDecl>(named) || isa<CXXDestructorDecl>(named)) continue;
            if (named->getAccess() == AS_private) continue;
            for (const void *origin : origin_of_path) names[named->getName().str()].insert(origin);
        }
        for (const CXXBaseSpecifier &base : record->bases()) {
            const CXXRecordDecl *parent = base.getType()->getAsCXXRecordDecl();
            if (parent == nullptr || base.getAccessSpecifier() == AS_private) continue;
            std::set<const void *> path = origin_of_path;
            if (base.isVirtual()) path = {parent->getCanonicalDecl()};
            inherited_names(parent, names, path, depth + 1);
        }
    }

    // C++: a class that inherits one member name from two different bases (not
    // one shared virtual base) and does not declare or bring in the name.
    bool ambiguous_inherited(const Decl *decl) const {
        const auto *record = dyn_cast<CXXRecordDecl>(decl);
        if (record == nullptr || !record->isThisDeclarationADefinition() || record->isDependentType() || record->isLambda() ||
            record->getNumBases() < 2 || !user_location(sm_, record->getLocation())) return false;
        std::map<std::string, std::set<const void *>> names;
        for (const CXXBaseSpecifier &base : record->bases()) {
            const CXXRecordDecl *parent = base.getType()->getAsCXXRecordDecl();
            if (parent == nullptr) return false;
            if (base.getAccessSpecifier() == AS_private) continue;
            std::set<const void *> path = {base.isVirtual() ? static_cast<const void *>(parent->getCanonicalDecl()) : static_cast<const void *>(&base)};
            inherited_names(parent, names, path, 0);
        }
        for (const auto &entry : names) {
            if (entry.second.size() < 2) continue;
            bool own = false;
            for (const NamedDecl *found : record->lookup(DeclarationName(&context_.Idents.get(entry.first)))) {
                own = own || found->getDeclContext() == record;
            }
            for (const Decl *inner : record->decls()) {
                if (const auto *brought = dyn_cast<UsingDecl>(inner)) own = own || brought->getName() == entry.first;
            }
            if (!own) return true;
        }
        return false;
    }

    // Can a statement throw: a throw, or a call, construction or new of
    // something not noexcept.
    bool may_throw(const Stmt *stmt) const {
        if (stmt == nullptr) return false;
        if (isa<CXXThrowExpr>(stmt) || isa<CXXNewExpr>(stmt)) return true;
        if (isa<LambdaExpr>(stmt) || isa<UnaryExprOrTypeTraitExpr>(stmt) || isa<CXXNoexceptExpr>(stmt)) return false;
        auto nothrow = [](const FunctionDecl *fn) {
            if (fn == nullptr) return false;
            const auto *proto = fn->getType()->getAs<FunctionProtoType>();
            return proto != nullptr && !isUnresolvedExceptionSpec(proto->getExceptionSpecType()) && proto->isNothrow();
        };
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            if (call->getBuiltinCallee() == 0 && !nothrow(call->getDirectCallee())) return true;
        }
        if (const auto *make = dyn_cast<CXXConstructExpr>(stmt)) {
            if (!make->getConstructor()->isTrivial() && !nothrow(make->getConstructor())) return true;
        }
        for (const Stmt *child : stmt->children()) {
            if (may_throw(child)) return true;
        }
        return false;
    }

    static bool catches_all(const CXXTryStmt *attempt) {
        for (unsigned at = 0; at < attempt->getNumHandlers(); ++at) {
            if (attempt->getHandler(at)->getExceptionDecl() == nullptr) return true;
        }
        return false;
    }

    // C++ with exceptions: a main whose statements that can throw are not all
    // inside a try block with a catch (...) handler.
    bool unguarded_main(const Decl *decl) const {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !context_.getLangOpts().CPlusPlus || !context_.getLangOpts().CXXExceptions || !fn->isMain() ||
            !fn->doesThisDeclarationHaveABody() || fn->getBody() == nullptr) return false;
        if (const auto *whole = dyn_cast<CXXTryStmt>(fn->getBody())) return !catches_all(whole);
        const auto *body = dyn_cast<CompoundStmt>(fn->getBody());
        if (body == nullptr) return false;
        for (const Stmt *item : body->body()) {
            if (const auto *attempt = dyn_cast<CXXTryStmt>(item)) {
                if (catches_all(attempt)) continue;
            }
            if (may_throw(item)) return true;
        }
        return false;
    }

    // C++: a declaration in a namespace after a using-declaration of its name
    // from that namespace (the using-declaration does not see it).
    bool straddled_by_using(const Decl *decl) {
        if (!context_.getLangOpts().CPlusPlus) return false;
        if (const auto *brought = dyn_cast<UsingDecl>(decl)) {
            if (user_location(sm_, brought->getLocation())) usings_.push_back(brought);
            return false;
        }
        const auto *named = dyn_cast<NamedDecl>(decl);
        if (named == nullptr || named->getIdentifier() == nullptr || named->isImplicit() || !user_location(sm_, named->getLocation())) return false;
        if (!(isa<FunctionDecl>(named) || isa<FunctionTemplateDecl>(named))) return false;
        const auto *space = dyn_cast<NamespaceDecl>(named->getDeclContext());
        if (space == nullptr) return false;
        for (const UsingDecl *brought : usings_) {
            if (brought->getDeclName() != named->getDeclName()) continue;
            const NamespaceDecl *target = nullptr;
            for (const UsingShadowDecl *shadow : brought->shadows()) {
                target = dyn_cast<NamespaceDecl>(shadow->getTargetDecl()->getDeclContext());
                if (target != nullptr) break;
            }
            if (target != nullptr && target->getCanonicalDecl() == space->getCanonicalDecl() &&
                sm_.isBeforeInTranslationUnit(brought->getLocation(), named->getLocation())) return true;
        }
        return false;
    }

    // C++: in an implicit instantiation, a named non-member call whose function
    // (its first declaration, the user's) comes after the template's definition
    // (mark late_call at the call).
    void note_late_calls(const Decl *decl) {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !context_.getLangOpts().CPlusPlus || fn->getTemplateSpecializationKind() != TSK_ImplicitInstantiation) return;
        const FunctionDecl *pattern = fn->getTemplateInstantiationPattern();
        if (pattern == nullptr || fn->getBody() == nullptr || !user_location(sm_, pattern->getLocation())) return;
        late_calls_in(fn->getBody(), pattern->getLocation());
    }

    void late_calls_in(const Stmt *stmt, SourceLocation defined) {
        if (stmt == nullptr) return;
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            const FunctionDecl *callee = call->getDirectCallee();
            if (callee != nullptr && !isa<CXXOperatorCallExpr>(call) && !isa<CXXMemberCallExpr>(call) && !isa<CXXMethodDecl>(callee) &&
                callee->getIdentifier() != nullptr) {
                const FunctionDecl *first = callee->getCanonicalDecl();
                if (const FunctionTemplateDecl *primary = callee->getPrimaryTemplate()) first = primary->getCanonicalDecl()->getTemplatedDecl();
                SourceLocation declared = first->getLocation();
                if (declared.isValid() && user_location(sm_, declared) && call->getBeginLoc().isValid() &&
                    sm_.isBeforeInTranslationUnit(defined, declared)) {
                    add_named_mark("late_call", call->getBeginLoc(), callee->getName());
                }
            }
        }
        for (const Stmt *child : stmt->children()) late_calls_in(child, defined);
    }

    // Interns one statement or expression (NodeCollector).
    int node_of(const Stmt *stmt, int parent_uid, int function_uid) {
        NodeRec rec;
        rec.uid = static_cast<int>(nodes.size());
        rec.kind = stmt->getStmtClassName();
        rec.pos = src_pos_of(stmt->getBeginLoc());
        rec.from_macro = stmt->getBeginLoc().isMacroID();
        SourceLocation spelled = sm_.getSpellingLoc(stmt->getBeginLoc());
        rec.in_system_header = spelled.isValid() && sm_.isInSystemHeader(spelled);
        rec.parent_uid = parent_uid;
        rec.function_uid = function_uid;
        auto role = roles_.find(stmt);
        if (role != roles_.end()) rec.role = role->second;
        note_roles(stmt);
        if (isa<IntegerLiteral>(stmt) || isa<FloatingLiteral>(stmt) || isa<CharacterLiteral>(stmt) ||
            isa<StringLiteral>(stmt)) {
            rec.spelling = literal_spelling(stmt);
        }
        if (isa<NullStmt>(stmt)) rec.before = char_before(stmt->getBeginLoc());
        rec.errno_write = errno_write(stmt);
        if (const auto *list = dyn_cast<InitListExpr>(stmt)) note_init_list(list, rec);
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            note_move(call);
            note_array_decay(call);
            if (unused_result(call)) add_shape(rec, "unused_result");
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) note_precedence(binary);
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (flexible_copy(binary)) add_shape(rec, "flexible_copy");
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) note_atomic_refs(ref);
        if (const auto *call = dyn_cast<CallExpr>(stmt)) note_thread_calls(call);
        if (const auto *call = dyn_cast<CallExpr>(stmt)) note_library_calls(call);
        if (const auto *call = dyn_cast<CallExpr>(stmt)) note_signal_calls(call);
        note_fgets_index(stmt);
        note_process_calls(stmt);
        note_stream_positions(stmt);
        note_batch35(stmt);
        if (const auto *block = dyn_cast<CompoundStmt>(stmt)) note_dead_statements(block);
        note_batch37(stmt);
        note_batch39(stmt);
        if (const auto *compare = dyn_cast<BinaryOperator>(stmt)) note_eof_compare(compare);
        if (const auto *make = dyn_cast<CXXConstructExpr>(stmt)) {
            if (std_class(make->getType(), "thread") || std_class(make->getType(), "jthread")) creates_threads_ = true;
        }
        if (const auto *atomic = dyn_cast<AtomicExpr>(stmt)) {
            if (atomic->getOp() == AtomicExpr::AO__c11_atomic_compare_exchange_weak && !inside_loop(atomic)) {
                add_ast_mark("unlooped_wait", atomic->getBeginLoc());
            }
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->isIncrementDecrementOp() && increment_with_effects(unary)) add_shape(rec, "busy_increment");
        }
        if (const auto *small = dyn_cast<Expr>(stmt)) {
            if (small_bitwise(small)) add_shape(rec, "small_bitwise");
        }
        if (const auto *lambda = dyn_cast<LambdaExpr>(stmt)) note_lambda(lambda, rec);
        if (const auto *back = dyn_cast<ReturnStmt>(stmt)) note_member_handle(back);
        if (const auto *generic = dyn_cast<GenericSelectionExpr>(stmt)) note_generic_types(generic);
        if (const auto *product = dyn_cast<BinaryOperator>(stmt)) {
            if (width_from_size(product)) add_shape(rec, "width_from_size");
        }
        if (const auto *generic = dyn_cast<GenericSelectionExpr>(stmt)) {
            if (!generic->getBeginLoc().isMacroID()) {
                add_shape(rec, "written_generic");
                const Expr *control = generic->isExprPredicate() ? generic->getControllingExpr() : nullptr;
                if (control != nullptr && control->HasSideEffects(context_)) add_pending(control, "generic_side_effect");
            }
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) note_scaled_offset(binary);
        if (const auto *explicit_cast = dyn_cast<ExplicitCastExpr>(stmt)) {
            if (alignment_increase(explicit_cast)) add_shape(rec, "align_increase");
        }
        if (const auto *fresh = dyn_cast<CXXNewExpr>(stmt)) {
            if (unmanaged_new(fresh)) add_shape(rec, "unmanaged_new");
        }
        if (const auto *expr = dyn_cast<Expr>(stmt)) note_temporary_use(expr, rec);
        if (const auto *member = dyn_cast<MemberExpr>(stmt)) {
            if (member_in_ctor_handler(member)) add_shape(rec, "handler_member");
        }
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            if (char_widened(expr)) add_shape(rec, "char_widened");
        }
        if (const auto *widen = dyn_cast<ExplicitCastExpr>(stmt)) {
            if (char_cast_widened(widen)) {
                SourceLocation written = sm_.getSpellingLoc(widen->getBeginLoc());
                if (written.isValid() && sm_.isInSystemHeader(written)) {
                    add_pending(widen->getSubExpr()->IgnoreParenImpCasts(), "char_widened");
                } else {
                    add_shape(rec, "char_widened");
                }
            }
        }
        note_float_counter(stmt);
        if (isa<IntegerLiteral>(stmt) || isa<GNUNullExpr>(stmt)) {
            if (const char *null = null_shape(cast<Expr>(stmt))) rec.shape = null;
        }
        if (const auto *explicit_cast = dyn_cast<ExplicitCastExpr>(stmt)) note_cast(explicit_cast, rec);
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) note_member_compare(binary, rec);
        if (const auto *explicit_cast = dyn_cast<ExplicitCastExpr>(stmt)) note_pointer_casts(explicit_cast, rec);
        auto shaped = pending_shape_.find(stmt);
        if (shaped != pending_shape_.end()) add_shape(rec, shaped->second);
        if (const auto *attempt = dyn_cast<CXXTryStmt>(stmt)) note_shadowed_handlers(attempt);
        SourceLocation key = stmt->getBeginLoc();
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) key = binary->getOperatorLoc();
        if (const auto *choice = dyn_cast<AbstractConditionalOperator>(stmt)) key = choice->getQuestionLoc();
        rec.macro_body = in_macro_body(key);
        for (const Stmt *child : stmt->children()) {
            if (child != nullptr) ++rec.child_count;
        }
        if (isa<SwitchStmt>(stmt) || isa<IfStmt>(stmt) || parent_uid < 0) {
            rec.has_skipped_code = skips_inside(stmt->getSourceRange());
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            rec.detail = binary->getOpcodeStr().str();
            // The operator written in a macro body, its operands passed as macro
            // arguments: the operator belongs to the macro, the node is placed there.
            SourceLocation op_loc = binary->getOperatorLoc();
            if (op_loc.isMacroID() && !sm_.isMacroArgExpansion(op_loc)) {
                rec.pos = src_pos_of(op_loc);
                rec.from_macro = true;
                rec.in_system_header = sm_.isInSystemHeader(sm_.getSpellingLoc(op_loc));
            }
        } else if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            rec.detail = UnaryOperator::getOpcodeStr(unary->getOpcode()).str();
        } else if (const auto *named = dyn_cast<CXXNamedCastExpr>(stmt)) {
            rec.detail = named->getCastName();
        } else if (const auto *jump = dyn_cast<GotoStmt>(stmt)) {
            rec.detail = jump->getLabel()->getName().str();
            rec.ref_uid = decl_of(jump->getLabel());
        } else if (const auto *label = dyn_cast<LabelStmt>(stmt)) {
            rec.detail = label->getName();
            rec.ref_uid = decl_of(label->getDecl());
        } else if (const auto *ref = dyn_cast<DeclRefExpr>(stmt)) {
            rec.ref_uid = decl_of(ref->getDecl());
        } else if (const auto *member = dyn_cast<MemberExpr>(stmt)) {
            rec.ref_uid = decl_of(member->getMemberDecl());
            rec.detail = member->isArrow() ? "->" : ".";
        } else if (const auto *literal = dyn_cast<IntegerLiteral>(stmt)) {
            rec.detail = llvm::toString(literal->getValue(), 10, false);
        } else if (const auto *fresh = dyn_cast<CXXNewExpr>(stmt)) {
            rec.detail = placement_new(fresh) ? "placement" : "";
        } else if (const auto *member_call = dyn_cast<CXXMemberCallExpr>(stmt)) {
            rec.detail = member_call->getMethodDecl() != nullptr && isa<CXXDestructorDecl>(member_call->getMethodDecl()) ? "destructor" : "";
        } else if (const auto *op_call = dyn_cast<CXXOperatorCallExpr>(stmt)) {
            rec.detail = getOperatorSpelling(op_call->getOperator());
        } else if (const auto *branch = dyn_cast<IfStmt>(stmt)) {
            rec.detail = branch->getElse() != nullptr ? "else" : "";
        } else if (const auto *address = dyn_cast<AddrLabelExpr>(stmt)) {
            rec.ref_uid = decl_of(address->getLabel());
        } else if (const auto *handler = dyn_cast<CXXCatchStmt>(stmt)) {
            rec.detail = handler->getExceptionDecl() == nullptr ? "..." : "";
            if (const VarDecl *caught = handler->getExceptionDecl()) {
                rec.ref_uid = decl_of(caught);
                rec.type_uid = type_of(handler->getCaughtType());
                if (shadowed_handlers_.count(handler) > 0) {
                    rec.detail = "shadowed";
                    SourceLocation name = caught_name_loc(caught);
                    if (name.isValid()) decls[static_cast<size_t>(rec.ref_uid)].type_pos = pos_of(sm_.getExpansionLoc(name));
                }
            }
        } else if (const auto *trait = dyn_cast<UnaryExprOrTypeTraitExpr>(stmt)) {
            if (trait->getKind() == UETT_SizeOf) rec.detail = trait->isArgumentType() ? "sizeof_type" : "sizeof";
            if (!trait->isArgumentType() && !trait->getArgumentExpr()->isInstantiationDependent() &&
                !trait->getArgumentExpr()->getType()->isVariablyModifiedType()) {
                rec.side_effect = trait->getArgumentExpr()->HasSideEffects(context_, false);
            }
        } else if (const auto *typeid_expr = dyn_cast<CXXTypeidExpr>(stmt)) {
            rec.detail = typeid_expr->isTypeOperand() ? "type"
                       : !typeid_expr->isValueDependent() && typeid_expr->isPotentiallyEvaluated() ? "polymorphic" : "expr";
        } else if (const auto *removal = dyn_cast<CXXDeleteExpr>(stmt)) {
            add_shape(rec, "raw_delete");
            QualType destroyed = removal->getDestroyedType();
            if (!destroyed.isNull() && !destroyed->isDependentType() && destroyed->isRecordType() &&
                destroyed->isIncompleteType()) {
                rec.detail = "incomplete";
            }
        } else if (const auto *choice = dyn_cast<SwitchStmt>(stmt)) {
            note_switch(choice, rec);
        } else if (isa<DefaultStmt>(stmt)) {
            auto known = default_order_.find(stmt);
            rec.detail = known != default_order_.end() ? known->second : "";
        }
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            if (rec.ref_uid < 0 && !isa<CXXMemberCallExpr>(call) && !isa<CXXOperatorCallExpr>(call) && call->getDirectCallee() != nullptr) {
                rec.ref_uid = decl_of(call->getDirectCallee());
            }
            const auto *callee = dyn_cast_or_null<FunctionDecl>(call->getCalleeDecl());
            if (callee != nullptr && !isa<CXXOperatorCallExpr>(call)) {
                OverloadedOperatorKind op = callee->getOverloadedOperator();
                if (op == OO_New || op == OO_Delete || op == OO_Array_New || op == OO_Array_Delete) rec.detail = "operator_new_delete";
            }
        }
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            rec.type_uid = type_of(expr->getType());
            rec.is_dependent = expr->isTypeDependent() || expr->isValueDependent();
        }
        if (const auto *generic = dyn_cast<GenericSelectionExpr>(stmt)) {
            rec.detail = generic->getNumAssocs() == 1 && generic->getAssociation(0).getTypeSourceInfo() == nullptr ? "default_only" : "";
        }
        nodes.push_back(rec);
        // A smart pointer made from a raw pointer: a marker node at that argument
        // (OC-0371 no_raw_smart_pointer_ctor).
        if (const Expr *raw = raw_smart_pointer_arg(stmt)) {
            NodeRec arg;
            arg.uid = static_cast<int>(nodes.size());
            arg.kind = "RawSmartPointer";
            arg.pos = src_pos_of(raw->getBeginLoc());
            arg.from_macro = raw->getBeginLoc().isMacroID();
            arg.in_system_header = rec.in_system_header;
            arg.parent_uid = rec.uid;
            arg.function_uid = function_uid;
            nodes.push_back(arg);
            return rec.uid;
        }
        // The range of a range-based for: a marker node at its start that counts the
        // function calls in it (OC-0345 simple_range_initializer).
        if (const auto *loop = dyn_cast<CXXForRangeStmt>(stmt)) {
            if (const Expr *range = loop->getRangeInit()) {
                NodeRec calls;
                calls.uid = static_cast<int>(nodes.size());
                calls.kind = "RangeInitCalls";
                calls.detail = std::to_string(count_calls(range));
                calls.pos = src_pos_of(range->getBeginLoc());
                calls.from_macro = range->getBeginLoc().isMacroID();
                calls.in_system_header = rec.in_system_header;
                calls.parent_uid = rec.uid;
                calls.function_uid = function_uid;
                nodes.push_back(calls);
            }
            return rec.uid;
        }
        // A default association between others: a marker node at `default`
        // (OC-0234 generic_default_position).
        if (const auto *generic = dyn_cast<GenericSelectionExpr>(stmt)) {
            unsigned count = generic->getNumAssocs();
            for (unsigned at = 1; at + 1 < count; ++at) {
                if (generic->getAssociation(at).getTypeSourceInfo() != nullptr) continue;
                NodeRec middle;
                middle.uid = static_cast<int>(nodes.size());
                middle.kind = "GenericDefaultMiddle";
                middle.pos = src_pos_of(generic->getDefaultLoc());
                middle.from_macro = generic->getDefaultLoc().isMacroID();
                middle.in_system_header = rec.in_system_header;
                middle.parent_uid = rec.uid;
                middle.function_uid = function_uid;
                nodes.push_back(middle);
            }
            return rec.uid;
        }
        // An `else if` whose `if` has no `else`: the chain is open.  A marker node at
        // the `else` keyword of the outer if (OC-0110 final_else).
        if (const auto *branch = dyn_cast<IfStmt>(stmt)) {
            const auto *inner = dyn_cast_or_null<IfStmt>(branch->getElse());
            if (inner != nullptr && inner->getElse() == nullptr) {
                NodeRec open;
                open.uid = static_cast<int>(nodes.size());
                open.kind = "OpenElseIfChain";
                open.pos = src_pos_of(branch->getElseLoc());
                open.from_macro = branch->getElseLoc().isMacroID();
                open.in_system_header = rec.in_system_header;
                open.parent_uid = rec.uid;
                open.function_uid = function_uid;
                open.has_skipped_code = rec.has_skipped_code;
                nodes.push_back(open);
                return rec.uid;
            }
        }
        return rec.uid;
    }

    // Placement new other than the non-allocating nothrow form.
    static bool placement_new(const CXXNewExpr *fresh) {
        if (fresh->getNumPlacementArgs() == 0) return false;
        if (fresh->getNumPlacementArgs() == 1) {
            QualType arg = fresh->getPlacementArg(0)->IgnoreParenImpCasts()->getType();
            const CXXRecordDecl *record = arg.isNull() ? nullptr : arg->getAsCXXRecordDecl();
            if (record != nullptr && record->getIdentifier() != nullptr && record->getName() == "nothrow_t") return false;
        }
        return true;
    }

    static bool smart_pointer_class(const CXXRecordDecl *record) {
        if (record == nullptr || record->getIdentifier() == nullptr) return false;
        if (record->getName() != "shared_ptr" && record->getName() != "unique_ptr") return false;
        return record->isInStdNamespace();
    }

    // The raw pointer argument of a std::shared_ptr or std::unique_ptr constructor
    // or reset call, if there is one.
    static const Expr *raw_smart_pointer_arg(const Stmt *stmt) {
        const Expr *arg = nullptr;
        if (const auto *make = dyn_cast<CXXConstructExpr>(stmt)) {
            if (make->getNumArgs() >= 1 && smart_pointer_class(make->getConstructor()->getParent())) arg = make->getArg(0);
        } else if (const auto *call = dyn_cast<CXXMemberCallExpr>(stmt)) {
            const CXXMethodDecl *method = call->getMethodDecl();
            if (method != nullptr && method->getIdentifier() != nullptr && method->getName() == "reset" && call->getNumArgs() >= 1 &&
                smart_pointer_class(method->getParent())) {
                arg = call->getArg(0);
            }
        }
        if (arg == nullptr || isa<CXXDefaultArgExpr>(arg)) return nullptr;
        const Expr *bare = arg->IgnoreParenImpCasts();
        if (bare->isTypeDependent() || !bare->getType()->isPointerType()) return nullptr;
        return bare;
    }

    // The function calls written in an expression: calls of functions, members and
    // overloaded operators; implicit conversion functions and lambda bodies do not
    // count.
    static int count_calls(const Stmt *stmt) {
        if (stmt == nullptr || isa<LambdaExpr>(stmt)) return 0;
        int count = 0;
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            const auto *member = dyn_cast<CXXMemberCallExpr>(call);
            bool conversion = member != nullptr && member->getMethodDecl() != nullptr &&
                              isa<CXXConversionDecl>(member->getMethodDecl());
            if (!conversion) ++count;
        }
        for (const Stmt *child : stmt->children()) count += count_calls(child);
        return count;
    }

    // The token at `loc` comes from a macro body (also of a macro called in a macro
    // argument), not from the file or from an argument written there.
    bool in_macro_body(SourceLocation loc) const {
        while (loc.isMacroID()) {
            if (!sm_.isMacroArgExpansion(loc)) return true;
            loc = sm_.getImmediateSpellingLoc(loc);
        }
        return loc.isValid() && sm_.isWrittenInScratchSpace(loc);
    }

    // The labels of a switch: how many, how many clauses (a top-level label of the
    // body starts a clause; `case 1: case 2:` nests and is one clause), and where
    // its default label sits in source order (first, last, middle, only).
    void note_switch(const SwitchStmt *choice, NodeRec &rec) {
        std::vector<const SwitchCase *> labels;
        for (const SwitchCase *label = choice->getSwitchCaseList(); label != nullptr;
             label = label->getNextSwitchCase()) {
            labels.push_back(label);
        }
        std::sort(labels.begin(), labels.end(), [this](const SwitchCase *left, const SwitchCase *right) {
            return sm_.isBeforeInTranslationUnit(left->getBeginLoc(), right->getBeginLoc());
        });
        rec.label_count = static_cast<int>(labels.size());
        for (size_t at = 0; at < labels.size(); ++at) {
            if (!isa<DefaultStmt>(labels[at])) continue;
            const char *where = labels.size() == 1 ? "only"
                              : at == 0 ? "first"
                              : at + 1 == labels.size() ? "last" : "middle";
            default_order_[labels[at]] = where;
            rec.detail = "default";
        }
        int clauses = 0;
        const Stmt *body = choice->getBody();
        if (const auto *block = dyn_cast_or_null<CompoundStmt>(body)) {
            for (const Stmt *item : block->body()) {
                if (isa<SwitchCase>(item)) ++clauses;
            }
        } else if (body != nullptr && isa<SwitchCase>(body)) {
            clauses = 1;
        }
        rec.clause_count = clauses;
        if (!skips_inside(choice->getSourceRange())) note_switch_clauses(choice, labels);
    }

    // Can control leave this statement at its end?  Unknown shapes (loops,
    // nested switches, try) count as no, so that nothing is reported for them.
    bool may_fall(const Stmt *stmt) const {
        if (stmt == nullptr) return true;
        if (isa<BreakStmt>(stmt) || isa<ReturnStmt>(stmt) || isa<ContinueStmt>(stmt) || isa<GotoStmt>(stmt) ||
            isa<IndirectGotoStmt>(stmt) || isa<CXXThrowExpr>(stmt)) {
            return false;
        }
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            const Expr *plain = expr->IgnoreParenImpCasts();
            if (isa<CXXThrowExpr>(plain)) return false;
            if (const auto *call = dyn_cast<CallExpr>(plain)) {
                const FunctionDecl *callee = call->getDirectCallee();
                if (callee != nullptr && (callee->isNoReturn() || callee->getBuiltinID() == Builtin::BI__builtin_unreachable)) return false;
            }
            return true;
        }
        if (const auto *block = dyn_cast<CompoundStmt>(stmt)) return block->body_empty() || may_fall(block->body_back());
        if (const auto *branch = dyn_cast<IfStmt>(stmt)) {
            return branch->getElse() == nullptr || may_fall(branch->getThen()) || may_fall(branch->getElse());
        }
        if (const auto *attributed = dyn_cast<AttributedStmt>(stmt)) return may_fall(attributed->getSubStmt());
        if (isa<DeclStmt>(stmt) || isa<NullStmt>(stmt)) return true;
        return false;
    }

    // OC-0111: the first part of a switch that breaks its structure
    // (switch_structure: a statement before the first label, a label nested in
    // another statement, a body that is no compound statement).  OC-0113: a label
    // that the clause before it falls into (fallthrough).
    void note_switch_clauses(const SwitchStmt *choice, const std::vector<const SwitchCase *> &labels) {
        const auto *block = dyn_cast_or_null<CompoundStmt>(choice->getBody());
        if (block == nullptr) {
            if (choice->getBody() != nullptr) pending_shape_[choice->getBody()] = "switch_structure";
            return;
        }
        std::set<const SwitchCase *> top;
        for (const Stmt *item : block->body()) {
            for (const auto *label = dyn_cast<SwitchCase>(item); label != nullptr;
                 label = dyn_cast<SwitchCase>(label->getSubStmt())) {
                top.insert(label);
            }
        }
        const Stmt *broken = nullptr;
        if (!block->body_empty() && !isa<SwitchCase>(block->body_front())) broken = block->body_front();
        if (broken == nullptr) {
            for (const SwitchCase *label : labels) {
                if (top.count(label) == 0) {
                    broken = label;
                    break;
                }
            }
        }
        if (broken != nullptr) {
            pending_shape_[broken] = "switch_structure";
            return;
        }
        const Stmt *last = nullptr;
        bool has_statements = false;
        for (const Stmt *item : block->body()) {
            if (const auto *label = dyn_cast<SwitchCase>(item)) {
                if (has_statements && may_fall(last)) {
                    std::string &pending = pending_shape_[label];
                    pending = pending.empty() ? "fallthrough" : pending + ",fallthrough";
                }
                const Stmt *inner = label->getSubStmt();
                while (const auto *chained = dyn_cast_or_null<SwitchCase>(inner)) inner = chained->getSubStmt();
                last = inner;
                has_statements = inner != nullptr;
            } else {
                last = item;
                has_statements = true;
            }
        }
    }

    // A base specifier of a class definition, as a node of kind CXXBaseSpecifier
    // (detail "virtual" for a virtual base), parented to nothing; ref_uid is the class.
    void base_node(const CXXBaseSpecifier &base, int class_uid) {
        NodeRec rec;
        rec.uid = static_cast<int>(nodes.size());
        rec.kind = "CXXBaseSpecifier";
        rec.detail = base.isVirtual() ? "virtual" : "";
        rec.pos = src_pos_of(base.getBeginLoc());
        rec.from_macro = base.getBeginLoc().isMacroID();
        SourceLocation spelled = sm_.getSpellingLoc(base.getBeginLoc());
        rec.in_system_header = spelled.isValid() && sm_.isInSystemHeader(spelled);
        rec.ref_uid = class_uid;
        nodes.push_back(rec);
    }

    std::vector<NodeRec> nodes;
    std::vector<MacroRec> macros;

private:
    // The first token of code in the file that no guard block protects (invalid
    // if there is none).  A guard block is a top-level `#ifndef NAME` or
    // `#if !defined NAME` whose next directive is `#define NAME`, up to its
    // `#endif`; an `#else` or `#elif` of it is not protected.
    SourceLocation unguarded_code(FileID fid, const LangOptions &lang) const {
        Lexer lexer(fid, sm_.getBufferOrFake(fid), sm_, lang);
        Token tok;
        lexer.LexFromRawLexer(tok);
        int depth = 0;
        bool guarded = false;
        std::string pending;   // guard name waiting for its #define
        while (tok.isNot(tok::eof)) {
            if (tok.is(tok::hash) && tok.isAtStartOfLine()) {
                std::vector<std::string> words;
                lexer.LexFromRawLexer(tok);
                while (tok.isNot(tok::eof) && !tok.isAtStartOfLine()) {
                    words.push_back(tok.is(tok::raw_identifier) ? tok.getRawIdentifier().str()
                                                                 : std::string(tok::getPunctuatorSpelling(tok.getKind()) ? tok::getPunctuatorSpelling(tok.getKind()) : ""));
                    lexer.LexFromRawLexer(tok);
                }
                std::string name = words.empty() ? "" : words[0];
                if (!pending.empty()) {
                    guarded = name == "define" && words.size() > 1 && words[1] == pending;
                    pending.clear();
                }
                if (name == "if" || name == "ifdef" || name == "ifndef") {
                    if (depth == 0) {
                        guarded = false;
                        if (name == "ifndef" && words.size() > 1) {
                            pending = words[1];
                        } else if (name == "if" && words.size() > 3 && words[1] == "!" && words[2] == "defined") {
                            pending = words[3] == "(" && words.size() > 4 ? words[4] : words[3];
                        }
                    }
                    ++depth;
                } else if (name == "endif") {
                    if (depth > 0) --depth;
                    if (depth == 0) guarded = false;
                } else if ((name == "else" || name == "elif") && depth == 1) {
                    guarded = false;
                }
                continue;
            }
            if (!(depth > 0 && guarded) && !offset_skipped(fid, sm_.getFileOffset(tok.getLocation()))) {
                return tok.getLocation();
            }
            lexer.LexFromRawLexer(tok);
        }
        return SourceLocation();
    }

    // Does `include_loc` lie inside a declaration of the TU (looking through
    // extern "C" blocks, which wrap ordinary includes)?
    bool included_in_declaration(SourceLocation include_loc) const {
        if (include_loc.isInvalid()) return false;
        std::vector<const DeclContext *> todo{context_.getTranslationUnitDecl()};
        while (!todo.empty()) {
            const DeclContext *scope = todo.back();
            todo.pop_back();
            for (const Decl *decl : scope->decls()) {
                if (const auto *linkage = dyn_cast<LinkageSpecDecl>(decl)) {
                    if (linkage->hasBraces()) {
                        todo.push_back(linkage);
                        continue;
                    }
                }
                SourceLocation first = sm_.getExpansionLoc(decl->getBeginLoc());
                SourceLocation last = sm_.getExpansionLoc(decl->getEndLoc());
                if (first.isInvalid() || last.isInvalid()) continue;
                if (sm_.isBeforeInTranslationUnit(first, include_loc) &&
                    sm_.isBeforeInTranslationUnit(include_loc, last)) {
                    return true;
                }
            }
        }
        return false;
    }

    // The record of a #define or #undef: its name and where its # is.
    MacroRec directive_rec(const char *kind, Preprocessor &pp, const Token &name) {
        MacroRec rec;
        rec.uid = static_cast<int>(macros.size());
        rec.kind = kind;
        rec.name = pp.getSpelling(name);
        SourceLocation loc = name.getLocation();
        rec.loc = loc;
        rec.name_pos = pos_of(loc);
        rec.pos = rec.name_pos;
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(loc);
        llvm::StringRef buffer = sm_.getBufferData(at.first);
        for (unsigned back = at.second; back > 0; --back) {
            char ch = buffer[back - 1];
            if (ch == '\n') break;
            if (ch == '#') {
                rec.pos = pos_of(loc.getLocWithOffset(-static_cast<int>(at.second - back + 1)));
                break;
            }
        }
        return rec;
    }

    // The type specifiers as written: is the declared type spelled as a builtin
    // type, and was `signed` or `unsigned` written?  Plain `int` and `signed int`
    // are the same type, only the spelling tells them apart.
    void note_class_shape(const NamedDecl *decl, DeclRec &rec) {
        const auto *record = dyn_cast<CXXRecordDecl>(decl);
        if (record == nullptr || !record->isThisDeclarationADefinition() || !record->hasDefinition()) return;
        if (!record->isDependentType() && record->isAbstract()) {
            for (const CXXMethodDecl *method : record->methods()) {
                if (method->isCopyAssignmentOperator() && method->getAccess() == AS_public && !method->isDeleted()) {
                    rec.abstract_public_copy = true;
                }
            }
        }
        bool template_ctor = false;
        bool template_assign = false;
        for (const Decl *member : record->decls()) {
            const auto *pattern = dyn_cast<FunctionTemplateDecl>(member);
            if (pattern == nullptr) continue;
            const FunctionDecl *fn = pattern->getTemplatedDecl();
            if (fn->getNumParams() < 1 || fn->getMinRequiredArguments() > 1) continue;
            if (isa<CXXConstructorDecl>(fn)) template_ctor = true;
            if (fn->getOverloadedOperator() == OO_Equal && fn->getNumParams() == 1) template_assign = true;
        }
        rec.template_ctor_no_copy = template_ctor && !record->hasUserDeclaredCopyConstructor();
        rec.template_assign_no_copy = template_assign && !record->hasUserDeclaredCopyAssignment();
    }

    // Spelled noexcept (noexcept, noexcept(true), throw()), and known.
    static bool spelled_noexcept(const FunctionDecl *fn) {
        const auto *proto = fn->getType()->getAs<FunctionProtoType>();
        if (proto == nullptr) return false;
        ExceptionSpecificationType spec = proto->getExceptionSpecType();
        return spec == EST_BasicNoexcept || spec == EST_NoexceptTrue || spec == EST_DynamicNone ||
               spec == EST_DependentNoexcept || spec == EST_Unevaluated || spec == EST_Uninstantiated ||
               spec == EST_Unparsed;
    }

    static bool same_class_param(const FunctionDecl *fn, unsigned at, const CXXRecordDecl *owner) {
        QualType type = fn->getParamDecl(at)->getType().getNonReferenceType();
        const CXXRecordDecl *record = type->getAsCXXRecordDecl();
        return record != nullptr && owner != nullptr && record->getCanonicalDecl() == owner->getCanonicalDecl();
    }

    void note_must_not_throw(const NamedDecl *decl, DeclRec &rec) {
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || fn->isDefaulted() || fn->isDeleted() || fn->isImplicit() || fn->isDependentContext()) return;
        const auto *proto = fn->getType()->getAs<FunctionProtoType>();
        if (proto == nullptr) return;
        if (isa<CXXDestructorDecl>(fn)) {
            rec.may_throw = proto->getExceptionSpecType() == EST_NoexceptFalse;
            return;
        }
        bool must = false;
        const auto *method = dyn_cast<CXXMethodDecl>(fn);
        if (const auto *ctor = dyn_cast<CXXConstructorDecl>(fn)) must = ctor->isMoveConstructor();
        if (method != nullptr && method->isMoveAssignmentOperator()) must = true;
        if (fn->getIdentifier() != nullptr && fn->getName() == "swap") {
            if (method != nullptr && !method->isStatic() && fn->getNumParams() == 1) {
                must = same_class_param(fn, 0, method->getParent());
            } else if (method == nullptr && fn->getNumParams() == 2) {
                QualType left = fn->getParamDecl(0)->getType().getNonReferenceType().getUnqualifiedType();
                QualType right = fn->getParamDecl(1)->getType().getNonReferenceType().getUnqualifiedType();
                must = context_.hasSameType(left, right) && left->isRecordType() &&
                       fn->getParamDecl(0)->getType()->isLValueReferenceType();
            }
        }
        if (method != nullptr && method->getOverloadedOperator() == OO_Call) {
            const CXXRecordDecl *owner = method->getParent();
            if (owner->getIdentifier() != nullptr && owner->getName() == "hash" && owner->isInStdNamespace()) must = true;
        }
        rec.may_throw = must && !spelled_noexcept(fn);
    }

    // The = before `loc` (white space between), else `loc` itself.
    SourceLocation equal_before(SourceLocation loc) const {
        std::pair<FileID, unsigned> at = sm_.getDecomposedLoc(loc);
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(at.first, &invalid);
        if (invalid) return loc;
        unsigned offset = at.second;
        while (offset > 0) {
            char ch = buffer[offset - 1];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                --offset;
                continue;
            }
            if (ch == '=') return loc.getLocWithOffset(static_cast<int>(offset) - 1 - static_cast<int>(at.second));
            break;
        }
        return loc;
    }

    // An overrider whose default argument differs in value from the overridden
    // function's, or that adds one; values that are not constants are left alone.
    void note_override_defaults(const NamedDecl *decl, DeclRec &rec) {
        const auto *method = dyn_cast<CXXMethodDecl>(decl);
        if (method == nullptr) return;
        rec.pure_overrides_impl = false;
        for (const CXXMethodDecl *base : method->overridden_methods()) {
            if (method->isPureVirtual() && !base->isPureVirtual()) rec.pure_overrides_impl = true;
            if (base->getNumParams() != method->getNumParams()) continue;
            for (unsigned at = 0; at < method->getNumParams(); ++at) {
                const ParmVarDecl *mine = method->getParamDecl(at);
                if (!mine->hasDefaultArg() || mine->hasUnparsedDefaultArg() || mine->hasUninstantiatedDefaultArg()) continue;
                const ParmVarDecl *theirs = base->getParamDecl(at);
                bool differs = !theirs->hasDefaultArg();
                if (!differs) {
                    if (theirs->hasUnparsedDefaultArg() || theirs->hasUninstantiatedDefaultArg()) continue;
                    Expr::EvalResult left;
                    Expr::EvalResult right;
                    const Expr *own = mine->getDefaultArg();
                    const Expr *other = theirs->getDefaultArg();
                    if (own == nullptr || other == nullptr || own->isValueDependent() || other->isValueDependent()) continue;
                    if (!own->EvaluateAsRValue(left, context_) || !other->EvaluateAsRValue(right, context_)) continue;
                    if (left.HasSideEffects || right.HasSideEffects) continue;
                    if (left.Val.isInt() && right.Val.isInt()) {
                        differs = llvm::APSInt::compareValues(left.Val.getInt(), right.Val.getInt()) != 0;
                    } else if (left.Val.isFloat() && right.Val.isFloat()) {
                        differs = !left.Val.getFloat().bitwiseIsEqual(right.Val.getFloat());
                    } else {
                        continue;
                    }
                }
                if (differs && rec.default_arg_pos.file < 0) {
                    const Expr *own = mine->getDefaultArg();
                    if (own != nullptr) rec.default_arg_pos = pos_of(equal_before(sm_.getExpansionLoc(own->getBeginLoc())));
                }
            }
        }
    }

    // The identifiers written in a function body, from the raw tokens of the file
    // between its braces (macros not expanded, skipped code included).
    const std::map<std::string, int> &body_names(const FunctionDecl *fn) {
        auto known = body_names_.find(fn);
        if (known != body_names_.end()) return known->second;
        std::map<std::string, int> &names = body_names_[fn];
        const Stmt *body = fn->getBody();
        SourceLocation open = sm_.getExpansionLoc(body->getBeginLoc());
        SourceLocation close = sm_.getExpansionRange(body->getEndLoc()).getEnd();
        if (open.isInvalid() || close.isInvalid()) return names;
        std::pair<FileID, unsigned> first = sm_.getDecomposedLoc(open);
        std::pair<FileID, unsigned> last = sm_.getDecomposedLoc(close);
        if (first.first != last.first || first.second > last.second) return names;
        bool invalid = false;
        llvm::StringRef buffer = sm_.getBufferData(first.first, &invalid);
        if (invalid) return names;
        Lexer lexer(sm_.getLocForStartOfFile(first.first), context_.getLangOpts(), buffer.begin(),
                    buffer.begin() + first.second, buffer.end());
        Token tok;
        while (true) {
            lexer.LexFromRawLexer(tok);
            if (tok.is(tok::eof)) break;
            if (sm_.getFileOffset(tok.getLocation()) > last.second) break;
            if (tok.is(tok::raw_identifier)) ++names[tok.getRawIdentifier().str()];
        }
        return names;
    }

    // The * of the outermost pointer of the written type.
    void note_pointer(TypeLoc loc, DeclRec &rec) {
        for (;;) {
            if (auto qualified = loc.getAs<QualifiedTypeLoc>()) {
                loc = qualified.getUnqualifiedLoc();
            } else if (auto paren = loc.getAs<ParenTypeLoc>()) {
                loc = paren.getInnerLoc();
            } else if (auto attributed = loc.getAs<AttributedTypeLoc>()) {
                loc = attributed.getModifiedLoc();
            } else {
                break;
            }
        }
        if (auto pointer = loc.getAs<PointerTypeLoc>()) {
            rec.pointer_pos = pos_of(sm_.getExpansionLoc(pointer.getStarLoc()));
        }
    }

    // A parameter written as an array, and where a `static` in its brackets is.
    void note_array_param(TypeLoc loc, DeclRec &rec) {
        while (auto paren = loc.getAs<ParenTypeLoc>()) loc = paren.getInnerLoc();
        auto array = loc.getAs<ArrayTypeLoc>();
        if (!array) return;
        rec.array_param = true;
        if (array.getTypePtr()->getSizeModifier() != ArraySizeModifier::Static) return;
        SourceLocation at = array.getLBracketLoc();
        for (int step = 0; step < 4 && at.isValid(); ++step) {
            std::optional<Token> next = Lexer::findNextToken(at, sm_, context_.getLangOpts());
            if (!next || next->is(tok::r_square)) return;
            if (next->is(tok::raw_identifier) && next->getRawIdentifier() == "static") {
                rec.static_array_pos = pos_of(sm_.getExpansionLoc(next->getLocation()));
                return;
            }
            at = next->getLocation();
        }
    }

    static void note_written(TypeLoc loc, DeclRec &rec) {
        for (;;) {
            if (auto qualified = loc.getAs<QualifiedTypeLoc>()) {
                loc = qualified.getUnqualifiedLoc();
            } else if (auto paren = loc.getAs<ParenTypeLoc>()) {
                loc = paren.getInnerLoc();
            } else if (auto attributed = loc.getAs<AttributedTypeLoc>()) {
                loc = attributed.getModifiedLoc();
            } else {
                break;
            }
        }
        if (auto builtin = loc.getAs<BuiltinTypeLoc>()) {
            rec.written_builtin = true;
            if (builtin.needsExtraLocalData()) {
                TypeSpecifierSign sign = builtin.getWrittenSignSpec();
                if (sign == TypeSpecifierSign::Signed) {
                    rec.sign_written = "signed";
                } else if (sign == TypeSpecifierSign::Unsigned) {
                    rec.sign_written = "unsigned";
                }
            }
        }
    }

    ASTContext &context_;
    SourceManager &sm_;
    std::map<FileID, int> file_ids_;
    std::map<const Type *, int> type_ids_;
    std::map<const NamedDecl *, int> decl_ids_;
    std::map<std::pair<const void *, unsigned>, int> groups_;
    std::map<const FunctionDecl *, std::map<std::string, int>> body_names_;
    std::map<const Stmt *, std::string> roles_;
    std::map<const Stmt *, std::string> default_order_;
    std::map<FileID, std::vector<std::pair<unsigned, unsigned>>> skipped_;
    std::set<const VarDecl *> errno_saved_;
    std::set<const Stmt *> shadowed_handlers_;
    std::map<const Stmt *, std::string> pending_shape_;
    std::map<FileID, std::string> ast_marks_;
    std::map<unsigned, SourceLocation> decl_begins_;
    std::set<std::pair<const void *, std::string>> long_names_;
    std::vector<const UsingDecl *> usings_;
    std::vector<SourceLocation> unsafe_calls_;
    bool creates_threads_ = false;
    std::vector<SourceLocation> signal_calls_;
    std::map<const FunctionDecl *, bool> throwing_memo_;
    std::vector<std::pair<const MacroInfo *, SourceLocation>> main_macros_;
    std::set<const MacroInfo *> used_macros_;
    std::set<std::string> guard_candidates_;
    std::vector<const FunctionDecl *> graph_functions_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> stream_uses_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> stream_closes_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> stream_writes_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> buffer_writes_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> readlinks_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> string_reads_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> path_checks_;
    std::vector<std::pair<const ValueDecl *, const Stmt *>> path_opens_;
    struct PositionUse {
        const ValueDecl *position;
        const ValueDecl *stream;
        SourceLocation where;
    };
    std::vector<PositionUse> position_sets_;
    std::vector<std::pair<const ValueDecl *, const ValueDecl *>> position_gets_;
    std::vector<const CallExpr *> uid_calls_;
    std::vector<const CallExpr *> gid_calls_;
    std::vector<const CallExpr *> env_calls_;
    std::vector<const DeclRefExpr *> envp_refs_;
    std::set<const ValueDecl *> fields_checked_;
    std::vector<std::pair<const ValueDecl *, SourceLocation>> asctime_args_;
    std::set<const VarDecl *> literal_pointers_;
    std::set<const ValueDecl *> literal_unsure_;
    std::vector<std::pair<const ValueDecl *, SourceLocation>> literal_writes_;
    std::set<const FunctionDecl *> signal_handlers_;
    std::set<const FunctionDecl *> fault_handlers_;
    std::set<const FunctionDecl *> exit_handlers_;
    std::set<const ValueDecl *> fgets_buffers_;
    std::set<const ValueDecl *> length_checked_;
    std::vector<std::pair<const ValueDecl *, SourceLocation>> fgets_indexes_;
    std::set<std::pair<FileID, unsigned>> ast_mark_seen_;
    std::map<FileID, std::vector<std::pair<unsigned, unsigned>>> argument_ranges_;
    struct ScopeRange {
        FileID fid;
        unsigned begin;
        unsigned end;
        const char *kind;
    };
    std::vector<ScopeRange> scopes_;
    std::set<std::string> call_site_macros_;
};

// Feeds the preprocessor's events into the tables.
class MacroCollector : public PPCallbacks {
public:
    MacroCollector(std::shared_ptr<Tables> tables, Preprocessor &pp) : tables_(std::move(tables)), pp_(pp) {}

    void FileChanged(SourceLocation loc, FileChangeReason reason, SrcMgr::CharacteristicKind,
                     FileID) override {
        if (reason == EnterFile) tables_->note_entered(loc);
    }
    void MacroDefined(const Token &name, const MacroDirective *directive) override {
        tables_->note_define(pp_, name, directive);
        tables_->note_main_define(name, directive != nullptr ? directive->getMacroInfo() : nullptr);
    }
    void MacroUndefined(const Token &name, const MacroDefinition &definition,
                        const MacroDirective *) override {
        tables_->note_undef(pp_, name, definition);
        tables_->note_macro_use(definition);
    }
    void Defined(const Token &, const MacroDefinition &definition, SourceRange) override { tables_->note_macro_use(definition); }
    void Ifdef(SourceLocation, const Token &, const MacroDefinition &definition) override { tables_->note_macro_use(definition); }
    void Ifndef(SourceLocation, const Token &name, const MacroDefinition &definition) override { tables_->note_ifndef(name, definition); }
    void Elifdef(SourceLocation, const Token &, const MacroDefinition &definition) override { tables_->note_macro_use(definition); }
    void Elifndef(SourceLocation, const Token &, const MacroDefinition &definition) override { tables_->note_macro_use(definition); }
    void MacroExpands(const Token &name, const MacroDefinition &definition, SourceRange range,
                      const MacroArgs *args) override {
        tables_->note_macro_use(definition);
        tables_->note_expansion(pp_, name, definition, args);
        tables_->note_argument_range(definition, range);
    }
    void PragmaDirective(SourceLocation loc, PragmaIntroducerKind) override {
        tables_->note_pragma(loc);
    }
    void SourceRangeSkipped(SourceRange range, SourceLocation) override {
        tables_->note_skipped(range);
    }
    void If(SourceLocation loc, SourceRange condition, ConditionValueKind value) override {
        if (value != CVK_NotEvaluated) tables_->note_condition(pp_, loc, condition);
    }
    void Elif(SourceLocation loc, SourceRange condition, ConditionValueKind value, SourceLocation) override {
        if (value != CVK_NotEvaluated) tables_->note_condition(pp_, loc, condition);
    }

private:
    std::shared_ptr<Tables> tables_;
    Preprocessor &pp_;
};

class Collector : public RecursiveASTVisitor<Collector> {
public:
    explicit Collector(Tables &tables, SourceManager &sm) : tables_(tables), sm_(sm) {}

    // Instantiations are visited too: a rule decides per instantiation, and the
    // records carry origin.instantiated and the point of instantiation.
    bool shouldVisitTemplateInstantiations() const { return true; }

    bool VisitNamedDecl(NamedDecl *decl) {
        if (isa<TranslationUnitDecl>(decl)) {
            return true;
        }
        SourceLocation loc = decl->getLocation();
        if (loc.isValid() && sm_.isInSystemHeader(sm_.getExpansionLoc(loc))) {
            return true;   // added on demand, see Tables::decl_of
        }
        tables_.decl_of(decl);
        tables_.note_scope(decl);
        return true;
    }

private:
    Tables &tables_;
    SourceManager &sm_;
};

// Records the statements and expressions with their parents.  Template
// instantiations and implicit code are not visited (the template is), declarations
// of system headers are skipped as a whole, implicit casts are left out for now.
class NodeCollector : public RecursiveASTVisitor<NodeCollector> {
public:
    explicit NodeCollector(Tables &tables, SourceManager &sm) : tables_(tables), sm_(sm) {}

    bool TraverseDecl(Decl *decl) {
        if (decl == nullptr) {
            return true;
        }
        if (!isa<TranslationUnitDecl>(decl)) {
            SourceLocation loc = decl->getLocation();
            if (loc.isValid() && sm_.isInSystemHeader(sm_.getExpansionLoc(loc))) {
                return true;
            }
        }
        const auto *fn = dyn_cast<FunctionDecl>(decl);
        if (fn == nullptr || !fn->doesThisDeclarationHaveABody()) {
            return RecursiveASTVisitor::TraverseDecl(decl);
        }
        int outer = function_;
        std::vector<int> outer_stack;
        outer_stack.swap(stack_);
        function_ = tables_.decl_of(fn);
        bool result = RecursiveASTVisitor::TraverseDecl(decl);
        function_ = outer;
        stack_.swap(outer_stack);
        return result;
    }

    bool VisitCXXRecordDecl(CXXRecordDecl *record) {
        if (record->isThisDeclarationADefinition() && record->hasDefinition()) {
            int class_uid = tables_.decl_of(record);
            for (const CXXBaseSpecifier &base : record->bases()) {
                tables_.base_node(base, class_uid);
            }
        }
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc loc) {
        tables_.note_vector_bool(loc);
        return true;
    }

    bool dataTraverseStmtPre(Stmt *stmt) {
        int parent = stack_.empty() ? -1 : stack_.back();
        SourceLocation loc = stmt->getBeginLoc();
        bool skipped = isa<ImplicitCastExpr>(stmt) ||
                       (loc.isValid() && sm_.isInSystemHeader(sm_.getExpansionLoc(loc)));
        if (const auto *implicit = dyn_cast<ImplicitCastExpr>(stmt)) {
            if (!(loc.isValid() && sm_.isInSystemHeader(sm_.getExpansionLoc(loc)))) tables_.note_implicit_cast(implicit);
        }
        stack_.push_back(skipped ? parent : tables_.node_of(stmt, parent, function_));
        return true;
    }

    bool dataTraverseStmtPost(Stmt *) {
        if (!stack_.empty()) stack_.pop_back();
        return true;
    }

private:
    Tables &tables_;
    SourceManager &sm_;
    int function_ = -1;
    std::vector<int> stack_;
};

// Counts the compile errors and keeps the first one; prints nothing.  Oculus
// reports units that did not compile cleanly, because a rule then sees only what
// Clang recovered.
class CountingDiagConsumer : public DiagnosticConsumer {
public:
    void HandleDiagnostic(DiagnosticsEngine::Level level, const Diagnostic &info) override {
        note_seen(level, info);
        if (level < DiagnosticsEngine::Error) {
            return;
        }
        llvm::SmallString<256> message;
        info.FormatDiagnostic(message);
        // Driver flags of another compiler (GCC's -fno-reorder-functions and the
        // like): remembered, the caller drops them and parses again.
        static const llvm::Regex unknown("unknown argument:? '([^']+)'");
        llvm::SmallVector<llvm::StringRef, 2> groups;
        if (unknown.match(message.str(), &groups)) {
            unknown_args.push_back(groups[1].str());
            return;
        }
        ++errors;
        if (first.empty()) {
            llvm::SmallString<256> text;
            info.FormatDiagnostic(text);
            if (info.getLocation().isValid() && info.hasSourceManager()) {
                const SourceManager &sm = info.getSourceManager();
                PresumedLoc where = sm.getPresumedLoc(sm.getExpansionLoc(info.getLocation()));
                if (where.isValid()) {
                    first = std::string(where.getFilename()) + ":" + std::to_string(where.getLine()) + ": ";
                }
            }
            first += text.str().str();
        }
    }
    // Keeps the diagnostics rules report (DiagSeen); a jump error is marked
    // when one of its notes names a try block or handler.
    void note_seen(DiagnosticsEngine::Level level, const Diagnostic &info) {
        unsigned id = info.getID();
        if (level == DiagnosticsEngine::Note) {
            if (last_jump && (id == diag::note_protected_by_cxx_try || id == diag::note_protected_by_cxx_catch)) seen.back().try_scope = true;
            return;
        }
        last_jump = false;
        if (!kept_diagnostic(id)) return;
        DiagSeen one;
        one.id = id;
        one.loc = info.getLocation();
        one.error = level >= DiagnosticsEngine::Error;
        if (id == diag::err_pp_file_not_found && info.getNumArgs() > 0 && info.getArgKind(0) == DiagnosticsEngine::ak_std_string) {
            llvm::StringRef name = info.getArgStdStr(0);
            one.odd_name = name.contains('\'') || name.contains('\\') || name.contains('"') || name.contains("/*") || name.contains("//");
        }
        if (id == diag::err_atomic_specifier_bad_type) {
            llvm::SmallString<128> text;
            info.FormatDiagnostic(text);
            llvm::StringRef message = llvm::StringRef(text).rtrim();
            one.atomic_void = message.ends_with(" 'void'") || message.ends_with(" void'");
        }
        seen.push_back(one);
        last_jump = id == diag::err_goto_into_protected_scope || id == diag::err_switch_into_protected_scope;
    }
    int errors = 0;
    std::string first;
    std::vector<std::string> unknown_args;
    std::vector<DiagSeen> seen;
    bool last_jump = false;
};

// Exactly the one command Oculus was given, no lookup.
class SingleCommandDatabase : public tooling::CompilationDatabase {
public:
    explicit SingleCommandDatabase(tooling::CompileCommand command) : command_(std::move(command)) {}
    std::vector<tooling::CompileCommand> getCompileCommands(llvm::StringRef) const override {
        return {command_};
    }
    std::vector<std::string> getAllFiles() const override { return {command_.Filename}; }

private:
    tooling::CompileCommand command_;
};

std::vector<std::string> default_argv(const std::string &file) {
    llvm::StringRef name(file);
    bool cpp = name.ends_with(".cpp") || name.ends_with(".cc") || name.ends_with(".cxx") ||
               name.ends_with(".hpp");
    return {cpp ? "clang++" : "clang", "-c", file};
}


// One parsed translation unit, as records.  `problem` is set when Clang could not
// parse it at all; compile errors do not stop the analysis (the rules see what
// Clang recovered), they are counted.
struct UnitResult {
    std::string language;
    std::string standard;
    std::string problem;
    int compile_errors = 0;
    std::string first_error;
    std::vector<std::string> dropped;
    std::vector<FileRec> files;
    std::vector<TypeRec> types;
    std::vector<DeclRec> decls;
    std::vector<NodeRec> nodes;
    std::vector<MacroRec> macros;
    bool parsed = false;   // the AST was built (with or without compile errors)
};

// The argv of a `command` string of compile_commands.json.
inline std::vector<std::string> tokenize_command(const std::string &line) {
    llvm::BumpPtrAllocator alloc;
    llvm::StringSaver saver(alloc);
    llvm::SmallVector<const char *, 32> pieces;
    llvm::cl::TokenizeGNUCommandLine(line, saver, pieces);
    std::vector<std::string> argv;
    for (const char *piece : pieces) argv.emplace_back(piece);
    return argv;
}

// Copies the AST into the tables once Sema is done with the unit (template
// instantiation at the end of the TU included); the AST is freed afterwards.
class UnitConsumer : public ASTConsumer {
public:
    UnitConsumer(std::shared_ptr<Tables> tables, Preprocessor &pp, UnitResult &result)
        : tables_(std::move(tables)), pp_(pp), result_(result) {}

    void HandleTranslationUnit(ASTContext &context) override {
        const LangOptions &lang = context.getLangOpts();
        result_.language = lang.CPlusPlus ? "c++" : "c";
        result_.standard = LangStandard::getLangStandardForKind(lang.LangStd).getName();
        Tables &tables = *tables_;
        tables.finish_files(pp_);
        Collector collector(tables, context.getSourceManager());
        collector.TraverseDecl(context.getTranslationUnitDecl());
        NodeCollector node_collector(tables, context.getSourceManager());
        node_collector.TraverseDecl(context.getTranslationUnitDecl());
        tables.finish_lines(lang);
        tables.finish_scopes();
        result_.files = std::move(tables.files);
        result_.types = std::move(tables.types);
        result_.decls = std::move(tables.decls);
        result_.nodes = std::move(tables.nodes);
        result_.macros = std::move(tables.macros);
        result_.parsed = true;
    }

private:
    std::shared_ptr<Tables> tables_;
    Preprocessor &pp_;
    UnitResult &result_;
};

// Parses the unit with the macro collector attached to the preprocessor.
class UnitAction : public ASTFrontendAction {
public:
    explicit UnitAction(UnitResult &result) : result_(result) {}

protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &instance, llvm::StringRef) override {
        auto tables = std::make_shared<Tables>(instance.getASTContext());
        Preprocessor &pp = instance.getPreprocessor();
        pp.addPPCallbacks(std::make_unique<MacroCollector>(tables, pp));
        return std::make_unique<UnitConsumer>(tables, pp, result_);
    }

private:
    UnitResult &result_;
};

class UnitActionFactory : public tooling::FrontendActionFactory {
public:
    explicit UnitActionFactory(UnitResult &result) : result_(result) {}
    std::unique_ptr<FrontendAction> create() override { return std::make_unique<UnitAction>(result_); }

private:
    UnitResult &result_;
};

// Parses FILE in DIRECTORY with the compiler argv of its compile command (argv[0]
// is the compiler; empty: a plain `clang -c FILE`).  Flags Clang does not know are
// dropped and the unit parsed again (a database written by a GCC build has some).
inline UnitResult parse_unit(const std::string &directory, const std::string &file,
                             std::vector<std::string> command_argv) {
    UnitResult result;
    if (command_argv.empty()) {
        command_argv = default_argv(file);
    }
    std::string resource_dir = OCULUS_RESOURCE_DIR;
    if (!resource_dir.empty()) {
        command_argv.insert(command_argv.begin() + 1, "-resource-dir=" + resource_dir);
    }
    CountingDiagConsumer quiet;
    std::vector<std::string> dropped;
    for (int attempt = 0; attempt < 4; ++attempt) {
        result = UnitResult();
        quiet = CountingDiagConsumer();
        diag_sink = &quiet.seen;
        SingleCommandDatabase database(tooling::CompileCommand(directory, file, command_argv, ""));
        tooling::ClangTool tool(database, {file});
        tool.appendArgumentsAdjuster(tooling::getClangStripOutputAdjuster());
        tool.appendArgumentsAdjuster(tooling::getClangStripDependencyFileAdjuster());
        tool.setDiagnosticConsumer(&quiet);
        tool.setPrintErrorMessage(false);
        UnitActionFactory factory(result);
        tool.run(&factory);
        diag_sink = nullptr;
        if (quiet.unknown_args.empty()) {
            break;
        }
        for (const std::string &flag : quiet.unknown_args) {
            auto at = std::find(command_argv.begin() + 1, command_argv.end(), flag);
            if (at != command_argv.end()) {
                command_argv.erase(at);
                dropped.push_back(flag);
            }
        }
    }
    result.dropped = dropped;
    if (!result.parsed) {
        result = UnitResult();
        result.dropped = dropped;
        result.problem = "clang could not parse " + file;
        return result;
    }
    result.compile_errors = std::max(0, quiet.errors - rule_errors(quiet.seen));
    result.first_error = quiet.first;
    return result;
}

}  // namespace oculus
