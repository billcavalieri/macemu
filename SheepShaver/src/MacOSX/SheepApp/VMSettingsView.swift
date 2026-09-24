/*
 *  VMSettingsView.swift - Settings sheet. Reads and writes the prefs file.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import SwiftUI

@_silgen_name("SheepPrefsGetString")
private func SheepPrefsGetString(_ key: UnsafePointer<CChar>) -> UnsafePointer<CChar>
@_silgen_name("SheepPrefsSetString")
private func SheepPrefsSetString(_ key: UnsafePointer<CChar>, _ value: UnsafePointer<CChar>)
@_silgen_name("SheepPrefsGetBool")
private func SheepPrefsGetBool(_ key: UnsafePointer<CChar>) -> Int32
@_silgen_name("SheepPrefsSetBool")
private func SheepPrefsSetBool(_ key: UnsafePointer<CChar>, _ value: Int32)
@_silgen_name("SheepPrefsGetInt")
private func SheepPrefsGetInt(_ key: UnsafePointer<CChar>) -> Int32
@_silgen_name("SheepPrefsSetInt")
private func SheepPrefsSetInt(_ key: UnsafePointer<CChar>, _ value: Int32)
@_silgen_name("SheepPrefsSave")
private func SheepPrefsSave()

private func prefString(_ key: String) -> String {
    key.withCString { String(cString: SheepPrefsGetString($0)) }
}

private func setPrefString(_ key: String, _ value: String) {
    key.withCString { keyPtr in
        value.withCString { SheepPrefsSetString(keyPtr, $0) }
    }
}

private func prefBool(_ key: String) -> Bool {
    key.withCString { SheepPrefsGetBool($0) != 0 }
}

private func setPrefBool(_ key: String, _ value: Bool) {
    key.withCString { SheepPrefsSetBool($0, value ? 1 : 0) }
}

private func prefInt(_ key: String) -> Int {
    key.withCString { Int(SheepPrefsGetInt($0)) }
}

private func setPrefInt(_ key: String, _ value: Int) {
    key.withCString { SheepPrefsSetInt($0, Int32(value)) }
}

private enum SettingsPage: String, CaseIterable, Identifiable {
    case drives = "Drives"
    case display = "Display"
    case sound = "Sound"
    case system = "System"
    case input = "Input"
    var id: String { rawValue }
}

struct VMSettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var page: SettingsPage = .drives

    @State private var disk = ""
    @State private var cdrom = ""
    @State private var extfs = ""
    @State private var bootdrive = 0
    @State private var bootdriver = 0
    @State private var nocdrom = false

    @State private var screen = "win/1024/768"
    @State private var windowmodes = 0
    @State private var screenmodes = 0
    @State private var frameskip = 1
    @State private var gfxaccel = true
    @State private var sheepforce = true
    @State private var qtcodec = true
    @State private var hardcursor = false
    @State private var scaleNearest = false
    @State private var scaleInteger = false
    @State private var initGrab = false

    @State private var nosound = false
    @State private var soundBuffer = 0
    @State private var dsp = "/dev/dsp"
    @State private var mixer = "/dev/mixer"

    @State private var ramMB = 512
    @State private var rom = ""
    @State private var jit = true
    @State private var jit68k = false
    @State private var ignoresegv = true
    @State private var ignoreillegal = true
    @State private var cpuclock = 0
    @State private var yearofs = 0
    @State private var dayofs = 0
    @State private var nogui = false
    @State private var noclipconversion = false
    @State private var nonet = false
    @State private var ether = ""
    @State private var idlewait = true

    @State private var seriala = "/dev/null"
    @State private var serialb = "/dev/null"
    @State private var keyboardtype = 5
    @State private var hotkey = 0
    @State private var swapOptCmd = false
    @State private var keycodes = false
    @State private var keycodefile = ""
    @State private var mousewheelmode = 1
    @State private var mousewheellines = 3
    @State private var nameEncoding = 0

    var body: some View {
        NavigationSplitView {
            List(SettingsPage.allCases, selection: $page) { item in
                Label(item.rawValue, systemImage: icon(item))
                    .tag(item)
            }
            .navigationSplitViewColumnWidth(min: 180, ideal: 200)
        } detail: {
            Form {
                switch page {
                case .drives:
                    TextField("disk", text: $disk)
                    TextField("cdrom", text: $cdrom)
                    TextField("extfs", text: $extfs)
                    TextField("bootdrive", value: $bootdrive, format: .number)
                    TextField("bootdriver", value: $bootdriver, format: .number)
                    Toggle("nocdrom", isOn: $nocdrom)
                case .display:
                    TextField("screen", text: $screen)
                    TextField("windowmodes", value: $windowmodes, format: .number)
                    TextField("screenmodes", value: $screenmodes, format: .number)
                    TextField("frameskip", value: $frameskip, format: .number)
                    Toggle("gfxaccel", isOn: $gfxaccel)
                    Toggle("sheepforce", isOn: $sheepforce)
                    Toggle("qtcodec", isOn: $qtcodec)
                    Toggle("hardcursor", isOn: $hardcursor)
                    Toggle("scale_nearest", isOn: $scaleNearest)
                    Toggle("scale_integer", isOn: $scaleInteger)
                    Toggle("init_grab", isOn: $initGrab)
                case .sound:
                    Toggle("nosound", isOn: $nosound)
                    TextField("sound_buffer", value: $soundBuffer, format: .number)
                    TextField("dsp", text: $dsp)
                    TextField("mixer", text: $mixer)
                case .system:
                    TextField("ramsize (MB)", value: $ramMB, format: .number)
                    TextField("rom", text: $rom)
                    Toggle("jit", isOn: $jit)
                    Toggle("jit68k", isOn: $jit68k)
                    Toggle("ignoresegv", isOn: $ignoresegv)
                    Toggle("ignoreillegal", isOn: $ignoreillegal)
                    TextField("cpuclock", value: $cpuclock, format: .number)
                    TextField("yearofs", value: $yearofs, format: .number)
                    TextField("dayofs", value: $dayofs, format: .number)
                    Toggle("nogui", isOn: $nogui)
                    Toggle("noclipconversion", isOn: $noclipconversion)
                    Toggle("nonet", isOn: $nonet)
                    TextField("ether", text: $ether)
                    Toggle("idlewait", isOn: $idlewait)
                    TextField("name_encoding", value: $nameEncoding, format: .number)
                case .input:
                    TextField("seriala", text: $seriala)
                    TextField("serialb", text: $serialb)
                    TextField("keyboardtype", value: $keyboardtype, format: .number)
                    TextField("hotkey", value: $hotkey, format: .number)
                    Toggle("swap_opt_cmd", isOn: $swapOptCmd)
                    Toggle("keycodes", isOn: $keycodes)
                    TextField("keycodefile", text: $keycodefile)
                    TextField("mousewheelmode", value: $mousewheelmode, format: .number)
                    TextField("mousewheellines", value: $mousewheellines, format: .number)
                }
            }
            .formStyle(.grouped)
            .navigationTitle(page.rawValue)
        }
        .onAppear(perform: load)
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") { dismiss() }
            }
            ToolbarItem(placement: .confirmationAction) {
                Button("Save") {
                    save()
                    dismiss()
                }
            }
        }
    }

    private func load() {
        disk = prefString("disk")
        cdrom = prefString("cdrom")
        extfs = prefString("extfs")
        bootdrive = prefInt("bootdrive")
        bootdriver = prefInt("bootdriver")
        nocdrom = prefBool("nocdrom")
        screen = prefString("screen")
        windowmodes = prefInt("windowmodes")
        screenmodes = prefInt("screenmodes")
        frameskip = prefInt("frameskip")
        gfxaccel = prefBool("gfxaccel")
        sheepforce = prefBool("sheepforce")
        qtcodec = prefBool("qtcodec")
        hardcursor = prefBool("hardcursor")
        scaleNearest = prefBool("scale_nearest")
        scaleInteger = prefBool("scale_integer")
        initGrab = prefBool("init_grab")
        nosound = prefBool("nosound")
        soundBuffer = prefInt("sound_buffer")
        dsp = prefString("dsp")
        mixer = prefString("mixer")
        ramMB = max(prefInt("ramsize") / (1024 * 1024), 1)
        rom = prefString("rom")
        jit = prefBool("jit")
        jit68k = prefBool("jit68k")
        ignoresegv = prefBool("ignoresegv")
        ignoreillegal = prefBool("ignoreillegal")
        cpuclock = prefInt("cpuclock")
        yearofs = prefInt("yearofs")
        dayofs = prefInt("dayofs")
        nogui = prefBool("nogui")
        noclipconversion = prefBool("noclipconversion")
        nonet = prefBool("nonet")
        ether = prefString("ether")
        idlewait = prefBool("idlewait")
        nameEncoding = prefInt("name_encoding")
        seriala = prefString("seriala")
        serialb = prefString("serialb")
        keyboardtype = prefInt("keyboardtype")
        hotkey = prefInt("hotkey")
        swapOptCmd = prefBool("swap_opt_cmd")
        keycodes = prefBool("keycodes")
        keycodefile = prefString("keycodefile")
        mousewheelmode = prefInt("mousewheelmode")
        mousewheellines = prefInt("mousewheellines")
    }

    private func save() {
        setPrefString("disk", disk)
        setPrefString("cdrom", cdrom)
        setPrefString("extfs", extfs)
        setPrefInt("bootdrive", bootdrive)
        setPrefInt("bootdriver", bootdriver)
        setPrefBool("nocdrom", nocdrom)
        setPrefString("screen", screen)
        setPrefInt("windowmodes", windowmodes)
        setPrefInt("screenmodes", screenmodes)
        setPrefInt("frameskip", frameskip)
        setPrefBool("gfxaccel", gfxaccel)
        setPrefBool("sheepforce", sheepforce)
        setPrefBool("qtcodec", qtcodec)
        setPrefBool("hardcursor", hardcursor)
        setPrefBool("scale_nearest", scaleNearest)
        setPrefBool("scale_integer", scaleInteger)
        setPrefBool("init_grab", initGrab)
        setPrefBool("nosound", nosound)
        setPrefInt("sound_buffer", soundBuffer)
        setPrefString("dsp", dsp)
        setPrefString("mixer", mixer)
        setPrefInt("ramsize", ramMB * 1024 * 1024)
        setPrefString("rom", rom)
        setPrefBool("jit", jit)
        setPrefBool("jit68k", jit68k)
        setPrefBool("ignoresegv", ignoresegv)
        setPrefBool("ignoreillegal", ignoreillegal)
        setPrefInt("cpuclock", cpuclock)
        setPrefInt("yearofs", yearofs)
        setPrefInt("dayofs", dayofs)
        setPrefBool("nogui", nogui)
        setPrefBool("noclipconversion", noclipconversion)
        setPrefBool("nonet", nonet)
        setPrefString("ether", ether)
        setPrefBool("idlewait", idlewait)
        setPrefInt("name_encoding", nameEncoding)
        setPrefString("seriala", seriala)
        setPrefString("serialb", serialb)
        setPrefInt("keyboardtype", keyboardtype)
        setPrefInt("hotkey", hotkey)
        setPrefBool("swap_opt_cmd", swapOptCmd)
        setPrefBool("keycodes", keycodes)
        setPrefString("keycodefile", keycodefile)
        setPrefInt("mousewheelmode", mousewheelmode)
        setPrefInt("mousewheellines", mousewheellines)
        SheepPrefsSave()
    }

    private func icon(_ page: SettingsPage) -> String {
        switch page {
        case .drives: return "externaldrive"
        case .display: return "display"
        case .sound: return "speaker.wave.2"
        case .system: return "cpu"
        case .input: return "keyboard"
        }
    }
}
