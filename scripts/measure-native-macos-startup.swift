#!/usr/bin/env swift
import ApplicationServices
import AppKit
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

@MainActor
func measure(_ bundle: URL) async throws -> (total: Double, launch: Double, settingsVisible: Double) {
    let profile = FileManager.default.temporaryDirectory.appendingPathComponent("podlord-startup-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: profile, withIntermediateDirectories: true)
    var application: NSRunningApplication?
    defer {
        if let application {
            let pid = application.processIdentifier
            if !application.isTerminated { _ = kill(pid, SIGTERM) }
            let deadline = ProcessInfo.processInfo.systemUptime + 2
            while kill(pid, 0) == 0 && ProcessInfo.processInfo.systemUptime < deadline {
                Thread.sleep(forTimeInterval: 0.01)
            }
            if kill(pid, 0) == 0 { _ = kill(pid, SIGKILL) }
        }
        try? FileManager.default.removeItem(at: profile)
    }

    let start = ProcessInfo.processInfo.systemUptime
    let configuration = NSWorkspace.OpenConfiguration()
    configuration.arguments = ["--profile", profile.path]
    configuration.environment = ["HOME": profile.path, "CFFIXED_USER_HOME": profile.path,
                                 "PODLORD_DISABLE_UPDATE_CHECK": "1", "PODLORD_DISABLE_AUDIO": "1"]
    configuration.activates = true
    configuration.createsNewApplicationInstance = true
    let process = try await NSWorkspace.shared.openApplication(at: bundle, configuration: configuration)
    application = process
    let launched = ProcessInfo.processInfo.systemUptime
    let app = AXUIElementCreateApplication(process.processIdentifier)
    AXUIElementSetMessagingTimeout(app, 0.5)
    func windows() -> [AXUIElement] {
        (attribute(app, kAXWindowsAttribute) as? [AXUIElement]) ?? []
    }
    let deadline = start + 5
    var settings: AXUIElement?
    while settings == nil && !process.isTerminated && ProcessInfo.processInfo.systemUptime < deadline {
        for window in windows() {
            settings = children(window).first { (attribute($0, kAXIdentifierAttribute) as? String)?.hasSuffix("settingsWorkspaceButton") == true }
            if settings != nil { break }
        }
        if settings == nil { try await Task.sleep(nanoseconds: 10_000_000) }
    }
    guard let settings else { throw MeasurementFailure("Settings was not accessible within five seconds.") }
    let settingsVisible = ProcessInfo.processInfo.systemUptime
    guard AXUIElementPerformAction(settings, kAXPressAction as CFString) == .success else {
        throw MeasurementFailure("Settings did not accept an accessibility press.")
    }
    while !process.isTerminated && ProcessInfo.processInfo.systemUptime < deadline {
        if windows().flatMap(children).contains(where: { (attribute($0, kAXIdentifierAttribute) as? String)?.hasSuffix("settingsAppearanceSection") == true }) {
            return ((ProcessInfo.processInfo.systemUptime - start) * 1000,
                    (launched - start) * 1000, (settingsVisible - start) * 1000)
        }
        try await Task.sleep(nanoseconds: 10_000_000)
    }
    throw MeasurementFailure("Settings did not become interactive within five seconds.")
}

Task { @MainActor in
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
        let timing = try await measure(URL(fileURLWithPath: CommandLine.arguments[1]))
        samples.append(timing.total)
        print(String(format: "startup %d: %.1f ms (process launch %.1f ms, Settings visible %.1f ms)",
                     index, timing.total, timing.launch, timing.settingsVisible))
    }
    let sorted = samples.sorted()
    let repeatMax = samples.dropFirst().max() ?? .infinity
    print(String(format: "installed Settings interaction: p50=%.1f ms p95=%.1f ms first=%.1f ms repeat max=%.1f ms; targets: first<=1500 ms, repeat<=1000 ms",
                 sorted[4], sorted[9], samples[0], repeatMax))
    guard samples[0] <= 1500, repeatMax <= 1000 else {
        throw MeasurementFailure("The installed startup target was exceeded.")
    }
    exit(0)
} catch {
    fputs("Startup measurement failed: \(error)\n", stderr)
    exit(1)
}
}
RunLoop.main.run()
