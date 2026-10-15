using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.UI.Dispatching;

namespace DashMaid.Settings;

// Reads and writes %APPDATA%\DashMaid\config.json. The toolbar process
// watches the same file. Changes go to the file after a short delay, so
// that typing in a text box does not write the file for each key.
// Fields that this window does not know stay in the file.
public sealed class ConfigStore : IDisposable
{
    public static string FilePath { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "DashMaid", "config.json");

    static readonly JsonSerializerOptions WriteOptions = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        WriteIndented = false,
    };

    readonly DispatcherQueueTimer flushTimer;
    readonly DispatcherQueueTimer reloadTimer;
    readonly FileSystemWatcher? watcher;
    string lastText = "";
    bool pending;

    public JsonObject Root { get; private set; } = new();

    // False when the file is not valid JSON. Then the window does not write the file.
    public bool Valid { get; private set; } = true;

    // Another program changed the file. The UI must show the new values.
    public event Action? Reloaded;

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

        var dir = Path.GetDirectoryName(FilePath)!;
        if (Directory.Exists(dir))
        {
            watcher = new FileSystemWatcher(dir, Path.GetFileName(FilePath))
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
            };
            FileSystemEventHandler changed = (_, _) => queue.TryEnqueue(() => reloadTimer.Start());
            watcher.Changed += changed;
            watcher.Created += changed;
            watcher.Renamed += (_, _) => queue.TryEnqueue(() => reloadTimer.Start());
            watcher.EnableRaisingEvents = true;
        }
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
        pending = false;
        var text = Root.ToJsonString(WriteOptions);
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        var temp = FilePath + ".tmp";
        File.WriteAllText(temp, text, new System.Text.UTF8Encoding(false));
        File.Move(temp, FilePath, overwrite: true);
        lastText = text;
    }

    public void Dispose()
    {
        Flush();
        watcher?.Dispose();
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
