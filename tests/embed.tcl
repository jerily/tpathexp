package require -exact tpathexp 1.1.0
set router [tpathexp::router]
$router add GET /users/:id unusedHandler
set result [$router match GET /users/42]
if {[dict get $result pathParameters id] ne {42}} { error "wrong parameter" }
$router destroy
interp create child
child eval [list set auto_path $auto_path]
child eval {
    package require -exact tpathexp 1.1.0
    set r [tpathexp::router]
    $r add GET / childHandler
    if {[dict get [$r match HEAD /] statusCode] != 200} { error "HEAD did not match" }
    $r destroy
}
interp delete child
