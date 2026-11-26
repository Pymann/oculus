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
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
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
};

// The tables of one TU, filled while the AST is alive.
class Tables {
public:
    explicit Tables(ASTContext &context) : context_(context), sm_(context.getSourceManager()) {}

    int file_of(SourceLocation loc) {
        if (loc.isInvalid()) {
            return -1;
        }
        FileID fid = sm_.getFileID(loc);
        auto known = file_ids_.find(fid);
        if (known != file_ids_.end()) {
            return known->second;
        }
        FileRec rec;
        rec.uid = static_cast<int>(files.size());
        rec.path = sm_.getFilename(loc).str();
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

private:
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
    std::vector<std::unique_ptr<ASTUnit>> units;
    CountingDiagConsumer quiet;
    for (int attempt = 0; attempt < 4; ++attempt) {
        units.clear();
        quiet = CountingDiagConsumer();
        SingleCommandDatabase database(tooling::CompileCommand(directory, file, command_argv, ""));
        tooling::ClangTool tool(database, {file});
        tool.appendArgumentsAdjuster(tooling::getClangStripOutputAdjuster());
        tool.appendArgumentsAdjuster(tooling::getClangStripDependencyFileAdjuster());
        tool.setDiagnosticConsumer(&quiet);
        tool.buildASTs(units);
        if (quiet.unknown_args.empty()) {
            break;
        }
        for (const std::string &flag : quiet.unknown_args) {
            auto at = std::find(command_argv.begin() + 1, command_argv.end(), flag);
            if (at != command_argv.end()) {
                command_argv.erase(at);
                result.dropped.push_back(flag);
            }
        }
    }
    if (units.empty() || !units.front()) {
        result.problem = "clang could not parse " + file;
        return result;
    }
    ASTContext &context = units.front()->getASTContext();
    const LangOptions &lang = context.getLangOpts();
    result.language = lang.CPlusPlus ? "c++" : "c";
    result.standard = LangStandard::getLangStandardForKind(lang.LangStd).getName();
    Tables tables(context);
    Collector collector(tables, context.getSourceManager());
    collector.TraverseDecl(context.getTranslationUnitDecl());
    result.compile_errors = quiet.errors;
    result.first_error = quiet.first;
    result.files = std::move(tables.files);
    result.types = std::move(tables.types);
    result.decls = std::move(tables.decls);
    return result;
}

}  // namespace oculus
