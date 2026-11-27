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

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Analysis/Analyses/ExprMutationAnalyzer.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
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
};

// One #define, #undef or #pragma (or _Pragma) outside system headers, in source
// order.  Recorded while the preprocessor runs; only directives of active code.
struct MacroRec {
    int uid = 0;
    std::string kind;           // define | undef | pragma | include | expansion
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
            if (const auto *ctor = dyn_cast<CXXConstructorDecl>(fn)) rec.is_explicit = ctor->isExplicit();
            if (const auto *conv = dyn_cast<CXXConversionDecl>(fn)) rec.is_explicit = conv->isExplicit();
            if (fn->doesThisDeclarationHaveABody() && fn->getBody() != nullptr && !(isa<CXXMethodDecl>(fn) && cast<CXXMethodDecl>(fn)->getParent()->isLambda())) {
                measure_body(fn, rec);
            }
        }
        if (const auto *var = dyn_cast<VarDecl>(decl)) {
            rec.paren_declarator = paren_declarator(var);
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
        } else if (const auto *loop = dyn_cast<CXXForRangeStmt>(stmt)) {
            mark(loop->getBody(), "body");
        } else if (const auto *choice = dyn_cast<SwitchStmt>(stmt)) {
            mark(choice->getBody(), "body");
        }
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
    void note_expansion(Preprocessor &pp, const Token &name, const MacroDefinition &definition) {
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
        }
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
            }
        } else if (const auto *choice = dyn_cast<SwitchStmt>(stmt)) {
            note_switch(choice, rec);
        } else if (isa<DefaultStmt>(stmt)) {
            auto known = default_order_.find(stmt);
            rec.detail = known != default_order_.end() ? known->second : "";
        }
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            rec.type_uid = type_of(expr->getType());
            rec.is_dependent = expr->isTypeDependent() || expr->isValueDependent();
        }
        nodes.push_back(rec);
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
    std::map<const Stmt *, std::string> roles_;
    std::map<const Stmt *, std::string> default_order_;
    std::map<FileID, std::vector<std::pair<unsigned, unsigned>>> skipped_;
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
    void MacroExpands(const Token &name, const MacroDefinition &definition, SourceRange,
                      const MacroArgs *) override {
        tables_->note_expansion(pp_, name, definition);
    }
    void PragmaDirective(SourceLocation loc, PragmaIntroducerKind) override {
        tables_->note_pragma(loc);
    }
    void SourceRangeSkipped(SourceRange range, SourceLocation) override {
        tables_->note_skipped(range);
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
