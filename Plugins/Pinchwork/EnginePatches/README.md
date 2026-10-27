# Engine Patches

Optional engine-source modifications that pair with Pinchwork. You only need
these if the specific feature applies to your project **and** you build Unreal
Engine from source.

> [!IMPORTANT]
> **License:** the `.patch` files here modify **Unreal Engine source code** and
> are provided under the terms of the **Unreal Engine EULA — *not* the MIT
> license** that covers the rest of Pinchwork. You must be an Unreal Engine
> licensee with engine source access to use them.

---

## `0001-arm64-lightmass.patch` — native arm64 UnrealLightmass (Apple Silicon)

**When you need it:** you want **baked / static lighting** in your packaged build
**and** you build the engine from source on an Apple Silicon (M-series) Mac.

**Why:** stock Unreal ships UnrealLightmass as x86_64 only. On Apple Silicon it
runs under Rosetta, where Embree's x86_64 slice fails `rtcNewDevice` (no AVX
under emulation) and static-lighting bakes fail silently. Epic never ported
Lightmass to arm64. This patch is a native arm64 port — 3 arch-guarded files
(`PLATFORM_CPU_ARM_FAMILY`); x86_64 codegen is unchanged.

- `LMMath.h` — route the hand-written SSE2 SIMD through Embree's vendored
  `sse2neon` shim instead of `<emmintrin.h>`.
- `LMStats.h` — read the arm64 virtual counter (`CNTVCT_EL0`) instead of x86 `__rdtsc`.
- `UnrealLightmass.Build.cs` — add the `sse2neon` include dir on the Mac/Linux path.

**Verified** on M1 Max: `file` reports arm64, Embree initializes with no
`rtcGetDeviceError`, and a MEDIUM bake produces real lightmap data.

### Apply

```bash
cd /path/to/your/UnrealEngine

# dry run — confirms it applies cleanly to your engine tree
git apply --check /path/to/Pinchwork/EnginePatches/0001-arm64-lightmass.patch

# apply as working-tree changes ...
git apply /path/to/Pinchwork/EnginePatches/0001-arm64-lightmass.patch
# ... or apply as a commit (keeps authorship/message)
git am < /path/to/Pinchwork/EnginePatches/0001-arm64-lightmass.patch
```

Then rebuild UnrealLightmass (or the editor). Source of truth: the
`ibrews/UnrealEngine` fork, commit `4f59b2fea5c6`.
