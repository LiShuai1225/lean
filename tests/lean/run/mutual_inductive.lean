set_option trace.inductive_compiler.mutual true
set_option pp.binder_types true

universe g
constant (f : ℕ → ℕ → Type.{g})
constant (f00 : f 0 0)

constant (g : ℕ → Type.{g})
constant (g0 : g 0)

set_option pp.all true
set_option trace.app_builder true
set_option pp.purify_locals false

mutual_inductive foo₁, foo₂, foo₃, foo₄ (A : Type)
with foo₁ : Π (n₁ n₂ : nat), f n₁ n₂ → Type
| mk : foo₁ 0 0 f00
with foo₂ : Π (n : nat), g n → Type
| mk : foo₂ 0 g0
with foo₃ : nat → Type
| mk : foo₃ 0
with foo₄ : Type
| mk : foo₄
