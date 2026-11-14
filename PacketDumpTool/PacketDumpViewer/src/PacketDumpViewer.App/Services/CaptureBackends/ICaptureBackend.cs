using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services.CaptureBackends;

public interface ICaptureBackend : IDisposable
{
    string EngineName { get; }
    CaptureBackendState CurrentState { get; }
    CaptureBackendState Start(CaptureRequest request);
    CaptureBackendState Stop();
    CaptureBackendState Clear();
}
