namespace X1

mutual_inductive foo, bar
with foo : Type
| mk : foo
with bar : Type
| mk : bar

check @foo
check @bar
check @foo.rec
check @bar.rec
check @foo.has_sizeof
check @bar.has_sizeof
check @foo.mk.has_sizeof_spec
check @bar.mk.has_sizeof_spec
end X1

namespace X2

mutual_inductive foo, bar
with foo : Type
| mk : bar -> foo
with bar : Type
| mk : foo -> bar

check @foo
check @bar
check @foo.rec
check @bar.rec
check @foo.has_sizeof
check @bar.has_sizeof
check @foo.mk.has_sizeof_spec
check @bar.mk.has_sizeof_spec
end X2

namespace X3

mutual_inductive foo, bar
with foo : bool -> Type
| mk : bar -> foo tt
with bar : Type
| mk : foo tt -> bar

check @foo
check @bar
check @foo.rec
check @bar.rec
check @foo.has_sizeof
check @bar.has_sizeof
check @foo.mk.has_sizeof_spec
check @bar.mk.has_sizeof_spec
end X3

namespace X4

mutual_inductive foo, bar, rig
with foo : bool -> bool -> Type
| mk : bar tt -> foo tt tt
with bar : bool -> Type
| mk : foo tt tt -> bar tt
with rig : Type
| mk : foo tt tt -> bar tt -> rig

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec
check @foo.has_sizeof
check @bar.has_sizeof
check @rig.has_sizeof
check @foo.mk.has_sizeof_spec
check @bar.mk.has_sizeof_spec
check @rig.mk.has_sizeof_spec
end X4

namespace X5

mutual_inductive foo, bar, rig
with foo : bool -> bool -> Prop
| mk : bar tt -> foo tt tt
with bar : bool -> Prop
| mk : foo tt tt -> bar tt
with rig : Prop
| mk : foo tt tt -> bar tt -> rig

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec
end X5

namespace X6

mutual_inductive foo, bar, rig (A : Type)
with foo : bool -> bool -> Prop
| mk : A -> bar tt -> foo tt tt
with bar : bool -> Prop
| mk : A -> foo tt tt -> bar tt
with rig : Prop
| mk : A -> foo tt tt -> bar tt -> rig

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec
end X6

namespace X7

mutual_inductive foo, bar, rig (A : Type)
with foo : Pi (b : bool), b = b -> Type
| mk : A -> bar tt ff tt -> foo tt rfl
with bar : bool -> bool -> bool -> Type
| mk : A -> foo tt rfl -> bar tt ff tt
with rig : Type
| mk : A -> foo tt rfl -> bar tt ff tt -> rig
| put : A -> foo tt rfl -> bar tt ff tt -> rig

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec
check @foo.has_sizeof
check @bar.has_sizeof
check @rig.has_sizeof
check @foo.mk.has_sizeof_spec
check @bar.mk.has_sizeof_spec
check @rig.mk.has_sizeof_spec
check @rig.put.has_sizeof_spec
end X7

namespace X8

mutual_inductive {l₁ l₂} foo, bar, rig (A : Type.{l₁}) (B : Type.{l₂})
with foo : Pi (b : bool), b = b -> Type.{max l₁ l₂}
| mk : A -> B -> bar tt ff tt -> foo tt rfl
with bar : bool -> bool -> bool -> Type.{max l₁ l₂}
| mk : A -> B -> foo tt rfl -> bar tt ff tt
with rig : Type.{max l₁ l₂}
| mk : A -> B -> foo tt rfl -> bar tt ff tt -> rig

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec
end X8

namespace X9

mutual_inductive {l₁ l₂ l₃} foo, bar, rig (A : Type.{l₁}) (B : Type.{l₂}) (a : A)
with foo : Pi (b : bool), b = b -> Type.{max l₁ l₂ l₃}
| mk : A -> B -> Pi x : A, x = a -> bar tt ff tt -> foo tt rfl
with bar : bool -> bool -> bool -> Type.{max l₁ l₂ l₃}
| mk : A -> B -> foo tt rfl -> bar tt ff tt
with rig : Type.{max l₁ l₂ l₃}
| mk : A -> B -> (Pi x : A, x = a -> foo tt rfl) -> bar tt ff tt -> rig

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec

end X9

namespace X10

mutual_inductive foo, bar, rig
with foo : Type -> Type
| mk : bar (foo poly_unit) -> foo (bar poly_unit)
with bar : Type -> Type
| mk : foo (bar poly_unit) -> bar (foo poly_unit)
with rig : Type -> Type
| mk : foo (bar (rig poly_unit)) -> rig (bar (foo poly_unit)) -> rig (bar (foo poly_unit))

check @foo
check @bar
check @rig
check @foo.rec
check @bar.rec
check @rig.rec
check @foo.has_sizeof
check @bar.has_sizeof
check @rig.has_sizeof
check @foo.mk.has_sizeof_spec
check @bar.mk.has_sizeof_spec
check @rig.mk.has_sizeof_spec

end X10
