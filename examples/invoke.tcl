source [file join [file dirname [info script]] lambda.tcl]

set event [dict create version 2.0 rawPath /hello/Ada \
    requestContext [dict create http [dict create method GET]]]
set response [lambda_handler $event [dict create requestId local-example]]
puts "[dict get $response statusCode]: [dict get $response body]"
