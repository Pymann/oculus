// oculus-clang: the JSON worker of Oculus.
//
// Parses one translation unit with Clang (LibTooling, unit_tables.h) and writes
// Oculus' RuleInformation for it as one JSON document to stdout.  The document is
// read by src/frontend/clang_frontend.aiml with sys.deserialize; its shape is the
// models in src/ir/ (doc/ir.md).  `oculus --frontend json` uses it; the default is
// the in-process shim (doc/inprocess_frontend.md), which fills the same records.
//
// Usage:
//   oculus-clang DIRECTORY FILE [-- COMPILER ARGV...]
//   oculus-clang DIRECTORY FILE --command "COMMAND LINE"
// The compiler argv is the one of compile_commands.json (argv[0] is the compiler).
// Exit code: 0 the document was written (it may still carry a problem), 2 usage.

#include "unit_tables.h"

#include "llvm/Support/JSON.h"

using namespace clang;
using namespace oculus;

namespace {

void write_pos(llvm::json::OStream &out, const char *key, const Pos &pos) {
    out.attributeObject(key, [&] {
        out.attribute("file_uid", pos.file);
        out.attribute("line", static_cast<int64_t>(pos.line));
        out.attribute("column", static_cast<int64_t>(pos.column));
    });
}

void write_document(llvm::raw_ostream &stream, const UnitResult &unit) {
    const UnitResult *tables = &unit;
    llvm::json::OStream out(stream);
    out.object([&] {
        out.attribute("frontend", "clang " CLANG_VERSION_STRING);
        out.attribute("language", unit.language);
        out.attribute("standard", unit.standard);
        out.attribute("problem", unit.problem);
        out.attribute("compile_errors", unit.compile_errors);
        out.attribute("first_error", unit.first_error);
        out.attributeArray("dropped_flags", [&] {
            for (const std::string &flag : unit.dropped) out.value(flag);
        });
        out.attributeArray("files", [&] {
            for (const FileRec &file : tables->files) {
                out.object([&] {
                    out.attribute("uid", file.uid);
                    out.attribute("path", file.path);
                    out.attribute("kind", file.kind);
                    out.attribute("is_main", file.is_main);
                    out.attribute("include_guarded", file.include_guarded);
                    out.attribute("first_line", static_cast<int64_t>(file.first_line));
                    out.attribute("first_column", static_cast<int64_t>(file.first_column));
                    out.attribute("included_in_declaration", file.included_in_declaration);
                });
            }
        });
        out.attributeArray("types", [&] {
            for (const TypeRec &type : tables->types) {
                out.object([&] {
                    out.attribute("uid", type.uid);
                    out.attribute("kind", type.kind);
                    out.attribute("name", type.name);
                    out.attribute("canonical_uid", type.canonical_uid);
                    out.attribute("sugar_uid", type.sugar_uid);
                    out.attribute("decl_uid", type.decl_uid);
                    out.attribute("is_typedef", type.is_typedef);
                    out.attribute("is_builtin", type.is_builtin);
                    out.attribute("is_bool", type.is_bool);
                    out.attribute("is_integer", type.is_integer);
                    out.attribute("int_signed", type.int_signed);
                    out.attribute("int_width", type.int_width);
                    out.attribute("plain_char", type.plain_char);
                    out.attribute("is_enum", type.is_enum);
                    out.attribute("is_record", type.is_record);
                    out.attribute("is_dependent", type.is_dependent);
                    out.attribute("enum_underlying_uid", type.enum_underlying_uid);
                    out.attribute("array_kind", type.array_kind);
                    out.attribute("array_size", type.array_size);
                    out.attribute("element_uid", type.element_uid);
                    out.attribute("is_pointer", type.is_pointer);
                });
            }
        });
        out.attributeArray("decls", [&] {
            for (const DeclRec &decl : tables->decls) {
                out.object([&] {
                    out.attribute("uid", decl.uid);
                    out.attribute("kind", decl.kind);
                    out.attribute("name", decl.name);
                    out.attributeObject("pos", [&] {
                        write_pos(out, "spelling", decl.pos.spelling);
                        write_pos(out, "expansion", decl.pos.expansion);
                        out.attribute("from_macro", decl.pos.from_macro);
                    });
                    write_pos(out, "name_pos", decl.name_pos);
                    out.attributeObject("origin", [&] {
                        out.attribute("in_system_header", decl.origin.in_system_header);
                        out.attribute("from_macro", decl.origin.from_macro);
                        out.attribute("from_macro_arg", decl.origin.from_macro_arg);
                        out.attribute("implicit", decl.origin.implicit);
                        out.attribute("instantiated", decl.origin.instantiated);
                    });
                    write_pos(out, "instantiation_pos", decl.instantiation_pos);
                    out.attribute("type_uid", decl.type_uid);
                    out.attribute("parent_uid", decl.parent_uid);
                    out.attribute("is_definition", decl.is_definition);
                    out.attribute("is_bit_field", decl.is_bit_field);
                    out.attribute("bit_width", decl.bit_width);
                    out.attribute("written_builtin", decl.written_builtin);
                    out.attribute("sign_written", decl.sign_written);
                    out.attribute("tag", decl.tag);
                    out.attribute("storage", decl.storage);
                    out.attribute("is_inline", decl.is_inline);
                    out.attribute("is_local", decl.is_local);
                    out.attribute("is_param", decl.is_param);
                    out.attribute("param_count", decl.param_count);
                    out.attribute("virtual_bases", decl.virtual_bases);
                    out.attribute("is_scoped", decl.is_scoped);
                    out.attribute("is_fixed", decl.is_fixed);
                    out.attribute("has_init", decl.has_init);
                    out.attribute("constant_init", decl.constant_init);
                    out.attribute("overrides", decl.overrides);
                    out.attribute("override_written", decl.override_written);
                    out.attribute("is_noreturn", decl.is_noreturn);
                    out.attribute("returns_void", decl.returns_void);
                    out.attribute("explicit_specialization", decl.explicit_specialization);
                    out.attribute("has_restrict", decl.has_restrict);
                    out.attribute("in_extern_c", decl.in_extern_c);
                    out.attribute("is_main", decl.is_main);
                    out.attribute("group_uid", decl.group_uid);
                    out.attribute("field_index", decl.field_index);
                    out.attribute("field_count", decl.field_count);
                    out.attribute("target", decl.target);
                    out.attribute("in_instantiation", decl.in_instantiation);
                    out.attribute("array_size_literal", decl.array_size_literal);
                    out.attribute("is_mutated", decl.is_mutated);
                    out.attribute("is_first_decl", decl.is_first_decl);
                    out.attribute("virtual_written", decl.virtual_written);
                    out.attribute("template_specialization", decl.template_specialization);
                    out.attribute("external_linkage", decl.external_linkage);
                    out.attribute("internal_linkage", decl.internal_linkage);
                    out.attribute("first_decl_static", decl.first_decl_static);
                    out.attribute("odr_definition", decl.odr_definition);
                    out.attribute("paren_declarator", decl.paren_declarator);
                    write_pos(out, "type_pos", decl.type_pos);
                    write_pos(out, "restrict_pos", decl.restrict_pos);
                    write_pos(out, "declarator_pos", decl.declarator_pos);
                });
            }
        });
        out.attributeArray("nodes", [&] {
            for (const NodeRec &node : tables->nodes) {
                out.object([&] {
                    out.attribute("uid", node.uid);
                    out.attribute("kind", node.kind);
                    out.attribute("detail", node.detail);
                    out.attributeObject("pos", [&] {
                        write_pos(out, "spelling", node.pos.spelling);
                        write_pos(out, "expansion", node.pos.expansion);
                        out.attribute("from_macro", node.pos.from_macro);
                    });
                    out.attribute("in_system_header", node.in_system_header);
                    out.attribute("from_macro", node.from_macro);
                    out.attribute("parent_uid", node.parent_uid);
                    out.attribute("function_uid", node.function_uid);
                    out.attribute("ref_uid", node.ref_uid);
                    out.attribute("child_count", node.child_count);
                    out.attribute("type_uid", node.type_uid);
                    out.attribute("is_dependent", node.is_dependent);
                    out.attribute("label_count", node.label_count);
                    out.attribute("clause_count", node.clause_count);
                    out.attribute("has_skipped_code", node.has_skipped_code);
                });
            }
        });
        out.attributeArray("macros", [&] {
            for (const MacroRec &macro : tables->macros) {
                out.object([&] {
                    out.attribute("uid", macro.uid);
                    out.attribute("kind", macro.kind);
                    out.attribute("name", macro.name);
                    out.attribute("body", macro.body);
                    write_pos(out, "pos", macro.pos);
                    write_pos(out, "name_pos", macro.name_pos);
                    out.attribute("function_like", macro.function_like);
                    out.attribute("variadic", macro.variadic);
                    out.attribute("param_count", macro.param_count);
                    out.attribute("stringize_count", macro.stringize_count);
                    out.attribute("paste_count", macro.paste_count);
                    write_pos(out, "first_operator", macro.first_operator);
                    write_pos(out, "second_operator", macro.second_operator);
                    write_pos(out, "stringize_paste", macro.stringize_paste);
                    out.attribute("uses_call_site", macro.uses_call_site);
                    out.attribute("line_count", macro.line_count);
                    out.attribute("defined_file_uid", macro.defined_file_uid);
                });
            }
        });
    });
    stream << "\n";
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        llvm::errs() << "usage: oculus-clang DIRECTORY FILE [-- ARGV... | --command LINE]\n";
        return 2;
    }
    std::string directory = argv[1];
    std::string file = argv[2];
    std::vector<std::string> command_argv;
    if (argc > 4 && std::string(argv[3]) == "--command") {
        command_argv = tokenize_command(argv[4]);
    } else if (argc > 3 && std::string(argv[3]) == "--") {
        for (int idx = 4; idx < argc; ++idx) command_argv.emplace_back(argv[idx]);
    }
    write_document(llvm::outs(), parse_unit(directory, file, command_argv));
    return 0;
}
