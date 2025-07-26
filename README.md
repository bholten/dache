# Dache
## or: How I Learned to Stop Worrying and Love the .tar.gz

So, you want fast builds. Excellent. So do we.

But listen, we’re not miracle workers. Dache is a content-addressed build cache, not your overly forgiving friend who lets you write to `/tmp`, `/dev/null`, and `~/.mystery-stuff` without consequence.

If you want clean caching, here’s what you need to do:

### Give us real inputs

Not “whatever this script happens to touch.” We mean:

- Actual files
- Actual directories
- Actual content

Not:

- Your shell history.
- The weather in Manila.
- The contents of `/proc`.

### Emit real outputs

That means: put your outputs in files.

If your tool “builds a container and pushes it to a daemon,” that’s adorable, but it’s not an output. We don’t cache vibes.

Make it output a `your-docker-container.tar.gz`. Or a `.wasm`. Or a fat `.jar`. Something we can hash and reuse.

### Stop touching your dotfiles

If your build process reads from `~/.npm`, `~/.cargo`, or `~/.config/doom-emacs`, just... don’t.

We’re not judging you. Okay, we are.

But seriously -- if your build depends on that kind of ambient entropy, we’re not going to cache it. We’re going to pretend it doesn’t exist. Like your failed resolution to “get into Rust this year.”

### Declare your dependencies

We don’t have time to spy on your build. You know what you used -- just tell us:

```sh
    --inputs src/ include/
    --outputs bin/ docs/
```

You can even declare your naughty dot files, if you must: `--inputs src/ ~/.cargo/config` or whatever. We'll still judge you.

If you don’t? No problem. We’ll run the build anyway. But you’ll miss out on sweet, sweet cache hits. Your call.

The alternative to just being explicit? We'd have to subscribe to syscalls to figure out what files you were reading; or sandbox and chroot the entire build -- and then you get... whatever Bazel and Nix are.

### It’s okay to rebuild sometimes

You know what’s faster than fighting the cache?

```sh
make clean && make
```

We’re not trying to be perfect. We’re trying to be worth it. Dache is a convenience, not a doctrine.

### In Summary

If you give Dache:

- Clear inputs,
- Clear outputs,
- Deterministic commands,

It gives you:

- Less waiting,
- Fewer re-compiles,
- And a snappy little wink every time it cache-hits.

If you give Dache nothing? That’s okay too. We’ll still hang out. We just won’t remember anything you did.
