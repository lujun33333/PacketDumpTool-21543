using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace PacketDumpViewer.App.Models;

public sealed class ParsedFieldNode : INotifyPropertyChanged
{
    private bool _isExpanded;

    public long Offset { get; init; }
    public int Size { get; init; }
    public required string Type { get; init; }
    public required string Name { get; init; }
    public required string Value { get; set; }
    public required string Status { get; set; }
    public string Note { get; set; } = "";
    public bool IsExpanded
    {
        get => _isExpanded;
        set
        {
            if (_isExpanded == value)
            {
                return;
            }

            _isExpanded = value;
            OnPropertyChanged();
        }
    }

    public ObservableCollection<ParsedFieldNode> Children { get; } = [];

    public bool HasChildren => Children.Count > 0;

    public string OffsetHex => $"0x{Offset:X8}";

    public string SizeText => Size > 0 ? $" size={Size}" : "";

    public string TypeText => $" {Type} ";

    public string NameText => Name;

    public string ValueText => string.IsNullOrWhiteSpace(Value) ? "" : $" = {Value}";

    public string NoteText => string.IsNullOrWhiteSpace(Note) ? "" : $" ; {Note}";

    public string DisplayText
    {
        get
        {
            var sizeText = Size > 0 ? $" size={Size}" : "";
            var valueText = string.IsNullOrWhiteSpace(Value) ? "" : $" = {Value}";
            var noteText = string.IsNullOrWhiteSpace(Note) ? "" : $" ; {Note}";
            return $"0x{Offset:X8}{sizeText} {Type} {Name}{valueText} [{Status}]{noteText}";
        }
    }

    public ParsedFieldNode AddChild(ParsedFieldNode child)
    {
        Children.Add(child);
        return child;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
