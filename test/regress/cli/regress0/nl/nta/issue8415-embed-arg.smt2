(set-logic ALL)
(set-info :status unsat)
(declare-fun r8 () Real)
(declare-fun v57 () Bool)
(assert (and (= 0.0 (exp (exp (+ 1.0 1.0)))) (not (= 0 (* r8 (ite v57 1 0))))))
(check-sat)