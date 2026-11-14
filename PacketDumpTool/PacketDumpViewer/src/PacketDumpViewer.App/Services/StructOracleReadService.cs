using System.IO;
using System.Text.Json;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class StructOracleReadService
{
    public StructOracleLoadResult? TryLoad(PacketRecord? packet)
    {
        if (packet is null || string.IsNullOrWhiteSpace(packet.FullPath))
        {
            return null;
        }

        var oraclePath = ResolveOraclePath(packet.FullPath);
        if (oraclePath is null || !File.Exists(oraclePath))
        {
            return null;
        }

        try
        {
            using var document = JsonDocument.Parse(File.ReadAllBytes(oraclePath));
            var rootElement = document.RootElement;
            var source = GetString(rootElement, "source");
            var sourceKind = GetString(rootElement, "sourceKind");
            var note = GetString(rootElement, "note");
            var detail = GetString(rootElement, "detail");
            var capturedLength = GetInt64(rootElement, "capturedLength");
            var declaredLength = GetInt64(rootElement, "declaredLength");

            var root = new ParsedFieldNode
            {
                Offset = 0,
                Size = ToNodeSize(capturedLength),
                Type = "struct_oracle",
                Name = "RuntimeStructOracle",
                Value = $"{source} / {sourceKind}",
                Status = "RuntimeSnapshot",
                Note = $"dump-time generated; {oraclePath}"
            };

            root.AddChild(new ParsedFieldNode
            {
                Offset = 0,
                Size = ToNodeSize(declaredLength),
                Type = "meta",
                Name = "OracleMeta",
                Value = $"pid={GetInt64(rootElement, "pid")}; tid={GetInt64(rootElement, "tid")}; seq={GetInt64(rootElement, "seq")}; msgId={GetInt64(rootElement, "msgId")}; direction={GetString(rootElement, "direction")}",
                Status = "RuntimeSnapshot",
                Note = $"payloadFile={GetString(rootElement, "payloadFile")}; detail={detail}"
            });

            if (rootElement.TryGetProperty("fields", out var fieldsElement) &&
                fieldsElement.ValueKind == JsonValueKind.Array)
            {
                var fieldsRoot = root.AddChild(new ParsedFieldNode
                {
                    Offset = 0,
                    Size = ToNodeSize(capturedLength),
                    Type = "fields",
                    Name = "RuntimeFields",
                    Value = $"count={fieldsElement.GetArrayLength()}",
                    Status = "RuntimeSnapshot",
                    Note = note
                });

                foreach (var fieldElement in fieldsElement.EnumerateArray())
                {
                    fieldsRoot.AddChild(new ParsedFieldNode
                    {
                        Offset = GetInt64(fieldElement, "offset"),
                        Size = ToNodeSize(GetInt64(fieldElement, "size")),
                        Type = GetString(fieldElement, "type"),
                        Name = GetString(fieldElement, "name"),
                        Value = GetString(fieldElement, "value"),
                        Status = GetString(fieldElement, "status"),
                        Note = GetString(fieldElement, "note")
                    });
                }
            }

            var summary = $"Runtime struct_oracle loaded: {Path.GetFileName(oraclePath)}";
            return new StructOracleLoadResult(root, summary, oraclePath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            var errorRoot = new ParsedFieldNode
            {
                Offset = 0,
                Size = 0,
                Type = "struct_oracle",
                Name = "RuntimeStructOracle",
                Value = Path.GetFileName(oraclePath),
                Status = "OracleLoadError",
                Note = ex.Message
            };
            return new StructOracleLoadResult(errorRoot, $"Runtime struct_oracle load failed: {ex.Message}", oraclePath);
        }
    }

    private static string? ResolveOraclePath(string payloadPath)
    {
        var payloadFile = new FileInfo(payloadPath);
        var payloadDir = payloadFile.Directory;
        var dumpDir = payloadDir?.Parent;
        if (payloadDir is null || dumpDir is null ||
            !payloadDir.Name.Equals("payloads", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var baseName = Path.GetFileNameWithoutExtension(payloadFile.Name);
        return Path.Combine(dumpDir.FullName, "struct_oracle", baseName + ".json");
    }

    private static string GetString(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value))
        {
            return "";
        }

        return value.ValueKind switch
        {
            JsonValueKind.String => value.GetString() ?? "",
            JsonValueKind.Number => value.GetRawText(),
            JsonValueKind.True => "true",
            JsonValueKind.False => "false",
            _ => ""
        };
    }

    private static long GetInt64(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value) ||
            value.ValueKind != JsonValueKind.Number)
        {
            return 0;
        }

        return value.TryGetInt64(out var result) ? result : 0;
    }

    private static int ToNodeSize(long value)
    {
        if (value <= 0)
        {
            return 0;
        }

        return value > int.MaxValue ? int.MaxValue : (int)value;
    }
}

public sealed record StructOracleLoadResult(ParsedFieldNode RootNode, string Summary, string FullPath);
