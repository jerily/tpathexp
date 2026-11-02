package require tpathexp 1.1.0

namespace eval ::example {
    # Initialize once when the runtime loads this file; reuse on warm invocations.
    variable router [::tpathexp::router]

    proc response {status body {headers {}}} {
        dict create statusCode $status headers $headers body $body isBase64Encoded false
    }

    proc hello {request context params} {
        ::example::response 200 "Hello, [dict get $params name]!\n" \
            [dict create content-type {text/plain; charset=utf-8}]
    }

    proc health {request context params} {
        ::example::response 200 "ok\n" [dict create content-type text/plain]
    }

    $router add GET /health ::example::health -strict true
    $router add GET /hello/:name ::example::hello
}

proc lambda_handler {event {context {}}} {
    # The runtime supplies a decoded event dict and serializes the response dict.
    $::example::router lambda $event $context
}
