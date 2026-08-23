# third_party/nlohmann — vendored dependency

## Dependency

- **Name**: nlohmann/json
- **Version**: v3.12.0
- **License**: MIT
- **Upstream**: https://github.com/nlohmann/json

## Why vendored (single-header)

This project vendors the official single-header `json.hpp` instead of using a
package manager or `FetchContent`:

- **Offline**: no network access required at build time.
- **Fixed version**: pinned to the tagged release `v3.12.0` (no floating
  `main`/`master`, no unpinned `FetchContent`).
- **Reproducible build**: the exact bytes are recorded by SHA-256 below, so
  every build uses identical source.

## Files

| File | Purpose |
| --- | --- |
| `json.hpp` | Official single-header amalgamated source (v3.12.0) |
| `LICENSE.MIT` | Upstream MIT license text |

## Official sources (only these are trusted)

1. `json.hpp`
   - URL: https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
   - SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`
2. `LICENSE.MIT`
   - URL: https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT

## Verification

```
sha256sum third_party/nlohmann/json.hpp
# expected: aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
```

If the SHA-256 does not match, the file must NOT be used; remove it and
re-fetch only from the official URLs above.
