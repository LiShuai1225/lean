/-
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Authors: Daniel Selsam
-/
constants (Int Real : Type)
instance : has_zero Int := sorry
instance : has_one Int := sorry
instance : has_add Int := sorry
instance : has_mul Int := sorry
instance : has_lt Int := sorry

instance : has_zero Real := sorry
instance : has_one Real := sorry
instance : has_add Real := sorry
instance : has_mul Real := sorry
instance : has_lt Real := sorry

set_option pp.all true
set_option eqn_compiler.max_steps 5000

namespace Util

private def list.withSepCore {X : Type} (f : X -> string) (sep : string) : bool -> list X -> string
| b [] := ""
| tt (x::xs) := f x ++ list.withSepCore ff xs
| ff (x::xs) := sep ++ f x ++ list.withSepCore ff xs

def list.withSep {X : Type} (f : X -> string) (sep : string) : list X -> string := list.withSepCore f sep tt

end Util

open Util

namespace level

@[reducible, pattern] definition one : level := succ zero
@[reducible, pattern] definition two : level := succ one

end level

namespace expr

@[reducible, pattern] meta def mk_Prop : expr := sort level.zero
@[reducible, pattern] meta def mk_Type : expr := sort level.one
@[reducible, pattern] meta def mk_Type₂ : expr := sort level.two

end expr

namespace tactic
namespace smt

meta constant trustZ3 : expr -> tactic expr
meta constant callZ3 : string -> tactic string

open expr nat

-- Sorts

-- (declare-const x <Sort>)
inductive Sort : Type
| Bool : Sort
| Int : Sort
| Real : Sort
| User : name → list Sort → Sort

namespace Sort

instance : inhabited Sort := ⟨Sort.User `_errorSort []⟩

meta def toSMT : Sort → string
| Bool        := "Bool"
| Int         := "Int"
| Real        := "Real"
| (User n []) := n~>to_string
| (User n xs) := "(" ++ n~>to_string ++ " " ++ list.withSep toSMT " " xs ++ ")"

meta def ofExpr : expr → Sort
| mk_Prop          := Bool
| (const `Int [])  := Int
| (const `Real []) := Real
| e                := match get_app_fn e with
                      | (local_const _ n _ _) := User n (list.map ofExpr $ get_app_args e)
                      | f                     := User (mk_str_name f~>to_string "SORT_ERROR") []
                      end

end Sort

-- (declare-sort List 1)
inductive SortDecl : Type
| mk : name → ℕ → SortDecl

namespace SortDecl

instance : inhabited SortDecl := ⟨⟨`_errorSortDecl, 0⟩⟩

meta def toSMT : SortDecl → string
| (mk n k) := n~>to_string ++ " " ++ k~>to_string

meta def ofExprCore (sortName : name) : expr → nat → SortDecl
| (pi _ _ mk_Type b)  k := ofExprCore b (succ k)
| mk_Type             k := ⟨sortName, k⟩
| e                   _ := ⟨mk_str_name e~>to_string "SORTDECL_ERROR", 0⟩

meta def ofExpr (n : name) (e : expr) : SortDecl := ofExprCore n e 0

end SortDecl

-- (declare-fun f (Int Real) Bool)
inductive FunDecl : Type
| mk : name → list Sort → Sort → FunDecl

namespace FunDecl

instance : inhabited FunDecl := ⟨⟨`_errorFunDecl, [], default Sort⟩⟩

meta def toSMT : FunDecl → string
| (mk n xs x) := n~>to_string ++ " (" ++ list.withSep Sort.toSMT " " xs ++ ") " ++ Sort.toSMT x

meta def ofExprCore : expr →  list Sort × Sort
| (pi _ _ dom bod) := match ofExprCore bod with
                      | (xs, x) := ((Sort.ofExpr dom) :: xs, x)
                      end
| bod              := ([], Sort.ofExpr bod)

meta def ofExpr (funName : name) (e : expr) : FunDecl :=
  match ofExprCore e with
  | (xs, x) := ⟨funName, xs, x⟩
  end

end FunDecl

-- Terms

namespace Term

inductive Nullary : Type
| true, false

meta def Nullary.toSMT : Nullary → string
| true := "true"
| false := "false"

inductive Unary : Type
| not
| uminus, abs --, to_Real, to_Int, is_Int

namespace Unary
meta def toSMT : Unary → string
| not     := "not"
| uminus  := "-"
| abs     := "abs"
end Unary

inductive Binary : Type
| and, or, eq, implies
| plus, sub, times, idiv, mod, div, lt, le, gt, ge

namespace Binary
meta def toSMT : Binary → string
| and     := "and"
| or      := "or"
| implies := "=>"
| eq      := "="
| plus    := "+"
| sub     := "-"
| times   := "*"
| idiv    := "div"
| mod     := "mod"
| div     := "/"
| lt      := "<"
| le      := "<="
| gt      := ">"
| ge      := ">="
end Binary

end Term

inductive Term : Type
| nullary : Term.Nullary → Term
| unary   : Term.Unary → Term → Term
| binary  : Term.Binary → Term → Term → Term
| num     : nat → Term
| user    : name → list Term → Term

namespace Term

instance : inhabited Term := ⟨user `_errorTerm []⟩

meta def toSMT : Term → string
| (nullary c)      := c~>toSMT
| (unary c t)      := "(" ++ c~>toSMT ++ " " ++ toSMT t ++ ")"
| (binary c t₁ t₂) := "(" ++ c~>toSMT ++ " " ++ toSMT t₁ ++ " " ++ toSMT t₂ ++ ")"
| (num k)          := k~>to_string
| (user c [])      := c~>to_string
| (user c ts)      := "(" ++ c~>to_string ++ " " ++ list.withSep toSMT " " ts ++ ")"

meta def ofExpr : expr → Term
| (const `true [])                  := nullary Nullary.true
| (const `false [])                 := nullary Nullary.false

| (app (const `not []) e)           := unary Unary.not (ofExpr e)

| (app (const `neg []) e)           := unary Unary.uminus (ofExpr e)
| (app (const `abs []) e)           := unary Unary.abs (ofExpr e)

| (app (app (app (const `eq ls) _) e₁) e₂) := binary Binary.eq (ofExpr e₁) (ofExpr e₂)
| (app (app (app (const `ne ls) _) e₁) e₂) := unary Unary.not (binary Binary.eq (ofExpr e₁) (ofExpr e₂))

| (app (app (const `and []) e₁) e₂) := binary Binary.and (ofExpr e₁) (ofExpr e₂)
| (app (app (const `or []) e₁) e₂)  := binary Binary.or (ofExpr e₁) (ofExpr e₂)

-- Arith
-- TODO(dhs): could flatten n-ary applications here
-- TODO(dhs): Do we want to strip the type class abstractions here or  during pre-processing?
| (app (app (const `zero [level.one]) (const `Int [])) _) := num 0
| (app (app (const `one [level.one])  (const `Int [])) _) := num 1
| (app (app (app (app (const `add [level.one]) (const `Int [])) _) e₁) e₂) := binary Binary.plus (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `mul [level.one]) (const `Int [])) _) e₁) e₂) := binary Binary.times (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `gt [level.one]) (const `Int [])) _) e₁) e₂) := binary Binary.gt (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `ge [level.one]) (const `Int [])) _) e₁) e₂) := binary Binary.ge (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `lt [level.one]) (const `Int [])) _) e₁) e₂) := binary Binary.lt (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `le [level.one]) (const `Int [])) _) e₁) e₂) := binary Binary.le (ofExpr e₁) (ofExpr e₂)

| (app (app (const `zero [level.one]) (const `Real [])) _) := num 0
| (app (app (const `one [level.one])  (const `Real [])) _) := num 1
| (app (app (app (app (const `add [level.one]) (const `Real [])) _) e₁) e₂) := binary Binary.plus (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `mul [level.one]) (const `Real [])) _) e₁) e₂) := binary Binary.times (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `gt [level.one]) (const `Real [])) _) e₁) e₂) := binary Binary.gt (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `ge [level.one]) (const `Real [])) _) e₁) e₂) := binary Binary.ge (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `lt [level.one]) (const `Real [])) _) e₁) e₂) := binary Binary.lt (ofExpr e₁) (ofExpr e₂)
| (app (app (app (app (const `le [level.one]) (const `Real [])) _) e₁) e₂) := binary Binary.le (ofExpr e₁) (ofExpr e₂)

| e := if is_arrow e then binary Binary.implies (ofExpr $ binding_domain e) (ofExpr $ binding_body e) else
       match (get_app_fn e, get_app_args e) with
       | (local_const _ userName _ _, args) := user userName (list.map ofExpr args)
       | _                                := user (mk_str_name e~>to_string "TERM_ERROR") [] -- ERROR
       end

end Term

inductive Command : Type
| declareSort : SortDecl → Command
| declareFun  : FunDecl → Command
| assert      : Term → Command
| checkSAT    : Command

namespace Command

meta def toSMT : Command -> string
| (declareSort sortDecl) := "(declare-sort " ++ sortDecl~>toSMT ++ ")"
| (declareFun funDecl)   := "(declare-fun " ++ funDecl~>toSMT ++ ")"
| (assert t)             := "(assert " ++ t~>toSMT ++ ")"
| checkSAT               := "(check-sat)"

open tactic

meta def ofHypothesis (hyp : expr) : tactic Command :=
do hypName ← return $ local_pp_name hyp,
   hypType ← infer_type hyp,
   hypTypeType ← infer_type hypType,
--   trace (hypName, hypType, hypTypeType),
   match hypTypeType with
   | mk_Prop  := return $ assert (Term.ofExpr hypType)
   | mk_Type  := return $ declareFun (FunDecl.ofExpr hypName hypType)
   | mk_Type₂ := return $ declareSort (SortDecl.ofExpr hypName hypType)
   | _        := fail $ "unexpected Type of hypothesis: " ++ expr.to_string hypType
   end

end Command


meta def Z3 : tactic unit :=
do intros,
   hyps  ← local_context,
   tgt   ← target,
   guard (tgt = expr.const `false []),
   cmds ← monad.mapM Command.ofHypothesis hyps,
   cmdString ← return $ list.withSep Command.toSMT " " (cmds ++ [Command.checkSAT]),
   trace cmdString,
   result ← callZ3 cmdString,
   trace result,
   if result = "unsat\n" then trustZ3 tgt >>= exact else fail "Z3 could not prove goal"

end smt
end tactic

namespace Examples
open tactic
open tactic.smt

-- Prop logic
example (P : Prop) : P → P → false := by Z3 -- should FAIL
example (P : Prop) : P → ¬ P → false := by Z3
example (P Q : Prop) : P ∧ Q → ¬ P → (¬ P ∨ ¬ Q) → false := by Z3
example (P Q : Prop) : P ∧ Q → ¬ P → (¬ P → ¬ Q) → false := by Z3

-- EUF
example (X Y : Type) (f g : X → X → Y) (x1 x1B x2 x2B : X) : x1 ≠ x1B → x2 ≠ x2B → f x1 x2 = f x1B x2B → false := by Z3 -- should FAIL
example (X Y : Type) (f g : X → X → Y) (x1 x1B x2 x2B : X) : x1 = x1B → x2 = x2B → f x1 x2 ≠ f x1B x2B → false := by Z3

-- Ints
example (z1 z2 z3 : Int) : z1 = z2 + z3 → z2 = z1 + z3 → z3 = z1 + z2 → z1 = 0 → false := by Z3 -- should FAIL
example (z1 z2 z3 : Int) : z1 = z2 + z3 → z2 = z1 + z3 → z3 = z1 + z2 → z1 > 0 → false := by Z3

-- Reals
example (z1 z2 z3 : Real) : z1 = z2 + z3 → z2 = z1 + z3 → z3 = z1 + z2 → z1 = 0 → false := by Z3 -- should FAIL
example (z1 z2 z3 : Real) : z1 = z2 + z3 → z2 = z1 + z3 → z3 = z1 + z2 → z1 > 0 → false := by Z3

end Examples

/-
Notes:

1. A lot of design freedom for how much is done in the pre-processing vs the (essentially-atomic) end-game tactic
   - type classes?
   - by contradiction?
   - intros?
   - P -> Q ==> implies P Q?

2. We need to escape strings
   - no unicode
   - no '
   - (check smtlib)

3. Flatten n-ary operators in Term.ofExpr?

4. BitVectors! Essential!
-/
