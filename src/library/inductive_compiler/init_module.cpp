/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Daniel Selsam
*/
#include "library/inductive_compiler/init_module.h"
#include "library/inductive_compiler/ginductive.h"

namespace lean {

void initialize_inductive_compiler_module() {
    initialize_ginductive();
}

void finalize_inductive_compiler_module() {
    finalize_ginductive();
}

}
