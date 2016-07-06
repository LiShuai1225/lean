/-
Copyright (c) Luke Nelson and Jared Roesch. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Authors: Luke Nelson and Jared Roesch
-/
prelude
import init.functor init.string init.trace init.fail

structure monad [class] (m : Type → Type) extends functor m : Type :=
(ret  : Π {a:Type}, a → m a)
(bind : Π {a b: Type}, m a → (a → m b) → m b)

inline definition return {m : Type → Type} [monad m] {A : Type} (a : A) : m A :=
monad.ret m a

inline definition fapp {A B : Type} {m : Type → Type} [monad m] (f : m (A → B)) (a : m A) : m B :=
do g ← f,
   b ← a,
   return (g b)

inline definition monad.and_then {A B : Type} {m : Type → Type} [monad m] (a : m A) (b : m B) : m B :=
do a, b

infixr ` <*> `:2 := fapp
infixl ` >>= `:2 := monad.bind
infixl ` >> `:2  := monad.and_then

inline definition guard {m : Type.{1} → Type} [monad m] [has_fail (m unit)] (P : Prop) [decidable P] : m unit :=
if P then return unit.star else fail
