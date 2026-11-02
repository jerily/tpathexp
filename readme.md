# tpathexp

A Tcl/C extension for precompiled path expressions and HTTP route dispatch.
It runs inside a Tcl process, including an embedded interpreter in an AWS Lambda
custom runtime. No listening socket, HTTP server, AWS SDK, or JSON library is
required by the package.

The native matcher adapts a small plain C path-to-regexp compiler. It retains its
POSIX regular expression engine and path syntax, replaces trie extraction with
Tcl dictionaries, and records capture indexes for nested custom patterns. A Tcl
route table supplies ordered method dispatch without native HTTP request or
response structures.

## Build and install

Requirements: a POSIX platform with `regex.h`, a C11 compiler, CMake 3.16 or later,
and Tcl 8.6 or 9.x development files (including its stub library). The selected
Tcl interpreter must include TclOO; tests also use its bundled `tcltest` package.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
TCLLIBPATH="$PWD/build" tclsh examples/invoke.tcl
cmake --install build --prefix /your/install/prefix
```

Use the shell for the Tcl version selected during configuration in place of
`tclsh` above. A binary built for Tcl 8 cannot be loaded into Tcl 9 or vice versa.
The generated package index checks the ABI family. For example, select Tcl 8.6
on a system with multiple versions using:

```sh
cmake -S . -B build86 \
  -DTCL_INCLUDE_PATH=/usr/include/tcl8.6 \
  -DTCL_LIBRARY=/usr/lib/x86_64-linux-gnu/libtcl8.6.so \
  -DTCL_STUB_LIBRARY=/usr/lib/x86_64-linux-gnu/libtclstub8.6.a \
  -DTCL_TCLSH=/usr/bin/tclsh8.6
```

Use paths appropriate to your system. `CMAKE_PREFIX_PATH` can select a custom Tcl
installation. The Makefile also supplies `all`, `test`, `install`, and `clean`
targets, with `CMAKE`, `BUILD_DIR`, and `PREFIX` overrides.

Installation creates `lib/tpathexp1.0.0/` containing the shared library, package
index, and Tcl module, plus `lib/libtpathexp.a` and `include/tpathexp/` for embedded
builds. `CMAKE_INSTALL_LIBDIR` can change `lib`. Add the package directory or its
parent to Tcl's `auto_path` or `TCLLIBPATH` if it is outside the normal search path.

## Compile once, match many times

```tcl
package require tpathexp 1.0.0

set matcher [tpathexp::compile {/users/:id([0-9]+)}]
if {[$matcher match /users/42 params]} {
    puts [dict get $params id]
}
$matcher destroy
```

`tpathexp::compile ?options? ?--? expression` returns a command owning the compiled
pattern. Options take explicit Tcl boolean values:

| Option | Default | Meaning |
| --- | --- | --- |
| `-strict` | `false` | Do not append an optional trailing slash. |
| `-start` | `true` | Anchor at the start of the supplied path. |
| `-end` | `true` | Anchor at the end of the supplied path. |
| `-nocase` | `false` | Use POSIX case-insensitive matching. |

Matcher commands:

| Command | Result |
| --- | --- |
| `$matcher match path ?paramsVar?` | Boolean; optionally write named captures to a caller variable. |
| `$matcher keys` | Parameter names in declaration order. |
| `$matcher pattern` | Generated POSIX extended regular expression. |
| `$matcher destroy` | Release the matcher and delete its command. |

A nonmatch writes an empty dictionary to `paramsVar`, clearing old values.
Unmatched optional parameters are omitted; participating empty captures are
included as empty strings. Local variables and array elements are supported.
Renaming a matcher works, and deleting its command or interpreter frees it.
Matchers and routers belong to one interpreter and must be used on its thread.

## Path syntax

| Expression | Behavior |
| --- | --- |
| `/users/:id` | Capture one nonempty segment as `id`. |
| `/users/:id([0-9]+)` | Constrain the parameter with a POSIX ERE. |
| `/:kind((cat\|dog))/:id` | Nested capture groups preserve later parameter indexes. |
| `/files/:path(.*)` | Capture zero or more characters, including slashes. |
| `/users/:id?` | Make the parameter optional; the preceding slash remains required. |
| `/:digit([0-9])+` | Repeat the parameter group; capture its final repetition. |
| `/literal/\:name` | Escape the colon to match it literally. |

Parameter names contain ASCII letters, digits, and underscores; duplicate names
are rejected. `?`, `+`, and `*` immediately after a parameter apply to its regex
group. They do not implicitly repeat slash-separated segments. Brace groups and
bare wildcard expressions are unsupported. Use `/:path(.*)` for a catch-all.
Regex syntax is POSIX ERE, so use `[0-9]` rather than PCRE's `\d`.

Literal characters outside parameters are escaped as regex literals. By default
the generated suffix is `/?$`; an expression already ending in `/` still requires
that slash and can accept a second one. `-strict true` disables the added slash.
With `-end false`, matching is a raw regex prefix: `/api` can match `/apix`.
Set boundaries explicitly when using it for routing.

Paths are used exactly as supplied: no URL decoding, query removal, slash
normalization, or dot-segment normalization takes place. Pass a path rather than
a full URL. NUL characters are rejected. POSIX character classes and case folding
use the process locale.

## Route table

```tcl
proc get_user {request context params} {
    tpathexp::response 200 "User [dict get $params id]\n" \
        [dict create content-type {text/plain; charset=utf-8}]
}

set router [tpathexp::router]
$router add GET {/users/:id([0-9]+)} get_user
$router add {POST PUT} /users/:id {my_handler configured_argument} -strict true

set response [$router dispatch [dict create method GET path /users/42]]
$router destroy
```

`$router add methods expression handler ?compileOptions?` compiles immediately
and returns the router. `methods` is a nonempty list of uppercase HTTP methods,
or `ANY` for GET, HEAD, POST, PUT, DELETE, OPTIONS, PATCH, TRACE, and CONNECT.
Register specific routes before broader routes: the first matching path and
method wins. GET also accepts HEAD. An explicit HEAD route can be registered
before a GET route to override it. OPTIONS has no automatic special handling.

`$router match method path` returns a dictionary with `statusCode`:

- `200`: includes `handler`, `expression`, and `pathParameters`.
- `405`: includes `allow`, combining methods from all matching paths.
- `404`: no matching path.

`$router dispatch request ?context?` requires `method` and `path` in the request
dictionary. It invokes the handler command prefix at global scope with three
additional arguments: `request context params`. Use fully qualified handler
names for namespace commands. The request includes freshly matched
`pathParameters`; other fields and the caller's context are preserved.

Handlers return a response dictionary containing `statusCode` (100–599) and
`body`. Optional `headers` defaults to an empty dictionary, and
`isBase64Encoded` defaults to `false`. Other fields pass through unchanged.
`tpathexp::response status body ?headers?` constructs this dictionary.
Dispatch generates text 404/405 responses, includes `Allow` on 405, and empties
all HEAD response bodies. Application errors propagate as Tcl errors, retaining
their error codes. Use Tcl command prefixes to compose authentication, logging,
or other application middleware around handlers.

Destroying the router releases its matchers. Build the route table during
initialization and retain it across requests; dispatch does not recompile routes.

## AWS Lambda

Source [examples/lambda.tcl](examples/lambda.tcl) once during runtime startup.
It defines `lambda_handler event ?context?`, accepting a Tcl dictionary and
returning a response dictionary. [examples/invoke.tcl](examples/invoke.tcl)
demonstrates a local invocation.

`$router lambda event ?context?` accepts these decoded HTTP event shapes:

| Payload | Method field | Path field |
| --- | --- | --- |
| API Gateway payload 1.0 / REST proxy | `httpMethod` | `path` |
| API Gateway payload 2.0 / Function URL | `requestContext.http.method` | `rawPath` |

Missing required fields and unsupported versions raise errors. The original
event remains available as `dict get $request event`. Body, encoding flag,
headers, cookies, query parameters, and request context are copied when present.
Bodies remain encoded when `isBase64Encoded` is true; handlers decide whether to
decode them. Matched parameters follow the selected event path's encoding. Stage
or API mapping prefixes are not stripped by the adapter.

The payload mappings follow the AWS documentation for
[API Gateway HTTP integrations](https://docs.aws.amazon.com/apigateway/latest/developerguide/http-api-develop-integrations-lambda.html),
[REST proxy integrations](https://docs.aws.amazon.com/apigateway/latest/developerguide/set-up-lambda-proxy-integrations.html),
and [Function URLs](https://docs.aws.amazon.com/lambda/latest/dg/urls-invocation.html).

Your custom runtime owns the Lambda Runtime API loop, JSON decoding/encoding,
and error reporting. Preserve JSON types when serializing a response:
`statusCode` is a number, `headers` an object, `body` a string, and
`isBase64Encoded` a boolean. Optional `cookies` is an array for payload 2.0;
payload 1.0 uses `multiValueHeaders` for repeated headers. The extension returns
Tcl values and does not perform JSON serialization.

Build in a Linux environment matching the runtime's architecture, libc, and Tcl
ABI. Package the installed module directory alongside Tcl in the deployment
artifact or layer, and put that directory on `auto_path`. For example:

```sh
cmake --install build --prefix "$PWD/stage"
```

Include `stage/lib/tpathexp1.0.0` (or the configured library directory) in the
artifact. Configure the runtime's actual Tcl package path to that location.
The loadable module uses Tcl stubs and needs no direct `libtcl` dependency.

For a runtime embedding Tcl in C or C++, link `libtpathexp.a` and Tcl, call
`Tcl_Init(interp)` then `Tpathexp_Init(interp)`, and source the installed
`tpathexp.tcl` once. The initializer provides `tpathexp::native`; the Tcl file
provides the complete `tpathexp` package. See [tests/embed.c](tests/embed.c) for a
working embedding example. Cross builds can set `BUILD_TESTING=OFF` and supply
the target Tcl include/library paths explicitly.

## Tests and limits

Tests cover route ordering, custom/nested captures, character classes, malformed
expressions, optional captures, Unicode values, NUL rejection, command deletion,
interpreter teardown, 404/405/HEAD handling, Lambda payload shapes, and warm
invocation reuse. CTest also executes a statically linked embedding program.

Route lookup scans the table in registration order. Patterns are application
configuration; do not compile untrusted per-request expressions. Request size
limits, authentication, application policy, and execution timeouts belong to the
host runtime. The package performs routing only. Local tests do not constitute
a deployment to AWS.
