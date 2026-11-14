using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class OnlineQqSpeedStreamDecoder
{
    private const int TcpHeaderLength = 18;
    private const int AppHeaderLength = 14;
    private const int MaxFrameLength = 2 * 1024 * 1024;
    private const int MaxBufferedBytes = 4 * 1024 * 1024;

    public IReadOnlyList<CapturedPacketPreview> DecodeSnapshot(IReadOnlyList<CapturedPacketPreview> packets, string sessionKeyHex)
    {
        var streams = new Dictionary<string, StreamState>(StringComparer.OrdinalIgnoreCase);
        var decoded = new List<CapturedPacketPreview>(packets.Count);
        var sessionKey = TryParseHexBytes(sessionKeyHex);

        foreach (var packet in packets.OrderBy(item => item.PacketIndex))
        {
            if (packet.ConnectionKey.Equals("internal-hook", StringComparison.OrdinalIgnoreCase) &&
                packet.Decode?.PlainPayloadBytes is { Length: > 0 })
            {
                decoded.Add(packet);
                continue;
            }

            var packetDecode = DecodePacket(streams, packet, sessionKey);
            decoded.Add(packet with { Decode = packetDecode });
        }

        return decoded;
    }

    private static PacketDecodePreview DecodePacket(IDictionary<string, StreamState> streams, CapturedPacketPreview packet, byte[]? sessionKey)
    {
        if (packet.PayloadBytes.Length == 0)
        {
            return PacketDecodePreview.Waiting("empty tcp payload");
        }

        var key = BuildStreamKey(packet);
        if (!streams.TryGetValue(key, out var state))
        {
            state = new StreamState();
            streams[key] = state;
        }

        state.Buffer.AddRange(packet.PayloadBytes);
        TrimOversizeBuffer(state);

        PacketDecodePreview? latestFrame = null;
        var completedFrames = 0;
        var droppedBytes = 0;

        while (true)
        {
            var sync = SyncToNextFrame(state);
            droppedBytes += sync.DroppedBytes;
            if (!sync.HasCandidate)
            {
                return latestFrame is null
                    ? PacketDecodePreview.Waiting(BuildWaitingDetail("syncing qqspeed tcp frame", droppedBytes, state.Buffer.Count))
                    : WithFrameCount(latestFrame, completedFrames, droppedBytes);
            }

            if (state.Buffer.Count < sync.TotalLength)
            {
                return latestFrame is null
                    ? PacketDecodePreview.Waiting(BuildWaitingDetail($"stream partial {state.Buffer.Count}/{sync.TotalLength}", droppedBytes, state.Buffer.Count))
                    : WithFrameCount(latestFrame, completedFrames, droppedBytes);
            }

            var frame = state.Buffer.GetRange(0, sync.TotalLength).ToArray();
            state.Buffer.RemoveRange(0, sync.TotalLength);
            completedFrames++;
            latestFrame = ParseFrame(frame, sessionKey);
        }
    }

    private static string BuildStreamKey(CapturedPacketPreview packet)
    {
        var connection = string.IsNullOrWhiteSpace(packet.ConnectionKey)
            ? packet.Endpoint
            : packet.ConnectionKey;
        return $"{connection}|{packet.Direction}";
    }

    private static FrameSyncResult SyncToNextFrame(StreamState state)
    {
        if (state.Buffer.Count < 4)
        {
            return FrameSyncResult.NoCandidate(0);
        }

        var maxStart = Math.Max(0, state.Buffer.Count - 4);
        for (var offset = 0; offset <= maxStart; offset++)
        {
            if (!TryReadTotalLength(state.Buffer, offset, out var totalLength))
            {
                continue;
            }

            if (offset > 0)
            {
                state.Buffer.RemoveRange(0, offset);
            }

            return new FrameSyncResult(true, totalLength, offset);
        }

        var keep = Math.Min(TcpHeaderLength - 1, state.Buffer.Count);
        var drop = state.Buffer.Count - keep;
        if (drop > 0)
        {
            state.Buffer.RemoveRange(0, drop);
        }

        return FrameSyncResult.NoCandidate(drop);
    }

    private static bool TryReadTotalLength(List<byte> buffer, int offset, out int totalLength)
    {
        totalLength = 0;
        if (offset + 4 > buffer.Count)
        {
            return false;
        }

        var value =
            (buffer[offset] << 24) |
            (buffer[offset + 1] << 16) |
            (buffer[offset + 2] << 8) |
            buffer[offset + 3];

        if (value < TcpHeaderLength || value > MaxFrameLength)
        {
            return false;
        }

        if (offset + TcpHeaderLength <= buffer.Count)
        {
            var flag = (buffer[offset + 0x10] << 8) | buffer[offset + 0x11];
            // QQSpeed outer flag stores the encrypt/plain mode in bits 8..9.
            // Other bits are expected to stay zero for the 21273-era samples; this
            // prevents locking onto random bytes inside HTTP/TLS/TUN payloads.
            if ((flag & 0xFCFF) != 0)
            {
                return false;
            }
        }

        totalLength = value;
        return true;
    }

    private static PacketDecodePreview ParseFrame(byte[] frame, byte[]? sessionKey)
    {
        if (frame.Length < TcpHeaderLength)
        {
            return new PacketDecodePreview("-", "-", "-", "-", "invalid", $"QQSpeed frame too short: {frame.Length}");
        }

        var totalLength = (int)ReadU32Be(frame, 0);
        var flag = ReadU16Be(frame, 0x10);
        var mode = (flag >> 8) & 3;
        if (totalLength != frame.Length)
        {
            return new PacketDecodePreview("-", "-", "-", "-", "unaligned", $"QQSpeed frame length mismatch header={totalLength} actual={frame.Length}");
        }

        if (mode != 0)
        {
            return ParseEncryptedFrame(frame, mode, sessionKey);
        }

        if (frame.Length < TcpHeaderLength + AppHeaderLength)
        {
            return new PacketDecodePreview("-", "-", "-", "-", "plain/short", $"QQSpeed plain frame len={frame.Length}; missing app header");
        }

        var appOffset = TcpHeaderLength;
        var msgId = ReadU16Be(frame, appOffset);
        var msgType = ReadU16Be(frame, appOffset + 2);
        var msgSeq = ReadU32Be(frame, appOffset + 4);
        var bodyLen = frame.Length - TcpHeaderLength - AppHeaderLength;
        if (!IsLikelyAppHeader(msgId, msgType, bodyLen))
        {
            return new PacketDecodePreview(
                "-",
                "-",
                "-",
                "-",
                "plain/unrecognized",
                $"QQSpeed frame len={frame.Length}; app header not plausible cmd={msgId}, msgtype={msgType}, seq={msgSeq}, body={bodyLen}");
        }

        var plainPayload = frame[TcpHeaderLength..];
        if (msgId == 28346)
        {
            return new PacketDecodePreview(
                msgId.ToString(),
                msgType.ToString(),
                msgSeq.ToString(),
                bodyLen.ToString(),
                "plain/28346-wrapper",
                $"QQSpeed plain frame len={frame.Length}; 28346 outer wrapper; body={bodyLen}",
                null,
                OuterMsgId: 28346,
                EffectiveMsgId: 28346,
                MsgIdDisplay: "28346",
                WrapperKind: "28346-wrapper");
        }

        return new PacketDecodePreview(
            msgId.ToString(),
            msgType.ToString(),
            msgSeq.ToString(),
            bodyLen.ToString(),
            "plain",
            $"QQSpeed plain frame len={frame.Length}; body={bodyLen}",
            plainPayload,
            EffectiveMsgId: msgId,
            MsgIdDisplay: msgId.ToString());
    }

    private static PacketDecodePreview ParseEncryptedFrame(byte[] frame, int mode, byte[]? sessionKey)
    {
        if (sessionKey is null || sessionKey.Length == 0)
        {
            return new PacketDecodePreview(
                "-",
                "-",
                "-",
                "-",
                "encrypted/no-key",
                $"QQSpeed frame len={frame.Length}; mode={mode}; encrypted payload captured, waiting Session Key Hex/KeyProbe");
        }

        if (sessionKey.Length != 16)
        {
            return new PacketDecodePreview(
                "-",
                "-",
                "-",
                "-",
                "encrypted/key-invalid",
                $"QQSpeed frame len={frame.Length}; mode={mode}; Session Key length={sessionKey.Length}, expected 16 bytes");
        }

        var cipher = frame.AsSpan(TcpHeaderLength);
        try
        {
            var plainPayload = QqTeaCipher.Decrypt(cipher, sessionKey, strict: true);
            if (plainPayload.Length < AppHeaderLength)
            {
                return new PacketDecodePreview("-", "-", "-", "-", "decrypted/short", $"QQSpeed decrypted mode={mode}; plain len={plainPayload.Length}");
            }

            var msgId = ReadU16Be(plainPayload, 0);
            var msgType = ReadU16Be(plainPayload, 2);
            var msgSeq = ReadU32Be(plainPayload, 4);
            var bodyLen = plainPayload.Length - AppHeaderLength;
            if (!IsLikelyAppHeader(msgId, msgType, bodyLen))
            {
                return new PacketDecodePreview(
                    "-",
                    "-",
                    "-",
                    "-",
                    "decrypted/unrecognized",
                    $"QQSpeed decrypted mode={mode}; app header not plausible cmd={msgId}, msgtype={msgType}, seq={msgSeq}, body={bodyLen}",
                    plainPayload);
            }

            if (msgId == 28346)
            {
                return new PacketDecodePreview(
                    msgId.ToString(),
                    msgType.ToString(),
                    msgSeq.ToString(),
                    bodyLen.ToString(),
                    $"decrypted/mode{mode}/28346-wrapper",
                    $"QQSpeed TEA decrypted frame len={frame.Length}; 28346 outer wrapper; plain={plainPayload.Length}; body={bodyLen}",
                    null,
                    OuterMsgId: 28346,
                    EffectiveMsgId: 28346,
                    MsgIdDisplay: "28346",
                    WrapperKind: "28346-wrapper");
            }

            return new PacketDecodePreview(
                msgId.ToString(),
                msgType.ToString(),
                msgSeq.ToString(),
                bodyLen.ToString(),
                $"decrypted/mode{mode}",
                $"QQSpeed TEA decrypted frame len={frame.Length}; plain={plainPayload.Length}; body={bodyLen}",
                plainPayload,
                EffectiveMsgId: msgId,
                MsgIdDisplay: msgId.ToString());
        }
        catch (Exception ex)
        {
            return new PacketDecodePreview(
                "-",
                "-",
                "-",
                "-",
                "encrypted/decrypt-failed",
                $"QQSpeed decrypt failed for mode={mode}: {ex.GetType().Name}");
        }
    }

    private static bool IsLikelyAppHeader(int msgId, int msgType, int bodyLen)
    {
        return msgId > 0 &&
               bodyLen >= 0 &&
               msgType is >= 0 and <= 0x400;
    }

    private static PacketDecodePreview WithFrameCount(PacketDecodePreview decode, int completedFrames, int droppedBytes)
    {
        var suffix = completedFrames > 1 ? $"; completedFrames={completedFrames}" : "";
        suffix += droppedBytes > 0 ? $"; syncDropped={droppedBytes}" : "";
        return decode with { Detail = decode.Detail + suffix };
    }

    private static string BuildWaitingDetail(string prefix, int droppedBytes, int bufferedBytes)
    {
        var detail = $"{prefix}; buffered={bufferedBytes}";
        return droppedBytes > 0 ? $"{detail}; syncDropped={droppedBytes}" : detail;
    }

    private static void TrimOversizeBuffer(StreamState state)
    {
        if (state.Buffer.Count <= MaxBufferedBytes)
        {
            return;
        }

        var drop = state.Buffer.Count - TcpHeaderLength;
        state.Buffer.RemoveRange(0, drop);
    }

    private static int ReadU16Be(byte[] data, int offset) => (data[offset] << 8) | data[offset + 1];

    private static int ReadU16Be(byte[] data, int offset, int _ = 0) => (data[offset] << 8) | data[offset + 1];

    private static int ReadU16Be(ReadOnlySpan<byte> data, int offset) => (data[offset] << 8) | data[offset + 1];

    private static uint ReadU32Be(byte[] data, int offset) =>
        ((uint)data[offset] << 24) |
        ((uint)data[offset + 1] << 16) |
        ((uint)data[offset + 2] << 8) |
        data[offset + 3];

    private static uint ReadU32Be(ReadOnlySpan<byte> data, int offset) =>
        ((uint)data[offset] << 24) |
        ((uint)data[offset + 1] << 16) |
        ((uint)data[offset + 2] << 8) |
        data[offset + 3];

    private static byte[]? TryParseHexBytes(string hexText)
    {
        if (string.IsNullOrWhiteSpace(hexText))
        {
            return null;
        }

        var hex = new string(hexText.Where(Uri.IsHexDigit).ToArray());
        if (hex.Length == 0 || hex.Length % 2 != 0)
        {
            return null;
        }

        var bytes = new byte[hex.Length / 2];
        for (var i = 0; i < bytes.Length; i++)
        {
            if (!byte.TryParse(hex.AsSpan(i * 2, 2), System.Globalization.NumberStyles.HexNumber, System.Globalization.CultureInfo.InvariantCulture, out bytes[i]))
            {
                return null;
            }
        }

        return bytes;
    }

    private sealed class StreamState
    {
        public List<byte> Buffer { get; } = [];
    }

    private readonly record struct FrameSyncResult(bool HasCandidate, int TotalLength, int DroppedBytes)
    {
        public static FrameSyncResult NoCandidate(int droppedBytes) => new(false, 0, droppedBytes);
    }
}
