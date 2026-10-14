namespace X360Extract;

public sealed class Options
{
    public string IsoPath = "";
    public string OutDir = ".";
    public bool ListOnly;
    public bool Quiet;
    public bool UnpackStfs;              // --unpack-stfs: expand STFS containers into directories
    public List<string> Files = new();   // --files: comma-separated root entry names
}

public static class Program
{
    public static int Main(string[] args)
    {
        var opts = new Options();
        try
        {
            if (!ParseArgs(args, opts)) return 1;
        }
        catch (OptionException ex)
        {
            Console.Error.WriteLine($"x360extract: {ex.Message}");
            Console.Error.WriteLine(Usage());
            return 1;
        }

        try
        {
            return opts.ListOnly ? ListDisc(opts) : Extract(opts);
        }
        catch (OptionException ex)
        {
            Console.Error.WriteLine($"x360extract: {ex.Message}");
            return 1;
        }
        catch (InvalidDataException ex)
        {
            Console.Error.WriteLine($"x360extract: error: {ex.Message}");
            return 2;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"x360extract: error: {ex.Message}");
            return 3;
        }
    }

    private sealed class OptionException : Exception
    {
        public OptionException(string msg) : base(msg) { }
    }

    private static string Usage() =>
"""
Usage:
  x360extract <iso> [options]          Extract game content from an Xbox 360 ISO
  x360extract --list <iso>             List the GDFX file tree without extracting

Options:
  -o, --out <dir>       Output directory (default: current directory)
  --list                List the GDFX directory tree
  --files <a,b,c>       Only extract the named root entries (case-insensitive)
  --unpack-stfs         Expand STFS (PIRS/LIVE/CON) file entries into directories
                        (default: copy them as raw files, matching retail tools)
  --quiet               Suppress progress output
  -h, --help            Show this help

Behavior:
  Reads the GDFX (Game Disc Format for Xbox) filesystem from the ISO and
  extracts the file tree to the output directory.  Files are copied
  byte-for-byte; STFS containers (e.g. nxeart) stay raw unless
  --unpack-stfs is given.
"""
        ;

    private static bool ParseArgs(string[] args, Options o)
    {
        string? iso = null;
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            switch (a)
            {
                case "-h":
                case "--help":
                    Console.WriteLine(Usage());
                    Environment.Exit(0);
                    break;
                case "-o":
                case "--out":
                    if (i + 1 >= args.Length) throw new OptionException("--out requires a directory");
                    o.OutDir = args[++i];
                    break;
                case "--list":
                    o.ListOnly = true;
                    break;
                case "--files":
                    if (i + 1 >= args.Length) throw new OptionException("--files requires a list");
                    o.Files = args[++i].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();
                    break;
                case "--unpack-stfs":
                    o.UnpackStfs = true;
                    break;
                case "--quiet":
                    o.Quiet = true;
                    break;
                default:
                    if (a.StartsWith('-'))
                        throw new OptionException($"unknown option '{a}'");
                    if (iso != null)
                        throw new OptionException("multiple ISO paths given");
                    iso = a;
                    break;
            }
        }
        if (iso == null)
            throw new OptionException("no ISO path given");
        o.IsoPath = iso;
        return true;
    }

    // ------------------------------------------------------------------
    //  Progress formatting (delegates all disk work to the X360Extract library)
    // ------------------------------------------------------------------

    private static void OnProgress(DiscProgress p, bool quiet)
    {
        if (quiet) return;
        switch (p.Phase)
        {
            case DiscProgressPhase.ScanningHeader:
                Console.Write($"\r    scanning for GDFX header: {p.Current / (1024 * 1024)} / {p.Total / (1024 * 1024)} MiB   ");
                break;
            case DiscProgressPhase.EnteringDirectory:
                Console.WriteLine($"  [dir]  {p.Name}/");
                break;
            case DiscProgressPhase.ExpandingStfs:
                Console.WriteLine($"  [stfs] {p.Name}/  ({p.EntryCount} entries)");
                break;
            case DiscProgressPhase.WritingFile:
                Console.Write($"  {p.Name}  ({p.SizeBytes:N0}) ... ");
                break;
            case DiscProgressPhase.FileWritten:
                Console.WriteLine("ok");
                break;
        }
    }

    private static string MiB(long bytes) => $"{bytes / (1024 * 1024):N0}";

    private static int ListDisc(Options o)
    {
        if (!File.Exists(o.IsoPath))
            throw new OptionException($"ISO not found: {o.IsoPath}");

        if (!o.Quiet)
            Console.WriteLine($"Scanning {o.IsoPath} ({MiB(new FileInfo(o.IsoPath).Length)} MiB) ...");

        using var disc = X360Disc.Open(o.IsoPath, p => OnProgress(p, o.Quiet));
        if (!o.Quiet)
        {
            Console.WriteLine();
            Console.WriteLine($"GDFX header at {disc.HeaderOffset:X8}  (base {disc.Gdfx.BaseOffset:X8})");
            Console.WriteLine($"Root sector: {disc.Gdfx.RootSector:X8}  size: {disc.Gdfx.RootSize}");
            Console.WriteLine();
            Console.Write(disc.ListTree());
        }
        return 0;
    }

    private static int Extract(Options o)
    {
        if (!File.Exists(o.IsoPath))
            throw new OptionException($"ISO not found: {o.IsoPath}");

        if (!o.Quiet)
            Console.WriteLine($"Scanning {o.IsoPath} ({MiB(new FileInfo(o.IsoPath).Length)} MiB) ...");

        using var disc = X360Disc.Open(o.IsoPath, p => OnProgress(p, o.Quiet));
        if (!o.Quiet)
            Console.WriteLine(); // terminate the "scanning for GDFX header" line

        var options = new ExtractionOptions
        {
            Files = o.Files,
            UnpackStfs = o.UnpackStfs,
            Progress = p => OnProgress(p, o.Quiet),
        };

        ExtractionResult result = disc.Extract(o.OutDir, options);

        if (!o.Quiet)
            Console.WriteLine($"Done: {result.Files} files, {MiB(result.Bytes)} MiB in {result.Elapsed.TotalSeconds:F1}s -> {Path.GetFullPath(o.OutDir)}");
        return 0;
    }
}
