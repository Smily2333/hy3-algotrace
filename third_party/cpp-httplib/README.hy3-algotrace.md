# cpp-httplib vendoring record

- Upstream: https://github.com/yhirose/cpp-httplib
- Version: `v0.51.0` (`d66d9a9`)
- Retrieved: 2026-08-24 from the upstream GitHub Contents API
- License: MIT; preserved verbatim in `LICENSE`
- `httplib.h` SHA-256: `dfbaccb76432ed6d56ddd9983fd9d262b61ba6ba0958f6b00db35c802607bd35`
- `LICENSE` SHA-256: `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b`

The header is vendored so canonical Windows and Ubuntu builds do not fetch
dependencies from the network. The interactive demo uses only its plain HTTP
server on the loopback interface; TokenHub HTTPS remains implemented by the
existing `ProductionHttpTransport`.
