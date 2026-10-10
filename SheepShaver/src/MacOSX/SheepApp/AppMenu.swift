/*
 *  AppMenu.swift - The menu bar. Two processes, two menu bars:
 *
 *  - The library (manager) has the full standard set: SheepShaver, File, Edit, View, Window, Help, with the usual
 *    shortcuts (Command-N, Command-O, Command-W, Command-comma, Command-Q …).
 *  - A virtual machine's window has the standard menus but no keyboard shortcuts at all. Mac OS 9 applications
 *    use Command-Q, Command-W, Command-N, Command-comma and the rest themselves, and an application menu shortcut
 *    would swallow them before the guest saw them. Everything is still reachable with the mouse.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import AppKit

@MainActor
enum AppMenu {
    private struct Builder {
        let shortcuts: Bool

        func item(_ title: String, _ action: Selector?, _ key: String = "", _ modifiers: NSEvent.ModifierFlags = .command, target: AnyObject? = nil) -> NSMenuItem {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: shortcuts ? key : "")
            if shortcuts, !key.isEmpty { item.keyEquivalentModifierMask = modifiers }
            item.target = target
            return item
        }

        func menu(_ title: String, _ items: [NSMenuItem]) -> NSMenuItem {
            let container = NSMenuItem(title: title, action: nil, keyEquivalent: "")
            let menu = NSMenu(title: title)
            items.forEach(menu.addItem)
            container.submenu = menu
            return container
        }
    }

    /// The application menu, up to and including Quit. `settings` and `quit` differ between the two processes.
    private static func applicationMenu(_ b: Builder, extra: [NSMenuItem], quit: NSMenuItem) -> NSMenuItem {
        let services = NSMenuItem(title: "Services", action: nil, keyEquivalent: "")
        let servicesMenu = NSMenu(title: "Services")
        services.submenu = servicesMenu
        NSApp.servicesMenu = servicesMenu
        var items: [NSMenuItem] = [b.item("About SheepShaver", #selector(NSApplication.orderFrontStandardAboutPanel(_:)))]
        if !extra.isEmpty { items.append(.separator()); items.append(contentsOf: extra) }
        items += [.separator(), services, .separator(),
                  b.item("Hide SheepShaver", #selector(NSApplication.hide(_:)), "h"),
                  b.item("Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option]),
                  b.item("Show All", #selector(NSApplication.unhideAllApplications(_:))),
                  .separator(), quit]
        return b.menu("SheepShaver", items)
    }

    private static func windowMenu(_ b: Builder) -> NSMenuItem {
        let item = b.menu("Window", [
            // A VM's menu uses miniaturize: (no shortcut of its own); AppKit gives performMiniaturize: Command-M itself
            b.item("Minimize", b.shortcuts ? #selector(NSWindow.performMiniaturize(_:)) : #selector(NSWindow.miniaturize(_:)), "m"),
            b.item("Zoom", #selector(NSWindow.performZoom(_:))),
            .separator(),
            b.item("Bring All to Front", #selector(NSApplication.arrangeInFront(_:))),
        ])
        NSApp.windowsMenu = item.submenu
        if !b.shortcuts {
            // AppKit gives a Window menu its own standard shortcuts (Command-M) once it is the windows menu
            item.submenu?.items.forEach { $0.keyEquivalent = "" }
        }
        return item
    }

    private static func helpMenu(_ b: Builder) -> NSMenuItem {
        let item = b.menu("Help", [])
        NSApp.helpMenu = item.submenu
        return item
    }

    private static func editMenu(_ b: Builder) -> NSMenuItem {
        b.menu("Edit", [
            b.item("Undo", Selector(("undo:")), "z"),
            b.item("Redo", Selector(("redo:")), "z", [.command, .shift]),
            .separator(),
            b.item("Cut", #selector(NSText.cut(_:)), "x"),
            b.item("Copy", #selector(NSText.copy(_:)), "c"),
            b.item("Paste", #selector(NSText.paste(_:)), "v"),
            b.item("Delete", #selector(NSText.delete(_:))),
            b.item("Select All", #selector(NSText.selectAll(_:)), "a"),
        ])
    }

    /// Every menu item as one line ("NW-MENU File / Close  cmd-w"), for the tests (printed with diagnostics on).
    static func describe(_ main: NSMenu) -> [String] {
        var lines: [String] = []
        func walk(_ menu: NSMenu, _ path: String) {
            for item in menu.items where !item.isSeparatorItem {
                let key = item.keyEquivalent.isEmpty ? "" : "  \(item.keyEquivalentModifierMask.contains(.command) ? "cmd-" : "")\(item.keyEquivalent)"
                lines.append("NW-MENU \(path) / \(item.title)\(key)")
                if let sub = item.submenu { walk(sub, path.isEmpty ? item.title : "\(path) / \(item.title)") }
            }
        }
        walk(main, "")
        return lines
    }

    private static func report(_ main: NSMenu) {
        guard nwDiagnosticsOn else { return }
        describe(main).forEach { fputs($0 + "\n", stdout) }
        fflush(stdout)
    }

    /// The library process: the full menu bar.
    static func installManager(_ controller: ManagerWindowController) {
        let b = Builder(shortcuts: true)
        let main = NSMenu()
        let settings = b.item("Settings…", #selector(ManagerWindowController.openAppSettings(_:)), ",", target: controller)
        let quit = b.item("Quit SheepShaver", #selector(ManagerWindowController.quitLibrary(_:)), "q", target: controller)
        main.addItem(applicationMenu(b, extra: [settings], quit: quit))
        main.addItem(b.menu("File", [
            b.item("New Virtual Machine…", #selector(ManagerWindowController.newMachine(_:)), "n", target: controller),
            b.item("Add Existing…", #selector(ManagerWindowController.addExistingMachine(_:)), "o", target: controller),
            .separator(),
            b.item("Start", #selector(ManagerWindowController.startSelected(_:)), "r", target: controller),
            b.item("Shut Down…", #selector(ManagerWindowController.shutDownSelected(_:)), ".", [.command, .shift], target: controller),
            b.item("Settings for This Virtual Machine…", #selector(ManagerWindowController.openMachineSettings(_:)), "i", target: controller),
            .separator(),
            b.item("Rename", #selector(ManagerWindowController.renameSelected(_:)), target: controller),
            b.item("Show in Finder", #selector(ManagerWindowController.showSelectedInFinder(_:)), "r", [.command, .shift], target: controller),
            b.item("Remove from Library…", #selector(ManagerWindowController.removeSelected(_:)), "\u{8}", .command, target: controller),
            .separator(),
            b.item("Close", #selector(NSWindow.performClose(_:)), "w"),
        ]))
        main.addItem(editMenu(b))
        main.addItem(b.menu("View", [
            b.item("Hide Sidebar", #selector(NSSplitViewController.toggleSidebar(_:)), "s", [.command, .control]),
            b.item("Enter Full Screen", #selector(NSWindow.toggleFullScreen(_:)), "f", [.command, .control]),
        ]))
        let guest = b.menu("Guest", [
            b.item("Guest not shown in this window", nil),
            .separator(),
            b.item("Release the Mouse at the Screen Edge", #selector(ManagerWindowController.toggleEdgeRelease(_:)), target: controller),
            b.item("Share the Clipboard", #selector(ManagerWindowController.toggleClipboard(_:)), target: controller),
            .separator(),
            b.item("Detach into Its Own Window", #selector(ManagerWindowController.toggleDetach(_:)), target: controller),
            .separator(),
            b.item("Install Sheep Shears…", #selector(ManagerWindowController.installSheepShears(_:)), target: controller),
            b.item("Shut Down Guest…", #selector(ManagerWindowController.shutDownSelected(_:)), target: controller),
        ])
        guest.submenu?.delegate = controller
        guest.submenu?.items.first?.isEnabled = false
        main.addItem(guest)
        main.addItem(windowMenu(b))
        main.addItem(helpMenu(b))
        NSApp.mainMenu = main
        report(main)
    }

    /// A virtual machine's process: the standard menus without shortcuts. The caller adds its Guest menu.
    static func installVM(_ controller: SheepWindowController) -> NSMenu {
        let b = Builder(shortcuts: false)
        let main = NSMenu()
        let settings = b.item("Virtual Machine Settings…", #selector(SheepWindowController.openSettings(_:)), target: controller)
        let quit = b.item("Close This Virtual Machine", #selector(SheepWindowController.closeMachine(_:)), target: controller)
        main.addItem(applicationMenu(b, extra: [settings], quit: quit))
        main.addItem(b.menu("File", [b.item("Close Window", #selector(NSWindow.performClose(_:)))]))
        var view = [b.item("Enter Full Screen", #selector(NSWindow.toggleFullScreen(_:)))]
        if SheepHost.isEmbedded {
            view.append(.separator())
            view.append(b.item("Attach to Library Window", #selector(SheepWindowController.attachToLibrary(_:)), target: controller))
        }
        main.addItem(b.menu("View", view))
        main.addItem(windowMenu(b))
        main.addItem(helpMenu(b))
        NSApp.mainMenu = main
        clearShortcuts(main)
        return main
    }

    /// AppKit adds standard shortcuts (Command-M for Minimize) when a menu becomes the main menu or the windows
    /// menu; a VM's menus must have none.
    static func clearShortcuts(_ menu: NSMenu) {
        for item in menu.items {
            item.keyEquivalent = ""
            if let sub = item.submenu { clearShortcuts(sub) }
        }
    }

    /// Called once the VM's menu bar is complete (the Guest menu is added after installVM).
    static func reportVM(_ main: NSMenu) { clearShortcuts(main); report(main) }
}
