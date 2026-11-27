// liboculus_clang.so: Clang inside the Oculus process (doc/inprocess_frontend.md).
//
// The C interface is specified in src/frontend/clang_shim.aiml; the header
// oculus_shim.h is generated from it (tools/gen_shim_header.py), so every
// definition below is checked against the specification by the compiler.
//
// A unit handle owns a UnitResult (unit_tables.h): the records of one parsed
// translation unit, the AST is already gone.  Accessors are null-safe and
// range-checked, strings are returned as fresh AIML strings (aiml_str_dup of
// libaiml.so, which the Oculus process has loaded).
//
// Symbol hiding: the shim is linked with LLVM and Clang statically, every symbol
// hidden except oc_* (oculus_shim.map), because the Oculus process also carries
// the LLVM that libaiml.so links (see the doc).

#include "unit_tables.h"

#include "oculus_shim.h"

#include <new>

extern "C" char *aiml_str_dup(const char *text);

using namespace oculus;

#define OC_EXPORT __attribute__((visibility("default")))

namespace {

constexpr int32_t OC_SHIM_ABI = 22;
static_assert(OC_SHIM_ABI == ClangShimAbi_version, "OC_SHIM_ABI and clang_shim.aiml disagree");

char *fresh(const std::string &text) { return aiml_str_dup(text.c_str()); }

int32_t flag(bool value) { return value ? 1 : 0; }

const UnitResult *unit_of(void *unit) { return static_cast<const UnitResult *>(unit); }

template <typename T>
const T *record_at(const std::vector<T> &records, int32_t index) {
    if (index < 0 || static_cast<size_t>(index) >= records.size()) {
        return nullptr;
    }
    return &records[static_cast<size_t>(index)];
}

std::vector<std::string> split_lines(const char *text) {
    std::vector<std::string> lines;
    if (text == nullptr || *text == '\0') {
        return lines;
    }
    std::string current;
    for (const char *at = text; *at != '\0'; ++at) {
        if (*at == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += *at;
        }
    }
    lines.push_back(current);
    return lines;
}

const Pos *pos_at(const DeclRec &decl, int32_t which) {
    switch (which) {
    case ClangPosWhich_spelling: return &decl.pos.spelling;
    case ClangPosWhich_expansion: return &decl.pos.expansion;
    case ClangPosWhich_name_pos: return &decl.name_pos;
    case ClangPosWhich_instantiation_pos: return &decl.instantiation_pos;
    case ClangPosWhich_type_pos: return &decl.type_pos;
    case ClangPosWhich_restrict_pos: return &decl.restrict_pos;
    case ClangPosWhich_declarator_pos: return &decl.declarator_pos;
    case ClangPosWhich_def_begin: return &decl.def_begin;
    case ClangPosWhich_body_end: return &decl.body_end;
    case ClangPosWhich_static_array_pos: return &decl.static_array_pos;
    case ClangPosWhich_second_align_pos: return &decl.second_align_pos;
    case ClangPosWhich_zero_align_pos: return &decl.zero_align_pos;
    case ClangPosWhich_pointer_pos: return &decl.pointer_pos;
    case ClangPosWhich_default_arg_pos: return &decl.default_arg_pos;
    default: return nullptr;
    }
}

}  // namespace

extern "C" {

OC_EXPORT int32_t oc_shim_abi(void) { return OC_SHIM_ABI; }

OC_EXPORT void *oc_parse(const char *directory, const char *file, const char *argv, const char *command) {
    std::string dir = directory != nullptr && *directory != '\0' ? directory : ".";
    std::string name = file != nullptr ? file : "";
    std::vector<std::string> command_argv = split_lines(argv);
    if (command_argv.empty() && command != nullptr && *command != '\0') {
        command_argv = tokenize_command(command);
    }
    return new (std::nothrow) UnitResult(parse_unit(dir, name, command_argv));
}

OC_EXPORT void oc_unit_free(void *unit) { delete static_cast<UnitResult *>(unit); }

OC_EXPORT char *oc_unit_text(void *unit, int32_t field) {
    const UnitResult *res = unit_of(unit);
    if (res == nullptr) {
        return fresh("");
    }
    switch (field) {
    case ClangUnitText_frontend: return fresh("clang " CLANG_VERSION_STRING " (in-process)");
    case ClangUnitText_language: return fresh(res->language);
    case ClangUnitText_standard: return fresh(res->standard);
    case ClangUnitText_problem: return fresh(res->problem);
    case ClangUnitText_first_error: return fresh(res->first_error);
    default: return fresh("");
    }
}

OC_EXPORT int32_t oc_unit_num(void *unit, int32_t field) {
    const UnitResult *res = unit_of(unit);
    if (res == nullptr) {
        return -1;
    }
    switch (field) {
    case ClangUnitNum_compile_errors: return res->compile_errors;
    case ClangUnitNum_file_count: return static_cast<int32_t>(res->files.size());
    case ClangUnitNum_type_count: return static_cast<int32_t>(res->types.size());
    case ClangUnitNum_decl_count: return static_cast<int32_t>(res->decls.size());
    case ClangUnitNum_dropped_count: return static_cast<int32_t>(res->dropped.size());
    case ClangUnitNum_node_count: return static_cast<int32_t>(res->nodes.size());
    case ClangUnitNum_macro_count: return static_cast<int32_t>(res->macros.size());
    default: return -1;
    }
}

OC_EXPORT char *oc_dropped_flag(void *unit, int32_t index) {
    const UnitResult *res = unit_of(unit);
    const std::string *flag_text = res != nullptr ? record_at(res->dropped, index) : nullptr;
    return fresh(flag_text != nullptr ? *flag_text : "");
}

OC_EXPORT char *oc_file_text(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const FileRec *file = res != nullptr ? record_at(res->files, index) : nullptr;
    if (file == nullptr) {
        return fresh("");
    }
    switch (field) {
    case ClangFileText_path: return fresh(file->path);
    case ClangFileText_kind: return fresh(file->kind);
    case ClangFileText_line_classes: return fresh(file->line_classes);
    case ClangFileText_lexical_marks: return fresh(file->lexical_marks);
    default: return fresh("");
    }
}

OC_EXPORT int32_t oc_file_num(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const FileRec *file = res != nullptr ? record_at(res->files, index) : nullptr;
    if (file == nullptr) {
        return -1;
    }
    switch (field) {
    case ClangFileNum_is_main: return flag(file->is_main);
    case ClangFileNum_include_guarded: return flag(file->include_guarded);
    case ClangFileNum_first_line: return static_cast<int32_t>(file->first_line);
    case ClangFileNum_first_column: return static_cast<int32_t>(file->first_column);
    case ClangFileNum_included_in_declaration: return flag(file->included_in_declaration);
    default: return -1;
    }
}

OC_EXPORT char *oc_type_text(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const TypeRec *type = res != nullptr ? record_at(res->types, index) : nullptr;
    if (type == nullptr) {
        return fresh("");
    }
    switch (field) {
    case ClangTypeText_kind: return fresh(type->kind);
    case ClangTypeText_name: return fresh(type->name);
    case ClangTypeText_array_kind: return fresh(type->array_kind);
    default: return fresh("");
    }
}

OC_EXPORT int32_t oc_type_num(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const TypeRec *type = res != nullptr ? record_at(res->types, index) : nullptr;
    if (type == nullptr) {
        return -1;
    }
    switch (field) {
    case ClangTypeNum_canonical_uid: return type->canonical_uid;
    case ClangTypeNum_sugar_uid: return type->sugar_uid;
    case ClangTypeNum_decl_uid: return type->decl_uid;
    case ClangTypeNum_is_typedef: return flag(type->is_typedef);
    case ClangTypeNum_is_builtin: return flag(type->is_builtin);
    case ClangTypeNum_is_bool: return flag(type->is_bool);
    case ClangTypeNum_is_integer: return flag(type->is_integer);
    case ClangTypeNum_int_signed: return flag(type->int_signed);
    case ClangTypeNum_int_width: return type->int_width;
    case ClangTypeNum_plain_char: return flag(type->plain_char);
    case ClangTypeNum_is_enum: return flag(type->is_enum);
    case ClangTypeNum_is_record: return flag(type->is_record);
    case ClangTypeNum_is_dependent: return flag(type->is_dependent);
    case ClangTypeNum_enum_underlying_uid: return type->enum_underlying_uid;
    case ClangTypeNum_array_size: return type->array_size;
    case ClangTypeNum_element_uid: return type->element_uid;
    case ClangTypeNum_is_pointer: return flag(type->is_pointer);
    default: return -1;
    }
}

OC_EXPORT char *oc_decl_text(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const DeclRec *decl = res != nullptr ? record_at(res->decls, index) : nullptr;
    if (decl == nullptr) {
        return fresh("");
    }
    switch (field) {
    case ClangDeclText_kind: return fresh(decl->kind);
    case ClangDeclText_name: return fresh(decl->name);
    case ClangDeclText_sign_written: return fresh(decl->sign_written);
    case ClangDeclText_tag: return fresh(decl->tag);
    case ClangDeclText_storage: return fresh(decl->storage);
    case ClangDeclText_target: return fresh(decl->target);
    case ClangDeclText_ref_qualifier: return fresh(decl->ref_qualifier);
    case ClangDeclText_returns_kind: return fresh(decl->returns_kind);
    case ClangDeclText_enum_value: return fresh(decl->enum_value);
    default: return fresh("");
    }
}

OC_EXPORT int32_t oc_decl_num(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const DeclRec *decl = res != nullptr ? record_at(res->decls, index) : nullptr;
    if (decl == nullptr) {
        return -1;
    }
    switch (field) {
    case ClangDeclNum_type_uid: return decl->type_uid;
    case ClangDeclNum_parent_uid: return decl->parent_uid;
    case ClangDeclNum_is_definition: return flag(decl->is_definition);
    case ClangDeclNum_is_bit_field: return flag(decl->is_bit_field);
    case ClangDeclNum_bit_width: return decl->bit_width;
    case ClangDeclNum_written_builtin: return flag(decl->written_builtin);
    case ClangDeclNum_pos_from_macro: return flag(decl->pos.from_macro);
    case ClangDeclNum_in_system_header: return flag(decl->origin.in_system_header);
    case ClangDeclNum_from_macro: return flag(decl->origin.from_macro);
    case ClangDeclNum_from_macro_arg: return flag(decl->origin.from_macro_arg);
    case ClangDeclNum_implicit: return flag(decl->origin.implicit);
    case ClangDeclNum_instantiated: return flag(decl->origin.instantiated);
    case ClangDeclNum_is_inline: return flag(decl->is_inline);
    case ClangDeclNum_is_local: return flag(decl->is_local);
    case ClangDeclNum_is_param: return flag(decl->is_param);
    case ClangDeclNum_param_count: return decl->param_count;
    case ClangDeclNum_virtual_bases: return decl->virtual_bases;
    case ClangDeclNum_is_scoped: return flag(decl->is_scoped);
    case ClangDeclNum_is_fixed: return flag(decl->is_fixed);
    case ClangDeclNum_has_init: return flag(decl->has_init);
    case ClangDeclNum_constant_init: return flag(decl->constant_init);
    case ClangDeclNum_overrides: return decl->overrides;
    case ClangDeclNum_override_written: return flag(decl->override_written);
    case ClangDeclNum_is_noreturn: return flag(decl->is_noreturn);
    case ClangDeclNum_returns_void: return flag(decl->returns_void);
    case ClangDeclNum_explicit_specialization: return flag(decl->explicit_specialization);
    case ClangDeclNum_has_restrict: return flag(decl->has_restrict);
    case ClangDeclNum_in_extern_c: return flag(decl->in_extern_c);
    case ClangDeclNum_is_main: return flag(decl->is_main);
    case ClangDeclNum_group_uid: return decl->group_uid;
    case ClangDeclNum_field_index: return decl->field_index;
    case ClangDeclNum_field_count: return decl->field_count;
    case ClangDeclNum_in_instantiation: return flag(decl->in_instantiation);
    case ClangDeclNum_array_size_literal: return flag(decl->array_size_literal);
    case ClangDeclNum_is_mutated: return flag(decl->is_mutated);
    case ClangDeclNum_is_first_decl: return flag(decl->is_first_decl);
    case ClangDeclNum_virtual_written: return flag(decl->virtual_written);
    case ClangDeclNum_template_specialization: return flag(decl->template_specialization);
    case ClangDeclNum_external_linkage: return flag(decl->external_linkage);
    case ClangDeclNum_internal_linkage: return flag(decl->internal_linkage);
    case ClangDeclNum_first_decl_static: return flag(decl->first_decl_static);
    case ClangDeclNum_odr_definition: return flag(decl->odr_definition);
    case ClangDeclNum_paren_declarator: return flag(decl->paren_declarator);
    case ClangDeclNum_is_explicit: return flag(decl->is_explicit);
    case ClangDeclNum_is_deleted: return flag(decl->is_deleted);
    case ClangDeclNum_is_defaulted: return flag(decl->is_defaulted);
    case ClangDeclNum_min_args: return decl->min_args;
    case ClangDeclNum_body_tokens: return decl->body_tokens;
    case ClangDeclNum_array_param: return flag(decl->array_param);
    case ClangDeclNum_copy_or_move: return flag(decl->copy_or_move);
    case ClangDeclNum_align_count: return decl->align_count;
    case ClangDeclNum_is_referenced: return flag(decl->is_referenced);
    case ClangDeclNum_maybe_unused: return flag(decl->maybe_unused);
    case ClangDeclNum_named_in_body: return flag(decl->named_in_body);
    case ClangDeclNum_name_count: return decl->name_count;
    case ClangDeclNum_special_local: return flag(decl->special_local);
    case ClangDeclNum_abstract_public_copy: return flag(decl->abstract_public_copy);
    case ClangDeclNum_template_ctor_no_copy: return flag(decl->template_ctor_no_copy);
    case ClangDeclNum_template_assign_no_copy: return flag(decl->template_assign_no_copy);
    case ClangDeclNum_may_throw: return flag(decl->may_throw);
    case ClangDeclNum_pure_overrides_impl: return flag(decl->pure_overrides_impl);
    case ClangDeclNum_designated_array: return flag(decl->designated_array);
    case ClangDeclNum_missing_base_init: return flag(decl->missing_base_init);
    default: return -1;
    }
}

OC_EXPORT int32_t oc_decl_pos(void *unit, int32_t index, int32_t which, int32_t part) {
    const UnitResult *res = unit_of(unit);
    const DeclRec *decl = res != nullptr ? record_at(res->decls, index) : nullptr;
    const Pos *pos = decl != nullptr ? pos_at(*decl, which) : nullptr;
    if (pos == nullptr) {
        return -1;
    }
    switch (part) {
    case ClangPosPart_file_uid: return pos->file;
    case ClangPosPart_line: return static_cast<int32_t>(pos->line);
    case ClangPosPart_column: return static_cast<int32_t>(pos->column);
    default: return -1;
    }
}

OC_EXPORT char *oc_node_text(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const NodeRec *node = res != nullptr ? record_at(res->nodes, index) : nullptr;
    if (node == nullptr) {
        return fresh("");
    }
    switch (field) {
    case ClangNodeText_kind: return fresh(node->kind);
    case ClangNodeText_detail: return fresh(node->detail);
    case ClangNodeText_spelling: return fresh(node->spelling);
    case ClangNodeText_role: return fresh(node->role);
    case ClangNodeText_before: return fresh(node->before);
    case ClangNodeText_shape: return fresh(node->shape);
    default: return fresh("");
    }
}

OC_EXPORT int32_t oc_node_num(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const NodeRec *node = res != nullptr ? record_at(res->nodes, index) : nullptr;
    if (node == nullptr) {
        return -1;
    }
    switch (field) {
    case ClangNodeNum_in_system_header: return flag(node->in_system_header);
    case ClangNodeNum_from_macro: return flag(node->from_macro);
    case ClangNodeNum_parent_uid: return node->parent_uid;
    case ClangNodeNum_function_uid: return node->function_uid;
    case ClangNodeNum_ref_uid: return node->ref_uid;
    case ClangNodeNum_child_count: return node->child_count;
    case ClangNodeNum_pos_from_macro: return flag(node->pos.from_macro);
    case ClangNodeNum_type_uid: return node->type_uid;
    case ClangNodeNum_is_dependent: return flag(node->is_dependent);
    case ClangNodeNum_label_count: return node->label_count;
    case ClangNodeNum_clause_count: return node->clause_count;
    case ClangNodeNum_has_skipped_code: return flag(node->has_skipped_code);
    case ClangNodeNum_macro_body: return flag(node->macro_body);
    case ClangNodeNum_side_effect: return flag(node->side_effect);
    case ClangNodeNum_errno_write: return flag(node->errno_write);
    default: return -1;
    }
}

OC_EXPORT int32_t oc_node_pos(void *unit, int32_t index, int32_t which, int32_t part) {
    const UnitResult *res = unit_of(unit);
    const NodeRec *node = res != nullptr ? record_at(res->nodes, index) : nullptr;
    if (node == nullptr) {
        return -1;
    }
    const Pos *pos = which == ClangPosWhich_spelling ? &node->pos.spelling
                   : which == ClangPosWhich_expansion ? &node->pos.expansion : nullptr;
    if (pos == nullptr) {
        return -1;
    }
    switch (part) {
    case ClangPosPart_file_uid: return pos->file;
    case ClangPosPart_line: return static_cast<int32_t>(pos->line);
    case ClangPosPart_column: return static_cast<int32_t>(pos->column);
    default: return -1;
    }
}

OC_EXPORT char *oc_macro_text(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const MacroRec *macro = res != nullptr ? record_at(res->macros, index) : nullptr;
    if (macro == nullptr) {
        return fresh("");
    }
    switch (field) {
    case ClangMacroText_kind: return fresh(macro->kind);
    case ClangMacroText_name: return fresh(macro->name);
    case ClangMacroText_body: return fresh(macro->body);
    case ClangMacroText_scope: return fresh(macro->scope);
    default: return fresh("");
    }
}

OC_EXPORT int32_t oc_macro_num(void *unit, int32_t index, int32_t field) {
    const UnitResult *res = unit_of(unit);
    const MacroRec *macro = res != nullptr ? record_at(res->macros, index) : nullptr;
    if (macro == nullptr) {
        return -1;
    }
    switch (field) {
    case ClangMacroNum_function_like: return flag(macro->function_like);
    case ClangMacroNum_variadic: return flag(macro->variadic);
    case ClangMacroNum_param_count: return macro->param_count;
    case ClangMacroNum_stringize_count: return macro->stringize_count;
    case ClangMacroNum_paste_count: return macro->paste_count;
    case ClangMacroNum_uses_call_site: return flag(macro->uses_call_site);
    case ClangMacroNum_line_count: return macro->line_count;
    case ClangMacroNum_defined_file_uid: return macro->defined_file_uid;
    default: return -1;
    }
}

OC_EXPORT int32_t oc_macro_pos(void *unit, int32_t index, int32_t which, int32_t part) {
    const UnitResult *res = unit_of(unit);
    const MacroRec *macro = res != nullptr ? record_at(res->macros, index) : nullptr;
    if (macro == nullptr) {
        return -1;
    }
    const Pos *pos = which == ClangMacroPos_directive ? &macro->pos
                   : which == ClangMacroPos_name_pos ? &macro->name_pos
                   : which == ClangMacroPos_first_operator ? &macro->first_operator
                   : which == ClangMacroPos_second_operator ? &macro->second_operator
                   : which == ClangMacroPos_stringize_paste ? &macro->stringize_paste
                   : which == ClangMacroPos_splice_pos ? &macro->splice_pos
                   : which == ClangMacroPos_argument_pos ? &macro->argument_pos : nullptr;
    if (pos == nullptr) {
        return -1;
    }
    switch (part) {
    case ClangPosPart_file_uid: return pos->file;
    case ClangPosPart_line: return static_cast<int32_t>(pos->line);
    case ClangPosPart_column: return static_cast<int32_t>(pos->column);
    default: return -1;
    }
}

}  // extern "C"
