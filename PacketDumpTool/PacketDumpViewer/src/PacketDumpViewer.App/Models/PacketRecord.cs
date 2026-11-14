using System.IO;
using PacketDumpViewer.App.Services;

namespace PacketDumpViewer.App.Models;

public sealed class PacketRecord
{
    public required string FullPath { get; init; }
    public required string RelativePath { get; init; }
    public required string FileName { get; init; }
    public required string Direction { get; init; }
    public int? Pid { get; init; }
    public int? Seq { get; init; }
    public int? MsgId { get; init; }
    public long DeclaredLength { get; init; }
    public long FileSize { get; init; }
    public bool IsUnknown { get; init; }

    public string SeqDisplay => Seq?.ToString() ?? "unknown";
    public string MsgIdDisplay => MsgId?.ToString() ?? "unknown";
    public string LengthDisplay => $"{DeclaredLength:N0} bytes";
    public string FileSizeDisplay => $"{FileSize:N0} bytes";
    public string StatusDisplay => IsUnknown ? "unknown filename" : "payload";
    public string DirectionDisplay => DirectionText.Describe(Direction);
    public string DirectionShortDisplay => DirectionText.ShortDescribe(Direction);
    public string FlowDisplay => DirectionText.Flow(Direction);
    public string MsgRoleDisplay => ProtocolNameService.Shared.DescribeMsgId(MsgId);

    public static PacketRecord Unknown(string filePath, string rootPath)
    {
        var fileInfo = new FileInfo(filePath);
        return new PacketRecord
        {
            FullPath = fileInfo.FullName,
            RelativePath = Path.GetRelativePath(rootPath, fileInfo.FullName),
            FileName = fileInfo.Name,
            Direction = "unknown",
            DeclaredLength = fileInfo.Exists ? fileInfo.Length : 0,
            FileSize = fileInfo.Exists ? fileInfo.Length : 0,
            IsUnknown = true
        };
    }
}

public static class DirectionText
{
    public static string Flow(string direction)
    {
        if (Contains(direction, "recv") ||
            Contains(direction, "inner_dispatch") ||
            Contains(direction, "special28346_after") ||
            Contains(direction, "login_cache_decode"))
        {
            return "S→C 响应/通知";
        }

        if (Contains(direction, "serialize") ||
            Contains(direction, "pack_") ||
            Contains(direction, "safe28346_builder") ||
            Contains(direction, "topkart_source_object"))
        {
            return "C→S 请求/发送";
        }

        return "方向未知";
    }

    public static string Describe(string direction)
    {
        var zh = direction switch
        {
            "recv_raw_packet" => "接收原始包",
            "recv_plain_full" => "接收解密完整明文",
            "recv_28346_plain_full" => "接收28346内层完整明文",
            "recv_plain_body" => "接收解密明文Body",
            "recv_unpack_failed_raw_packet" => "解包失败原始包",
            "recv_unpack_failed_out_full" => "解包失败输出完整明文",
            "special28346_before_full" => "28346解密前完整包",
            "special28346_after_full" => "28346解密后完整明文",
            "special28346_after_body" => "28346解密后Body",
            "inner_dispatch_body" => "内部派发Body",
            "inner_dispatch_out_full" => "内部派发输出完整包",
            "inner_dispatch_out_body" => "内部派发输出Body",
            "pack_before_body" => "打包前Body",
            "pack_before_extra" => "打包前Extra",
            "pack_after_packet" => "打包后网络包",
            "serialize_body_plain" => "序列化明文Body",
            "serialize_body_output_preview" => "序列化Body预览",
            "serialize_src_object" => "序列化源对象",
            "serialize_aux_preview" => "序列化辅助预览",
            "safe28346_builder_payload_full" => "安全28346构造完整载荷",
            "safe28346_builder_src_object_before" => "安全28346构造前源对象",
            "safe28346_builder_src_object_after" => "安全28346构造后源对象",
            "send_topkart_source_object_after" => "Top-Kart发送源对象预览",
            "login_cache_decode_before" => "登录缓存解码前",
            "login_cache_decode_after" => "登录缓存解码后",
            "unknown" => "未知阶段",
            _ when direction.Contains("topkart_source_object", StringComparison.OrdinalIgnoreCase) => "Top-Kart源对象预览",
            _ when direction.StartsWith("special28346_before_inner_body_at", StringComparison.OrdinalIgnoreCase) => "28346内层Body解密前",
            _ when direction.StartsWith("special28346_after_inner_body_at", StringComparison.OrdinalIgnoreCase) => "28346内层Body解密后",
            _ when direction.StartsWith("send_safe28346_builder_inner_body_at", StringComparison.OrdinalIgnoreCase) => "28346构造内层Body",
            _ when direction.StartsWith("send_serialize_body_28346_inner_body_at", StringComparison.OrdinalIgnoreCase) => "28346序列化内层Body",
            _ => "未命名阶段"
        };

        return $"{direction} / {zh}";
    }

    public static string ShortDescribe(string direction)
    {
        if (string.IsNullOrWhiteSpace(direction))
        {
            return "unknown";
        }

        if (direction.Length <= 32)
        {
            return direction;
        }

        return $"{direction[..29]}...";
    }

    private static bool Contains(string value, string part)
    {
        return value.Contains(part, StringComparison.OrdinalIgnoreCase);
    }
}
