using System.IO;
using System.Reflection;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class NativeModuleExtractor
{
    public const string KeyProbeModuleName = "key_probe.dll";

    public NativeModuleExtractor()
        : this(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "PacketDumpViewer",
            "native"))
    {
    }

    public NativeModuleExtractor(string nativeWorkingDirectory)
    {
        NativeWorkingDirectory = nativeWorkingDirectory;
    }

    public string NativeWorkingDirectory { get; }

    public NativeModuleStatus PrepareModule(string moduleName)
    {
        Directory.CreateDirectory(NativeWorkingDirectory);

        foreach (var candidatePath in EnumerateCandidatePaths(moduleName))
        {
            if (File.Exists(candidatePath))
            {
                if (string.Equals(moduleName, KeyProbeModuleName, StringComparison.OrdinalIgnoreCase))
                {
                    var stagedPath = TryStageNativeModule(candidatePath, moduleName, out var stagingDetail);
                    if (!string.IsNullOrWhiteSpace(stagedPath) && File.Exists(stagedPath))
                    {
                        return new NativeModuleStatus(
                            moduleName,
                            stagedPath,
                            true,
                            stagingDetail);
                    }

                    return new NativeModuleStatus(
                        moduleName,
                        candidatePath,
                        true,
                        $"native module ready without staging: {candidatePath}; staging={stagingDetail}");
                }

                return new NativeModuleStatus(
                    moduleName,
                    candidatePath,
                    true,
                    $"native module ready: {candidatePath}");
            }
        }

        foreach (var stagedCandidate in EnumerateExistingStagedModules(moduleName))
        {
            return new NativeModuleStatus(
                moduleName,
                stagedCandidate,
                true,
                $"existing staged native module ready: {stagedCandidate}");
        }

        var extractedPath = TryExtractEmbeddedModule(moduleName);
        if (!string.IsNullOrWhiteSpace(extractedPath) && File.Exists(extractedPath))
        {
            return new NativeModuleStatus(
                moduleName,
                extractedPath,
                true,
                $"embedded native module extracted: {extractedPath}");
        }

        return new NativeModuleStatus(
            moduleName,
            null,
            false,
            $"missing {moduleName}; prepared native directory: {NativeWorkingDirectory}");
    }

    private IEnumerable<string> EnumerateCandidatePaths(string moduleName)
    {
        var baseDirectory = AppContext.BaseDirectory;

        yield return Path.Combine(baseDirectory, "native_src", moduleName);
        yield return Path.Combine(baseDirectory, "runtimes", "win-x64", "native", moduleName);

        if (!string.Equals(moduleName, KeyProbeModuleName, StringComparison.OrdinalIgnoreCase))
        {
            yield return Path.Combine(baseDirectory, "native", moduleName);
            yield return Path.Combine(baseDirectory, moduleName);
        }

        yield return Path.GetFullPath(Path.Combine(
            baseDirectory,
            "..",
            "..",
            "..",
            "..",
            "..",
            "native",
            Path.GetFileNameWithoutExtension(moduleName),
            "x64",
            "Release",
            moduleName));
    }

    private IEnumerable<string> EnumerateExistingStagedModules(string moduleName)
    {
        var stem = Path.GetFileNameWithoutExtension(moduleName);
        var extension = Path.GetExtension(moduleName);
        var pattern = $"{stem}_*{extension}";
        if (!Directory.Exists(NativeWorkingDirectory))
        {
            yield break;
        }

        foreach (var path in Directory.EnumerateFiles(NativeWorkingDirectory, pattern)
                     .OrderByDescending(File.GetLastWriteTimeUtc))
        {
            yield return path;
        }
    }

    private string? TryStageNativeModule(string sourcePath, string moduleName, out string detail)
    {
        try
        {
            var sourceInfo = new FileInfo(sourcePath);
            if (!sourceInfo.Exists || sourceInfo.Length <= 0)
            {
                detail = $"staging skipped: source not ready: {sourcePath}";
                return null;
            }

            var stem = Path.GetFileNameWithoutExtension(moduleName);
            var extension = Path.GetExtension(moduleName);
            var stagedName = $"{stem}_{sourceInfo.LastWriteTimeUtc.Ticks:x16}_{sourceInfo.Length:x}{extension}";
            var stagedPath = Path.Combine(NativeWorkingDirectory, stagedName);

            if (!File.Exists(stagedPath) || new FileInfo(stagedPath).Length != sourceInfo.Length)
            {
                File.Copy(sourcePath, stagedPath, overwrite: true);
            }

            detail = $"native module staged: {stagedPath}; source={sourcePath}";
            return stagedPath;
        }
        catch (Exception ex)
        {
            detail = $"staging failed: {ex.GetType().Name}: {ex.Message}";
            return null;
        }
    }

    private string? TryExtractEmbeddedModule(string moduleName)
    {
        var assembly = Assembly.GetExecutingAssembly();
        var resourceName = assembly.GetManifestResourceNames()
            .FirstOrDefault(name => name.EndsWith("." + moduleName, StringComparison.OrdinalIgnoreCase));

        if (string.IsNullOrWhiteSpace(resourceName))
        {
            return null;
        }

        var targetPath = Path.Combine(NativeWorkingDirectory, moduleName);
        using var resourceStream = assembly.GetManifestResourceStream(resourceName);
        if (resourceStream is null)
        {
            return null;
        }

        using var fileStream = File.Create(targetPath);
        resourceStream.CopyTo(fileStream);
        return targetPath;
    }
}
