/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Daniel Selsam
*/
#include "util/sstream.h"
#include "kernel/expr.h"
#include "library/kernel_serializer.h"
#include "library/util.h"
#include "library/constants.h"
#include "library/app_builder.h"
#include "library/tactic/simplifier/util.h"

namespace lean {

optional<pair<expr, expr> > is_assoc(type_context & tctx, name const & rel, expr const & e) {
    auto op = get_binary_op(e);
    if (!op)
        return optional<pair<expr, expr> >();
    // TODO(dhs): optimized helper for instantiating a relation given a single element of a given type
    expr e_rel = app_fn(app_fn(mk_rel(tctx, rel, e, e)));
    try {
        expr assoc_class = mk_app(tctx, get_is_associative_name(), e_rel, *op);
        if (auto assoc_inst = tctx.mk_class_instance(assoc_class))
            return optional<pair<expr, expr> >(mk_pair(mk_app(tctx, get_is_associative_op_assoc_name(), 4, e_rel, *op, *assoc_inst), *op));
        else
            return optional<pair<expr, expr> >();
    } catch (app_builder_exception ex) {
        return optional<pair<expr, expr> >();
    }
}

// TODO(dhs): flat/congr/simp macros
expr mk_flat_simp_macro(expr const & assoc, expr const & thm, optional<expr> pf_of_simp) {
    throw exception("NYI");
    return expr();
}

expr mk_congr_flat_macro(expr const & assoc, expr const & thm, optional<expr> pf_op, buffer<optional<expr> > const & pf_nary_args) {
    throw exception("NYI");
    return expr();
}

expr mk_congr_flat_simp_macro(expr const & assoc, expr const & thm, optional<expr> const & pf_op,
                              buffer<optional<expr> > const & pf_nary_args, optional<expr> const & pf_of_simp) {
    throw exception("NYI");
    return expr();
}

// Rewrite-assoc macro
static name * g_rewrite_assoc_macro_name    = nullptr;
static std::string * g_rewrite_assoc_opcode = nullptr;

class rewrite_assoc_macro_definition_cell : public macro_definition_cell {
    void check_macro(expr const & m) const {
        if (!is_macro(m) || macro_num_args(m) != 3)
            throw exception(sstream() << "invalid 'rewrite_assoc' macro, incorrect number of arguments");
    }

public:
    rewrite_assoc_macro_definition_cell() {}

    virtual name get_name() const { return *g_rewrite_assoc_macro_name; }
    virtual expr check_type(expr const & m, abstract_type_context &, bool) const {
        check_macro(m);
        return macro_arg(m, 1);
    }

    virtual optional<expr> expand(expr const & m, abstract_type_context &) const {
        check_macro(m);
        // TODO(dhs): expand rewrite-assoc macro
        return none_expr();
    }

    virtual void write(serializer & s) const {
        s.write_string(*g_rewrite_assoc_opcode);
    }

    virtual bool operator==(macro_definition_cell const & other) const {
        if (auto other_ptr = dynamic_cast<rewrite_assoc_macro_definition_cell const *>(&other)) {
            return true;
        } else {
            return false;
        }
    }

    virtual unsigned hash() const {
        return get_name().hash();
    }
};

expr mk_rewrite_assoc_macro(unsigned num_args, expr const * args) {
    lean_assert(num_args == 3);
    macro_definition m(new rewrite_assoc_macro_definition_cell());
    return mk_macro(m, 3, args);
}

expr mk_rewrite_assoc_macro(expr const & assoc, expr const & thm, expr const & pf_of_step) {
    expr margs[3];
    margs[0] = assoc;
    margs[1] = thm;
    margs[2] = pf_of_step;
    macro_definition m(new rewrite_assoc_macro_definition_cell());
    return mk_macro(m, 3, margs);
}

// Rewrite-ac macro
static name * g_rewrite_ac_macro_name    = nullptr;
static std::string * g_rewrite_ac_opcode = nullptr;

class rewrite_ac_macro_definition_cell : public macro_definition_cell {
    void check_macro(expr const & m) const {
        if (!is_macro(m) || macro_num_args(m) != 4)
            throw exception(sstream() << "invalid 'rewrite_ac' macro, incorrect number of arguments");
    }

public:
    rewrite_ac_macro_definition_cell() {}

    virtual name get_name() const { return *g_rewrite_ac_macro_name; }
    virtual expr check_type(expr const & m, abstract_type_context &, bool) const {
        check_macro(m);
        return macro_arg(m, 2);
    }

    virtual optional<expr> expand(expr const & m, abstract_type_context &) const {
        check_macro(m);
        // TODO(dhs): expand rewrite-ac macro
        return none_expr();
    }

    virtual void write(serializer & s) const {
        s.write_string(*g_rewrite_ac_opcode);
    }

    virtual bool operator==(macro_definition_cell const & other) const {
        if (auto other_ptr = dynamic_cast<rewrite_ac_macro_definition_cell const *>(&other)) {
            return true;
        } else {
            return false;
        }
    }

    virtual unsigned hash() const {
        return get_name().hash();
    }
};

expr mk_rewrite_ac_macro(unsigned num_args, expr const * args) {
    lean_assert(num_args == 4);
    macro_definition m(new rewrite_ac_macro_definition_cell());
    return mk_macro(m, 4, args);
}

expr mk_rewrite_ac_macro(expr const & assoc, expr const & comm, expr const & thm, expr const & pf_of_step) {
    expr margs[4];
    margs[0] = assoc;
    margs[1] = comm;
    margs[2] = thm;
    margs[3] = pf_of_step;
    macro_definition m(new rewrite_ac_macro_definition_cell());
    return mk_macro(m, 4, margs);
}

// Setup and teardown
void initialize_simp_util() {
    // rewrite_assoc macro
    g_rewrite_assoc_macro_name = new name("rewrite_assoc");
    g_rewrite_assoc_opcode     = new std::string("REWRITE_ASSOC");
    register_macro_deserializer(*g_rewrite_assoc_opcode,
                                [](deserializer & d, unsigned num, expr const * args) {
                                    if (num != 3)
                                        throw corrupted_stream_exception();
                                    return mk_rewrite_assoc_macro(num, args);
                                });

    // rewrite_ac macro
    g_rewrite_ac_macro_name = new name("rewrite_ac");
    g_rewrite_ac_opcode     = new std::string("REWRITE_AC");
    register_macro_deserializer(*g_rewrite_ac_opcode,
                                [](deserializer & d, unsigned num, expr const * args) {
                                    if (num != 4)
                                        throw corrupted_stream_exception();
                                    return mk_rewrite_ac_macro(num, args);
                                });
}

void finalize_simp_util() {
    // rewrite_ac macro
    delete g_rewrite_ac_macro_name;
    delete g_rewrite_ac_opcode;

    // rewrite_assoc macro
    delete g_rewrite_assoc_macro_name;
    delete g_rewrite_assoc_opcode;
}
}
