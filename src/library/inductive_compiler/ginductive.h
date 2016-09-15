/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Daniel Selsam
*/
#pragma once
#include "kernel/environment.h"
#include "kernel/find_fn.h"
#include "library/inductive_compiler/ginductive_decl.h"

namespace lean {

environment register_ginductive_decl(environment const & env, ginductive_decl const & decl);

bool is_ginductive(environment const & env, name const & ind_name);

/* \brief Returns the names of the introduction rules for the inductive type \e ind_name. */
optional<list<name> > get_ginductive_intro_rules(environment const & env, name const & ind_name);

/* \brief Returns the name of the inductive type if \e ir_name is indeed an introduction rule. */
optional<name> is_ginductive_intro_rule(environment const & env, name const & ir_name);

/* \brief Returns the number of parameters for the given inductive type \e ind_name. */
unsigned get_ginductive_num_params(environment const & env, name const & ind_name);

/* \brief Returns the names of all types that are mutually inductive with \e ind_name */
list<name> get_ginductive_mut_ind_names(environment const & env, name const & ind_name);

/* \brief Returns the names of all ginductive types */
list<name> get_ginductive_all_ind_names(environment const & env);

void initialize_inductive_compiler_ginductive();
void finalize_inductive_compiler_ginductive();
}
