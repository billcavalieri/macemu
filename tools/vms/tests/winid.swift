// winid <pid> <title fragment>: prints the window number of that process's window (larger than a dialog) whose title
// contains the fragment (any window when it is empty) and that is on screen (a closed window is not), for `screencapture -l` (captures only that window, never the rest of the screen).
import CoreGraphics
import Foundation
let pid = Int32(CommandLine.arguments[1])!
let title = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : ""
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
for w in list where (w[kCGWindowOwnerPID as String] as? Int32) == pid && (w[kCGWindowLayer as String] as? Int) == 0 && (w[kCGWindowIsOnscreen as String] as? Bool) == true {
    let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    if (b["Width"] as? Double ?? 0) > 400, (b["Height"] as? Double ?? 0) > 300, title.isEmpty || (w[kCGWindowName as String] as? String ?? "").contains(title) {
        print(w[kCGWindowNumber as String] as! Int)
        break
    }
}
