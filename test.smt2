(set-logic QF_SLIA)
(set-option :produce-models true)

(define-const ab String "ab")
(define-const abc String "abc")

(declare-const x String)
(declare-const y String)
(declare-const z String)
(declare-const k String)

; x.ab.y = abc.z
(assert (= (str.++ x ab y) (str.++ abc z)))
; |y| >= 0
(assert (>= (str.len y) 0))
; Regular expression: (ab[c-e]*f)|g|h
(define-const r RegLan
  (re.union
    (re.++ (str.to_re "ab") (re.* (re.range "a" "k")) (str.to_re "g"))
    (str.to_re "af")
    (str.to_re "h")
    )
  )

; Regular expression: (a[\u0001-\u4005][c-e]*f)|g|h
(define-const R RegLan
  (re.union
    (re.++ (str.to_re "ab") (re.* (re.range "a" "b")) (str.to_re "j"))
    (re.inter (re.++ (str.to_re "a") (re.range "\u0001" "\u4005")) (re.++ (re.range "a" "c") (str.to_re "f")))
    (str.to_re "k")
    (str.to_re "kk")
    )
  )


; intersection of regular expressions R and r
(define-const RI RegLan
  (re.inter R r)
)

(assert (str.in_re k RI))

(check-sat)

; 打印满足条件的字符串
(get-value (k))
