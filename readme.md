# tpathexp

A Tcl/C extension for precompiled path expressions and HTTP route dispatch.
It runs inside a Tcl process, including an embedded interpreter in an AWS Lambda
custom runtime. No listening socket, HTTP server, AWS SDK, or JSON library is
required by the package.

The native matcher adapts a small plain C path-to-regexp compiler. It retains its
POSIX regular expression engine and path syntax, replaces trie extraction with
Tcl dictionaries, and records capture indexes for nested custom patterns. A C
route table supplies ordered method dispatch and is shared by the public
C API and the Tcl commands. The package has no TclOO classes or Tcl implementation
files; the package index loads the native extension directly.

## Build and install

Requirements: a POSIX platform with `regex.h`, a C11 compiler, CMake 3.16 or later,
and Tcl 8.6 or 9.x development files. Shared builds use Tcl stubs; the test suite
uses the bundled `tcltest` package.

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
targets, with `CMAKE`, `BUILD_DIR`, `PREFIX`, and `BUILD_SHARED_LIBS` overrides.

`BUILD_SHARED_LIBS=ON` is the default and builds only the loadable module.
Installation creates `lib/tpathexp1.2.0/` containing the shared library and package
index, plus public headers in `include/tpathexp/`. `CMAKE_INSTALL_LIBDIR` can change
`lib`. Add the package directory or its parent to Tcl's `auto_path` or `TCLLIBPATH`
if it is outside the normal search path.

For a static archive, use a separate build directory:

```sh
cmake -S . -B build-static -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-static
ctest --test-dir build-static --output-on-failure
cmake --install build-static --prefix /your/install/prefix
```

This installs only `lib/libtpathexp.a`, the headers, and a package index that loads
a statically registered package. It does not build or install a shared module.
A host interpreter must register the static package before `package require` can
load it; a regular `tclsh` cannot load the archive. Use fresh install prefixes when
switching modes, since CMake does not remove files from earlier installations.

## Compile once, match many times

```tcl
package require tpathexp 1.2.0

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
    dict create statusCode 200 body "User [dict get $params id]\n" \
        headers [dict create content-type {text/plain; charset=utf-8}]
}

set router [tpathexp::router]
$router add GET {/users/:id([0-9]+)} get_user
$router add {POST PUT} /users/:id {my_handler configured_argument} -strict true

set response [$router dispatch [dict create method GET path /users/42]]
$router destroy
```

`$router add methods expression handler ?options?` compiles immediately
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
Handlers construct ordinary dictionaries. The Lambda example defines its own
`::example::response` helper; response construction is not a package command.
Dispatch generates text 404/405 responses, includes `Allow` on 405, and empties
all HEAD response bodies. Application errors propagate as Tcl errors, retaining
their error codes.

Destroying the router releases its matchers. Build the route table during
initialization and retain it across requests; dispatch does not recompile routes.

## Middleware

Router and route hooks run in the native C dispatch command (including `lambda`):

```tcl
proc enterA {context request} {
    dict set request authenticated true
    return $request
}
proc leaveA {context request response} {
    dict set response headers X-Processed true
    return $response
}
$router add_middleware -enter_proc enterA -leave_proc leaveA
$router add GET /users/:id get_user -enter_proc check_user -leave_proc log_user
```

`$router add_middleware ?-enter_proc prefix? ?-leave_proc prefix?` appends a layer
and returns the router; at least one nonempty hook is required. `$router add`
also accepts `-enter_proc` and `-leave_proc`, alongside the compile options.
Each hook is a Tcl command prefix; an empty list disables it. Use fully qualified
names for namespace commands. Enter hooks receive `context request` and return
an updated request dictionary. Leave hooks receive `context request response`
and return an updated response dictionary. Handlers still receive
`request context params`.

Matching happens first, so enter hooks see `pathParameters`; changing the request
does not select a different route. Router enter hooks run in registration order,
then the route enter hook and handler. The route leave hook runs first, followed
by router leave hooks in reverse order. Router hooks also wrap automatic 404/405
responses; route hooks only run for a matched method and path. Middleware added
after routes applies to those routes too.

An enter hook or handler can stop processing with
`return -code error -options [dict create statusCode 403 body Forbidden]`.
This skips later enter hooks and the handler, then runs leaves for reached
layers, including the layer that stopped processing. Ordinary Tcl errors skip
leaves and preserve their return options. A failing leave stops remaining leaves.
Response validation and HEAD body suppression happen after all successful leaves.

Each dispatch retains its selected handler and hooks. Callbacks may recursively
dispatch or destroy the router safely; new registrations affect subsequent
dispatches. Deleting the router or its interpreter releases the stored prefixes.

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

Include `stage/lib/tpathexp1.2.0` (or the configured library directory) in the
artifact. Configure the runtime's actual Tcl package path to that location.
The loadable module uses Tcl stubs and needs no direct `libtcl` dependency.

For a runtime embedding Tcl in C or C++, build with `BUILD_SHARED_LIBS=OFF` and
link `libtpathexp.a` plus Tcl and any system libraries required by that Tcl build.
Register the package once with
`Tcl_StaticPackage(NULL, "Tpathexp", Tpathexp_Init, NULL)`, then initialize each
interpreter with `Tcl_Init`. With the installed package index on `auto_path`,
`package require tpathexp 1.2.0` loads the registered package, including in child
interpreters. Alternatively, call `Tpathexp_Init(interp)` directly in each
initialized interpreter. No Tcl implementation file needs to be sourced.
See [tests/embed.c](tests/embed.c) for a working embedding example. Cross builds
can set `BUILD_TESTING=OFF` and supply target Tcl include/library paths explicitly.

## Native C route table

Include `router.h` to use routing without a Tcl interpreter. A route definition
contains an expression, a method bit mask, compilation flags, and caller-owned
`user_data`. [examples/router.c](examples/router.c) is a complete executable.

```c
tpathexp_route_t routes[] = {
    {"/users/:id([0-9]+)", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, NULL},
    {"/files/:path(.*)", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, NULL}
};
tpathexp_router_t *router = NULL;
char error[512] = {0};
if (tpathexp_router_create(&router, routes, 2, error, sizeof(error)) != 0) {
    /* Report error and stop initialization. */
    return 1;
}
tpathexp_match_t match = {0};
int rc = tpathexp_router_match(router, "GET", "/users/42", &match, error, sizeof(error));
if (rc == 0 && match.status_code == 200) {
    const char *id = tpathexp_match_param(&match, "id");
    /* Use id and match.user_data to invoke application logic here. */
    (void)id;
}
tpathexp_match_destroy(&match);
tpathexp_router_destroy(router);
```

`create`, `add`, and `match` return `0` on success and `-1` on an operational or
input error, writing the error buffer when supplied. `create` leaves its output
NULL on failure. `add` compiles and appends one route; a failed addition preserves
existing routes. `TPATHEXP_DEFAULT_FLAGS` enables full-path matching with an
optional trailing slash; combine it with the `PATH_REGEXP_*` flags as needed.

The match status is `200`, `404`, or `405`. GET also accepts HEAD. Explicit HEAD
routes take precedence when registered first. `allowed_methods` contains the
union of methods across matching paths for a 405; use `tpathexp_allow_header` to
format it into a caller-supplied buffer. Headers use canonical method order:
GET, HEAD, POST, PUT, DELETE, OPTIONS, PATCH, TRACE, CONNECT. A successful match
returns the first matching route and its named parameters. An unknown request
method yields 405 for a known path, or 404 otherwise.

Route expressions and patterns are owned by the table. `user_data` is borrowed
and never freed by the library. Match results own their parameter values and
borrow parameter names, the expression, and `user_data`. Call
`tpathexp_match_destroy` before reusing a result and before destroying the table;
it is also safe on a zeroed result or after a failed match. Optional unmatched
parameters are absent, while participating empty captures are empty strings.
An immutable table can be shared by concurrent callers using separate results;
route additions and destruction require exclusive access.

The C API selects routes and extracts parameters. Application code invokes its
handlers and constructs responses. It has no dependency on Tcl response formats.
A C-only program can link the static archive without Tcl: the linker pulls only
the native route-table and pattern objects. For example, after a static install:

```sh
cc -I/your/install/prefix/include/tpathexp examples/router.c \
  /your/install/prefix/lib/libtpathexp.a -o router-example
./router-example
```

## Tests and limits

Tests cover route ordering, custom/nested captures, character classes, malformed
expressions, optional captures, Unicode values, NUL rejection, command deletion,
interpreter teardown, 404/405/HEAD handling, Lambda payload shapes, and warm
invocation reuse. Both build modes run the same Tcl tests. Static mode runs them
through an embedding executable, including child-interpreter package loading.
CTest also exercises the C route-table API, the C example, and the installed
artifact layout so the selected mode cannot silently install both library types.

Route lookup scans the table in registration order. Patterns are application
configuration; do not compile untrusted per-request expressions. Request size
limits, authentication, application policy, and execution timeouts belong to the
host runtime. The package performs routing only. Local tests do not constitute
a deployment to AWS.
