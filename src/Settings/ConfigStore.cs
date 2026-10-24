using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.UI.Dispatching;

namespace DashMaid.Settings;

// Reads and writes config.json. The file is next to DashMaid.exe if it
// exists there, else in %APPDATA%\DashMaid. The toolbar process uses the
// same rule and watches both folders. Changes go to the file after a short
// delay, so that typing in a text box does not write the file for each key.
// Fields that this window does not know stay in the file.
public sealed class ConfigStore : IDisposable
{
    public static string UserPath { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "DashMaid", "config.json");

    // The settings window is in the "Settings" folder next to DashMaid.exe.
    public static string AppPath { get; } =
        Path.Combine(Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(AppContext.BaseDirectory))!, "config.json");

    public static bool InAppFolder => File.Exists(AppPath);

    public static string FilePath => InAppFolder ? AppPath : UserPath;

    static readonly JsonSerializerOptions WriteOptions = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        WriteIndented = false,
    };

    readonly DispatcherQueueTimer flushTimer;
    readonly DispatcherQueueTimer reloadTimer;
    readonly List<FileSystemWatcher> watchers = [];
    string lastText = "";
    bool pending;

    public JsonObject Root { get; private set; } = new();

    // False when the file is not valid JSON. Then the window does not write the file.
    public bool Valid { get; private set; } = true;

    // Another program changed the file. The UI must show the new values.
    public event Action? Reloaded;

    // This window changed a value.
    public event Action? Changed;

    public ConfigStore(DispatcherQueue queue)
    {
        flushTimer = queue.CreateTimer();
        flushTimer.Interval = TimeSpan.FromMilliseconds(400);
        flushTimer.IsRepeating = false;
        flushTimer.Tick += (_, _) => Flush();
        reloadTimer = queue.CreateTimer();
        reloadTimer.Interval = TimeSpan.FromMilliseconds(150);
        reloadTimer.IsRepeating = false;
        reloadTimer.Tick += (_, _) => OnExternalChange();

        Load();

        Directory.CreateDirectory(Path.GetDirectoryName(UserPath)!);
        foreach (var file in new[] { UserPath, AppPath })
        {
            var watcher = new FileSystemWatcher(Path.GetDirectoryName(file)!, Path.GetFileName(file))
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
            };
            FileSystemEventHandler changed = (_, _) => queue.TryEnqueue(() => reloadTimer.Start());
            watcher.Changed += changed;
            watcher.Created += changed;
            watcher.Deleted += changed;
            watcher.Renamed += (_, _) => queue.TryEnqueue(() => reloadTimer.Start());
            watcher.EnableRaisingEvents = true;
            watchers.Add(watcher);
        }
    }

    // Moves the file next to DashMaid.exe (inAppFolder) or to %APPDATA%.
    // The new file is written before the old file is deleted, so that the
    // toolbar always finds a file. Returns false if the folder cannot be written.
    public bool MoveFile(bool inAppFolder)
    {
        if (!Valid || inAppFolder == InAppFolder)
        {
            return true;
        }
        Flush();
        var from = FilePath;
        var to = inAppFolder ? AppPath : UserPath;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            var temp = to + ".tmp";
            File.WriteAllText(temp, Root.ToJsonString(WriteOptions), new System.Text.UTF8Encoding(false));
            File.Move(temp, to, overwrite: true);
            File.Delete(from);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            try { File.Delete(to + ".tmp"); } catch (Exception) { }
            if (inAppFolder && File.Exists(from))
            {
                try { File.Delete(to); } catch (Exception) { }
            }
            return false;
        }
        lastText = Root.ToJsonString(WriteOptions);
        return true;
    }

    void Load()
    {
        var text = ReadText();
        if (text == null)
        {
            return;
        }
        lastText = text;
        try
        {
            Root = JsonNode.Parse(text) as JsonObject ?? new JsonObject();
            Valid = true;
        }
        catch (JsonException)
        {
            Valid = false;
        }
    }

    void OnExternalChange()
    {
        if (pending)
        {
            return;  // The changes of this window go to the file soon.
        }
        var text = ReadText();
        if (text == null || text == lastText)
        {
            return;
        }
        Load();
        Reloaded?.Invoke();
    }

    static string? ReadText()
    {
        for (int i = 0; i < 10; i++)
        {
            try
            {
                using var stream = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                using var reader = new StreamReader(stream, new System.Text.UTF8Encoding(false));
                return reader.ReadToEnd();
            }
            catch (FileNotFoundException)
            {
                return "{}";
            }
            catch (DirectoryNotFoundException)
            {
                return "{}";
            }
            catch (IOException)
            {
                Thread.Sleep(30);
            }
        }
        return null;
    }

    // Applies a change and writes the file after a short delay.
    public void Change(Action<JsonObject> change)
    {
        if (!Valid)
        {
            return;
        }
        change(Root);
        pending = true;
        Changed?.Invoke();
        flushTimer.Stop();
        flushTimer.Start();
    }

    public void Flush()
    {
        flushTimer.Stop();
        if (!pending)
        {
            return;
        }
        var text = Root.ToJsonString(WriteOptions);
        if (!WriteText(text))
        {
            flushTimer.Start();  // Try again later. The changes stay pending.
            return;
        }
        pending = false;
        lastText = text;
    }

    // The toolbar reads the file after each change. While it reads, the
    // replace can fail with a sharing violation or "access denied".
    static bool WriteText(string text)
    {
        var temp = FilePath + ".tmp";
        for (int i = 0; i < 10; i++)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
                File.WriteAllText(temp, text, new System.Text.UTF8Encoding(false));
                File.Move(temp, FilePath, overwrite: true);
                return true;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Thread.Sleep(30);
            }
        }
        return false;
    }

    public void Dispose()
    {
        Flush();
        foreach (var w in watchers)
        {
            w.Dispose();
        }
    }

    // ---- Typed access ----

    public JsonObject Object(string name)
    {
        if (Root[name] is JsonObject o)
        {
            return o;
        }
        var created = new JsonObject();
        Root[name] = created;
        return created;
    }

    public JsonArray Items
    {
        get
        {
            if (Root["items"] is JsonArray a)
            {
                return a;
            }
            var created = new JsonArray();
            Root["items"] = created;
            return created;
        }
    }

    public static string Str(JsonNode? node, string name, string fallback = "") =>
        node is JsonObject o && o[name] is JsonValue v && v.TryGetValue<string>(out var s) ? s : fallback;

    public static bool Bool(JsonNode? node, string name, bool fallback) =>
        node is JsonObject o && o[name] is JsonValue v && v.TryGetValue<bool>(out var b) ? b : fallback;

    public static double Num(JsonNode? node, string name, double fallback) =>
        node is JsonObject o && o[name] is JsonValue v && v.TryGetValue<double>(out var d) ? d : fallback;
}
