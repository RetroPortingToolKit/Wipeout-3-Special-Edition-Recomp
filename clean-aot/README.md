# Clean AOT framework

This tree is the independently implemented static-recompilation lane. It is a
standalone build and deliberately does not include or link the quarantined
legacy runner.

Configure, build, and run every available component test from the repository
root:

```text
cmake -S clean-aot -B build-clean-aot -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-aot --config Release
ctest --test-dir build-clean-aot --build-config Release --output-on-failure
```

The generic directories contain platform/compiler mechanisms only. Game IDs,
guest addresses, enhancement choices, media identities, and other title policy
belong in the product layer outside this tree. A release is not accepted until
the source-isolation and release-link-map gates both pass without broad
exceptions.
