# FICO-Command_line_tool
FICO is a Windows command-line tool built with C++17 for handling file archives. It supports 7z, ZIP, and RAR compression, extraction, archive testing, SHA-256 hashing, and compression benchmarks, with options to split archives and exclude specific files. It uses 7-Zip and WinRAR for archive operations.
## Features

- Create **7z, ZIP, and RAR** archives.
- Extract archives to a chosen folder.
- View archive contents without extracting them.
- Test archives for corruption.
- Generate and verify SHA-256 file hashes.
- Choose compression levels to balance speed and file size.
- Exclude files using filename patterns.
- Split large archives into smaller volumes.
- Benchmark compression levels to compare archive sizes and processing times.
- Detect installed archiving tools.

## Requirements

- Windows
- A C++17-compatible compiler to build from source.
- [7-Zip](https://www.7-zip.org/) for compression, extraction, listing, testing, and benchmarking.
- WinRAR for creating RAR archives.

## Usage

```bat
fico help
```

Check which tools are available:

```bat
fico tools
```

Create a 7z archive:

```bat
fico compress 7z "C:\Projects\MyApp"
```

Create a ZIP archive:

```bat
fico compress zip "C:\Projects\MyApp" "D:\Backups\MyApp.zip"
```

Extract an archive:

```bat
fico extract "D:\Backups\MyApp.7z" "D:\Restored"
```

Check an archive:

```bat
fico test "D:\Backups\MyApp.7z"
```

Generate a SHA-256 hash:

```bat
fico hash "D:\Backups\MyApp.7z"
```

Compare compression performance:

```bat
fico benchmark "C:\Projects\MyApp"
```

For the full list of commands and options, run `fico help`.

## Why FICO?

The goal of this project is to make everyday archive operations easier to run from the terminal.

FICO is a work in progress, and feedback, bug reports, and suggestions are welcome.

## Note

FICO relies on external archiving programs for its archive operations. Be careful when extracting archives from untrusted sources, as they may contain malicious files or paths.
