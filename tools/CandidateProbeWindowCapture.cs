using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public sealed class CandidateProbeWindowInfo {
    public long Hwnd { get; set; }
    public uint ProcessId { get; set; }
    public string Title { get; set; }
    public string ClassName { get; set; }
    public int ClientLeft { get; set; }
    public int ClientTop { get; set; }
    public int ClientWidth { get; set; }
    public int ClientHeight { get; set; }
    public bool Visible { get; set; }
    public bool Iconic { get; set; }
}

public sealed class CandidateProbeCaptureResult {
    public string Method { get; set; }
    public bool PrintWindowReturned { get; set; }
    public bool Accepted { get; set; }
    public string RejectionReason { get; set; }
    public int Width { get; set; }
    public int Height { get; set; }
    public double MeanLuma { get; set; }
    public double NonBlackFraction { get; set; }
    public double SentinelFraction { get; set; }
    public int SampledPixels { get; set; }
}

public static class CandidateProbeWindow {
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }

    [DllImport("user32.dll", SetLastError = true)] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", SetLastError = true)] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", SetLastError = true)] static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder text, int count);
    [DllImport("user32.dll", SetLastError = true)] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetWindowPos(
        IntPtr h, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SetThreadDpiAwarenessContext(
        IntPtr dpiContext);
    [DllImport("kernel32.dll")] static extern void Sleep(uint milliseconds);

    const uint WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101;
    const uint SWP_NOMOVE = 0x0002, SWP_NOZORDER = 0x0004, SWP_NOACTIVATE = 0x0010;
    static readonly IntPtr DpiAwarenessPerMonitorV2 = new IntPtr(-4);
    const uint PW_CLIENTONLY = 0x00000001;
    const uint PW_RENDERFULLCONTENT = 0x00000002;
    const string WindowCaptureMethod = "PrintWindow(PW_CLIENTONLY|PW_RENDERFULLCONTENT)";
    const string PresentCaptureMethod = "debug_server present_shot/present_shot_seq";

    static void InspectBitmap(Bitmap bitmap, CandidateProbeCaptureResult result) {
        long luma = 0, nonBlack = 0, sentinel = 0, samples = 0;
        int step = Math.Max(1, Math.Min(bitmap.Width, bitmap.Height) / 240);
        for (int y = 0; y < bitmap.Height; y += step) {
            for (int x = 0; x < bitmap.Width; x += step) {
                Color c = bitmap.GetPixel(x, y);
                int sum = c.R + c.G + c.B;
                luma += sum;
                if (sum > 12) nonBlack++;
                if (c.R >= 250 && c.G <= 5 && c.B >= 250) sentinel++;
                samples++;
            }
        }
        result.SampledPixels = (int)samples;
        result.MeanLuma = samples == 0 ? 0 : (double)luma / (samples * 3.0);
        result.NonBlackFraction = samples == 0 ? 0 : (double)nonBlack / samples;
        result.SentinelFraction = samples == 0 ? 1 : (double)sentinel / samples;
    }

    public static CandidateProbeWindowInfo Describe(IntPtr h) {
        /* PowerShell is otherwise DPI-virtualized on high-scale desktops, so
         * GetClientRect/SetWindowPos can agree on a logical 640x360 while SDL
         * actually renders ~1440x810. Match the candidate's per-monitor pixel
         * coordinate space before any geometry is observed or changed. */
        if (SetThreadDpiAwarenessContext(DpiAwarenessPerMonitorV2) == IntPtr.Zero)
            throw new Exception("cannot enter per-monitor DPI awareness for candidate geometry");
        if (h == IntPtr.Zero || !IsWindow(h)) throw new Exception("capture HWND is not a live window");
        RECT r;
        POINT p = new POINT();
        if (!GetClientRect(h, out r) || !ClientToScreen(h, ref p))
            throw new Exception("cannot resolve capture client rectangle");
        uint pid;
        GetWindowThreadProcessId(h, out pid);
        StringBuilder title = new StringBuilder(1024);
        StringBuilder className = new StringBuilder(256);
        GetWindowText(h, title, title.Capacity);
        GetClassName(h, className, className.Capacity);
        return new CandidateProbeWindowInfo {
            Hwnd = h.ToInt64(), ProcessId = pid, Title = title.ToString(),
            ClassName = className.ToString(), ClientLeft = p.X, ClientTop = p.Y,
            ClientWidth = r.R - r.L, ClientHeight = r.B - r.T,
            Visible = IsWindowVisible(h), Iconic = IsIconic(h)
        };
    }

    public static CandidateProbeWindowInfo RequireOwnedClient(
        IntPtr h, uint expectedPid, string expectedTitlePrefix) {
        CandidateProbeWindowInfo info = Describe(h);
        if (info.ProcessId != expectedPid)
            throw new Exception("capture HWND owner PID changed");
        if (String.IsNullOrEmpty(expectedTitlePrefix) ||
            !info.Title.StartsWith(expectedTitlePrefix, StringComparison.Ordinal))
            throw new Exception("capture HWND title does not match the candidate title prefix");
        if (!info.Visible || info.Iconic)
            throw new Exception("candidate client is hidden or minimized");
        if (info.ClientWidth < 64 || info.ClientHeight < 64)
            throw new Exception("candidate client rectangle is too small");
        return info;
    }

    public static CandidateProbeWindowInfo ResizeOwnedClient(
        IntPtr h, uint expectedPid, string expectedTitlePrefix,
        int clientWidth, int clientHeight, int timeoutMs) {
        if (clientWidth < 64 || clientHeight < 64)
            throw new ArgumentOutOfRangeException("requested client size must be at least 64x64");
        if (clientWidth > 16384 || clientHeight > 16384)
            throw new ArgumentOutOfRangeException("requested client size exceeds 16384x16384");
        if (timeoutMs < 100 || timeoutMs > 10000)
            throw new ArgumentOutOfRangeException("resize timeout must be 100..10000 ms");

        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        int stableExactSamples = 0;
        CandidateProbeWindowInfo current = null;
        while (DateTime.UtcNow < deadline) {
            current = RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
            if (current.ClientWidth == clientWidth && current.ClientHeight == clientHeight) {
                if (++stableExactSamples >= 3) return current;
                Sleep(20);
                continue;
            }
            stableExactSamples = 0;

            RECT outer;
            if (!GetWindowRect(h, out outer))
                throw new Exception("cannot resolve candidate outer window rectangle for resize");
            int outerWidth = outer.R - outer.L;
            int outerHeight = outer.B - outer.T;
            int targetOuterWidth = clientWidth + (outerWidth - current.ClientWidth);
            int targetOuterHeight = clientHeight + (outerHeight - current.ClientHeight);
            if (targetOuterWidth <= 0 || targetOuterHeight <= 0)
                throw new Exception("candidate non-client bounds produced an invalid resize target");
            if (!SetWindowPos(h, IntPtr.Zero, 0, 0, targetOuterWidth, targetOuterHeight,
                              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                throw new Exception("SetWindowPos failed while resizing candidate client");
            Sleep(20);
        }
        current = RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        throw new Exception(String.Format(
            "candidate client did not settle at {0}x{1} within {2} ms (actual {3}x{4})",
            clientWidth, clientHeight, timeoutMs,
            current.ClientWidth, current.ClientHeight));
    }

    public static void PressX(IntPtr h, uint expectedPid, string expectedTitlePrefix, int holdMs) {
        RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        SetForegroundWindow(h); // best effort for input only; capture never reads the desktop
        uint scan = MapVirtualKey(0x58, 0);
        IntPtr down = (IntPtr)(1L | ((long)scan << 16));
        IntPtr up = (IntPtr)(1L | ((long)scan << 16) | 0xC0000000L);
        PostMessage(h, WM_KEYDOWN, (IntPtr)0x58, down);
        Sleep((uint)holdMs);
        PostMessage(h, WM_KEYUP, (IntPtr)0x58, up);
    }

    static void PressVirtualKey(IntPtr h, uint virtualKey, int holdMs) {
        uint scan = MapVirtualKey(virtualKey, 0);
        IntPtr down = (IntPtr)(1L | ((long)scan << 16));
        IntPtr up = (IntPtr)(1L | ((long)scan << 16) | 0xC0000000L);
        PostMessage(h, WM_KEYDOWN, (IntPtr)virtualKey, down);
        Sleep((uint)holdMs);
        PostMessage(h, WM_KEYUP, (IntPtr)virtualKey, up);
    }

    public static void PressStart(IntPtr h, uint expectedPid,
                                  string expectedTitlePrefix, int holdMs) {
        RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        SetForegroundWindow(h);
        PressVirtualKey(h, 0x0D, holdMs); // Return = PlayStation Start
    }

    public static void SaveStateSlot(IntPtr h, uint expectedPid,
                                     string expectedTitlePrefix, int slot) {
        RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        if (slot < 0 || slot > 11)
            throw new ArgumentOutOfRangeException("savestate slot must be 0..11");
        SetForegroundWindow(h);
        PressVirtualKey(h, 0x76, 45); // F7: open host savestate menu
        Sleep(250);
        uint slotKey = slot <= 8 ? (uint)(0x31 + slot)
                     : slot == 9 ? 0x30u
                     : slot == 10 ? 0xBDu : 0xBBu; // 1..9, 0, -, =
        PressVirtualKey(h, slotKey, 45);
        Sleep(100);
        PressVirtualKey(h, 0x53, 45); // S: save selected slot
        Sleep(250);
    }

    public static void PressStartSelect(IntPtr h, uint expectedPid,
                                        string expectedTitlePrefix, int holdMs) {
        RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        SetForegroundWindow(h);
        uint startScan = MapVirtualKey(0x0D, 0);   // Return = Start
        uint selectScan = MapVirtualKey(0xA1, 0);  // Right Shift = Select
        IntPtr startDown = (IntPtr)(1L | ((long)startScan << 16));
        IntPtr selectDown = (IntPtr)(1L | ((long)selectScan << 16));
        IntPtr startUp = (IntPtr)(1L | ((long)startScan << 16) | 0xC0000000L);
        IntPtr selectUp = (IntPtr)(1L | ((long)selectScan << 16) | 0xC0000000L);
        PostMessage(h, WM_KEYDOWN, (IntPtr)0x0D, startDown);
        PostMessage(h, WM_KEYDOWN, (IntPtr)0xA1, selectDown);
        Sleep((uint)holdMs);
        PostMessage(h, WM_KEYUP, (IntPtr)0xA1, selectUp);
        PostMessage(h, WM_KEYUP, (IntPtr)0x0D, startUp);
    }

    public static CandidateProbeCaptureResult InspectComposedFrame(string path) {
        if (String.IsNullOrWhiteSpace(path) || !System.IO.File.Exists(path))
            throw new Exception("composed capture PNG does not exist");
        CandidateProbeCaptureResult result = new CandidateProbeCaptureResult {
            Method = PresentCaptureMethod
        };
        try {
            using (Bitmap bitmap = new Bitmap(path)) {
                result.Width = bitmap.Width;
                result.Height = bitmap.Height;
                InspectBitmap(bitmap, result);
            }
        } catch (Exception e) {
            result.RejectionReason = "composed capture is not a readable PNG: " + e.Message;
            return result;
        }
        if (result.Width < 64 || result.Height < 64)
            result.RejectionReason = "composed capture dimensions are too small";
        else if (result.MeanLuma < 1.0 || result.NonBlackFraction < 0.001)
            result.RejectionReason = "composed capture is black or effectively empty";
        else
            result.Accepted = true;
        return result;
    }

    public static CandidateProbeCaptureResult CaptureClient(
        IntPtr h, uint expectedPid, string expectedTitlePrefix, string path) {
        CandidateProbeWindowInfo before = RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        CandidateProbeCaptureResult result = new CandidateProbeCaptureResult {
            Method = WindowCaptureMethod, Width = before.ClientWidth, Height = before.ClientHeight
        };
        using (Bitmap bitmap = new Bitmap(before.ClientWidth, before.ClientHeight,
                                           PixelFormat.Format24bppRgb)) {
            using (Graphics graphics = Graphics.FromImage(bitmap)) {
                graphics.Clear(Color.Magenta);
                IntPtr dc = graphics.GetHdc();
                try { result.PrintWindowReturned = PrintWindow(h, dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT); }
                finally { graphics.ReleaseHdc(dc); }
            }
            InspectBitmap(bitmap, result);
            if (!result.PrintWindowReturned) result.RejectionReason = "PrintWindow returned false";
            else if (result.SentinelFraction > 0.01) result.RejectionReason = "client render retained sentinel pixels";
            else if (result.MeanLuma < 1.0 || result.NonBlackFraction < 0.001)
                result.RejectionReason = "client render is black or effectively empty";
            else result.Accepted = true;
            bitmap.Save(path, ImageFormat.Png);
        }
        CandidateProbeWindowInfo after = RequireOwnedClient(h, expectedPid, expectedTitlePrefix);
        if (after.ClientWidth != before.ClientWidth || after.ClientHeight != before.ClientHeight)
            throw new Exception("candidate client resized during capture");
        return result;
    }

    public static string Compare(string first, string second) {
        using (Bitmap a = new Bitmap(first)) using (Bitmap b = new Bitmap(second)) {
            if (a.Width != b.Width || a.Height != b.Height) throw new Exception("capture sizes differ");
            long total = 0, changed = 0, samples = 0, luma = 0;
            int step = Math.Max(1, Math.Min(a.Width, a.Height) / 240);
            for (int y = 0; y < a.Height; y += step) for (int x = 0; x < a.Width; x += step) {
                Color ca = a.GetPixel(x, y), cb = b.GetPixel(x, y);
                int da = Math.Abs(ca.R-cb.R) + Math.Abs(ca.G-cb.G) + Math.Abs(ca.B-cb.B);
                total += da; if (da > 36) changed++; samples++;
                luma += ca.R + ca.G + ca.B;
            }
            double meanChannelDelta = samples == 0 ? 0 : (double)total / (samples * 3.0);
            double changedFraction = samples == 0 ? 0 : (double)changed / samples;
            double meanLuma = samples == 0 ? 0 : (double)luma / (samples * 3.0);
            return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "{0:F4}|{1:F4}|{2:F2}|{3}", meanChannelDelta, changedFraction, meanLuma, samples);
        }
    }
}
