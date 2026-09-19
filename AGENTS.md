# Repository Guidelines

## C++ Code Style

- Apply these rules to first-party C++ code. Do not modify code under `external/` to enforce them.
- Prefix every private member function name with a single underscore, for example `_CreateBuffer`.
- Declare every function with a non-`void` return type using trailing return type syntax: `auto Function(...) -> ReturnType`.
- Functions returning `void` are exempt from the trailing return type rule.
