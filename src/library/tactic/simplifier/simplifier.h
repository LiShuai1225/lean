/*
Copyright (c) 2015 Daniel Selsam. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Author: Daniel Selsam
*/
#pragma once
#include "kernel/expr_pair.h"
#include "library/type_context.h"
#include "library/vm/vm.h"
#include "library/tactic/simplifier/simp_result.h"
#include "library/tactic/simplifier/simp_lemmas.h"

namespace lean {

simp_result simplify(type_context & ctx, name const & rel, simp_lemmas const & simp_lemmas, vm_obj const & prove_fn, expr const & e);

void initialize_simplifier();
void finalize_simplifier();

}
