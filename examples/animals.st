Object subclass: #Animal instanceVariableNames: 'name'
Animal compile: 'setName: n  name := n. ^self'
Animal compile: 'name  ^name'
Animal compile: 'speak  ^name , '' makes a sound'''
Animal subclass: #Dog instanceVariableNames: ''
Dog compile: 'speak  ^super speak , '' (woof!)'''
Animal subclass: #Cat instanceVariableNames: ''
Cat compile: 'speak  ^super speak , '' (meow!)'''
rex := Dog new setName: 'Rex'
tom := Cat new setName: 'Tom'
rex speak
tom speak
rex class printString
tom class printString
