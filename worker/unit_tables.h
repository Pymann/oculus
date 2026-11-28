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
#include "clang/Basic/Builtins.h"
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

    // A named public or protected field of a C++ class that is not an aggregate and
    // has a user-provided constructor or destructor, a virtual function, a base or a
    // private field.
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
        for (const FieldDecl *other : record->fields()) invariant = invariant || other->getAccess() == AS_private;
        return invariant;
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

    // C library functions whose result may be ignored: printf to stdout (the
    // catalogue reports fprintf) and the functions that return their first
    // argument.
    static bool ignorable_result(const FunctionDecl *callee, const SourceManager &sm) {
        if (callee->getIdentifier() == nullptr) return false;
        const DeclContext *owner = callee->getDeclContext()->getRedeclContext();
        if (!owner->isTranslationUnit() && !callee->isInStdNamespace()) return false;
        SourceLocation spelled = sm.getSpellingLoc(callee->getFirstDecl()->getLocation());
        if (spelled.isInvalid() || !sm.isInSystemHeader(spelled)) return false;
        static const char *const names[] = {
            "printf", "vprintf", "wprintf", "vwprintf",
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
    static void add_shape(NodeRec &rec, const std::string &shape) {
        if (shape.empty()) return;
        std::string list = "," + rec.shape + ",";
        if (list.find("," + shape + ",") != std::string::npos) return;
        rec.shape = rec.shape.empty() ? shape : rec.shape + "," + shape;
    }

    bool null_constant(const Expr *expr) {
        return expr->isNullPointerConstant(context_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull;
    }

    // A call of malloc, calloc or realloc (the C library functions).
    static bool allocation(const Expr *expr) {
        const auto *call = dyn_cast<CallExpr>(expr->IgnoreParenImpCasts());
        const FunctionDecl *callee = call == nullptr ? nullptr : call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return false;
        StringRef name = callee->getName();
        return (name == "malloc" || name == "calloc" || name == "realloc") && callee->getDeclContext()->getRedeclContext()->isTranslationUnit();
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
    }

    // A parameter that is an operand of # or ## and also used where it is
    // expanded: kind mixed_param at the first expanded use (name the parameter).
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
        for (const auto &entry : file_ids_) {
            FileRec &rec = files[static_cast<size_t>(entry.second)];
            if (rec.kind == "system_header" || !sm_.getFileEntryRefForID(entry.first)) continue;
            rec.line_classes = line_classes(entry.first, lang);
            rec.lexical_marks = lexical_marks(entry.first, lang);
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
    }
    void MacroUndefined(const Token &name, const MacroDefinition &definition,
                        const MacroDirective *) override {
        tables_->note_undef(pp_, name, definition);
    }
    void MacroExpands(const Token &name, const MacroDefinition &definition, SourceRange range,
                      const MacroArgs *args) override {
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
    int errors = 0;
    std::string first;
    std::vector<std::string> unknown_args;
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
        SingleCommandDatabase database(tooling::CompileCommand(directory, file, command_argv, ""));
        tooling::ClangTool tool(database, {file});
        tool.appendArgumentsAdjuster(tooling::getClangStripOutputAdjuster());
        tool.appendArgumentsAdjuster(tooling::getClangStripDependencyFileAdjuster());
        tool.setDiagnosticConsumer(&quiet);
        tool.setPrintErrorMessage(false);
        UnitActionFactory factory(result);
        tool.run(&factory);
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
    result.compile_errors = quiet.errors;
    result.first_error = quiet.first;
    return result;
}

}  // namespace oculus
