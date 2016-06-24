/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include <algorithm>
#include "util/sstream.h"
#include "kernel/environment.h"
#include "library/metavar_context.h"
#include "library/type_context.h"
#include "library/vm/vm.h"

namespace lean {
class tactic_state_cell {
    MK_LEAN_RC();
    environment     m_env;
    options         m_options;
    metavar_context m_mctx;
    list<expr>      m_goals;
    expr            m_main;
    friend class tactic_state;
    void dealloc();
public:
    tactic_state_cell(environment const & env, options const & o, metavar_context const & ctx, list<expr> const & gs,
                      expr const & main):
        m_rc(0), m_env(env), m_options(o), m_mctx(ctx), m_goals(gs), m_main(main) {}
};

class tactic_state {
private:
    tactic_state_cell * m_ptr;
    tactic_state_cell * steal_ptr() { tactic_state_cell * r = m_ptr; m_ptr = nullptr; return r; }
    friend class optional<tactic_state>;
    tactic_state():m_ptr(nullptr) {}
    explicit tactic_state(tactic_state_cell * ptr):m_ptr(ptr) { if (m_ptr) m_ptr->inc_ref(); }
    format pp_goal(formatter_factory const & fmtf, expr const & g) const;
public:
    tactic_state(environment const & env, options const & o, metavar_context const & ctx, list<expr> const & gs,
                 expr const & main);
    tactic_state(tactic_state const & s):m_ptr(s.m_ptr) { if (m_ptr) m_ptr->inc_ref(); }
    tactic_state(tactic_state && s):m_ptr(s.m_ptr) { s.m_ptr = nullptr; }
    ~tactic_state() { if (m_ptr) m_ptr->dec_ref(); }

    optional<expr> get_main_goal() const;
    optional<metavar_decl> get_main_goal_decl() const;
    tactic_state_cell * raw() const { return m_ptr; }
    options const & get_options() const { lean_assert(m_ptr); return m_ptr->m_options; }
    environment const & env() const { lean_assert(m_ptr); return m_ptr->m_env; }
    metavar_context const & mctx() const { lean_assert(m_ptr); return m_ptr->m_mctx; }
    list<expr> const & goals() const { lean_assert(m_ptr); return m_ptr->m_goals; }
    expr const & main() const { lean_assert(m_ptr); return m_ptr->m_main; }

    tactic_state & operator=(tactic_state const & s) { LEAN_COPY_REF(s); }
    tactic_state & operator=(tactic_state && s) { LEAN_MOVE_REF(s); }

    friend void swap(tactic_state & a, tactic_state & b) { std::swap(a.m_ptr, b.m_ptr); }
    friend bool is_eqp(tactic_state const & a, tactic_state const & b) { return a.m_ptr == b.m_ptr; }

    format pp(formatter_factory const & fmtf) const;
    format pp_expr(formatter_factory const & fmtf, expr const & e) const;
    format pp() const;
    format pp_expr(expr const & e) const;
};

inline bool operator==(tactic_state const & s1, tactic_state const & s2) { return is_eqp(s1, s2); }

SPECIALIZE_OPTIONAL_FOR_SMART_PTR(tactic_state)

inline optional<tactic_state> none_tactic_state() { return optional<tactic_state>(); }
inline optional<tactic_state> some_tactic_state(tactic_state const & e) { return optional<tactic_state>(e); }
inline optional<tactic_state> some_tactic_state(tactic_state && e) { return optional<tactic_state>(std::forward<tactic_state>(e)); }

tactic_state mk_tactic_state_for(environment const & env, options const & opts, local_context const & lctx, expr const & type);

tactic_state set_options(tactic_state const & s, options const & o);
tactic_state set_env(tactic_state const & s, environment const & env);
tactic_state set_mctx(tactic_state const & s, metavar_context const & mctx);
tactic_state set_goals(tactic_state const & s, list<expr> const & gs);
tactic_state set_mctx_goals(tactic_state const & s, metavar_context const & mctx, list<expr> const & gs);

tactic_state const & to_tactic_state(vm_obj const & o);
vm_obj to_obj(tactic_state const & s);

transparency_mode to_transparency_mode(vm_obj const & o);
vm_obj to_obj(transparency_mode m);

vm_obj mk_tactic_success(vm_obj const & a, tactic_state const & s);
vm_obj mk_tactic_success(tactic_state const & s);
vm_obj mk_tactic_exception(vm_obj const & fn, tactic_state const & s);
vm_obj mk_tactic_exception(throwable const & ex, tactic_state const & s);
vm_obj mk_tactic_exception(format const & fmt, tactic_state const & s);
vm_obj mk_tactic_exception(sstream const & strm, tactic_state const & s);
vm_obj mk_tactic_exception(char const * msg, tactic_state const & s);
vm_obj mk_no_goals_exception(tactic_state const & s);

format pp_expr(tactic_state const & s, expr const & e);
format pp_indented_expr(tactic_state const & s, expr const & e);

/* If r is (base_tactic_result.success a s), then return s */
optional<tactic_state> is_tactic_success(vm_obj const & r);

/* If ex is (base_tactic_result.exception fn), then return (fn opts).
   The vm_state S is used to execute (fn opts). */
optional<pair<format, tactic_state>> is_tactic_exception(vm_state & S, options const & opts, vm_obj const & ex);

type_context_cache & get_type_context_cache_for(environment const & env, options const & o);
type_context_cache & get_type_context_cache_for(tactic_state const & s);

inline type_context mk_type_context_for(tactic_state const & s, metavar_context & mctx,
                                        transparency_mode m = transparency_mode::Semireducible) {
    local_context lctx;
    if (auto d = s.get_main_goal_decl()) lctx = d->get_context();
    mctx = s.mctx();
    return type_context(mctx, lctx, get_type_context_cache_for(s), m);
}

#define lean_tactic_trace(N, S, Code) lean_trace(N, {   \
    metavar_context _mctx = (S).mctx();                 \
    type_context _ctx = mk_type_context_for(S, _mctx);  \
    scope_trace_env _scope((S).env(), _ctx);            \
    Code                                                \
})

struct type_context_scope {
    metavar_context m_mctx;
    type_context    m_ctx;
    type_context_scope(tactic_state const & s, transparency_mode m = transparency_mode::Semireducible):
        m_mctx(s.mctx()), m_ctx(mk_type_context_for(s, m_mctx, m)) {}
    type_context_scope(vm_obj const & s, transparency_mode m = transparency_mode::Semireducible):
        type_context_scope(to_tactic_state(s), m) {}
    type_context_scope(vm_obj const & s, vm_obj const & m):
        type_context_scope(to_tactic_state(s), to_transparency_mode(m)) {}
    operator type_context &() { return m_ctx; }
};

#define LEAN_TACTIC_TRY try {
#define LEAN_TACTIC_CATCH(S) } catch (exception const & ex) { return mk_tactic_exception(ex, S); }

vm_state & get_tactic_vm_state(environment const & env);

void initialize_tactic_state();
void finalize_tactic_state();
}
