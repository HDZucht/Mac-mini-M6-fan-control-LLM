// Fan Guard menu bar item: temperature of the hottest deciding sensor and fan speed, plus a menu to switch the
// service mode. Reads through the bundled `fanguard line` (no root needed) and only ever writes the mode file.
// Written by Argus (Claude Opus 5.5) for Hans-Dieter Zucht, October 2026. MIT License.
import AppKit
import ServiceManagement

let modeFile = "/Users/Shared/mac-fan-guard/mode"
let reader = Bundle.main.bundlePath + "/Contents/MacOS/fanguard"

final class App: NSObject, NSApplicationDelegate, NSMenuDelegate {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    let menu = NSMenu()
    let infoLine = NSMenuItem(title: "…", action: nil, keyEquivalent: "")
    let modes: [(String, String)] = [("curve", "Curve (60 → 85 °C)"), ("auto", "Apple automatic"), ("50", "At least 50 %"), ("100", "Full speed (100 %)")]
    var modeItems: [NSMenuItem] = []
    var currentMode = "curve"
    let font = NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .regular)

    func applicationDidFinishLaunching(_ n: Notification) {
        menu.delegate = self
        menu.addItem(infoLine)
        menu.addItem(.separator())
        for (value, title) in modes {
            let mi = NSMenuItem(title: title, action: #selector(setMode(_:)), keyEquivalent: "")
            mi.target = self; mi.representedObject = value
            menu.addItem(mi); modeItems.append(mi)
        }
        menu.addItem(.separator())
        let log = NSMenuItem(title: "Open log", action: #selector(openLog), keyEquivalent: "l"); log.target = self; menu.addItem(log)
        menu.addItem(NSMenuItem(title: "Quit Fan Guard menu", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        item.menu = menu
        // Register as a login item (System Settings → General → Login Items).
        if SMAppService.mainApp.status != .enabled { try? SMAppService.mainApp.register() }
        refresh()
        Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in self?.refresh() }
    }

    func readLine() -> [String]? {
        let p = Process(); p.executableURL = URL(fileURLWithPath: reader); p.arguments = ["line"]
        let pipe = Pipe(); p.standardOutput = pipe
        do { try p.run() } catch { return nil }
        p.waitUntilExit()
        let s = String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        let f = s.split(separator: " ").map { String($0).trimmingCharacters(in: .whitespacesAndNewlines) }
        return f.count >= 5 ? f : nil
    }

    func refresh() {
        guard let f = readLine(), let t = Double(f[1]), let rpm = Double(f[2]) else { item.button?.title = "🌡 ?"; return }
        currentMode = f[4]
        let color: NSColor = t >= 85 ? .systemRed : (t >= 70 ? .systemOrange : .labelColor)
        let suffix = currentMode == "curve" ? "" : (currentMode == "auto" ? " A" : " \(currentMode)%")
        item.button?.attributedTitle = NSAttributedString(string: String(format: "%.0f° · %.0f%@", t, rpm, suffix),
                                                          attributes: [.foregroundColor: color, .font: font])
        infoLine.title = String(format: "\(f[0]) %.1f °C · fan %.0f rpm (%@) · mode: %@", t, rpm, f[3], currentMode)
        for mi in modeItems { mi.state = (mi.representedObject as? String) == currentMode ? .on : .off }
    }

    func menuWillOpen(_ m: NSMenu) { refresh() }

    @objc func setMode(_ sender: NSMenuItem) {
        guard let value = sender.representedObject as? String else { return }
        try? (value + "\n").write(toFile: modeFile, atomically: false, encoding: .utf8)
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { self.refresh() }
    }

    @objc func openLog() { NSWorkspace.shared.open(URL(fileURLWithPath: "/var/log/mac-fan-guard.log")) }
}

let app = NSApplication.shared
let delegate = App()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
