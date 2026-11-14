using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Web.WebView2.Core;
using PacketDumpViewer.App.ViewModels;

namespace PacketDumpViewer.App;

public partial class MainWindow : Window
{
    private string? _webViewUserDataFolder;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = new MainViewModel();
        Loaded += MainWindow_Loaded;
        Closed += MainWindow_Closed;
        TranslateWebView.NavigationCompleted += TranslateWebView_NavigationCompleted;
    }

    private MainViewModel ViewModel => (MainViewModel)DataContext;

    private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        try
        {
            _webViewUserDataFolder = Path.Combine(
                Path.GetTempPath(),
                $"PacketDumpViewer-WebView2-{Process.GetCurrentProcess().Id}");

            var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: _webViewUserDataFolder);
            await TranslateWebView.EnsureCoreWebView2Async(environment);
            TranslateWebView.CoreWebView2.Settings.AreDevToolsEnabled = false;
            NavigateTranslationHome();
        }
        catch (Exception ex)
        {
            ViewModel.StatusText = $"翻译网页初始化失败: {ex.Message}";
        }
    }

    private void MainWindow_Closed(object? sender, EventArgs e)
    {
        TranslateWebView.Dispose();
        if (string.IsNullOrWhiteSpace(_webViewUserDataFolder))
        {
            return;
        }

        try
        {
            if (Directory.Exists(_webViewUserDataFolder))
            {
                Directory.Delete(_webViewUserDataFolder, recursive: true);
            }
        }
        catch
        {
            // 临时 WebView2 数据目录清理失败不影响 Viewer 使用。
        }
    }

    private void OpenTranslate_Click(object sender, RoutedEventArgs e)
    {
        CopyTranslateInputToClipboard();
        NavigateTranslationHome();
        SelectTranslateTab();
    }

    private void TranslateZhToEn_Click(object sender, RoutedEventArgs e)
    {
        NavigateTranslation("zh", "en");
    }

    private void TranslateEnToZh_Click(object sender, RoutedEventArgs e)
    {
        NavigateTranslation("en", "zh");
    }

    private void CopyTranslateInput_Click(object sender, RoutedEventArgs e)
    {
        if (CopyTranslateInputToClipboard())
        {
            ViewModel.StatusText = "已复制翻译原文。";
        }
    }

    private void ClearTranslateInput_Click(object sender, RoutedEventArgs e)
    {
        TranslateInputBox.Clear();
        TranslateInputBox.Focus();
        ViewModel.StatusText = "已清空翻译输入。";
    }

    private void TranslateWebView_NavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
    {
        if (!e.IsSuccess)
        {
            ViewModel.StatusText = $"翻译网页加载失败: {e.WebErrorStatus}";
        }
    }

    private void NavigateTranslation(string from, string to)
    {
        CopyTranslateInputToClipboard();
        var text = TranslateInputBox.Text.Trim();
        var url = BuildTranslationUrl(GetSelectedTranslateSite(), from, to, text);
        NavigateWebView(url);
        SelectTranslateTab();
        ViewModel.StatusText = string.IsNullOrWhiteSpace(text)
            ? "已打开翻译网站；未输入原文。"
            : "已复制原文并打开内嵌翻译网站。";
    }

    private void NavigateTranslationHome()
    {
        NavigateWebView(BuildTranslationHomeUrl(GetSelectedTranslateSite()));
    }

    private void NavigateWebView(string url)
    {
        try
        {
            TranslateWebView.Source = new Uri(url);
        }
        catch (Exception ex)
        {
            ViewModel.StatusText = $"翻译网页打开失败: {ex.Message}";
        }
    }

    private bool CopyTranslateInputToClipboard()
    {
        var text = TranslateInputBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        Clipboard.SetText(text);
        return true;
    }

    private string GetSelectedTranslateSite()
    {
        return (TranslateSiteCombo.SelectedItem as ComboBoxItem)?.Content?.ToString() ?? "百度翻译";
    }

    private void SelectTranslateTab()
    {
        RightTabs.SelectedItem = TranslateTab;
    }

    private static string BuildTranslationHomeUrl(string site)
    {
        return site switch
        {
            "DeepL" => "https://www.deepl.com/translator",
            "Google Translate" => "https://translate.google.com/",
            _ => "https://fanyi.baidu.com/"
        };
    }

    private static string BuildTranslationUrl(string site, string from, string to, string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return BuildTranslationHomeUrl(site);
        }

        var encoded = Uri.EscapeDataString(text);
        return site switch
        {
            "DeepL" => BuildDeepLUrl(from, to, encoded),
            "Google Translate" => BuildGoogleUrl(from, to, encoded),
            _ => BuildBaiduUrl(from, to, encoded)
        };
    }

    private static string BuildBaiduUrl(string from, string to, string encodedText)
    {
        var source = from == "zh" ? "zh" : "en";
        var target = to == "zh" ? "zh" : "en";
        return $"https://fanyi.baidu.com/#{source}/{target}/{encodedText}";
    }

    private static string BuildDeepLUrl(string from, string to, string encodedText)
    {
        var source = from == "zh" ? "zh" : "en";
        var target = to == "zh" ? "zh" : "en-US";
        return $"https://www.deepl.com/translator#{source}/{target}/{encodedText}";
    }

    private static string BuildGoogleUrl(string from, string to, string encodedText)
    {
        var source = from == "zh" ? "zh-CN" : "en";
        var target = to == "zh" ? "zh-CN" : "en";
        return $"https://translate.google.com/?sl={source}&tl={target}&text={encodedText}&op=translate";
    }
}
