# X360Extract

Reusable .NET 8 library for reading and extracting **Xbox 360 disc images**.
It wraps the GDFX (Game Disc Format for Xbox) filesystem, the ISO 9660 layer
underneath it, and the STFS (LIVE / PIRS / CON) container, and exposes a single
high-level entry point: `X360Disc`.

Produced by `X360Extract.Core.csproj` as **`X360ExtractCore.dll`**
(assembly `X360ExtractCore`, namespace `X360Extract`). The standalone
`x360extract` CLI in the parent folder and the Fable II launcher both consume
this library, so this is the one place the disc-reading logic lives.

## Using it

Reference the library, then use `X360Disc`:

```csharp
using X360Extract;

using var disc = X360Disc.Open(@"C:\dumps\fable2.iso");

// Inspect the root.
foreach (var entry in disc.RootEntries)
    Console.WriteLine($"{(entry.IsDirectory ? "dir " : "file")} {entry.Name}  ({entry.Size})");

// Stream default.xex while hashing it.
var xex = disc.DefaultXex;
using var sha = System.Security.Cryptography.IncrementalHash.CreateHash(
    System.Security.Cryptography.HashAlgorithmName.SHA256);
disc.WriteFile(xex, @"C:\out\default.xex", (b, o, n) => sha.AppendData(b, o, n));
Console.WriteLine(Convert.ToHexString(sha.GetHashAndReset()).ToLowerInvariant());

// Extract the rest of the disc to a folder.
var result = disc.Extract(@"C:\out");
Console.WriteLine($"Wrote {result.Files} files, {result.Bytes} bytes in {result.Elapsed}");
```

### Main API

- `X360Disc.Open(path, progress?)` — scan the image for the GDFX header, open
  the filesystem, read the root. Returns an `IDisposable`.
- `X360Disc.RootEntries` / `DefaultXex` — the root directory and the
  root-level `default.xex`.
- `X360Disc.Extract(outDir, options?)` — extract the disc (optionally filtered
  to named root entries via `ExtractionOptions.Files`).
- `X360Disc.ExtractEntries(entries, outDir, options?)` — extract an explicit set
  of entries (e.g. everything except `default.xex`).
- `X360Disc.WriteFile(entry, path, onBytes?)` — stream one file to disk, with an
  optional per-chunk callback (e.g. for hashing).
- `X360Disc.ListTree()` — an indented, human-readable directory tree.

`ExtractionOptions` controls behavior:
- `Files` — only extract these root entries (case-insensitive). Empty = all.
- `UnpackStfs` — expand root-level STFS containers into directories. When
  `false` (default) they are copied as a single raw file, matching retail
  extraction tools.
- `Progress` — a structured `Action<DiscProgress>` callback.

Lower-level types (`GdfxFilesystem`, `IsoImage`, `StfsContainer` and their
entry structs) are also public for advanced use, such as streaming a specific
file or expanding a nested STFS container yourself.

## Packaging

Pack a NuGet package from the repo root:

```
tools\x360extract\build.cmd pack
```

This produces `out\tooling\nuget\Fable2.X360Extract.<version>.nupkg`. Add that
package (or the `.nupkg` file) to any .NET 8+ project to get the library — no
need to copy sources.

## Notes

- Files are written byte-for-byte with `FileMode.Create`: re-running over an
  existing output tree overwrites each file but does not delete files that are
  no longer in the disc tree.
- STFS block-chain math follows the Free60 / Velocity `StfsPackage`
  implementation (algorithm only).
