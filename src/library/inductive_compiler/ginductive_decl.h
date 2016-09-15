/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Daniel Selsam
*/
#pragma once
#include "kernel/environment.h"
#include "kernel/find_fn.h"

namespace lean {

class ginductive_decl {
    buffer<name> m_lp_names;
    buffer<expr> m_params;
    buffer<expr> m_inds;
    buffer<buffer<expr> > m_intro_rules;
public:
    ginductive_decl() {}
    ginductive_decl(buffer<name> const & lp_names, buffer<expr> const & params):
        m_lp_names(lp_names), m_params(params) {}
    ginductive_decl(buffer<name> const & lp_names, buffer<expr> const & params,
                    buffer<expr> const & inds, buffer<buffer<expr> > const & intro_rules):
        m_lp_names(lp_names), m_params(params), m_inds(inds), m_intro_rules(intro_rules) {}

    bool is_mutual() const { return m_inds.size() > 1; }
    unsigned get_num_params() const { return m_params.size(); }
    unsigned get_num_inds() const { return m_inds.size(); }
    unsigned get_num_intro_rules(unsigned ind_idx) const { return m_intro_rules[ind_idx].size(); }
    levels get_levels() const { return param_names_to_levels(to_list(m_lp_names)); }

    expr const & get_param(unsigned i) const { return m_params[i]; }
    expr const & get_ind(unsigned i) const { return m_inds[i]; }
    expr const & get_intro_rule(unsigned ind_idx, unsigned ir_idx) const { return m_intro_rules[ind_idx][ir_idx]; }
    buffer<expr> const & get_intro_rules(unsigned ind_idx) const { return m_intro_rules[ind_idx]; }

    buffer<name> const & get_lp_names() const { return m_lp_names; }
    buffer<expr> const & get_params() const { return m_params; }
    buffer<expr> const & get_inds() const { return m_inds; }
    buffer<buffer<expr> > const & get_intro_rules() const { return m_intro_rules; }

    buffer<name> & get_lp_names() { return m_lp_names; }
    buffer<expr> & get_params() { return m_params; }
    buffer<expr> & get_inds() { return m_inds; }
    buffer<buffer<expr> > & get_intro_rules() { return m_intro_rules; }

    expr mk_const(name const & n) const { return mk_constant(n, get_levels()); }
    expr mk_const_params(name const & n) const { return mk_app(mk_const(n), m_params); }
    expr get_c_ind(unsigned ind_idx) const { return mk_const(mlocal_name(m_inds[ind_idx])); }
    expr get_c_ind_params(unsigned ind_idx) const { return mk_const_params(mlocal_name(m_inds[ind_idx])); }
    expr get_c_ir(unsigned ind_idx, unsigned ir_idx) const { return mk_const(mlocal_name(m_intro_rules[ind_idx][ir_idx])); }
    expr get_c_ir_params(unsigned ind_idx, unsigned ir_idx) const { return mk_const_params(mlocal_name(m_intro_rules[ind_idx][ir_idx])); }

    bool is_ind(expr const & e, unsigned ind_idx) const { return e == get_c_ind(ind_idx); }
    bool is_ind(expr const & e) const;
    bool has_ind_occ(expr const & t) const;
    bool is_ind_app(expr const & e, unsigned ind_idx) const { return is_ind(get_app_fn(e), ind_idx); }
    bool is_ind_app(expr const & e, unsigned ind_idx, buffer<expr> & indices) const;
    bool is_ind_app(expr const & e) const { return is_ind(get_app_fn(e)); }
    bool is_ind_app(expr const & e, buffer<expr> & indices) const;
    bool is_ir(expr const & e, unsigned ind_idx) const;
    bool is_ir(expr const & e) const;
    void args_to_indices(buffer<expr> const & args, buffer<expr> & indices) const;
    expr get_app_indices(expr const & e, buffer<expr> & indices) const;
    bool is_param(expr const & e) const;
    level get_result_level() const;
};
}
