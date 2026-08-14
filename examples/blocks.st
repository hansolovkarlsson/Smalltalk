[3 + 4] value
[:a :b | a + b] value: 3 value: 4
[:x | x * 2] value: 5
Object subclass: #Adder instanceVariableNames: ''
Adder compile: 'makeAdder: n  ^[:x | x + n]'
add5 := Adder new makeAdder: 5
add10 := Adder new makeAdder: 10
add5 value: 1
add10 value: 1
add5 value: 100
3 < 4 ifTrue: ['yes'] ifFalse: ['no']
3 > 4 ifTrue: ['yes'] ifFalse: ['no']
n := 0
sum := 0
[n < 5] whileTrue: [sum := sum + n. n := n + 1]
sum
Object subclass: #Math instanceVariableNames: ''
Math compile: 'fact: n  n = 0 ifTrue: [^1]. ^n * (self fact: n - 1)'
Math new fact: 10
10 factorial
