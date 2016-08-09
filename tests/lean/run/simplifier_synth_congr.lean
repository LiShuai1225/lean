open tactic

universe variable l
constants (ss₁ : Type.{l} → Type.{l})
          (ss₂ : Π {A : Type.{l}}, A → Type.{l})
          [sss₁ : ∀ T, subsingleton (ss₁ T)]
          [sss₂ : ∀ T (t : T), subsingleton (ss₂ t)]
          (A B : Type.{l}) (HAB : A = B)
          (ss_A : ss₁ A) (ss_B : ss₁ B)
          (a₁ a₁' a₂ a₂' : A)
          (H₁ : a₁ = a₁') (H₂ : a₂ = a₂')
          (ss_a₁ : ss₂ a₁)
          (ss_a₁' : ss₂ a₁')
          (ss_a₂ : ss₂ a₂)
          (ss_a₂' : ss₂ a₂')
          (f :  Π (X : Type.{l}) (ss_X : ss₁ X) (x₁ x₂ : X) (ss_x₁ : ss₂ x₁) (ss_x₂ : ss₂ x₂), Type.{l})

attribute sss₁ [instance]
attribute sss₂ [instance]

attribute HAB [simp]
attribute H₁ [simp]
attribute H₂ [simp]

example : f A ss_A a₁ a₂ ss_a₁ ss_a₂ = f A ss_A a₁' a₂' ss_a₁' ss_a₂' := by simp

definition c₁' [reducible] := a₁'
definition c₂' [reducible] := a₂'

example : f A ss_A a₁' a₂' ss_a₁' ss_a₂' = f A ss_A c₁' c₂' ss_a₁' ss_a₂' := by simp
example : f A ss_A a₁ a₂ ss_a₁ ss_a₂ = f A ss_A c₁' c₂' ss_a₁' ss_a₂' := by simp
