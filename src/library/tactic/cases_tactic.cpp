/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "util/list_fn.h"
#include "kernel/instantiate.h"
#include "kernel/inductive/inductive.h"
#include "library/util.h"
#include "library/constants.h"
#include "library/locals.h"
#include "library/app_builder.h"
#include "library/trace.h"
#include "library/vm/vm_list.h"
#include "library/vm/vm_expr.h"
#include "library/tactic/cases_tactic.h"
#include "library/tactic/intro_tactic.h"
#include "library/tactic/clear_tactic.h"
#include "library/tactic/subst_tactic.h"

namespace lean {
struct cases_tactic_exception : public exception {
    tactic_state m_state;
    cases_tactic_exception(tactic_state const & s, char const * msg):exception(msg), m_state(s) {}
};

struct cases_tactic_fn {
    environment const &           m_env;
    options const &               m_opts;
    transparency_mode             m_mode;
    metavar_context &             m_mctx;
    /* User provided ids to name new hypotheses */
    list<name> &                  m_ids;

    /* Inductive datatype information */
    bool                          m_dep_elim;
    unsigned                      m_nparams;
    unsigned                      m_nindices;
    unsigned                      m_nminors;
    declaration                   m_I_decl;
    declaration                   m_cases_on_decl;

    type_context mk_type_context_for(metavar_decl const & g) {
        return ::lean::mk_type_context_for(m_env, m_opts, m_mctx, g.get_context(), m_mode);
    }

    type_context mk_type_context_for(expr const & mvar) {
        return mk_type_context_for(*m_mctx.get_metavar_decl(mvar));
    }

    [[ noreturn ]] void throw_ill_formed_datatype() {
        throw exception("tactic cases failed, unexpected inductive datatype type");
    }

    /* throw exception that stores the intermediate state */
    [[ noreturn ]] void throw_exception(expr const & mvar, char const * msg) {
        throw cases_tactic_exception(tactic_state(m_env, m_opts, m_mctx, to_list(mvar), mvar), msg);
    }

    #define lean_cases_trace(MVAR, CODE) lean_trace(name({"tactic", "cases"}), type_context TMP_CTX = mk_type_context_for(MVAR); scope_trace_env _scope1(m_env, TMP_CTX); CODE)

    void init_inductive_info(name const & n) {
        m_dep_elim       = inductive::has_dep_elim(m_env, n);
        m_nindices       = *inductive::get_num_indices(m_env, n);
        m_nparams        = *inductive::get_num_params(m_env, n);
        // This tactic is bases on cases_on construction which only has
        // minor premises for the introduction rules of this datatype.
        m_nminors        = *inductive::get_num_intro_rules(m_env, n);
        m_I_decl         = m_env.get(n);
        m_cases_on_decl  = m_env.get({n, "cases_on"});
    }

    bool is_cases_applicable(expr const & mvar, expr const & H) {
        type_context ctx = mk_type_context_for(mvar);
        expr t = ctx.infer(H);
        buffer<expr> args;
        expr const & fn = get_app_args(t, args);
        if (!is_constant(fn))
            return false;
        if (!inductive::is_inductive_decl(m_env, const_name(fn)))
            return false;
        if (!m_env.find(name{const_name(fn), "cases_on"}) || !m_env.find(get_eq_name()))
            return false;
        if (is_standard(m_env) && !m_env.find(get_heq_name()))
            return false;
        init_inductive_info(const_name(fn));
        if (args.size() != m_nindices + m_nparams)
            return false;
        lean_cases_trace(mvar, tout() << "inductive type: " << const_name(fn) << ", num. params: " << m_nparams << ", num. indices: " << m_nindices << "\n";);
        return true;
    }

    /** \brief We say h has independent indices IF
        1- it is *not* an indexed inductive family, OR
        2- it is an indexed inductive family, but all indices are distinct local constants,
        and all hypotheses of g different from h and indices, do not depend on the indices.
        3- if not m_dep_elim, then the conclusion does not depend on the indices. */
    bool has_indep_indices(metavar_decl const & g, expr const & h) {
        lean_assert(is_local(h));
        if (m_nindices == 0)
            return true;
        type_context ctx = mk_type_context_for(g);
        expr h_type = ctx.infer(h);
        buffer<expr> args;
        get_app_args(h_type, args);
        lean_assert(m_nindices <= args.size());
        unsigned fidx = args.size() - m_nindices;
        for (unsigned i = fidx; i < args.size(); i++) {
            if (!is_local(args[i]))
                return false; // the indices must be local constants
            for (unsigned j = 0; j < i; j++) {
                if (is_local(args[j]) && mlocal_name(args[j]) == mlocal_name(args[i]))
                    return false; // the indices must be distinct local constants
            }
        }
        if (!m_dep_elim) {
            expr const & g_type = g.get_type();
            if (depends_on(g_type, h))
                return false;
        }
        local_context lctx          = g.get_context();
        optional<local_decl> h_decl = lctx.get_local_decl(h);
        lean_assert(h_decl);
        bool ok = true;
        lctx.for_each_after(*h_decl, [&](local_decl const & h1) {
                if (!ok) return;
                /* h1 must not depend on the indices */
                if (depends_on(h1, m_nindices, args.end() - m_nindices))
                    ok = false;
            });
        return ok;
    }

    pair<expr, expr> mk_eq(type_context & ctx, expr const & lhs, expr const & rhs) {
        // make sure we don't assign regular metavars at is_def_eq
        type_context::tmp_mode_scope scope(ctx);
        expr lhs_type = ctx.infer(lhs);
        expr rhs_type = ctx.infer(rhs);
        level l       = get_level(ctx, lhs_type);
        if (ctx.is_def_eq(lhs_type, rhs_type)) {
            return mk_pair(mk_app(mk_constant(get_eq_name(), to_list(l)), lhs_type, lhs, rhs),
                           mk_app(mk_constant(get_eq_refl_name(), to_list(l)), lhs_type, lhs));
        } else {
            return mk_pair(mk_app(mk_constant(get_heq_name(), to_list(l)), lhs_type, lhs, rhs_type, rhs),
                           mk_app(mk_constant(get_heq_refl_name(), to_list(l)), lhs_type, lhs));
        }
    }

    /** \brief Given a goal of the form

              Ctx, h : I A j, D |- T

        where the type of h is the inductive datatype (I A j) where A are parameters,
        and j the indices. Generate the goal

              Ctx, h : I A j, D, j' : J, h' : I A j' |- j == j' -> h == h' -> T

        Remark: (j == j' -> h == h') is a "telescopic" equality.

        Remark: this procedure assumes we have a standard environment

        Remark: j is sequence of terms, and j' a sequence of local constants.

        The original goal is solved if we can solve the produced goal. */
    expr generalize_indices(expr const & mvar, expr const & h, buffer<name> & new_indices_H, unsigned & num_eqs) {
        lean_assert(is_standard(m_env));
        metavar_decl g     = *m_mctx.get_metavar_decl(mvar);
        type_context ctx   = mk_type_context_for(g);
        expr h_type        = ctx.infer(h);
        buffer<expr> I_args;
        expr const & I     = get_app_args(h_type, I_args);
        lean_assert(I_args.size() == m_nparams + m_nindices);
        expr h_new_type    = mk_app(I, I_args.size() - m_nindices, I_args.data());
        expr d             = ctx.infer(h_new_type);
        name t_prefix("t");
        unsigned nidx = 1;
        name eq_prefix("H");
        unsigned eq_idx  = 1;
        buffer<expr> ts; /* new j' indices */
        buffer<expr> eqs;
        buffer<expr> refls;
        /* auxiliary function for populating eqs and refls. */
        auto add_eq = [&](expr const & lhs, expr const & rhs) {
            pair<expr, expr> p = mk_eq(ctx, lhs, rhs);
            expr new_eq_type   = p.first;
            expr new_eq_refl   = p.second;
            name new_eq_name   = ctx.lctx().get_unused_name(eq_prefix, eq_idx);
            eqs.push_back(ctx.push_local(new_eq_name, new_eq_type));
            refls.push_back(new_eq_refl);
        };
        /* create new indices and eqs */
        for (unsigned i = I_args.size() - m_nindices; i < I_args.size(); i++) {
            d           = ctx.try_to_pi(d);
            if (!is_pi(d))
                throw_ill_formed_datatype();
            expr t_type = binding_domain(d);
            expr t      = ctx.push_local(ctx.lctx().get_unused_name(t_prefix, nidx), t_type);
            ts.push_back(t);
            d           = instantiate(binding_body(d), t);
            h_new_type  = mk_app(h_new_type, t);
            expr const & index = I_args[i];
            add_eq(index, t);
        }
        name h_new_name = ctx.lctx().get_unused_name(local_pp_name(h));
        expr h_new      = ctx.push_local(h_new_name, h_new_type);
        if (m_dep_elim)
            add_eq(h, h_new);
        /* aux_type is  Pi (j' : J) (h' : I A j'), j == j' -> h == h' -> T */
        expr aux_type   = ctx.mk_pi(ts, ctx.mk_pi(h_new, ctx.mk_pi(eqs, g.get_type())));
        expr aux_mvar   = m_mctx.mk_metavar_decl(g.get_context(), aux_type);
        /* assign mvar := aux_mvar indices h refls */
        m_mctx.assign(mvar, mk_app(mk_app(mk_app(aux_mvar, m_nindices, I_args.end() - m_nindices), h), refls));
        /* introduce indices j' and h' */
        auto r = intron(m_env, m_opts, m_mctx, aux_mvar, m_nindices + 1, new_indices_H);
        lean_assert(r);
        num_eqs = eqs.size();
        return *r;
    }

    format pp_goal(expr const & mvar) {
        tactic_state tmp(m_env, m_opts, m_mctx, to_list(mvar), mvar);
        return tmp.pp_goal(mvar);
    }

    list<expr> elim_aux_indices(list<expr> const & goals, buffer<name> const & aux_indices_H, renaming_list & rlist) {
        lean_assert(length(goals) == length(rlist));
        buffer<expr>           new_goals;
        buffer<name_map<name>> new_rlist;
        list<expr> it1           = goals;
        list<name_map<name>> it2 = rlist;
        while (it1 && it2) {
            expr mvar           = head(it1);
            name_map<name> rmap = head(rlist);
            lean_assert(aux_indices_H.size() > 1);
            unsigned i = aux_indices_H.size() - 1; /* last element is the auxiliary major premise */
            while (i > 0) {
                --i;
                name idx_name = aux_indices_H[i];
                if (auto ridx_name = rmap.find(idx_name)) {
                    idx_name = *ridx_name;
                    rmap.erase(idx_name);
                }
                expr H_idx = m_mctx.get_hypothesis_of(mvar, idx_name)->mk_ref();
                mvar = clear(m_mctx, mvar, H_idx);
            }
            new_goals.push_back(mvar);
            new_rlist.push_back(rmap);
            it1 = tail(it1);
            it2 = tail(it2);
        }
        lean_assert(!it1 && !it2);
        rlist = to_list(new_rlist);
        return to_list(new_goals);
    }

    /* Apply the new_renames at new_names and renames. */
    void merge_renames(bool update_renames, list<name> & new_names, name_map<name> & renames, name_map<name> new_renames) {
        if (!update_renames) return;
        /* Apply new_renames to new_names. */
        buffer<name> new_new_names;
        for (name const & n : new_names) {
            if (auto r = new_renames.find(n))
                new_new_names.push_back(*r);
            else
                new_new_names.push_back(n);
        }
        new_names = to_list(new_new_names);
        /* Merge renames and new_names */
        name_map<name> m;
        renames.for_each([&](name const & k, name const & d) {
                if (auto r = new_renames.find(d)) {
                    m.insert(k, *r);
                    /* entry d -> *r can be removed from new_renames, since d was not in the initial state. */
                    new_renames.erase(d);
                } else {
                    m.insert(k, d);
                }
            });
        /* Copy remaining at new_renames entries to m. */
        new_renames.for_each([&](name const & k, name const & d) {
                m.insert(k, d);
            });
        renames = m;
    }

    optional<expr> unify_eqs(expr mvar, unsigned num_eqs, bool update_renames, list<name> & new_names, name_map<name> & renames) {
        if (num_eqs == 0)
            return some_expr(mvar);
        expr A, B, lhs, rhs;
        lean_cases_trace(mvar, tout() << "unifying equalities [" << num_eqs << "]\n" << pp_goal(mvar) << "\n";);
        metavar_decl g       = *m_mctx.get_metavar_decl(mvar);
        local_context lctx   = g.get_context();
        /* Normalize next equation lhs and rhs if needed */
        expr target          = g.get_type();
        lean_assert(is_pi(target) && is_arrow(target));
        if (is_eq(binding_domain(target), lhs, rhs)) {
            type_context ctx     = mk_type_context_for(mvar);
            expr lhs_n = ctx.whnf(lhs);
            expr rhs_n = ctx.whnf(rhs);
            if (lhs != lhs_n || rhs != rhs_n) {
                expr new_eq     = ::lean::mk_eq(ctx, lhs_n, rhs_n);
                expr new_target = mk_arrow(new_eq, binding_body(target));
                expr new_mvar   = m_mctx.mk_metavar_decl(lctx, new_target);
                m_mctx.assign(mvar, new_mvar);
                mvar = new_mvar;
                lean_cases_trace(mvar, tout() << "normalize lhs/rhs:\n" << pp_goal(mvar) << "\n";);
            }
        }
        /* Introduce next equality */
        optional<expr> mvar1 = intron(m_env, m_opts, m_mctx, mvar, 1);
        if (!mvar1) throw_exception(mvar, "cases tactic failed, unexpected failure when introducing auxiliary equatilies");
        metavar_decl g1      = *m_mctx.get_metavar_decl(*mvar1);
        local_decl H_decl    = *g1.get_context().get_last_local_decl();
        expr H_type          = H_decl.get_type();
        expr H               = H_decl.mk_ref();
        type_context ctx     = mk_type_context_for(*mvar1);
        if (is_heq(H_type, A, lhs, B, rhs)) {
            if (!ctx.is_def_eq(A, B)) {
                throw_exception(mvar, "cases tactic failed, when processing auxiliary heterogeneous equality");
            }
            /* Create helper goal mvar2 :  ctx |- lhs = rhs -> type, and assign
               mvar1 := mvar2 (eq_of_heq H) */
            expr new_target = mk_arrow(::lean::mk_eq(ctx, lhs, rhs), g1.get_type());
            expr mvar2      = m_mctx.mk_metavar_decl(lctx, new_target);
            expr val        = mk_app(mvar2, mk_eq_of_heq(ctx, H));
            m_mctx.assign(*mvar1, val);
            lean_cases_trace(mvar, tout() << "converted heq => eq\n";);
            return unify_eqs(mvar2, num_eqs, update_renames, new_names, renames);
        } else if (is_eq(H_type, A, lhs, rhs)) {
            if (is_local(rhs) || is_local(lhs)) {
                lean_cases_trace(mvar, tout() << "substitute\n";);
                name_map<name> extra_renames;
                bool symm  = is_local(rhs);
                expr mvar2 = subst(m_env, m_opts, m_mode, m_mctx, *mvar1, H, symm, update_renames ? &extra_renames : nullptr);
                merge_renames(update_renames, new_names, renames, extra_renames);
                return unify_eqs(mvar2, num_eqs - 1, update_renames, new_names, renames);
            } else {
                optional<name> c1 = is_constructor_app(m_env, lhs);
                optional<name> c2 = is_constructor_app(m_env, rhs);
                A = ctx.whnf(A);
                buffer<expr> A_args;
                expr const & A_fn   = get_app_args(A, A_args);
                if (!is_constant(A_fn) || !inductive::is_inductive_decl(m_env, const_name(A_fn)))
                    throw_ill_formed_datatype();
                name no_confusion_name(const_name(A_fn), "no_confusion");
                if (!m_env.find(no_confusion_name)) {
                    throw exception(sstream() << "cases tactic failed, construction '" << no_confusion_name << "' is not available in the environment");
                }
                expr target       = g1.get_type();
                level target_lvl  = get_level(ctx, target);
                expr no_confusion = mk_app(mk_app(mk_constant(no_confusion_name, cons(target_lvl, const_levels(A_fn))), A_args), target, lhs, rhs, H);
                if (c1 && c2) {
                    if (*c1 == *c2) {
                        lean_cases_trace(mvar, tout() << "injection\n";);
                        expr new_target = binding_domain(ctx.whnf(ctx.infer(no_confusion)));
                        expr mvar2      = m_mctx.mk_metavar_decl(lctx, new_target);
                        expr val        = mk_app(no_confusion, mvar2);
                        m_mctx.assign(*mvar1, val);
                        unsigned A_nparams = *inductive::get_num_params(m_env, const_name(A_fn));
                        lean_assert(get_app_num_args(lhs) >= A_nparams);
                        return unify_eqs(mvar2, num_eqs - 1 + get_app_num_args(lhs) - A_nparams, update_renames, new_names, renames);
                    } else {
                        /* conflict, closes the goal */
                        lean_cases_trace(*mvar1, tout() << "conflicting equality detected, closing goal using no_confusion\n";);
                        m_mctx.assign(*mvar1, no_confusion);
                        return none_expr();
                    }
                }
                throw_exception(mvar, "cases tactic failed, unsupported equality");
            }
        } else {
            throw_exception(mvar, "cases tactic failed, equality expected");
        }
    }

    pair<list<expr>, list<name>> unify_eqs(list<expr> const & mvars, list<name> const & cnames, unsigned num_eqs, intros_list * ilist, renaming_list * rlist) {
        buffer<expr>            new_goals;
        buffer<list<name>>      new_ilist;
        buffer<name_map<name>>  new_rlist;
        buffer<name>            new_cnames;
        list<expr> it1            = mvars;
        list<name> itn            = cnames;
        intros_list const * it2   = ilist;
        renaming_list const * it3 = rlist;
        while (it1) {
            list<name> new_names;
            name_map<name> renames;
            if (ilist) {
                new_names = head(*it2);
                renames = head(*it3);
            }
            optional<expr> new_mvar = unify_eqs(head(it1), num_eqs, ilist != nullptr, new_names, renames);
            if (new_mvar) {
                new_goals.push_back(*new_mvar);
                new_cnames.push_back(head(itn));
            }
            it1 = tail(it1);
            itn = tail(itn);
            if (ilist) {
                it2 = &tail(*it2);
                it3 = &tail(*it3);
                if (new_mvar) {
                    new_ilist.push_back(new_names);
                    new_rlist.push_back(renames);
                }
            }
        }
        if (ilist) {
            *ilist = to_list(new_ilist);
            *rlist = to_list(new_rlist);
        }
        return mk_pair(to_list(new_goals), to_list(new_cnames));
    }

    cases_tactic_fn(environment const & env, options const & opts, transparency_mode m, metavar_context & mctx, list<name> & ids):
        m_env(env),
        m_opts(opts),
        m_mode(m),
        m_mctx(mctx),
        m_ids(ids) {
    }

    pair<list<expr>, list<name>> operator()(expr const & mvar, expr const & H, intros_list * ilist, renaming_list * rlist) {
        lean_assert((ilist != nullptr) == (rlist != nullptr));
        lean_assert(is_metavar(mvar));
        lean_assert(m_mctx.get_metavar_decl(mvar));
        if (!is_local(H))
            throw exception("cases tactic failed, argumen must be a hypothesis");
        if (!is_cases_applicable(mvar, H))
            throw exception("cases tactic failed, it is not applicable to the given hypothesis");
        buffer<name> cnames;
        get_intro_rule_names(m_env, m_I_decl.get_name(), cnames);
        list<name> cname_list = to_list(cnames);
        metavar_decl g = *m_mctx.get_metavar_decl(mvar);
        if (has_indep_indices(g, H)) {
            /* Easy case */
            return mk_pair(induction(m_env, m_opts, m_mode, m_mctx, mvar, H, m_cases_on_decl.get_name(), m_ids, ilist, rlist),
                           cname_list);
        } else {
            buffer<name> aux_indices_H; /* names of auxiliary indices and major  */
            unsigned num_eqs; /* number of equations that need to be processed */
            expr mvar1 = generalize_indices(mvar, H, aux_indices_H, num_eqs);
            lean_cases_trace(mvar1, tout() << "after generalize_indices:\n" << pp_goal(mvar1) << "\n";);
            expr H1    = m_mctx.get_metavar_decl(mvar1)->get_context().get_last_local_decl()->mk_ref();
            intros_list tmp_ilist;
            renaming_list tmp_rlist;
            list<expr> new_goals1 = induction(m_env, m_opts, m_mode, m_mctx, mvar1, H1, m_cases_on_decl.get_name(), m_ids, &tmp_ilist, &tmp_rlist);
            lean_cases_trace(mvar1, tout() << "after applying cases_on:";
                             for (auto g : new_goals1) tout() << "\n" << pp_goal(g) << "\n";);
            list<expr> new_goals2 = elim_aux_indices(new_goals1, aux_indices_H, tmp_rlist);
            lean_cases_trace(mvar1, tout() << "after eliminating auxiliary indices:";
                             for (auto g : new_goals2) tout() << "\n" << pp_goal(g) << "\n";);
            if (ilist) *ilist = tmp_ilist;
            if (rlist) *rlist = tmp_rlist;
            return unify_eqs(new_goals2, cname_list, num_eqs, ilist, rlist);
        }
    }
};

pair<list<expr>, list<name>>
cases(environment const & env, options const & opts, transparency_mode const & m, metavar_context & mctx,
      expr const & mvar, expr const & H, list<name> & ids, intros_list * ilist, renaming_list * rlist) {
    auto r = cases_tactic_fn(env, opts, m, mctx, ids)(mvar, H, ilist, rlist);
    lean_assert(length(r.first) == length(r.second));
    return r;
}

vm_obj tactic_cases_core(vm_obj const & m, vm_obj const & H, vm_obj const & ns, vm_obj const & _s) {
    tactic_state const & s   = to_tactic_state(_s);
    try {
        if (!s.goals()) return mk_no_goals_exception(s);
        list<name> ids       = to_list_name(ns);
        metavar_context mctx = s.mctx();
        list<expr> new_goals = cases(s.env(), s.get_options(), to_transparency_mode(m), mctx, head(s.goals()),
                                     to_expr(H), ids, nullptr, nullptr).first;
        return mk_tactic_success(set_mctx_goals(s, mctx, append(new_goals, tail(s.goals()))));
    } catch (cases_tactic_exception & ex) {
        return mk_tactic_exception(ex.what(), ex.m_state);
    } catch (exception & ex) {
        return mk_tactic_exception(ex, s);
    }
}

void initialize_cases_tactic() {
    DECLARE_VM_BUILTIN(name({"tactic", "cases_core"}), tactic_cases_core);
    register_trace_class(name{"tactic", "cases"});
}

void finalize_cases_tactic() {
}
}
