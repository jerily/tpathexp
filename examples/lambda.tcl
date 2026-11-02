package require tpathexp 1.0.0

namespace eval ::example {
    # Initialize once when the runtime loads this file; reuse on warm invocations.
    variable router [::tpathexp::router]

    proc hello {request context params} {
        ::tpathexp::response 200 "Hello, [dict get $params name]!\n" \
            [dict create content-type {text/plain; charset=utf-8}]
    }

    proc health {request context params} {
        ::tpathexp::response 200 "ok\n" [dict create content-type text/plain]
    }

    $router add GET /health ::example::health -strict true
    $router add GET /hello/:name ::example::hello
}

proc lambda_handler {event {context {}}} {
    # The runtime supplies a decoded event dict and serializes the response dict.
    $::example::router lambda $event $context
}
