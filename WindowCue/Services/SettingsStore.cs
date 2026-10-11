using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using WindowCue.Models;

namespace WindowCue.Services;

internal static class SettingsStore
{
    private static readonly string DirectoryPath = ResolveDirectoryPath();
    private static readonly string FilePath = Path.Combine(DirectoryPath, "bindings.json");
    private static readonly string LegacyFilePath = Path.Combine(DirectoryPath, "binding.json");

    private static string ResolveDirectoryPath()
    {
        string? overridePath = Environment.GetEnvironmentVariable("WINDOWCUE_SETTINGS_DIR");
        return string.IsNullOrWhiteSpace(overridePath)
            ? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "WindowCue")
            : Path.GetFullPath(overridePath);
    }

    internal static IReadOnlyList<BindingSettings> LoadAll()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                return JsonSerializer.Deserialize<List<BindingSettings>>(File.ReadAllText(FilePath))
                       ?? new List<BindingSettings>();
            }

            if (File.Exists(LegacyFilePath))
            {
                BindingSettings? legacy = JsonSerializer.Deserialize<BindingSettings>(
                    File.ReadAllText(LegacyFilePath));
                return legacy is null
                    ? Array.Empty<BindingSettings>()
                    : new[] { legacy };
            }

            return Array.Empty<BindingSettings>();
        }
        catch
        {
            return Array.Empty<BindingSettings>();
        }
    }

    internal static void SaveAll(IEnumerable<BindingSettings> settings)
    {
        BindingSettings[] items = settings.ToArray();
        if (items.Length == 0)
        {
            DeleteAll();
            return;
        }

        Directory.CreateDirectory(DirectoryPath);
        File.WriteAllText(FilePath, JsonSerializer.Serialize(
            items,
            new JsonSerializerOptions { WriteIndented = true }));
        TryDelete(LegacyFilePath);
    }

    internal static void DeleteAll()
    {
        TryDelete(FilePath);
        TryDelete(LegacyFilePath);
    }

    private static void TryDelete(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch
        {
            // A stale settings file must not keep dead hotkeys registered.
        }
    }
}
