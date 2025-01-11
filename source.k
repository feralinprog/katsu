method: [sum: a and: b and: c] does: [
    let: [x] =: a + b
    let: 'y =: x + c
    y
]
print: (sum: 1 and: 2 and: 3)

method: [(a: Number) plus: (b: Number)] does: [
    a + b
]
print: (1 plus: 2)

method: [a str~: b] does: [
    a >string ~ b >string
]
print: ("abc" str~: 2)

# TODO: precompute <null> and use this, so it's not looked up at runtime.
# (really, figure out how this can work. maybe something like:
# >> method: [if: cond then: body] does: (bake: [ if: cond then: body else: $null ])
# where :bake: replaces any instances of a `$ expr` unary operator with `expr call`)
method: [if: (cond: Bool) then: body] does: [ if: cond then: body else: null ]
if: 2 > 1 then: [ print: "good: 2 > 1" ]
if: t == f then: [ print: "bad! t == f" ] else: [ print: "good: t != f" ]

let: 'a =: 5
print: a
'a = 10
print: a

method: [while: (cond: Quote) do: (body: Quote)] does: [
    if: cond call then: [
        body call
        while: cond do: body
    ]
]

method: [test-while] does: [
    let: 'i =: 10
    while: [i > 0] do: [
        print: i
        'i = i - 1
    ]
]
test-while

method: [from: low to<: high do: (body: Quote)] does: [
    let: 'i =: low
    while: [i < high] do: [
        body call: i
        'i = i + 1
    ]
]
from: 0 to<: 10 do: \i [
    print: i
]

print: ((1, 2, 3), 4,)
print: t and f
print: not t
print: - 5
print: (1 + 3 * 6 / 2 * 3) == 4

method: [vec each: action] does: [
    let: 'i =: 0
    while: [i < vec length] do: [
        action call: (vec at: i)
        'i = i + 1
    ]
]

method: [vec each-reversed: action] does: [
    let: 'i =: vec length - 1
    while: [i >= 0] do: [
        action call: (vec at: i)
        'i = i - 1
    ]
]

let: 'data =: {
    { 1; 2; 3 }
    { 4; 5; }
    { 6; }
    { 7; }
}
print: data
data each: \v [print: v]
data each: \v [v each: \w [print: w]]
(data at: 0) at: 1 =: "a"
data at: 2 =: "b"
print: data

data append: "last elem"
print: data

let: 'other =: { "more"; "data" }
method: [prefix extend: suffix] does: [
    suffix each: \v [prefix append: v]
]
print: other
data extend: other
print: data

#method: [length] does: [
#    let: 'count =: 0
#    self each: [ 'count = count + 1 ]
#    count
#]
print: data length

method: [vec map: f] does: [
    let: 'new-vec =: {}
    vec each: \v [new-vec append: (f call: v)]
    new-vec
]
print: data
data each: [print]
print: (data map: [length])

method: [vec from: initial-value accumulate: f] does: [
    let: 'cur =: initial-value
    vec each: \v ['cur = f call*: (cur, v)]
    cur
]
print: ((data map: [length]) from: 0 accumulate: \sum-so-far current-value [
    sum-so-far + current-value
])

method: [vec map2: f] does: [
    vec from: {} accumulate: \new-vec x [
        new-vec append: (f call: x)
    ]
]
print: (data map2: [length])

method: [vec strsum] does: [
    vec from: "" accumulate: \a b [a ~ b >string]
]
({{"a";"b"};{"c";"d"}} map2: [strsum]) each: [print]

method: [vec select: filter] does: [
    vec from: {} accumulate: \new-vec x [
        if: (filter call: x) then: [ new-vec append: x ] else: new-vec
    ]
]
print: (data select: [it length < 2])
print: (data select: [it length >= 2])

method: [v print-with-wrapper: s] does: [
    print: s ~ v >string ~ s
]
data each: [print-with-wrapper: "!"]

# example of runtime error
#data each: [strsum]

method: [nonlocal-return-test: f] does: [
    print: "should get here"
    f call: 5
    print: "should not get here!"
]
print: \cc [nonlocal-return-test: cc] call/cc + 10

# If running this in a method, it will infinite loop on the first `cc call: ...`
# since in that case, the current continuation includes the `cc call:`, whereas
# when evaluating at the top level, the current continuation only goes to the end
# of the top-level sequence component, i.e. the `print: ...`.
let: 'cc =: null
print: "result is: " ~ ['cc = it; null] call/cc >string
cc call: 5
cc call: 10

data: 'Point has: { :x; :y }
let: 'p =: (Point x: 5 y: 10)
print: p
print: p .x
print: p .y
p set-x: 6
print: p
p set-x: 5

p type
p type == Point
p Point?
p Number?
p String? Bool?

t type == Bool

data: 'Point3D extends: { Point } has: { :z }

method: [+: (p: Point) _: (q: Point)] does: [
    Point x: (p .x + q .x) y: (p .y + q .y)
]

let: 'p3 =: (Point3D x: 5 y: 10 z: 15)
let: 'q =: (Point x: 3 y: 2)
let: 'q3 =: (Point3D x: 3 y: 2 z: 1)
p + q
p + q3
p3 + q
p3 + q3

method: [+: (p: Point3D) _: (q: Point3D)] does: [
    Point3D x: (p .x + q .x) y: (p .y + q .y) z: (p .z + q .z)
]
p + q
p + q3
p3 + q
p3 + q3

method: [while-with-break: cond do: body] does: [
    \cc [
        while: cond do: [body call: cc]
    ] call/cc
]

let: 'i =: 0
while-with-break: [t] do: \break [
    print: i
    if: i == 5 then: [break call: "result of break"]
    'i = i + 1
]

method: [while-with-break-and-continue: cond do: body] does: [
    \break-cc [
        while: cond do: [
            \cont-cc [
                body call*: break-cc, cont-cc
            ] call/cc
        ]
    ] call/cc
]

'i = 0
while-with-break-and-continue: [t] do: \break continue [
    'i = i + 1
    print: i
    if: i == 3 then: [continue call: "result of continue"]
    if: i == 5 then: [break call: "result of break"]
    print: "end of while loop body"
]

method: [assert: condition] does: [
    if: not condition call then: [
        print: "Assertion failed: " ~ condition >string
        assertion-failed
    ]
]

method: [receiver switch: (options: Vector)] does: [
    \ret [
        options each: \option [
            assert: [option length == 2]
            let: 'condition =: (option at: 0)
            let: 'response =: (option at: 1)
            if: (condition call: receiver) then: [ret call: (response call: receiver)]
        ]
        signal: (SwitchFallThrough message: "no switch conditions matched; fell through")
    ] call/rc
]

'i = 5
switch: {
    #{ [test]; [response]; :blah }
    { [(i / 2) * 2 == i]; [print: "i is even"] }
    { [i == 5]; [print: "i is 5"] }
    { t; [print: "i is odd and not 5"] }
}

[ print: "in body" ] cleanup: [ print: "in cleanup" ]
#[ print: "in body 2" ] cleanup: "shouldn't see this string!"
1234 cleanup: [ print: "cleaning up from 1234" ]

[
    let: 'result =: \ret [
        print: "before return"
        1 + (ret call: "<the result>")
        print: "after return"
    ] call/rc
    print: "result: " ~ result >string
] call

method: [top: return] does: [
    print: "top, before return"
    1 + (return call: "<the result from top>")
    print: "top, after return (BAD)"
]
method: [middle: return] does: [
    print: "middle, before calling top"
    [top: return] cleanup: [
        print: "middle, doing cleanup"
    ]
    print: "middle, after calling top (BAD)"
]
method: [bottom: return] does: [
    print: "bottom, before calling middle"
    [middle: return] cleanup: [
        print: "bottom, doing cleanup"
    ]
    print: "bottom, after calling middle (BAD)"
]
print: \ret [bottom: ret] call/rc

[
    let: 'rc-handle =: null
    \rc ['rc-handle = rc] call/rc
    print: rc-handle
    # This raises an error now.
    #print: (rc-handle call: 5)
    # TODO: hm, need to fix some syntax here. shouldn't need to put an expr at the end after a comment...
    1
] call

#\cc [cc call*: 1,2] call/cc

[
    \ret [
        [
            ret call: 1234
        ] cleanup: [ print: "first cleanup" ]
    ] call/rc
] cleanup: [
    print: "second cleanup"
]

method: [do-a-tail-cleanup] does: [ [ [ 1234 ] call ] cleanup: [ print: "other cleanup" ] ]
[ do-a-tail-cleanup ] cleanup: [ print: "some cleanup" ]

###################################################################

data: 'Optional has: {
    :has-value?
    :value
}

method: [some: value] does: [ Optional has-value?: t value: value ]
method: [none]        does: [ Optional has-value?: f value: null ]

method: [(opt: Optional) then: some-body else: none-body] does: [
    if: opt .has-value? then: [
        some-body call: opt .value
    ] else: [
        none-body call
    ]
]

method: [(opt: Optional) value!] does: [
    opt then: [it] else: [signal: (:none-no-value, "none has no value")]
]

(some: 5) then: [print] else: [print: "none"]
none then: [print] else: [print: "none"]

# Sequences must have:
#   length
#   at:
mixin: 'Sequence
method: [(seq: Sequence) each: (action: Quote)] does: [
    let: 'i =: 0
    while: [i < seq length] do: [
        action call: (seq at: i)
        'i = i + 1
    ]
]

mix-in: Sequence to: Vector

method: [(opt: Optional) length] does: [ opt then: 1 else: 0 ]
method: [(opt: Optional) at: (i: Number)] does: [
    if: i == 0 then: [
        opt value!
    ] else: [
        signal: (:out-of-range, "index " ~ i >string ~ " out of range for optional")
    ]
]
mix-in: Sequence to: Optional

{ 1; 2; 3 } each: [print]
(some: 123) each: [print]
none each: [print]


###################################################################


# Define assocs. (For now, these are just vectors of key/value pairs.)

data: 'AssocEntry has: { :key; :value }

data: 'Assoc has: { :entries }
method: [assoc: vector-entries] does: [
    Assoc entries: (vector-entries map: \entry [ AssocEntry key: (entry at: 0) value: (entry at: 1) ])
]

method: [(assoc: Assoc) each-entry: (action: Quote)] does: [ assoc .entries each: action ]

method: [(a: Assoc) if-has: key then*: then-fn else: else-fn] does: [
    \ret [
        a each-entry: [
            if: it .key == key then: [ret call: (then-fn call: it)]
        ]
        else-fn call
    ] call/rc
]

method: [(a: Assoc) if-has: key then: then-fn else: else-fn] does: [
    a if-has: key then*: [then-fn call: it .value] else: else-fn
]

method: [(a: Assoc) at*: key] does: [
    a if-has: key then: [some: it] else: [none]
]

method: [(a: Assoc) at: key] does: [ (a at*: key) value! ]

method: [(a: Assoc) has: key] does: [ (a at*: key) .has-value?  ]

method: [(a: Assoc) at: key put: value] does: [
    a if-has: key then*: [ it set-value: value ] else: [
        a .entries append: (AssocEntry key: key value: value)
    ]
]

let: 'm =: (assoc: { { 1; "one" }; { 2; "two" } })
m
m has: 1
m has: 2
m has: 3
m at*: 1
m at*: 3
m at: 1
m at: 2 put: "duo"
m at: 2
m at: 3 put: "three"
m


###################################################################

let: 'namestack =: { assoc: {} }
method: [push-scope: (vars: Assoc)] does: [namestack append: vars]
method: [pop-scope] does: [namestack pop]
method: [scope has-var: (var: Symbol)] does: [ scope has: var ]
method: [scope get-var: (var: Symbol)] does: [ scope get: var ]
method: [scope set: (var: Symbol) value: value] does: [ scope at: var put: value ]
method: [(name: Symbol) get] does: [
    \ret [
        namestack each-reversed: \scope [
            scope if-has: name then: [ret call: it] else: none
        ]
    ] call/rc
]
method: [(name: Symbol) set: value] does: [(namestack at: (namestack length - 1)) at: name put: value ]
method: [with-scope: (body: Quote)] does: [
    push-scope: (assoc: {})
    body cleanup: [pop-scope]
]

###################################################################

:x set: 1
print: :x get
with-scope: [
    print: :x get
    :x set: 2
    print: :x get
]
print: :x get

###################################################################

# Stack of list-of-pairs, where each pair holds a predicate quote
# and a handler quote.
let: 'handler-stack =: {}
method: [(body: Quote) with-handlers: (handlers: Vector)] does: [
    handler-stack append: handlers
    body cleanup: [handler-stack pop]
]

method: [(predicate: Quote) matches-condition?: condition] does: [ predicate call: condition ]
method: [(type: Type) matches-condition?: condition] does: [ condition is-instance: type ]
method: [value matches-condition?: condition] does: [ condition == value ]

# Call a handler if one matches; else return null.
method: [(handlers: Vector) call-handler*: condition] does: [
    \ret [
        handlers each: [
            let: 'predicate =: (it at: 0)
            let: 'handler =: (it at: 1)
            if: (predicate matches-condition?: condition) then: [ret call: (handler call: condition)]
        ]
        null
    ] call/rc
]

# Stack of assocs (from symbol to restart quote).
let: 'restart-stack =: {}
method: [(body: Quote) with-restarts: (restarts: Assoc)] does: [
    restart-stack append: restarts
    body cleanup: [restart-stack pop]
]

method: [(restart: Symbol) get-restart] does: [
    \ret [
        restart-stack each-reversed: [
            it if-has: restart then: \quote [ret call: quote] else: null
        ]
        signal: (:no-such-restart, "restart " ~ restart >string ~ " not found")
    ] call/rc
]

method: [(restart: Symbol) invoke-restart] does: [
    restart get-restart call
]

data: 'Condition has: { :message }
data: 'InternalError extends: { Condition } has: {}
data: 'UndefinedSlot extends: { Condition } has: {}

method: [signal: condition] does: [
    \ret [
        handler-stack each-reversed: [
            if: not (let: 'handler-result =: (it call-handler*: condition)) Null? then: [
                ret call: handler-result
            ]
        ]
        # No handlers resolved the condition. Let's try asking the user!
        # TODO: get list of restarts, call builtin (... builtin for now) to get user input.
        panic!: condition
    ] call/rc
]

# Handle a signal from the VM.
# TODO: have VM just produce actual Condition (or subtype thereof) objects.
method: [tup condition-tuple>object] does: [
    \condition message [
        condition switch: {
            { [it == :internal-error]; [InternalError message: message] }
            { [it == :undefined-slot]; [UndefinedSlot message: message] }
            { t;                       [Condition message: message] }
        } pr
    ] call*: tup
]
method: [handle-signal: condition-tuple] does: [ signal: condition-tuple condition-tuple>object ]

[
    [ blah ] with-restarts: (assoc: {
        { :use-10; [ 10 ] }
    })
] with-handlers: {
    {
        UndefinedSlot; \condition [
            print: "got an UndefinedSlot:: " ~ condition >string
            print: "invoking :use-10 restart!"
            :use-10 invoke-restart
        ]
    }
}

[
    [ 1 / 0 ] with-restarts: (assoc: {
        { :use-10; [ 10 ] }
    })
] with-handlers: {
    {
        InternalError; \condition [
            print: "got an InternalError:: " ~ condition >string
            print: "invoking :use-15 restart, which does not exist!"
            :use-15 invoke-restart
        ]
    }
}
