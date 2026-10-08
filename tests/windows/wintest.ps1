# Helpers for testing Amplitude on Windows by script. See docs/WINDOWS_TESTING.md.
#
#     . .\tests\windows\wintest.ps1          (dot-source it, from the project root)
#
# STATUS: every function here has been run on Windows 11 (24H2, PowerShell 7,
# 150% display scaling) while working through the test document, twice; see
# its section 6 for what had to be changed and added.
#
# The player is always started from a scratch copy with an amplitude.ini next
# to it. That makes it "portable": settings and playlist are read and written
# there, and the user's own settings in %APPDATA%\Amplitude are never touched.

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class AmpWin {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr lparam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindowA(string cls, string title);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint vk);
    [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr hwnd, int id);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);

    /* Bounding box of the pixels that differ between two images of the same
       size: {left, top, right, bottom, count}; count is 0 if they are equal. */
    public static int[] DiffBox(int[] a, int[] b, int w, int h) {
        int l = w, t = h, r = -1, bt = -1, n = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (a[y * w + x] != b[y * w + x]) {
                    n++;
                    if (x < l) l = x;
                    if (x > r) r = x;
                    if (y < t) t = y;
                    if (y > bt) bt = y;
                }
        return new int[] { l, t, r, bt, n };
    }
}
"@
[void][AmpWin]::SetProcessDPIAware()    # so that our coordinates are real pixels, like the player's

$script:AmpRoot = (Get-Location).Path
$script:AmpWork = Join-Path $env:TEMP "amplitude-test"
$script:AmpProc = $null
$script:AmpScale = 100          # percent; Start-Amp sets it
# A later PowerShell (each tool call may be a new one) picks the running
# player up again from this file with Connect-Amp.
$script:AmpState = Join-Path $env:TEMP "amplitude-test.state"

# --- Starting and stopping ---------------------------------------------------

# Refuses to run while any other Amplitude is open: on Windows a second copy
# hands its files to the first one, whoever owns it, and exits.
function Assert-NoOtherAmp {
    if ([AmpWin]::FindWindowA("AmplitudeWindow", "Amplitude") -ne [IntPtr]::Zero) {
        throw "An Amplitude window is already open. Close it before testing (it may be the user's own)."
    }
}

# Copies the given build (32 or 64) into a fresh scratch folder in portable mode.
# -Dir puts it somewhere else (for a folder named in another script). -Keep
# leaves the settings and playlist of an existing sandbox as they are.
function New-AmpSandbox([int]$Bits = 64, [string]$Ini = "", [string]$Dir = "", [switch]$Keep) {
    if ($Dir) { $script:AmpWork = $Dir }
    if ($Keep -and (Test-Path (Join-Path $script:AmpWork "amplitude.ini"))) {
        Copy-Item (Join-Path $script:AmpRoot "build\win$Bits\amplitude.exe") $script:AmpWork -Force
        return $script:AmpWork
    }
    Remove-Item -Recurse -Force $script:AmpWork -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $script:AmpWork | Out-Null
    Copy-Item (Join-Path $script:AmpRoot "build\win$Bits\amplitude.exe") $script:AmpWork
    Set-Content -Path (Join-Path $script:AmpWork "amplitude.ini") -Value $Ini -Encoding ASCII
    return $script:AmpWork
}

# Starts the sandboxed player. Scale is a factor: 1 = 100%, where a logical
# pixel is one real pixel and the coordinates in the test document apply as
# they are. Extra arguments (files, --enqueue, ...) are passed through.
#
# -Scale 0 passes no --scale, so the player picks its own size (or the one
# saved in the sandbox); tell the helper what that was with Set-AmpScale
# before clicking. Arguments containing spaces are quoted here.
function Start-Amp([double]$Scale = 1, [string[]]$Arguments = @()) {
    Assert-NoOtherAmp
    $all = @($Arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
    if ($Scale -gt 0) {
        $script:AmpScale = [int]($Scale * 100)
        $all = @("--scale", $Scale.ToString([Globalization.CultureInfo]::InvariantCulture)) + $all
    }
    $exe = Join-Path $script:AmpWork "amplitude.exe"
    if ($all.Count) { $script:AmpProc = Start-Process -FilePath $exe -ArgumentList $all -PassThru }
    else { $script:AmpProc = Start-Process -FilePath $exe -PassThru }
    Start-Sleep -Milliseconds 1500
    Save-AmpState
    return $script:AmpProc
}

function Save-AmpState {
    Set-Content -Path $script:AmpState -Encoding UTF8 -Value @($script:AmpProc.Id, $script:AmpScale, $script:AmpWork)
}

function Set-AmpScale([int]$Percent) { $script:AmpScale = $Percent; Save-AmpState }

# Picks up the player an earlier PowerShell started.
function Connect-Amp {
    $state = @(Get-Content -Encoding UTF8 $script:AmpState)
    $script:AmpProc = Get-Process -Id ([int]$state[0]) -ErrorAction Stop
    $script:AmpScale = [int]$state[1]
    $script:AmpWork = $state[2]
    return $script:AmpProc
}

function Stop-Amp {
    if ($script:AmpProc -and -not $script:AmpProc.HasExited) {
        $main = Get-AmpWindow "Amplitude"
        if ($main) { [void][AmpWin]::PostMessage($main.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }   # WM_CLOSE
        if (-not $script:AmpProc.WaitForExit(5000)) { $script:AmpProc.Kill() }
    }
    $script:AmpProc = $null
}

# --- Finding windows ---------------------------------------------------------

# Every top-level window of the test player: Handle, Title, Visible, X, Y, W, H.
# The popup menu is the visible one with an empty title.
function Get-AmpWindows {
    $list = New-Object System.Collections.ArrayList
    $target = [uint32]$script:AmpProc.Id
    $callback = [AmpWin+EnumProc]{
        param($hwnd, $lparam)
        $owner = [uint32]0
        [void][AmpWin]::GetWindowThreadProcessId($hwnd, [ref]$owner)
        if ($owner -eq $target) {
            $title = New-Object System.Text.StringBuilder 256
            $class = New-Object System.Text.StringBuilder 256
            [void][AmpWin]::GetWindowText($hwnd, $title, 256)
            [void][AmpWin]::GetClassName($hwnd, $class, 256)
            $rect = New-Object AmpWin+RECT
            [void][AmpWin]::GetWindowRect($hwnd, [ref]$rect)
            if ($class.ToString() -eq "AmplitudeWindow") {
                [void]$list.Add([pscustomobject]@{
                    Handle = $hwnd; Title = $title.ToString(); Visible = [AmpWin]::IsWindowVisible($hwnd)
                    X = $rect.Left; Y = $rect.Top; W = $rect.Right - $rect.Left; H = $rect.Bottom - $rect.Top })
            }
        }
        return $true
    }
    [void][AmpWin]::EnumWindows($callback, [IntPtr]::Zero)
    return $list
}

# Titles: "Amplitude", "Amplitude Equalizer", "Amplitude Playlist",
# "Jump to file", "About Amplitude", "Open location"; "" is the popup menu.
function Get-AmpWindow([string]$Title) {
    return Get-AmpWindows | Where-Object { $_.Title -eq $Title -and ($Title -ne "" -or $_.Visible) } | Select-Object -First 1
}

# --- Input -------------------------------------------------------------------
# Coordinates are logical (the 275x116 layout); they are multiplied by the
# scale the player was started with. Messages are posted to the window, so
# the real mouse and keyboard are left alone.

function Get-LParam([int]$X, [int]$Y) {
    $rx = [int]($X * $script:AmpScale / 100); $ry = [int]($Y * $script:AmpScale / 100)
    return [IntPtr](($ry -shl 16) -bor ($rx -band 0xFFFF))
}

# -Ctrl and -Shift press the real key for the length of the click: the player
# reads modifiers with GetKeyState, not from the message, so the window is
# brought to the front for those. -Wait is the pause after the click.
function Send-AmpClick([string]$Title, [int]$X, [int]$Y, [switch]$Right, [switch]$Ctrl, [switch]$Shift, [int]$Wait = 300) {
    $win = Get-AmpWindow $Title
    if (-not $win) { throw "window '$Title' not found" }
    $down = 0x0201; $up = 0x0202; $keys = 0x0001          # WM_LBUTTONDOWN/UP, MK_LBUTTON
    if ($Right) { $down = 0x0204; $up = 0x0205; $keys = 0x0002 }
    if ($Ctrl) { $keys = $keys -bor 0x0008 }
    if ($Shift) { $keys = $keys -bor 0x0004 }
    if ($Ctrl -or $Shift) { [void][AmpWin]::SetForegroundWindow($win.Handle); Start-Sleep -Milliseconds 200 }
    if ($Ctrl) { [AmpWin]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero) }
    if ($Shift) { [AmpWin]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
    if ($Ctrl -or $Shift) { Start-Sleep -Milliseconds 100 }
    $lparam = Get-LParam $X $Y
    [void][AmpWin]::PostMessage($win.Handle, 0x0200, [IntPtr]::Zero, $lparam)      # WM_MOUSEMOVE
    [void][AmpWin]::PostMessage($win.Handle, $down, [IntPtr]$keys, $lparam)
    Start-Sleep -Milliseconds 80
    [void][AmpWin]::PostMessage($win.Handle, $up, [IntPtr]::Zero, $lparam)
    if ($Ctrl -or $Shift) { Start-Sleep -Milliseconds 100 }
    if ($Shift) { [AmpWin]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }             # KEYEVENTF_KEYUP
    if ($Ctrl) { [AmpWin]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds $Wait
}

# Two clicks close enough together to count as a double click (under 400 ms).
function Send-AmpDoubleClick([string]$Title, [int]$X, [int]$Y) {
    Send-AmpClick $Title $X $Y -Wait 60
    Send-AmpClick $Title $X $Y
}

# Button down at one point, moves along a straight line, button up at the other.
function Send-AmpDrag([string]$Title, [int]$X1, [int]$Y1, [int]$X2, [int]$Y2, [int]$Steps = 8) {
    $win = Get-AmpWindow $Title
    if (-not $win) { throw "window '$Title' not found" }
    [void][AmpWin]::PostMessage($win.Handle, 0x0200, [IntPtr]::Zero, (Get-LParam $X1 $Y1))
    [void][AmpWin]::PostMessage($win.Handle, 0x0201, [IntPtr]1, (Get-LParam $X1 $Y1))
    Start-Sleep -Milliseconds 80
    for ($i = 1; $i -le $Steps; $i++) {
        $x = $X1 + ($X2 - $X1) * $i / $Steps; $y = $Y1 + ($Y2 - $Y1) * $i / $Steps
        [void][AmpWin]::PostMessage($win.Handle, 0x0200, [IntPtr]1, (Get-LParam $x $y))
        Start-Sleep -Milliseconds 40
    }
    [void][AmpWin]::PostMessage($win.Handle, 0x0202, [IntPtr]::Zero, (Get-LParam $X2 $Y2))
    Start-Sleep -Milliseconds 300
}

# A virtual-key code (0x1B Escape, 0x0D Enter, 0x2E Delete, 0x25..0x28 arrows,
# or a capital letter's ASCII code).
#
# -Ctrl holds the real Ctrl key for the length of the press, as Send-AmpClick
# does (Ctrl+L is 0x4C, Ctrl+V 0x56). The window has to be in front for the
# player to see the key as down; Windows may refuse to bring it there.
function Send-AmpKey([string]$Title, [int]$VirtualKey, [switch]$Ctrl) {
    $win = Get-AmpWindow $Title
    if (-not $win) { throw "window '$Title' not found" }
    if ($Ctrl) {
        [void][AmpWin]::SetForegroundWindow($win.Handle); Start-Sleep -Milliseconds 200
        [AmpWin]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 100
    }
    # The key-up needs its "was down, now released" bits: with a plain zero,
    # TranslateMessage in the player turns it into a second typed character.
    [void][AmpWin]::PostMessage($win.Handle, 0x0100, [IntPtr]$VirtualKey, [IntPtr]1)                    # WM_KEYDOWN
    [void][AmpWin]::PostMessage($win.Handle, 0x0101, [IntPtr]$VirtualKey, [IntPtr]::new(0xC0000001))    # WM_KEYUP
    if ($Ctrl) { Start-Sleep -Milliseconds 100; [AmpWin]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }     # KEYEVENTF_KEYUP
    Start-Sleep -Milliseconds 200
}

# Brings a window of the player to the front and says whether that worked.
# Windows refuses SetForegroundWindow from a background program at times (for
# one, while the notification panel is open). -RealClick then clicks the real
# mouse once on the window's title text, a place that only drags, and puts the
# pointer back: the user sees the pointer jump, so say so beforehand.
function Set-AmpForeground([string]$Title, [switch]$RealClick) {
    $win = Get-AmpWindow $Title
    if (-not $win) { throw "window '$Title' not found" }
    [void][AmpWin]::SetForegroundWindow($win.Handle); Start-Sleep -Milliseconds 300
    if ($RealClick -and [AmpWin]::GetForegroundWindow() -ne $win.Handle) {
        $old = New-Object AmpWin+POINT
        [void][AmpWin]::GetCursorPos([ref]$old)
        [void][AmpWin]::SetCursorPos($win.X + [int](40 * $script:AmpScale / 100), $win.Y + [int](7 * $script:AmpScale / 100))
        Start-Sleep -Milliseconds 100
        [AmpWin]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60     # MOUSEEVENTF_LEFTDOWN
        [AmpWin]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150    # MOUSEEVENTF_LEFTUP
        [void][AmpWin]::SetCursorPos($old.X, $old.Y); Start-Sleep -Milliseconds 300
    }
    return [AmpWin]::GetForegroundWindow() -eq $win.Handle
}

# Media keys: 0xB3 play/pause, 0xB2 stop, 0xB0 next, 0xB1 previous.
#
# Whether somebody holds all four as hot keys: with the test player running
# and nothing held before it started, that somebody is the player.
function Test-AmpMediaKeys {
    foreach ($vk in 0xB3, 0xB2, 0xB0, 0xB1) {
        if ([AmpWin]::RegisterHotKey([IntPtr]::Zero, 77, 0, $vk)) {
            [void][AmpWin]::UnregisterHotKey([IntPtr]::Zero, 77)
            return $false
        }
    }
    return $true
}

# Presses the real key, for the whole system. It refuses unless the keys are
# held (Test-AmpMediaKeys): a press nobody holds goes to whatever other
# player the user has, and may start it playing aloud.
function Send-AmpMediaKey([int]$VirtualKey) {
    if (-not (Test-AmpMediaKeys)) { throw "the media keys are not held as hot keys: not pressing" }
    [AmpWin]::keybd_event([byte]$VirtualKey, 0, 1, [UIntPtr]::Zero)      # KEYEVENTF_EXTENDEDKEY
    [AmpWin]::keybd_event([byte]$VirtualKey, 0, 3, [UIntPtr]::Zero)      # ... | KEYEVENTF_KEYUP
    Start-Sleep -Milliseconds 900
}

function Send-AmpText([string]$Title, [string]$Text) {
    $win = Get-AmpWindow $Title
    foreach ($ch in $Text.ToCharArray()) {
        [void][AmpWin]::PostMessage($win.Handle, 0x0102, [IntPtr][int]$ch, [IntPtr]::Zero)  # WM_CHAR
    }
    Start-Sleep -Milliseconds 200
}

# --- Looking -----------------------------------------------------------------

# Saves what is on screen where the window is, as PNG. The window has to be
# unobstructed: it is brought to the front first. (PrintWindow cannot be used:
# the player paints from its own loop, not in answer to WM_PAINT.)
function Save-AmpShot([string]$Title, [string]$Path) {
    $win = Get-AmpWindow $Title
    if (-not $win) { throw "window '$Title' not found" }
    [void][AmpWin]::SetForegroundWindow($win.Handle)
    Start-Sleep -Milliseconds 400
    $win = Get-AmpWindow $Title
    $bitmap = New-Object System.Drawing.Bitmap $win.W, $win.H
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($win.X, $win.Y, 0, 0, $bitmap.Size)
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $Path
}

# The same for the rectangle around all visible windows of the player, menu
# included, without changing which one is in front.
function Save-AmpShotAll([string]$Path) {
    $wins = @(Get-AmpWindows | Where-Object { $_.Visible -and $_.X -gt -30000 })
    if (-not $wins.Count) { throw "no visible window" }
    $l = ($wins | ForEach-Object { $_.X } | Measure-Object -Minimum).Minimum
    $t = ($wins | ForEach-Object { $_.Y } | Measure-Object -Minimum).Minimum
    $r = ($wins | ForEach-Object { $_.X + $_.W } | Measure-Object -Maximum).Maximum
    $b = ($wins | ForEach-Object { $_.Y + $_.H } | Measure-Object -Maximum).Maximum
    $bitmap = New-Object System.Drawing.Bitmap ([int]($r - $l)), ([int]($b - $t))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen([int]$l, [int]$t, 0, 0, $bitmap.Size)
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $Path
}

function Get-AmpPixels([string]$Path) {
    $bitmap = New-Object System.Drawing.Bitmap $Path
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bitmap.Width, $bitmap.Height
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object int[] ($bitmap.Width * $bitmap.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
    $bitmap.UnlockBits($data)
    $result = [pscustomobject]@{ W = $bitmap.Width; H = $bitmap.Height; Pixels = $pixels }
    $bitmap.Dispose()
    return $result
}

# Where two screenshots differ: Count of pixels and their bounding box in real
# pixels, or Count 0. Different sizes give Count -1.
function Compare-AmpShots([string]$PathA, [string]$PathB) {
    $a = Get-AmpPixels $PathA; $b = Get-AmpPixels $PathB
    if ($a.W -ne $b.W -or $a.H -ne $b.H) { return [pscustomobject]@{ Count = -1; Left = 0; Top = 0; Right = 0; Bottom = 0 } }
    $box = [AmpWin]::DiffBox($a.Pixels, $b.Pixels, $a.W, $a.H)
    return [pscustomobject]@{ Count = $box[4]; Left = $box[0]; Top = $box[1]; Right = $box[2]; Bottom = $box[3] }
}

# One line of the sandbox's amplitude.ini, e.g. Get-AmpSetting "scale_percent".
# Settings are written when the player exits.
function Get-AmpSetting([string]$Key) {
    $line = Get-Content (Join-Path $script:AmpWork "amplitude.ini") | Where-Object { $_ -like "$Key=*" } | Select-Object -First 1
    if ($line) { return $line.Substring($Key.Length + 1) }
    return $null
}

function Get-AmpPlaylist {
    $path = Join-Path $script:AmpWork "amplitude.m3u"
    if (Test-Path $path) { return @(Get-Content -Encoding UTF8 $path | Where-Object { $_ -and -not $_.StartsWith("#") }) }
    return @()
}

# --- Measuring ---------------------------------------------------------------

# Processor time used over a period, as a percentage of one core.
function Measure-AmpCpu([int]$Seconds = 20) {
    $script:AmpProc.Refresh(); $before = $script:AmpProc.TotalProcessorTime
    Start-Sleep -Seconds $Seconds
    $script:AmpProc.Refresh(); $after = $script:AmpProc.TotalProcessorTime
    return [math]::Round(($after - $before).TotalSeconds / $Seconds * 100, 2)
}

# Working set in MB, and the GDI and USER object counts. Counts that keep
# climbing while the same actions are repeated mean a handle leak.
function Get-AmpResources {
    $script:AmpProc.Refresh()
    return [pscustomobject]@{
        WorkingSetMB = [math]::Round($script:AmpProc.WorkingSet64 / 1MB, 1)
        PrivateMB    = [math]::Round($script:AmpProc.PrivateMemorySize64 / 1MB, 1)
        Gdi          = [AmpWin]::GetGuiResources($script:AmpProc.Handle, 0)
        User         = [AmpWin]::GetGuiResources($script:AmpProc.Handle, 1)
        Handles      = $script:AmpProc.HandleCount
        Threads      = $script:AmpProc.Threads.Count }
}
