Object subclass: #Point instanceVariableNames: 'x y'
Point compile: 'setX: ax setY: ay  x := ax. y := ay. ^self'
Point compile: 'x  ^x'
Point compile: 'y  ^y'
Point compile: '+ aPoint  ^Point new setX: x + aPoint x setY: y + aPoint y'
Point compile: 'printString  ^x printString , ''@'' , y printString'
p := Point new setX: 3 setY: 4
q := Point new setX: 1 setY: 2
p
q
p + q
(p + q) x
(p + q) y
