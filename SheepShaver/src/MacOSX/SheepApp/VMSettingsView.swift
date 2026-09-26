/*
 *  VMSettingsView.swift - Settings sheet. Reads and writes the prefs file.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import SwiftUI

private enum SettingsPage: String, CaseIterable, Identifiable {
    case drives = "Drives"
    case display = "Display"
    case sound = "Sound"
    case system = "System"
    case input = "Input"
    var id: String { rawValue }
}

struct VMSettingsView: View {
    var prefsPath: String?
    var live: Bool
    var onClose: () -> Void

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
    @State private var bootchime = true
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
    @State private var edgegrab = true
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
                    Toggle("bootchime", isOn: $bootchime)
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
                    Toggle("edgegrab", isOn: $edgegrab)
                }
            }
            .formStyle(.grouped)
            .navigationTitle(page.rawValue)
        }
        .onAppear(perform: load)
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") { onClose() }
            }
            ToolbarItem(placement: .confirmationAction) {
                Button("Save") {
                    save()
                    onClose()
                }
            }
        }
    }

    private func load() {
        if live {
            disk = PrefsBridge.string("disk")
            cdrom = PrefsBridge.string("cdrom")
            extfs = PrefsBridge.string("extfs")
            bootdrive = PrefsBridge.int("bootdrive")
            bootdriver = PrefsBridge.int("bootdriver")
            nocdrom = PrefsBridge.bool("nocdrom")
            screen = PrefsBridge.string("screen")
            windowmodes = PrefsBridge.int("windowmodes")
            screenmodes = PrefsBridge.int("screenmodes")
            frameskip = PrefsBridge.int("frameskip")
            gfxaccel = PrefsBridge.bool("gfxaccel")
            sheepforce = PrefsBridge.bool("sheepforce")
            qtcodec = PrefsBridge.bool("qtcodec")
            hardcursor = PrefsBridge.bool("hardcursor")
            scaleNearest = PrefsBridge.bool("scale_nearest")
            scaleInteger = PrefsBridge.bool("scale_integer")
            initGrab = PrefsBridge.bool("init_grab")
            nosound = PrefsBridge.bool("nosound")
            bootchime = PrefsBridge.bool("bootchime")
            soundBuffer = PrefsBridge.int("sound_buffer")
            dsp = PrefsBridge.string("dsp")
            mixer = PrefsBridge.string("mixer")
            ramMB = max(PrefsBridge.int("ramsize") / (1024 * 1024), 1)
            rom = PrefsBridge.string("rom")
            jit = PrefsBridge.bool("jit")
            jit68k = PrefsBridge.bool("jit68k")
            ignoresegv = PrefsBridge.bool("ignoresegv")
            ignoreillegal = PrefsBridge.bool("ignoreillegal")
            cpuclock = PrefsBridge.int("cpuclock")
            yearofs = PrefsBridge.int("yearofs")
            dayofs = PrefsBridge.int("dayofs")
            nogui = PrefsBridge.bool("nogui")
            noclipconversion = PrefsBridge.bool("noclipconversion")
            nonet = PrefsBridge.bool("nonet")
            ether = PrefsBridge.string("ether")
            idlewait = PrefsBridge.bool("idlewait")
            nameEncoding = PrefsBridge.int("name_encoding")
            seriala = PrefsBridge.string("seriala")
            serialb = PrefsBridge.string("serialb")
            keyboardtype = PrefsBridge.int("keyboardtype")
            hotkey = PrefsBridge.int("hotkey")
            swapOptCmd = PrefsBridge.bool("swap_opt_cmd")
            keycodes = PrefsBridge.bool("keycodes")
            keycodefile = PrefsBridge.string("keycodefile")
            mousewheelmode = PrefsBridge.int("mousewheelmode")
            mousewheellines = PrefsBridge.int("mousewheellines")
            edgegrab = PrefsBridge.bool("edgegrab")
            return
        }
        guard let prefsPath else { return }
        let values = PrefsFile.load(prefsPath)
        disk = PrefsFile.string(values, "disk")
        cdrom = PrefsFile.string(values, "cdrom")
        extfs = PrefsFile.string(values, "extfs")
        bootdrive = PrefsFile.int(values, "bootdrive")
        bootdriver = PrefsFile.int(values, "bootdriver")
        nocdrom = PrefsFile.bool(values, "nocdrom")
        screen = PrefsFile.string(values, "screen", "win/1024/768")
        windowmodes = PrefsFile.int(values, "windowmodes")
        screenmodes = PrefsFile.int(values, "screenmodes")
        frameskip = PrefsFile.int(values, "frameskip", 1)
        gfxaccel = PrefsFile.bool(values, "gfxaccel", true)
        sheepforce = PrefsFile.bool(values, "sheepforce", true)
        qtcodec = PrefsFile.bool(values, "qtcodec", true)
        hardcursor = PrefsFile.bool(values, "hardcursor")
        scaleNearest = PrefsFile.bool(values, "scale_nearest")
        scaleInteger = PrefsFile.bool(values, "scale_integer")
        initGrab = PrefsFile.bool(values, "init_grab")
        nosound = PrefsFile.bool(values, "nosound")
        bootchime = PrefsFile.bool(values, "bootchime", true)
        soundBuffer = PrefsFile.int(values, "sound_buffer")
        dsp = PrefsFile.string(values, "dsp", "/dev/dsp")
        mixer = PrefsFile.string(values, "mixer", "/dev/mixer")
        ramMB = max(PrefsFile.int(values, "ramsize", 536870912) / (1024 * 1024), 1)
        rom = PrefsFile.string(values, "rom")
        jit = PrefsFile.bool(values, "jit", true)
        jit68k = PrefsFile.bool(values, "jit68k")
        ignoresegv = PrefsFile.bool(values, "ignoresegv", true)
        ignoreillegal = PrefsFile.bool(values, "ignoreillegal", true)
        cpuclock = PrefsFile.int(values, "cpuclock")
        yearofs = PrefsFile.int(values, "yearofs")
        dayofs = PrefsFile.int(values, "dayofs")
        nogui = PrefsFile.bool(values, "nogui")
        noclipconversion = PrefsFile.bool(values, "noclipconversion")
        nonet = PrefsFile.bool(values, "nonet")
        ether = PrefsFile.string(values, "ether")
        idlewait = PrefsFile.bool(values, "idlewait", true)
        nameEncoding = PrefsFile.int(values, "name_encoding")
        seriala = PrefsFile.string(values, "seriala", "/dev/null")
        serialb = PrefsFile.string(values, "serialb", "/dev/null")
        keyboardtype = PrefsFile.int(values, "keyboardtype", 5)
        hotkey = PrefsFile.int(values, "hotkey")
        swapOptCmd = PrefsFile.bool(values, "swap_opt_cmd")
        keycodes = PrefsFile.bool(values, "keycodes")
        keycodefile = PrefsFile.string(values, "keycodefile")
        mousewheelmode = PrefsFile.int(values, "mousewheelmode", 1)
        mousewheellines = PrefsFile.int(values, "mousewheellines", 3)
        edgegrab = PrefsFile.bool(values, "edgegrab", true)
    }

    private func documentValues() -> [String: String] {
        [
            "disk": disk,
            "cdrom": cdrom,
            "extfs": extfs,
            "bootdrive": "\(bootdrive)",
            "bootdriver": "\(bootdriver)",
            "nocdrom": nocdrom ? "true" : "false",
            "screen": screen,
            "windowmodes": "\(windowmodes)",
            "screenmodes": "\(screenmodes)",
            "frameskip": "\(frameskip)",
            "gfxaccel": gfxaccel ? "true" : "false",
            "sheepforce": sheepforce ? "true" : "false",
            "qtcodec": qtcodec ? "true" : "false",
            "hardcursor": hardcursor ? "true" : "false",
            "scale_nearest": scaleNearest ? "true" : "false",
            "scale_integer": scaleInteger ? "true" : "false",
            "init_grab": initGrab ? "true" : "false",
            "nosound": nosound ? "true" : "false",
            "bootchime": bootchime ? "true" : "false",
            "sound_buffer": "\(soundBuffer)",
            "dsp": dsp,
            "mixer": mixer,
            "ramsize": "\(ramMB * 1024 * 1024)",
            "rom": rom,
            "jit": jit ? "true" : "false",
            "jit68k": jit68k ? "true" : "false",
            "ignoresegv": ignoresegv ? "true" : "false",
            "ignoreillegal": ignoreillegal ? "true" : "false",
            "cpuclock": "\(cpuclock)",
            "yearofs": "\(yearofs)",
            "dayofs": "\(dayofs)",
            "nogui": nogui ? "true" : "false",
            "noclipconversion": noclipconversion ? "true" : "false",
            "nonet": nonet ? "true" : "false",
            "ether": ether,
            "idlewait": idlewait ? "true" : "false",
            "name_encoding": "\(nameEncoding)",
            "seriala": seriala,
            "serialb": serialb,
            "keyboardtype": "\(keyboardtype)",
            "hotkey": "\(hotkey)",
            "swap_opt_cmd": swapOptCmd ? "true" : "false",
            "keycodes": keycodes ? "true" : "false",
            "keycodefile": keycodefile,
            "mousewheelmode": "\(mousewheelmode)",
            "mousewheellines": "\(mousewheellines)",
            "edgegrab": edgegrab ? "true" : "false"
        ]
    }

    private func save() {
        if live {
            PrefsBridge.setString("disk", disk)
            PrefsBridge.setString("cdrom", cdrom)
            PrefsBridge.setString("extfs", extfs)
            PrefsBridge.setInt("bootdrive", bootdrive)
            PrefsBridge.setInt("bootdriver", bootdriver)
            PrefsBridge.setBool("nocdrom", nocdrom)
            PrefsBridge.setString("screen", screen)
            PrefsBridge.setInt("windowmodes", windowmodes)
            PrefsBridge.setInt("screenmodes", screenmodes)
            PrefsBridge.setInt("frameskip", frameskip)
            PrefsBridge.setBool("gfxaccel", gfxaccel)
            PrefsBridge.setBool("sheepforce", sheepforce)
            PrefsBridge.setBool("qtcodec", qtcodec)
            PrefsBridge.setBool("hardcursor", hardcursor)
            PrefsBridge.setBool("scale_nearest", scaleNearest)
            PrefsBridge.setBool("scale_integer", scaleInteger)
            PrefsBridge.setBool("init_grab", initGrab)
            PrefsBridge.setBool("nosound", nosound)
            PrefsBridge.setBool("bootchime", bootchime)
            PrefsBridge.setInt("sound_buffer", soundBuffer)
            PrefsBridge.setString("dsp", dsp)
            PrefsBridge.setString("mixer", mixer)
            PrefsBridge.setInt("ramsize", ramMB * 1024 * 1024)
            PrefsBridge.setString("rom", rom)
            PrefsBridge.setBool("jit", jit)
            PrefsBridge.setBool("jit68k", jit68k)
            PrefsBridge.setBool("ignoresegv", ignoresegv)
            PrefsBridge.setBool("ignoreillegal", ignoreillegal)
            PrefsBridge.setInt("cpuclock", cpuclock)
            PrefsBridge.setInt("yearofs", yearofs)
            PrefsBridge.setInt("dayofs", dayofs)
            PrefsBridge.setBool("nogui", nogui)
            PrefsBridge.setBool("noclipconversion", noclipconversion)
            PrefsBridge.setBool("nonet", nonet)
            PrefsBridge.setString("ether", ether)
            PrefsBridge.setBool("idlewait", idlewait)
            PrefsBridge.setInt("name_encoding", nameEncoding)
            PrefsBridge.setString("seriala", seriala)
            PrefsBridge.setString("serialb", serialb)
            PrefsBridge.setInt("keyboardtype", keyboardtype)
            PrefsBridge.setInt("hotkey", hotkey)
            PrefsBridge.setBool("swap_opt_cmd", swapOptCmd)
            PrefsBridge.setBool("keycodes", keycodes)
            PrefsBridge.setString("keycodefile", keycodefile)
            PrefsBridge.setInt("mousewheelmode", mousewheelmode)
            PrefsBridge.setInt("mousewheellines", mousewheellines)
            PrefsBridge.setBool("edgegrab", edgegrab)
            PrefsBridge.save()
        }
        if let prefsPath {
            PrefsFile.save(prefsPath, documentValues())
        }
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
