Object subclass: #Adder instanceVariableNames: ''
Adder compile: 'makeAdder: n  ^[:x | x + n]'
add5 := Adder new makeAdder: 5
n := 0
sum := 0
[n < 20000] whileTrue: [b := [:x | x + n]. sum := sum + (b value: 1). n := n + 1]
sum
add5 value: 100
