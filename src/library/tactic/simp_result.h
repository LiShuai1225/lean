/*
Copyright (c) 2015 Daniel Selsam. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Author: Daniel Selsam
*/
#pragma once
#include "kernel/expr_pair.h"

namespace lean {

struct simp_result {
    /* Invariant [m_pf : m_orig <rel> m_new] */
    expr m_new;

    /* If proof is not provided, it is assumed to be reflexivity */
    optional<expr> m_proof;
public:
    simp_result() {}
    simp_result(expr const & e): m_new(e) {}
    simp_result(expr const & e, expr const & proof): m_new(e), m_proof(proof) {}
    simp_result(expr const & e, optional<expr> const & proof): m_new(e), m_proof(proof) {}
    simp_result(pair<expr, optional<expr>> const & r) { simp_result(r.first, r.second); }

    bool has_proof() const { return static_cast<bool>(m_proof); }

    expr get_new() const { return m_new; }
    expr get_proof() const { lean_assert(m_proof); return *m_proof; }

    /* The following assumes that [e] and [m_new] are definitionally equal */
    void update(expr const & e) { m_new = e; }
};

}
