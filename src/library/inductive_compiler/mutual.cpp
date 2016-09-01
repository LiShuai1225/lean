/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Daniel Selsam
*/
#include "kernel/inductive/inductive.h"
#include "kernel/abstract.h"
#include "kernel/instantiate.h"
#include "kernel/type_checker.h"
#include "kernel/find_fn.h"
#include "util/sexpr/option_declarations.h"
#include "library/locals.h"
#include "library/app_builder.h"
#include "library/constants.h"
#include "library/module.h"
#include "library/trace.h"
#include "library/type_context.h"
#include "library/attribute_manager.h"
#include "library/inductive_compiler/compiler.h"
#include "library/inductive_compiler/basic.h"
#include "library/inductive_compiler/mutual.h"
#include "library/inductive_compiler/util.h"

namespace lean {

static unsigned g_next_mutual_id = 0;

static name * g_mutual_prefix = nullptr;

class add_mutual_inductive_decl_fn {
    environment                   m_env;
    options const &               m_opts;
    name_map<implicit_infer_kind> m_implicit_infer_map;
    ginductive_decl const &       m_mut_decl;
    ginductive_decl               m_basic_decl;
    name                          m_prefix;

    type_context                  m_tctx;

    buffer<expr>                  m_index_types;
    expr                          m_full_index_type;
    buffer<expr>                  m_makers;
    buffer<expr>                  m_putters;

    buffer<expr>                  m_ind_ir_locals;
    buffer<expr>                  m_ind_ir_cs;

    // For the recursor
    level                         m_elim_level;
    expr poly_unit() const { return mk_constant(get_poly_unit_name(), {m_elim_level}); }
    expr poly_unit_star() const { return mk_constant(get_poly_unit_star_name(), {m_elim_level}); }

    bool is_ind(expr const & e) {
        return is_local(e)
            && std::any_of(m_mut_decl.get_inds().begin(), m_mut_decl.get_inds().end(), [&](expr const & ind) { return e == ind; });
    }

    bool has_ind_occ(expr const & t) {
        return (bool)find(t, [&](expr const & e, unsigned) { return is_ind(e); });
    }

    expr mk_local_for(expr const & b) { return mk_local(mk_fresh_name(), binding_name(b), binding_domain(b), binding_info(b)); }
    expr mk_local_pp(name const & n, expr const & ty) { return mk_local(mk_fresh_name(), n, ty, binder_info()); }

    expr to_sigma_type(expr const & _ty) {
        expr ty = m_tctx.relaxed_whnf(_ty);
        if (!is_pi(ty))
            return mk_constant(get_unit_name());
        if (!is_pi(binding_body(ty)))
            return binding_domain(ty);
        expr l = mk_local_for(ty);
        expr dom = binding_domain(ty);
        expr rest = Fun(l, to_sigma_type(instantiate(binding_body(ty), l)));
        return mk_app(m_tctx, get_sigma_name(), {dom, rest});
    }

    expr mk_sum(expr const & A, expr const & B) {
        // TODO(dhs): skip the app-builder
        return mk_app(m_tctx, get_sum_name(), A, B);
    }

    expr mk_sum(unsigned num_args, expr const * args) {
        expr sum = args[num_args-1];
        for (unsigned i = num_args - 2; i + 1 > 0; --i) {
            sum = mk_sum(args[i], sum);
        }
        return sum;
    }

    bool dep_elim() { return inductive::has_dep_elim(m_env, mlocal_name(m_basic_decl.get_inds()[0])); }

    void compute_index_types() {
        for (expr const & ind : m_mut_decl.get_inds()) {
            m_index_types.push_back(to_sigma_type(mlocal_type(ind)));
            lean_trace(name({"inductive_compiler", "mutual", "index_types"}), tout() << mlocal_name(ind) << " ==> " << m_index_types.back() << "\n";);
        }
        m_full_index_type = mk_sum(m_index_types.size(), m_index_types.data());
        lean_trace(name({"inductive_compiler", "mutual", "full_index_type"}), tout() << m_full_index_type << "\n";);
    }

    // Returns the maker, and the sigma type that is being "made"
    pair<expr, expr> to_maker_core(expr const & _ty) {
        expr ty = m_tctx.relaxed_whnf(_ty);
        buffer<expr> locals;
        while (is_pi(ty)) {
            expr l = mk_local_for(ty);
            ty = m_tctx.relaxed_whnf(instantiate(binding_body(ty), l));
            locals.push_back(l);
        }
        expr maker, stype;
        if (locals.empty()) {
            maker = mk_constant(get_unit_star_name());
            stype = mk_constant(get_unit_name());
        } else {
            maker = locals.back();
            stype = mlocal_type(maker);
        }

        for (int i = locals.size() - 2; i >= 0; --i) {
            expr const & l = locals[i];
            expr A = mlocal_type(l);
            level l1 = get_level(m_tctx, A);
            level l2 = get_level(m_tctx, stype);
            stype = Fun(l, stype);
            maker = mk_app(mk_constant(get_sigma_mk_name(), {l1, l2}), A, stype, l, maker);
            stype = mk_app(m_tctx, get_sigma_name(), {A, stype});
        }
        return mk_pair(Fun(locals, maker), stype);
    }

    expr to_maker(expr const & ty) {
        return to_maker_core(ty).first;
    }

    expr args_to_sigma_type(expr const & ty) {
        return to_maker_core(ty).second;
    }

    void compute_makers() {
        for (expr const & ind : m_mut_decl.get_inds()) {
            m_makers.push_back(to_maker(mlocal_type(ind)));
            lean_trace(name({"inductive_compiler", "mutual", "makers"}), tout() << mlocal_name(ind) << " ==> " << m_makers.back() << "\n";);
        }
    }

    expr mk_put_rest(unsigned i) {
        expr l = mk_local_pp("rest", mk_sum(m_index_types.size() - i, m_index_types.data() + i));
        expr putter = l;
        for (unsigned j = i; j > 0; --j) {
            putter = mk_app(m_tctx, get_sum_inr_name(), m_index_types[j-1], mk_sum(m_index_types.size() - j, m_index_types.data() + j), putter);
        }
        return Fun(l, putter);
    }

    expr to_putter(unsigned ind_idx) {
        unsigned num_inds = m_index_types.size();
        expr l = mk_local_pp(name("idx"), m_index_types[ind_idx]);

        expr putter;
        if (ind_idx == num_inds - 1) {
            putter = mk_app(m_tctx, get_sum_inr_name(), m_index_types[ind_idx - 1], m_index_types[ind_idx], l);
            ind_idx--;
        } else {
            putter = mk_app(m_tctx, get_sum_inl_name(), m_index_types[ind_idx], mk_sum(num_inds - ind_idx - 1, m_index_types.data() + ind_idx + 1), l);
        }
        for (unsigned i = ind_idx; i > 0; --i) {
            putter = mk_app(m_tctx, get_sum_inr_name(), m_index_types[i - 1], mk_sum(num_inds - i, m_index_types.data() + i), putter);
        }
        return Fun(l, putter);
    }

    void compute_putters() {
        for (unsigned i = 0; i < m_mut_decl.get_inds().size(); ++i) {
            m_putters.push_back(to_putter(i));
            lean_trace(name({"inductive_compiler", "mutual", "putters"}), tout() << mlocal_name(m_mut_decl.get_inds()[i]) << " ==> " << m_putters.back() << "\n";);
        }
    }

    name mk_prefix() {
        return m_prefix;
    }

    void compute_new_ind() {
        expr ind = mk_local(mk_prefix(), mk_arrow(m_full_index_type, get_ind_result_type(m_tctx, m_mut_decl.get_inds()[0])));
        lean_trace(name({"inductive_compiler", "mutual", "basic_ind"}), tout() << mlocal_name(ind) << " : " << mlocal_type(ind) << "\n";);
        m_basic_decl.get_inds().push_back(ind);
    }

    optional<expr> translate_ind_app(expr const & app) {
        buffer<expr> args;
        expr fn = get_app_args(app, args);
        for (unsigned i = 0; i < m_mut_decl.get_inds().size(); ++i) {
            expr ind = m_mut_decl.get_inds()[i];
            if (fn == ind)
                return some_expr(mk_app(m_basic_decl.get_inds()[0], mk_app(m_putters[i], mk_app(m_makers[i], args))));
        }
        return none_expr();
    }

    expr translate_ir_arg(expr const & arg_type) {
        expr ty = m_tctx.relaxed_whnf(arg_type);
        buffer<expr> locals;
        while (is_pi(ty)) {
            expr l = mk_local_for(ty);
            locals.push_back(l);
            ty = instantiate(binding_body(ty), l);
            ty = m_tctx.relaxed_whnf(ty);
        }
        return Pi(locals, translate_all_ind_apps(ty));
    }

    expr translate_all_ind_apps(expr const & e) {
        // We might have a nested occurrence of foo in the return type of an introduction rule!
        // Example:
        // inductive foo : Type -> Type
        // | mk : foo (foo poly_unit)
        // We cannot use replace since we need to translate bottom-up
        buffer<expr> args;
        expr fn = get_app_args(e, args);
        for (expr & arg : args)
            arg = translate_all_ind_apps(arg);

        expr new_e = copy_tag(e, mk_app(fn, args));
        if (auto res = translate_ind_app(new_e))
            return *res;
        else
            return new_e;
    }

    expr translate_ir(expr const & ir) {
        name ir_name = mk_prefix() + mlocal_name(ir);
        buffer<expr> locals;
        expr ty = m_tctx.relaxed_whnf(mlocal_type(ir));
        while (is_pi(ty)) {
            expr l = mk_local_pp(binding_name(ty), translate_ir_arg(binding_domain(ty)));
            locals.push_back(l);
            ty = instantiate(binding_body(ty), l);
            ty = m_tctx.relaxed_whnf(ty);
        }
        expr result_type = translate_all_ind_apps(ty);
        return mk_local(ir_name, Pi(locals, result_type));
    }

    void compute_new_intro_rules() {
        m_basic_decl.get_intro_rules().emplace_back();
        for (unsigned i = 0; i < m_mut_decl.get_inds().size(); ++i) {
            buffer<expr> const & irs = m_mut_decl.get_intro_rules()[i];
            for (expr const & ir : irs) {
                expr new_ir = translate_ir(ir);
                m_basic_decl.get_intro_rules().back().push_back(new_ir);
                lean_trace(name({"inductive_compiler", "mutual", "basic_irs"}), tout() << mlocal_name(new_ir) << " : " << mlocal_type(new_ir) << "\n";);
            }
        }
    }

    void define_ind_types() {
        for (unsigned ind_idx = 0; ind_idx < m_mut_decl.get_inds().size(); ++ind_idx) {
            expr const & ind = m_mut_decl.get_inds()[ind_idx];
            buffer<expr> locals;
            expr ty = m_tctx.relaxed_whnf(mlocal_type(ind));
            while (is_pi(ty)) {
                expr l = mk_local_for(ty);
                locals.push_back(l);
                ty = m_tctx.relaxed_whnf(instantiate(binding_body(ty), l));
            }
            buffer<expr> args(m_mut_decl.get_params());
            args.push_back(mk_app(m_putters[ind_idx],
                                  mk_app(m_makers[ind_idx], locals)));
            expr new_ind_val = Fun(locals, mk_app(mk_constant(mlocal_name(m_basic_decl.get_inds()[0]), param_names_to_levels(to_list(m_mut_decl.get_lp_names()))),
                                                  args));
            expr new_ind_type = mlocal_type(ind);
            new_ind_val = Fun(m_mut_decl.get_params(), new_ind_val);
            new_ind_type = Pi(m_mut_decl.get_params(), new_ind_type);

            lean_trace(name({"inductive_compiler", "mutual", "new_inds"}), tout()
                       << mlocal_name(ind) << " : " << new_ind_type << " :=\n  " << new_ind_val << "\n";);
            lean_assert(!has_local(new_ind_type));
            lean_assert(!has_local(new_ind_val));
            m_env = module::add(m_env, check(m_env, mk_definition(m_env, mlocal_name(ind), to_list(m_mut_decl.get_lp_names()), new_ind_type, new_ind_val)));
        }
    }

    void compute_local_to_constant_map() {
        for (expr const & ind : m_mut_decl.get_inds()) {
            m_ind_ir_locals.push_back(ind);
            m_ind_ir_cs.push_back(mk_app(mk_constant(mlocal_name(ind), param_names_to_levels(to_list(m_mut_decl.get_lp_names()))),
                                         m_mut_decl.get_params()));
        }
        m_ind_ir_locals.push_back(m_basic_decl.get_inds()[0]);
        m_ind_ir_cs.push_back(mk_app(mk_constant(mlocal_name(m_basic_decl.get_inds()[0]), param_names_to_levels(to_list(m_basic_decl.get_lp_names()))),
                                     m_mut_decl.get_params()));

        for (buffer<expr> const & irs : m_mut_decl.get_intro_rules()) {
            for (expr const & ir : irs) {
                m_ind_ir_locals.push_back(ir);
                m_ind_ir_cs.push_back(mk_app(mk_constant(mlocal_name(ir), param_names_to_levels(to_list(m_mut_decl.get_lp_names()))),
                                             m_mut_decl.get_params()));
            }
        }
    }

    expr convert_locals_to_constants(expr const & e) {
        return replace_locals(e, m_ind_ir_locals, m_ind_ir_cs);
    }

    void define_intro_rules() {
        unsigned basic_ir_idx = 0;
        for (unsigned ind_idx = 0; ind_idx < m_mut_decl.get_inds().size(); ++ind_idx) {
            buffer<expr> const & irs = m_mut_decl.get_intro_rules()[ind_idx];
            for (expr const & ir : irs) {
                expr new_ir_val = Fun(m_mut_decl.get_params(), mk_app(mk_constant(mlocal_name(m_basic_decl.get_intro_rules()[0][basic_ir_idx]),
                                                                                  param_names_to_levels(to_list(m_mut_decl.get_lp_names()))),
                                                                      m_mut_decl.get_params()));
                expr new_ir_type = Pi(m_mut_decl.get_params(), convert_locals_to_constants(mlocal_type(ir)));
                implicit_infer_kind k = get_implicit_infer_kind(m_implicit_infer_map, mlocal_name(ir));
                new_ir_type = infer_implicit_params(new_ir_type, m_mut_decl.get_params().size(), k);
                lean_assert(!has_local(new_ir_type));
                lean_assert(!has_local(new_ir_val));
                m_env = module::add(m_env, check(m_env, mk_definition(m_env, mlocal_name(ir), to_list(m_mut_decl.get_lp_names()), new_ir_type, new_ir_val)));
                basic_ir_idx++;
            }
        }
    }

    expr mk_sigma(list<expr> const & rev_unpacked_sigma_args, expr const & idx) {
        buffer<expr> rev_sigma_args;
        to_buffer(rev_unpacked_sigma_args, rev_sigma_args);
        expr sigma = idx;
        expr stype = m_tctx.infer(sigma);
        for (expr const & sarg : rev_sigma_args) {
            expr A = mlocal_type(sarg);
            level l1 = get_level(m_tctx, A);
            level l2 = get_level(m_tctx, stype);
            stype = Fun(sarg, stype);
            sigma = mk_app(mk_constant(get_sigma_mk_name(), {l1, l2}), A, stype, sarg, sigma);
            stype = mk_app(m_tctx, get_sigma_name(), {A, stype});
        }
        return sigma;
    }

    expr unpack_sigma_and_apply_C_core(unsigned ind_idx, expr const & ty, list<expr> const & rev_unpacked_sigma_args,
                                       expr const & idx, expr const & C) {
        if (!is_pi(binding_body(ty))) {
            expr x = mk_local_pp("x", mk_app(m_basic_decl.get_inds()[0],
                                             mk_app(m_putters[ind_idx], mk_sigma(rev_unpacked_sigma_args, idx))));
            buffer<expr> indices;
            to_buffer(reverse(rev_unpacked_sigma_args), indices);
            indices.push_back(idx);
            return dep_elim() ? Fun(x, mk_app(mk_app(C, indices), x)) : mk_app(C, indices);
        }
        lean_assert(is_pi(binding_body(ty)));

        expr A = binding_domain(ty);
        expr a = mk_local_for(ty);
        expr B = args_to_sigma_type(instantiate(binding_body(ty), a));
        expr A_to_B = Fun(a, B);

        expr motive;
        level motive_level;
        {
            expr idx = mk_local_pp("idx", args_to_sigma_type(ty));
            if (dep_elim()) {
                expr x = mk_local_pp("x", mk_app(m_basic_decl.get_inds()[0],
                                                 mk_app(m_putters[ind_idx], mk_sigma(rev_unpacked_sigma_args, idx))));
                motive = Fun(idx, Pi(x, mk_sort(m_elim_level)));
                motive_level = get_level(m_tctx, Pi(x, mk_sort(m_elim_level)));
            } else {
                motive = Fun(idx, mk_sort(m_elim_level));
                motive_level = get_level(m_tctx, mk_sort(m_elim_level));
            }
        }

        expr major_premise = idx;

        expr minor_premise;
        {
            expr b = mk_local_pp("b", mk_app(A_to_B, a));
            expr rest = unpack_sigma_and_apply_C_core(ind_idx,
                                                      instantiate(binding_body(ty), a),
                                                      list<expr>(a, rev_unpacked_sigma_args),
                                                      b,
                                                      C);
            minor_premise = Fun({a, b}, rest);
        }
        levels lvls = {motive_level, get_level(m_tctx, A), get_level(m_tctx, B)};
        return mk_app(mk_constant(get_sigma_cases_on_name(), lvls), {A, A_to_B, motive, major_premise, minor_premise});
    }

    expr unpack_sigma_and_apply_C(unsigned ind_idx, expr const & idx, expr const & C) {
        expr const & ind = m_mut_decl.get_inds()[ind_idx];
        expr ind_ty = mlocal_type(ind);
        if (!is_pi(ind_ty)) {
            if (!dep_elim())
                return C;
            expr u = mk_local_pp("u", mk_constant(get_unit_name()));
            expr x_u = mk_local_pp("x_u", mk_app(m_basic_decl.get_inds()[0], mk_app(m_putters[ind_idx], u)));
            expr unit_C = Fun(u, Pi(x_u, mk_sort(m_elim_level)));
            level motive_level = get_level(m_tctx, Pi(x_u, mk_sort(m_elim_level)));
            expr unit_major_premise = idx;

            expr x_star = mk_local_pp("x", mk_app(m_basic_decl.get_inds()[0], mk_app(m_putters[ind_idx], mk_constant(get_unit_star_name()))));
            expr unit_minor_premise = Fun(x_star, mk_app(C, x_star));

            return mk_app(mk_constant(get_unit_cases_on_name(), {motive_level}), unit_C, unit_major_premise, unit_minor_premise);
        }
        if (!is_pi(binding_body(ind_ty))) {
            expr x = mk_local_pp("x", mk_app(ind, idx));
            return dep_elim() ? Fun(x, mk_app(C, {idx, x})) : mk_app(C, {idx});
        }
        expr ty = mlocal_type(m_mut_decl.get_inds()[ind_idx]);
        list<expr> rev_unpacked_sigma_args;
        return unpack_sigma_and_apply_C_core(ind_idx, ty, rev_unpacked_sigma_args, idx, C);
    }

    expr construct_inner_C_core(expr const & C, expr const & index, unsigned i, unsigned ind_idx) {
        expr A = m_index_types[i];
        expr B = mk_sum(m_index_types.size() - (i+1), m_index_types.data() + (i+1));

        expr motive;
        level motive_level;
        {
            expr c = mk_local_pp("c", mk_sum(A, B));
            if (dep_elim()) {
                expr x = mk_local_pp("x", mk_app(m_basic_decl.get_inds()[0], mk_app(mk_put_rest(i), c)));
                motive = Fun(c, Pi(x, mk_sort(m_elim_level)));
                motive_level = get_level(m_tctx, Pi(x, mk_sort(m_elim_level)));
            } else {
                motive = Fun(c, mk_sort(m_elim_level));
                motive_level = get_level(m_tctx, mk_sort(m_elim_level));
            }
            lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "inner C motive: " << motive << "\n";);
        }
        bool found_target = false;
        expr case1;
        {
            expr idx = mk_local_pp("idx", A);
            if (i == ind_idx) {
                found_target = true;
                case1 = Fun(idx, unpack_sigma_and_apply_C(ind_idx, idx, C));
            } else {
                if (dep_elim()) {
                    expr x = mk_local_pp("x", mk_app(m_basic_decl.get_inds()[0], mk_app(m_putters[i], idx)));
                    case1 = Fun({idx, x}, poly_unit());
                } else {
                    case1 = Fun(idx, poly_unit());
                }
            }
            lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "inner C case1: " << case1 << "\n";);
        }

        expr case2;
        {
            expr idx = mk_local_pp("idx", B);
            if (found_target) {
                // case2 absorbs everything else
                if (dep_elim()) {
                    expr x = mk_local_pp("x", mk_app(m_basic_decl.get_inds()[0], mk_app(mk_put_rest(ind_idx+1), idx)));
                    case2 = Fun({idx, x}, poly_unit());
                } else {
                    case2 = Fun(idx, poly_unit());
                }
            } else if (i + 1 == ind_idx && ind_idx + 1 == m_mut_decl.get_inds().size()) {
                // case2 is the end, and has the payload
                case2 = Fun(idx, unpack_sigma_and_apply_C(ind_idx, idx, C));
            } else {
                // case2 is a recursive call
               case2 = Fun(idx, construct_inner_C_core(C, idx, i+1, ind_idx));
            }
            lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "inner C case2: " << case2 << "\n";);
        }
        level l1 = get_level(m_tctx, A);
        level l2 = get_level(m_tctx, B);
        return mk_app(mk_constant(get_sum_cases_on_name(), {motive_level, l1, l2}), {A, B, motive, index, case1, case2});
    }

    expr construct_inner_C(expr const & C, unsigned ind_idx) {
/* (λ (i : I), @sum.cases_on I₁
                             I₂
                             (λ (c : I₁ ⊎ I₂), @foo_vector c -> Type)
                             i
                             (λ (i : I₁) (x : @foo_vector (put₁ i)), poly_unit)
                             (λ (n : I₂) (x : @foo_vector (put₂ n)), C n x)) */
        expr index = mk_local_pp("idx", m_full_index_type);
        return Fun(index, construct_inner_C_core(C, index, 0, ind_idx));
    }

    expr introduce_locals_for_rec_args(unsigned ind_idx, expr & C, buffer<expr> & minor_premises, buffer<expr> & indices, expr & major_premise) {
        expr const & ind = m_mut_decl.get_inds()[ind_idx];
        {
            buffer<expr> C_args;
            expr ind_ty = m_tctx.relaxed_whnf(mlocal_type(ind));
            while (is_pi(ind_ty)) {
                expr C_arg = mk_local_for(ind_ty);
                C_args.push_back(C_arg);
                ind_ty = m_tctx.relaxed_whnf(instantiate(binding_body(ind_ty), C_arg));
            }
            expr C_type;
            if (dep_elim()) {
                C_type = Pi(C_args, mk_arrow(mk_app(ind, C_args), mk_sort(m_elim_level)));
            } else {
                C_type = Pi(C_args, mk_sort(m_elim_level));
            }
            C = mk_local_pp("C", C_type);
            lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "C_type: " << C_type << "\n";);
        }

        for (expr const & ir : m_mut_decl.get_intro_rules()[ind_idx]) {
            buffer<expr> ir_args;
            buffer<expr> rec_args;
            expr ir_ty = m_tctx.relaxed_whnf(mlocal_type(ir));
            while (is_pi(ir_ty)) {
                expr minor_premise_arg = mk_local_for(ir_ty);
                ir_args.push_back(minor_premise_arg);

                buffer<expr> ir_arg_args;
                expr ir_arg = binding_domain(ir_ty);
                while (is_pi(ir_arg)) {
                    expr ir_arg_arg = mk_local_for(ir_arg);
                    ir_arg_args.push_back(ir_arg_arg);
                    ir_arg = instantiate(binding_body(ir_arg), ir_arg_arg);
                }

                buffer<expr> inner_args;
                expr arg_fn = get_app_args(ir_arg, inner_args);
                if (arg_fn == ind) {
                    expr rec_arg_type;
                    if (dep_elim()) {
                        rec_arg_type = Pi(ir_arg_args, mk_app(mk_app(C, inner_args), mk_app(minor_premise_arg, ir_arg_args)));
                    } else {
                        rec_arg_type = Pi(ir_arg_args, mk_app(C, inner_args));
                    }
                    expr rec_arg = mk_local_pp("x", rec_arg_type);
                    rec_args.push_back(rec_arg);
                }
                ir_ty = m_tctx.relaxed_whnf(instantiate(binding_body(ir_ty), minor_premise_arg));
            }
            buffer<expr> result_indices;
            expr should_be_ind = get_app_args(ir_ty, result_indices);
            lean_assert(should_be_ind == ind);
            expr minor_premise_type;
            if (dep_elim()) {
                minor_premise_type = Pi(ir_args, Pi(rec_args, mk_app(mk_app(C, result_indices), mk_app(ir, ir_args))));
            } else {
                minor_premise_type = Pi(ir_args, Pi(rec_args, mk_app(C, result_indices)));
            }
            expr minor_premise = mk_local_pp("mp", minor_premise_type);
            minor_premises.push_back(minor_premise);
            lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "mp_type: " << minor_premise_type << "\n";);
        }

        {
            expr ind_ty = m_tctx.relaxed_whnf(mlocal_type(ind));
            while (is_pi(ind_ty)) {
                expr index = mk_local_for(ind_ty);
                indices.push_back(index);
                ind_ty = m_tctx.relaxed_whnf(instantiate(binding_body(ind_ty), index));
            }
            expr major_premise_type = mk_app(ind, indices);
            major_premise = mk_local_pp("x", major_premise_type);
            lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "major premise type: " << major_premise_type << "\n";);
        }

        expr rec_type = dep_elim() ? mk_app(mk_app(C, indices), major_premise) : mk_app(C, indices);
        lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "rec_type: " << rec_type << "\n";);
        return rec_type;
    }

    void define_recursor(name const & rec_name, level_param_names const & rec_lp_names, unsigned ind_idx) {
        expr const & ind = m_mut_decl.get_inds()[ind_idx];

        expr C;
        buffer<expr> minor_premises, indices;
        expr major_premise;
        expr rec_type = introduce_locals_for_rec_args(ind_idx, C, minor_premises, indices, major_premise);

        expr inner_C = construct_inner_C(C, ind_idx);
        lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "inner C: " << inner_C << "\n";);

        buffer<expr> inner_minor_premises;
        for (unsigned i = 0; i < m_mut_decl.get_inds().size(); ++i) {
            expr const & ind = m_mut_decl.get_inds()[i];
            buffer<expr> const & irs = m_mut_decl.get_intro_rules()[i];
            for (unsigned ir_idx = 0; ir_idx < irs.size(); ++ir_idx) {
                expr const & ir = irs[ir_idx];
                buffer<expr> locals;
                buffer<expr> rec_args;
                buffer<expr> return_args;
                buffer<expr> return_rec_args;
                expr ir_type = mlocal_type(ir);
                while (is_pi(ir_type)) {
                    expr l = mk_local_for(ir_type);
                    locals.push_back(l);

                    buffer<expr> ir_arg_args;
                    expr ir_arg = binding_domain(ir_type);

                    while (is_pi(ir_arg)) {
                        expr ir_arg_arg = mk_local_for(ir_arg);
                        ir_arg_args.push_back(ir_arg_arg);
                        ir_arg = instantiate(binding_body(ir_arg), ir_arg_arg);
                    }

                    buffer<expr> inner_args;
                    expr arg_fn = get_app_args(ir_arg, inner_args);

                    ir_type = m_tctx.relaxed_whnf(instantiate(binding_body(ir_type), l));
                    return_args.push_back(l);
                    if (is_ind(arg_fn)) {
                        expr C_term = dep_elim() ? mk_app(mk_app(C, inner_args), mk_app(l, ir_arg_args)) : mk_app(C, inner_args);
                        expr rec_arg_type = Pi(ir_arg_args, (arg_fn == m_mut_decl.get_inds()[ind_idx]) ? C_term : poly_unit());
                        expr l2 = mk_local_pp("x", rec_arg_type);
                        rec_args.push_back(l2);
                        // We only pass recursive arguments of the inductive type in question to the minor premise
                        if (arg_fn == ind) {
                            return_rec_args.push_back(l2);
                        }
                    }
                }
                locals.append(rec_args);
                expr return_value;
                if (i == ind_idx) {
                    return_value = mk_app(mk_app(minor_premises[ir_idx], return_args), return_rec_args);
                } else {
                    return_value = poly_unit_star();
                }
                expr inner_minor_premise = Fun(locals, return_value);
                lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "inner minor premise: " << inner_minor_premise << "\n";);
                inner_minor_premises.push_back(inner_minor_premise);
            }
        }

        // TODO(dhs): revert to being uniform with unit
        expr inner_index = indices.empty() ? mk_app(m_putters[ind_idx], mk_constant(get_unit_star_name())) : mk_app(m_putters[ind_idx], mk_app(m_makers[ind_idx], indices));
        lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "inner index: " << inner_index << "\n";);
        expr inner_major_premise = major_premise;
        expr rec_val = mk_app(mk_app(mk_app(mk_app(mk_app(mk_constant(rec_name, param_names_to_levels(rec_lp_names)), m_mut_decl.get_params()), inner_C),
                                            inner_minor_premises), inner_index), inner_major_premise);

        rec_type = Pi(C, Pi(minor_premises, Pi(indices, Pi(major_premise, rec_type))));
        rec_val  = Fun(C, Fun(minor_premises, Fun(indices, Fun(major_premise, rec_val))));

        // Note: the constants are applied to the params, so they need to be replaced before the params are abstracted
        rec_type = convert_locals_to_constants(rec_type);
        rec_val = convert_locals_to_constants(rec_val);

        rec_type = Pi(m_mut_decl.get_params(), rec_type);
        rec_val  = Fun(m_mut_decl.get_params(), rec_val);

        lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "rec type: " << rec_type << "\n";);
        lean_trace(name({"inductive_compiler", "mutual", "rec"}), tout() << "rec val: " << rec_val << "\n";);

        lean_assert(!has_local(rec_type));
        lean_assert(!has_local(rec_val));
        m_env = module::add(m_env, check(m_env, mk_definition(m_env, inductive::get_elim_name(mlocal_name(ind)), rec_lp_names, rec_type, rec_val)));

        // TODO(dhs): now we have all the arguments.
        // Time to get down to business
    }

    void define_recursors() {
        name rec_name          = inductive::get_elim_name(mlocal_name(m_basic_decl.get_inds()[0]));
        declaration rec_decl   = m_env.get(rec_name);

        level_param_names rec_lp_names = rec_decl.get_univ_params();
        bool elim_to_prop = rec_decl.get_num_univ_params() == m_basic_decl.get_lp_names().size();
        m_elim_level      = elim_to_prop ? mk_level_zero() : mk_param_univ(head(rec_lp_names));

        levels rec_levels = param_names_to_levels(rec_lp_names);
        expr rec_const = mk_constant(rec_name, rec_levels);

        for (unsigned i = 0; i < m_mut_decl.get_inds().size(); ++i) {
            // TODO(dhs): pass more? We'll see what we need
            define_recursor(rec_name, rec_lp_names, i);
        }
    }
public:
    add_mutual_inductive_decl_fn(environment const & env, options const & opts,
                                 name_map<implicit_infer_kind> const & implicit_infer_map, ginductive_decl const & mut_decl):
        m_env(env), m_opts(opts), m_implicit_infer_map(implicit_infer_map),
        m_mut_decl(mut_decl), m_basic_decl(m_mut_decl.get_lp_names(), m_mut_decl.get_params()),
        m_prefix("_mut" + std::to_string(g_next_mutual_id++)),
        m_tctx(env) {}

    environment operator()() {
        compute_index_types();
        compute_makers();
        compute_putters();

        compute_new_ind();
        compute_new_intro_rules();
        m_env = add_inner_inductive_declaration(m_env, m_opts, m_implicit_infer_map, m_basic_decl);

        compute_local_to_constant_map();

        define_ind_types();
        define_intro_rules();

        define_recursors();
        return m_env;
    }
};

environment add_mutual_inductive_decl(environment const & env, options const & opts,
                                      name_map<implicit_infer_kind> const & implicit_infer_map, ginductive_decl const & mut_decl) {
    return add_mutual_inductive_decl_fn(env, opts, implicit_infer_map, mut_decl)();
}

void initialize_inductive_compiler_mutual() {
    register_trace_class(name({"inductive_compiler", "mutual"}));
    register_trace_class(name({"inductive_compiler", "mutual", "index_types"}));
    register_trace_class(name({"inductive_compiler", "mutual", "full_index_type"}));
    register_trace_class(name({"inductive_compiler", "mutual", "makers"}));
    register_trace_class(name({"inductive_compiler", "mutual", "putters"}));
    register_trace_class(name({"inductive_compiler", "mutual", "basic_ind"}));
    register_trace_class(name({"inductive_compiler", "mutual", "basic_irs"}));
    register_trace_class(name({"inductive_compiler", "mutual", "new_irs"}));
    register_trace_class(name({"inductive_compiler", "mutual", "new_inds"}));
    register_trace_class(name({"inductive_compiler", "mutual", "rec"}));

    g_mutual_prefix = new name(name::mk_internal_unique_name());
}

void finalize_inductive_compiler_mutual() {
    delete g_mutual_prefix;
}
}