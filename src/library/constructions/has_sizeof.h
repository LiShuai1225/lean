/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Daniel Selsam
*/
#pragma once
#include "kernel/environment.h"
#include "library/tactic/simplifier/simp_lemmas.h"

namespace lean {
/** \brief Given an inductive datatype \c n in \c env, add
    <tt>n.has_sizeof</tt> instance to the environment. */
environment mk_has_sizeof(environment const & env, name const & ind_name);

name mk_has_sizeof_name(name const & n);
name simp_sizeof_attribute_name();
simp_lemmas get_sizeof_simp_lemmas(type_context & tctx);
environment set_simp_sizeof(environment const & env, name const & n);

void initialize_has_sizeof();
void finalize_has_sizeof();
}
