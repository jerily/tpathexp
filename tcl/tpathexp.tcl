package require Tcl 8.6-9 9.0-10
package require tpathexp::native 1.0.0

namespace eval ::tpathexp {}

proc ::tpathexp::response {status body {headers {}}} {
    dict create statusCode $status headers $headers body $body isBase64Encoded false
}

oo::class create ::tpathexp::Router {
    variable routes

    constructor {} {
        set routes {}
    }

    destructor {
        foreach route $routes {
            set matcher [dict get $route matcher]
            if {[namespace which -command $matcher] ne {}} { $matcher destroy }
        }
    }

    method add {methods expression handler args} {
        set supported {GET HEAD POST PUT DELETE OPTIONS PATCH TRACE CONNECT}
        if {![llength $methods]} { error "at least one method is required" }
        if {![llength $handler]} { error "handler must be a nonempty command prefix" }
        if {$methods eq "ANY"} { set methods $supported }
        foreach method $methods {
            if {$method ni $supported} { error "unsupported route method: $method" }
        }
        if {"GET" in $methods && "HEAD" ni $methods} { lappend methods HEAD }
        set matcher [::tpathexp::compile {*}$args -- $expression]
        lappend routes [dict create methods $methods expression $expression matcher $matcher handler $handler]
        return [self]
    }

    method match {method path} {
        set allowed {}
        foreach route $routes {
            if {![[dict get $route matcher] match $path params]} { continue }
            foreach candidate [dict get $route methods] {
                if {$candidate ni $allowed} { lappend allowed $candidate }
            }
            if {$method in [dict get $route methods]} {
                return [dict create statusCode 200 handler [dict get $route handler] \
                    pathParameters $params expression [dict get $route expression]]
            }
        }
        if {[llength $allowed]} {
            return [dict create statusCode 405 allow [join $allowed {, }]]
        }
        return [dict create statusCode 404]
    }

    method dispatch {request {context {}}} {
        set method [dict get $request method]
        set route [my match $method [dict get $request path]]
        switch [dict get $route statusCode] {
            200 {
                set params [dict get $route pathParameters]
                dict set request pathParameters $params
                set response [uplevel #0 [list {*}[dict get $route handler] $request $context $params]]
                if {![dict exists $response statusCode] || ![dict exists $response body]} {
                    error "handler response must contain statusCode and body"
                }
                set status [dict get $response statusCode]
                if {![string is integer -strict $status] || $status < 100 || $status > 599} {
                    error "handler statusCode must be an integer from 100 to 599"
                }
                if {![dict exists $response headers]} { dict set response headers {} }
                dict size [dict get $response headers]
                if {![dict exists $response isBase64Encoded]} { dict set response isBase64Encoded false }
                if {![string is boolean -strict [dict get $response isBase64Encoded]]} {
                    error "handler isBase64Encoded must be a boolean"
                }
            }
            405 {
                set response [::tpathexp::response 405 "method not allowed\n" \
                    [dict create content-type {text/plain; charset=utf-8} Allow [dict get $route allow]]]
            }
            404 {
                set response [::tpathexp::response 404 "not found\n" \
                    [dict create content-type {text/plain; charset=utf-8}]]
            }
        }
        if {$method eq "HEAD"} { dict set response body {} }
        return $response
    }

    method lambda {event {context {}}} {
        if {[dict exists $event version] && [dict get $event version] eq "2.0"} {
            set method [dict get $event requestContext http method]
            set path [dict get $event rawPath]
        } elseif {![dict exists $event version] || [dict get $event version] eq "1.0"} {
            set method [dict get $event httpMethod]
            set path [dict get $event path]
        } else {
            error "unsupported Lambda HTTP payload version"
        }
        set request [dict create method $method path $path event $event]
        foreach field {headers body isBase64Encoded queryStringParameters multiValueHeaders multiValueQueryStringParameters cookies rawQueryString requestContext} {
            if {[dict exists $event $field]} { dict set request $field [dict get $event $field] }
        }
        return [my dispatch $request $context]
    }
}

proc ::tpathexp::router {} {
    ::tpathexp::Router new
}

package provide tpathexp 1.0.0
