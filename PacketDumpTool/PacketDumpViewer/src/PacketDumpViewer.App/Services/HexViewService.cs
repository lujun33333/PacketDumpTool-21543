using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class HexViewService
{
    private const int BytesPerRow = 16;

    public IReadOnlyList<HexRow> BuildRows(byte[] bytes, long baseOffset)
    {
        var rows = new List<HexRow>();
        for (var rowStart = 0; rowStart < bytes.Length; rowStart += BytesPerRow)
        {
            var rowBytes = bytes.Skip(rowStart).Take(BytesPerRow).ToArray();
            var hex = string.Join(" ", rowBytes.Select(value => value.ToString("X2")));
            var ascii = new string(rowBytes.Select(ToAscii).ToArray());

            rows.Add(new HexRow
            {
                Offset = $"0x{baseOffset + rowStart:X8}",
                HexBytes = hex.PadRight(BytesPerRow * 3 - 1),
                Ascii = ascii
            });
        }

        return rows;
    }

    private static char ToAscii(byte value)
    {
        return value is >= 0x20 and <= 0x7E ? (char)value : '.';
    }
}
