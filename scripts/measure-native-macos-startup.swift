#!/usr/bin/env swift
import ApplicationServices
import Darwin
import Foundation

struct MeasurementFailure: Error, CustomStringConvertible {
    let description: String
    init(_ description: String) { self.description = description }
}

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var result: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, name as CFString, &result) == .success ? result : nil
}

func children(_ element: AXUIElement) -> [AXUIElement] {
    (attribute(element, kAXChildrenAttribute) as? [AXUIElement]) ?? []
}

func measure(_ executable: URL) throws -> (total: Double, launch: Double, settingsVisible: Double) {
    let profile = FileManager.default.temporaryDirectory.appendingPathComponent("podlord-startup-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: profile, withIntermediateDirectories: true)
    let process = Process()
    process.executableURL = executable
    process.arguments = ["--profile", profile.path]
    var started = false
    defer {
        if started {
            if process.isRunning { process.terminate() }
            let deadline = ProcessInfo.processInfo.systemUptime + 2
            while process.isRunning && ProcessInfo.processInfo.systemUptime < deadline {
                Thread.sleep(forTimeInterval: 0.01)
            }
            if process.isRunning { _ = kill(process.processIdentifier, SIGKILL) }
            process.waitUntilExit()
        }
        try? FileManager.default.removeItem(at: profile)
    }

    let start = ProcessInfo.processInfo.systemUptime
    try process.run()
    let launched = ProcessInfo.processInfo.systemUptime
    started = true
    let app = AXUIElementCreateApplication(process.processIdentifier)
    AXUIElementSetMessagingTimeout(app, 0.5)
    func windows() -> [AXUIElement] {
        (attribute(app, kAXWindowsAttribute) as? [AXUIElement]) ?? []
    }
    let deadline = start + 5
    var settings: AXUIElement?
    while settings == nil && process.isRunning && ProcessInfo.processInfo.systemUptime < deadline {
        for window in windows() {
            settings = children(window).first { (attribute($0, kAXTitleAttribute) as? String) == "SETTINGS" }
            if settings != nil { break }
        }
        if settings == nil { Thread.sleep(forTimeInterval: 0.01) }
    }
    guard let settings else { throw MeasurementFailure("Settings was not accessible within five seconds.") }
    let settingsVisible = ProcessInfo.processInfo.systemUptime
    guard AXUIElementPerformAction(settings, kAXPressAction as CFString) == .success else {
        throw MeasurementFailure("Settings did not accept an accessibility press.")
    }
    while process.isRunning && ProcessInfo.processInfo.systemUptime < deadline {
        if windows().flatMap(children).contains(where: { (attribute($0, kAXTitleAttribute) as? String) == "Appearance" }) {
            return ((ProcessInfo.processInfo.systemUptime - start) * 1000,
                    (launched - start) * 1000, (settingsVisible - start) * 1000)
        }
        Thread.sleep(forTimeInterval: 0.01)
    }
    throw MeasurementFailure("Settings did not become interactive within five seconds.")
}

do {
    guard CommandLine.arguments.count == 2, CommandLine.arguments[1].hasPrefix("/"),
          CommandLine.arguments[1].hasSuffix(".app") else {
        throw MeasurementFailure("Usage: swift scripts/measure-native-macos-startup.swift /absolute/podlord-native.app")
    }
    let executable = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent("Contents/MacOS/podlord-native")
    guard FileManager.default.isExecutableFile(atPath: executable.path) else {
        throw MeasurementFailure("The packaged Podlord executable is missing.")
    }
    guard AXIsProcessTrusted() else {
        throw MeasurementFailure("Accessibility access is required for this benchmark; no permission prompt was opened.")
    }
    var samples = [Double]()
    for index in 1...10 {
        let timing = try measure(executable)
        samples.append(timing.total)
        print(String(format: "startup %d: %.1f ms (process launch %.1f ms, Settings visible %.1f ms)",
                     index, timing.total, timing.launch, timing.settingsVisible))
    }
    let sorted = samples.sorted()
    print(String(format: "installed Settings interaction: p50=%.1f ms p95=%.1f ms max=%.1f ms; target=1000 ms",
                 sorted[4], sorted[9], sorted[9]))
    guard sorted[9] <= 1000 else { throw MeasurementFailure("The installed startup target was exceeded.") }
} catch {
    fputs("Startup measurement failed: \(error)\n", stderr)
    exit(1)
}
