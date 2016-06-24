import data.nat
open tactic expr prod option

meta_definition is_op_app (op : expr) (e : expr) : option (expr × expr) :=
match e with
| app (app fn a1) a2 := if op = fn then some (a1, a2) else none
| _                  := none
end

meta_definition flat_with (op : expr) (assoc : expr) (e : expr) (rhs : expr) : tactic (expr × expr) :=
match is_op_app op e with
| some (a1, a2) :=  do
  r₁             ← flat_with op assoc a2 rhs,
  rhs₁ : expr    ← return (prod.pr1 r₁),
  -- H₁ is a proof for a2 + rhs = rhs₁
  H₁ : expr      ← return (prod.pr2 r₁),
  r₂             ← flat_with op assoc a1 rhs₁,
  new_app : expr ← return (prod.pr1 r₂),
  -- H₂ is a proof for a1 + rhs₁ = rhs₂
  H₂ : expr      ← return (prod.pr2 r₂),
  -- We need to generate a proof that (a1 + a2) + rhs = a1 + (a2 + rhs)
  -- H₃ is a proof for (a1 + a2) + rhs = a1 + (a2 + rhs)
  H₃ : expr      ← return (app (app (app assoc a1) a2) rhs),
  -- H₃ is a proof for a1 + (a2 + rhs) = a1 + rhs1
  H₄ : expr      ← mk_app "congr_arg" [app op a1, H₁],
  H₅             ← mk_app ("eq" <.> "trans") [H₃, H₄],
  H              ← mk_app ("eq" <.> "trans") [H₅, H₂],
  return (new_app, H)
| none          := do
  new_app ← return $ app (app op e) rhs,
  H       ← mk_app ("eq" <.> "refl") [new_app],
  return (new_app, H)
end

meta_definition flat (op : expr) (assoc : expr) (e : expr) : tactic (expr × expr) :=
match is_op_app op e with
| some (a1, a2) := do
  r₁             ← flat op assoc a2,
  new_a2 : expr  ← return (prod.pr1 r₁),
  -- H₁ is a proof that a2 = new_a2
  H₁     : expr  ← return (prod.pr2 r₁),
  r₂             ← flat_with op assoc a1 new_a2,
  new_app : expr ← return (prod.pr1 r₂),
  -- H₂ is a proof that a1 + new_a2 = new_app
  H₂ : expr      ← return (prod.pr2 r₂),
  -- We need a proof that (a1 + a2) = new_app
  -- H₃ is a proof for a1 + a2 = a1 + new_a2
  H₃ : expr      ← mk_app "congr_arg" [app op a1, H₁],
  H              ← mk_app ("eq" <.> "trans") [H₃, H₂],
  return (new_app, H)
| none          :=
  do pr ← mk_app ("eq" <.> "refl") [e],
     return (e, pr)
end

local infix `+` := nat.add
set_option trace.app_builder true
set_option pp.all true

example (a b c d e f g : nat) : ((a + b) + c) + ((d + e) + (f + g))  = a + (b + (c + (d + (e + (f + g))))) :=
by do
  assoc : expr ← mk_const $ "nat" <.> "add_assoc",
  op : expr    ← mk_const $ "nat" <.> "add",
  tgt          ← target,
  match is_eq tgt with
  | some (lhs, rhs) := do
    r ← flat op assoc lhs,
    trace (prod.pr2 r),
    exact (prod.pr2 r)
  | none            := failed
  end
