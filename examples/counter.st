Object subclass: #Counter instanceVariableNames: 'count'
Counter compile: 'initialize  count := 0. ^self'
Counter compile: 'count  ^count'
Counter compile: 'increment  count := count + 1. ^self'
Counter compile: 'incrementBy: n  count := count + n. ^self'
c := Counter new initialize
c increment; increment; increment
c count
c incrementBy: 10
c count
d := Counter new initialize
d count
