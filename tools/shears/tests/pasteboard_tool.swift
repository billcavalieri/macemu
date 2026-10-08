// Host pasteboard helper for tools/shears/test.sh clip.
//   pasteboard_tool save <file>      write every item and flavor of the general pasteboard to a file
//   pasteboard_tool restore <file>   put them back
//   pasteboard_tool set <text>       replace the pasteboard with this text
//   pasteboard_tool get              print the pasteboard's text (UTF-8), nothing if it has none
//   pasteboard_tool count            print the change count
import AppKit

let pb = NSPasteboard.general
let args = CommandLine.arguments
guard args.count >= 2 else { fputs("usage: pasteboard_tool save|restore|set|get|count [arg]\n", stderr); exit(2) }
switch args[1] {
case "save":
    var items: [[String: Data]] = []
    for item in pb.pasteboardItems ?? [] {
        var d: [String: Data] = [:]
        for t in item.types { if let data = item.data(forType: t) { d[t.rawValue] = data } }
        items.append(d)
    }
    let data = try NSKeyedArchiver.archivedData(withRootObject: items, requiringSecureCoding: false)
    try data.write(to: URL(fileURLWithPath: args[2]))
case "restore":
    let data = try Data(contentsOf: URL(fileURLWithPath: args[2]))
    let items = try NSKeyedUnarchiver.unarchivedObject(ofClasses: [NSArray.self, NSDictionary.self, NSString.self, NSData.self], from: data) as? [[String: Data]] ?? []
    pb.clearContents()
    let objs: [NSPasteboardItem] = items.map { d in
        let it = NSPasteboardItem()
        for (t, v) in d { it.setData(v, forType: NSPasteboard.PasteboardType(t)) }
        return it
    }
    if !objs.isEmpty { pb.writeObjects(objs) }
case "set":
    pb.clearContents()
    pb.setString(args[2], forType: .string)
case "get":
    if let s = pb.string(forType: .string) { FileHandle.standardOutput.write(s.data(using: .utf8)!) }
case "count":
    print(pb.changeCount)
default:
    exit(2)
}
