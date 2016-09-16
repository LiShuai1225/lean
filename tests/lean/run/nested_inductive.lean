set_option trace.inductive_compiler.nested.define.failure true
set_option max_memory 1000000

inductive vec (A : Type) : nat -> Type
| vnil : vec 0
| vcons : Pi (n : nat), A -> vec n -> vec (n+1)

namespace X1
print "simple"
inductive foo
| mk : list foo -> foo

end X1

namespace X2
print "with param"
inductive foo (A : Type)
| mk : A -> list foo -> foo

end X2

namespace X3
print "with indices"
inductive foo (A B : Type)
| mk : A -> B -> vec foo 0 -> foo

end X3

namespace X4
print "with locals in indices"
inductive foo (A : Type)
| mk : Pi (n : nat), A -> vec foo n -> foo

end X4

namespace X5
print "nested-reflexive"
inductive foo (A : Type)
| mk : A -> (Pi (m : nat), vec foo m) -> foo

end X5

namespace X6
print "locals + nested-reflexive locals in indices"
inductive foo (A : Type)
| mk : Pi (n : nat), A -> (Pi (m : nat), vec foo (n + m)) -> foo

end X6

namespace X7
print "many different nestings"
inductive foo (A : Type)
| mk : Pi (n : nat), A -> list A -> prod A A -> (Pi (m : nat), vec foo (n + m)) -> vec foo n -> foo

end X7

namespace X8
print "many different nestings, some sharing"
inductive foo (A : Type)
| mk₁ : Pi (n : nat), A -> (Pi (m : nat), vec (list (list foo)) (n + m)) -> vec foo n -> foo
| mk₂ : Pi (n : nat), A -> list A -> prod A A -> (Pi (m : nat), vec foo (n + m)) -> vec foo n -> foo

end X8

namespace X9b
print "mutual + nesting"
mutual_inductive foo, bar
with foo : Type
| mk : list (list foo) -> foo
with bar : Type
| mk : list foo -> bar

end X9b

namespace X10
print "many layers of nesting nested inductive types"

inductive wrap (A : Type)
| mk : A -> wrap

inductive box (A : Type)
| mk : A -> wrap (list box) -> box

inductive foo (A : Type)
| mk : A -> box foo -> foo

inductive bar
| mk : box (foo bar) -> bar

end X10

namespace X11
print "intro rule that introduces additional nesting"

inductive wrap (A : Type) : Type
| mk : list A -> wrap

inductive foo
| mk : wrap foo -> foo

end X11

namespace X12
print "intro rule that introduces a lot of additional nesting"

inductive wrap (A : Type) : Type
| mk : list (list A) -> wrap

inductive box (A : Type)
| mk : A -> wrap (wrap box) -> box

end X12

namespace X13
print "with reducible definitions"

attribute [reducible] definition list' := @list

inductive wrap (A : Type) : Type
| mk : A -> list' A -> wrap

attribute [reducible] definition wrap' := @wrap

inductive foo (A : Type)
| mk : A -> wrap' (list' foo) -> foo

inductive bar (A : Type)
| mk : A -> foo bar -> bar

end X13

namespace X14
print "several layers"

attribute [reducible] definition list' := @list

inductive wrap (A : Type) : Type
| mk : list (list A) -> wrap

inductive foo (A : Type)
| mk : wrap (wrap (wrap foo)) -> foo

end X14
