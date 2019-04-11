---
id: static
title: Enabling Static Checks
---

This doc will cover how to enable and disable the **static checks** that
`srb` reports. Specifically, we'll look at how to toggle these checks...

- ...within an entire **file**.
- ...for a particular **method**.
- ...for a single **argument** of a method.
- ...at a specific **call site**.

Before we get into the mechanics of how to toggle Sorbet's static checks, let's
slap on a quick warning label:

> **Warning**: Think carefully before disabling static checks!

## File-level granularity: strictness levels

When run at the command line, `srb` roughly works like this:

1.  Read, parse, and analyze every Ruby file in a project.
2.  Generate a list of errors within the project.
3.  Display all errors to the user.

However, in step (3), most kinds of errors are *silenced* by default, instead of
being reported. To opt into *more* checks, we use `# typed:` **sigils**[^sigil]. A
`# typed:` sigil is a comment placed at the top of a Ruby file, indicating to
`srb` which errors to report and which to silence. These are the available
sigils, each defining a **strictness level**:

[^sigil]: Google defines sigil as, "an inscribed or painted symbol considered to
have magical power," and we like to think of types as pretty magical 🙂

| Most errors silenced |                 |                   | All errors reported |
| ---                  | ---             | ---               | ---                 |
| `# typed: false`     | `# typed: true` | `# typed: strict` | `# typed: strong`   |

Each strictness level reports all errors at lower levels, plus new errors:

- At `# typed: false`, only errors related to things like syntax and constant
  resolution are reported. Fixing these errors is the baseline for adopting
  Sorbet in a new codebase, and provides value even before adding type
  annotations. `# typed: false` is the **default** for files without sigils.

- At `# typed: true`, things that would normally be called "type errors" are
  reported. This includes calling a non-existent method, calling a method with
  mismatched argument counts, using variables inconsistently with their types,
  etc.

- At `# typed: strict`, Sorbet no longer implicitly marks things as being
  [dynamically typed](untyped.md). At this level all methods, constants, and
  instance variables must have [explicitly annotated types](strict.md).
  This is analogous to TypeScript's `noImplicitAny` flag.

- At `# typed: strong`, Sorbet no longer allows [`T.untyped`](untyped.md) as the
  intermediate result of any method call. This effectively means that Sorbet
  knew the type statically for 100% of calls within a file. It's currently
  impossible to write code with no errors at this strictness level.

To recap: adding one of these comments to the top of a Ruby file controls which
errors `srb` reports or silences in that file. The strictness level only
affects which errors are reported.

> **Note**: Type annotations from `# typed: false` files are *still parsed and
> used* by Sorbet if that method is called in other files. Specifically, adding
> a signature might introduce new type errors when called from a `# typed: true`
> file.


## Method-level granularity: `sig`

After enabling `# typed: true` in some files, we can opt individual methods into
even more checks by adding signatures (or `sig`s) to them. For example `srb`
reports no errors in this file:

```ruby
# typed: true

def log_env(env, key)
  puts "LOG: #{key} => #{env[key]}"
end

log_env({timeout_len: 2000}, 'timeout_len')
```

It would be nice to be warned about our call to `log_env`, because we passed a
Hash with `Symbol` keys but tried to ask about a `String` key. To opt into this
check, we can add a signature to `log_env`:

```ruby
# typed: true

# (1) add this to get access to sig method
extend T::Sig

# (2) add a signature
sig {params(env: T::Hash[Symbol, Integer], key: Symbol).void}
def log_env(env, key)
  puts "LOG: #{key} => #{env[key]}"
end

log_env({timeout_len: 2000}, 'timeout_len') # => `String("timeout_len")` doesn't match `Symbol`
```

In this example, we add a line like `sig {...}` above the `def log_env` line.
This is a Sorbet method signature---it declares the parameter and return types
of a method. By adding the `sig` to `log_env`, we opted this method into
additional checks. Now `srb` reports this:

```
`String("timeout_len")` doesn't match `Symbol` for argument `key`
```

## Argument-level granularity: `T.untyped`

In our previous example, the `sig` we added was a bit too restrictive for the
`env` parameter:

```ruby
T::Hash[Symbol, Integer]
                ^^^^^^^
```

For example, it's possible that we don't care about what's stored in the `env`,
only that we access things in the `env` with `Symbol` keys. Right now though,
an `env` of `{user: 'jez'}` is a type error. In this case, we may want to *opt
out* of some static checks on this speficic argument, without opting out the
method entirely. In this case, we can use [`T.untyped`](untyped.md):

```ruby
# typed: true
extend T::Sig

sig {params(env: T::Hash[Symbol, T.untyped], key: Symbol).void}
def log_env(env, key)
  puts "LOG: #{key} => #{env[key]}"
end

log_env({timeout_len: 2000, user: 'jez'}, :user)  # ok
```

`T.untyped` is a type that effectively makes a region of code act like it was
written in a dynamically typed language with no static checks. By using
`T.untyped` in specific arguments within a `sig`, we can silence most (but
not all) errors relating to that argument.

> **Warning**: Be careful about opting out of static checks with `T.untyped`!
> Usually, we can rewrite our code to avoid silencing errors. For example, we
> could have refactored this code to use [Shape types](shapes.md), `Struct`s, or
> best of all: [Typed Structs](tstruct.md).


## Call-site granularity: `T.unsafe`

Using sigils and method signatures are the primary ways we opt *into* static
checks, and using `T.untyped` we can opt a specific argument *out of* static
checks.

One last way we can opt out of static checks is with `T.unsafe`. `T.unsafe` is a
method (not a type) that returns its input unchanged and marks the result as
`T.untyped`. Like how `T.untyped` in a signature lets us opt an argument out of
type checks, `T.unsafe` lets us opt out a local variable or method call.

This is frequently necessary when using Ruby's various "metaprogramming"
features:

```ruby
class A
  define_method(:foo) { puts 'In A#foo' }
end

a = A.new
a.foo             # => Method `foo` does not exist on `A`
T.unsafe(a).foo   # ok
```

The call to `T.unsafe` marks `a` as `T.untyped`, which causes Sorbet to silence
the error about the method `foo` as missing. Note: sometimes what looks like
a local variable is actually a method call on `self` in Ruby:

```ruby
define_singleton_method(:foo) { puts 'A.foo'; true }

if foo                 # => Method `foo` does not exist on `T.class_of(A)`
  puts 'succeeded'
end
```

In this case the tendency is to want to wrap the call to `foo` in `T.unsafe`
(for example: `T.unsafe(foo)`) but in fact what we need to wrap is the method's
**receiver** (the thing the method is being called on). When there is no
explicit receiver, it's `self` in Ruby:

```ruby
define_singleton_method(:foo) { puts 'A.foo'; true }

if T.unsafe(self).foo  # ok
  puts 'succeeded'
end
```

The call to `T.unsafe(self)` evaluates to `self`, but forces Sorbet to think it
has type `T.untyped`, which permits calling any method. Using `T.unsafe` we can
limit untyped code to a specific call site, and make explicit where we're
relying on dynamic behaviors.

## What's next?

- [Enabling Runtime Checks](runtime.md)

  The runtime component of Sorbet supports the static component. Learn how it
  works and how to best take advantage of it.

- [Gradual Type Checking](gradual.md)

  If you haven't already read this, learn what makes Sorbet different from other
  static type systems.