using System.IO;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class MultiDumpComparisonService
{
    private readonly Msg100FullParser _parser;

    public MultiDumpComparisonService(Msg100FullParser parser)
    {
        _parser = parser;
    }

    public ParserStatus CompareSiblingDumps(PacketRecord currentPacket, IReadOnlyList<ParsedFieldNode> currentNodes)
    {
        var samples = FindSiblingSamples(currentPacket).ToList();
        if (samples.Count == 0)
        {
            return new ParserStatus
            {
                Status = "parsed",
                Note = "未找到同级 msg100 full 样本；当前字段标记为 SingleSample。",
                StructureText = "No sibling samples.",
                RootNodes = currentNodes
            };
        }

        var sampleMaps = new List<Dictionary<string, ParsedFieldNode>>();
        foreach (var sample in samples)
        {
            try
            {
                var bytes = File.ReadAllBytes(sample);
                if (bytes.Length != Msg100FullParser.FullLengthCurrent3805)
                {
                    continue;
                }

                var result = _parser.ParseBytes(bytes, Path.GetFileName(sample));
                sampleMaps.Add(Msg100FullParser.Flatten(result.RootNodes).ToDictionary(FieldKey, node => node));
            }
            catch
            {
                // Bad/deleted samples should not break the UI.
            }
        }

        var sampleCount = sampleMaps.Count;
        foreach (var node in Msg100FullParser.Flatten(currentNodes))
        {
            if (node.Children.Count > 0)
            {
                if (node.Status != "Unaligned")
                {
                    node.Status = sampleCount > 1 ? "Verified" : "SingleSample";
                    node.Note = AppendNote(node.Note, $"samples={sampleCount}");
                }
                continue;
            }

            if (node.Status == "Reserved")
            {
                node.Note = AppendNote(node.Note, $"samples={sampleCount}");
                continue;
            }

            var key = FieldKey(node);
            var values = new List<string>();
            var missing = 0;
            foreach (var map in sampleMaps)
            {
                if (map.TryGetValue(key, out var sampleNode))
                {
                    values.Add(sampleNode.Value);
                }
                else
                {
                    missing++;
                }
            }

            if (missing > 0)
            {
                node.Status = "Unaligned";
                node.Note = AppendNote(node.Note, $"missing in {missing}/{sampleCount} samples");
                continue;
            }

            var distinctCount = values.Distinct(StringComparer.Ordinal).Count();
            if (node.Status == "UnknownName")
            {
                node.Note = AppendNote(node.Note, $"layout stable; samples={sampleCount}; distinctValues={distinctCount}");
                continue;
            }

            node.Status = distinctCount <= 1 ? "Verified" : "Dynamic";
            node.Note = AppendNote(node.Note, $"samples={sampleCount}; distinctValues={distinctCount}");
        }

        return new ParserStatus
        {
            Status = "parsed + compared",
            Note = $"已对照同级 dump：{sampleCount} 个 msg100 full 样本；无法确认字段保留 UnknownName/Unaligned。",
            StructureText = "Msg100 structure tree compared.",
            RootNodes = currentNodes
        };
    }

    private static IEnumerable<string> FindSiblingSamples(PacketRecord currentPacket)
    {
        var dumpRoot = FindDumpRoot(currentPacket.FullPath);
        var parent = dumpRoot is null ? null : Directory.GetParent(dumpRoot)?.FullName;
        if (parent is null || !Directory.Exists(parent))
        {
            yield break;
        }

        foreach (var dumpDir in Directory.EnumerateDirectories(parent, "packet_dumps_*"))
        {
            var payloadDir = Path.Combine(dumpDir, "payloads");
            if (!Directory.Exists(payloadDir))
            {
                continue;
            }

            foreach (var file in Directory.EnumerateFiles(payloadDir, "*_msg100_len317329.bin"))
            {
                var name = Path.GetFileName(file);
                if (name.Contains("recv_plain_full", StringComparison.OrdinalIgnoreCase) ||
                    name.Contains("special28346_after_full", StringComparison.OrdinalIgnoreCase) ||
                    name.Contains("login_cache_decode_before", StringComparison.OrdinalIgnoreCase) ||
                    name.Contains("login_cache_decode_after", StringComparison.OrdinalIgnoreCase))
                {
                    yield return file;
                }
            }
        }
    }

    private static string? FindDumpRoot(string filePath)
    {
        var dir = Directory.GetParent(filePath);
        while (dir is not null)
        {
            if (dir.Name.StartsWith("packet_dumps_", StringComparison.OrdinalIgnoreCase))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        return null;
    }

    private static string FieldKey(ParsedFieldNode node)
    {
        return $"{node.Offset:X8}:{node.Size}:{node.Type}:{node.Name}";
    }

    private static string AppendNote(string original, string addition)
    {
        if (string.IsNullOrWhiteSpace(original))
        {
            return addition;
        }
        if (original.Contains(addition, StringComparison.OrdinalIgnoreCase))
        {
            return original;
        }
        return $"{original}; {addition}";
    }
}
