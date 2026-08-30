import Foundation

private func digest<H: StreamingFileHasher>(
    _ text: String, _ hasher: inout H) -> String
{
    hasher.update(Data(text.utf8))
    return hasher.finalizeHex()
}

@main
struct CourierHashTests
{
    static func main()
    {
        var emptyMD5 = MD5()
        guard digest("", &emptyMD5)
                == "d41d8cd98f00b204e9800998ecf8427e" else {
            fatalError("MD5 empty-string vector failed")
        }

        var abcMD5 = MD5()
        guard digest("abc", &abcMD5)
                == "900150983cd24fb0d6963f7d28e17f72" else {
            fatalError("MD5 abc vector failed")
        }

        var emptySHA256 = SHA256()
        guard digest("", &emptySHA256)
                == "e3b0c44298fc1c149afbf4c8996fb924"
                 + "27ae41e4649b934ca495991b7852b855" else {
            fatalError("SHA-256 empty-string vector failed")
        }

        var abcSHA256 = SHA256()
        guard digest("abc", &abcSHA256)
                == "ba7816bf8f01cfea414140de5dae2223"
                 + "b00361a396177a9cb410ff61f20015ad" else {
            fatalError("SHA-256 abc vector failed")
        }

        let millionAs = Data(repeating: 0x61, count: 1_000_000)
        var chunkedMD5 = MD5()
        var chunkedSHA256 = SHA256()
        var offset = 0
        while offset < millionAs.count {
            let end = min(offset + 7_777, millionAs.count)
            let chunk = millionAs.subdata(in: offset..<end)
            chunkedMD5.update(chunk)
            chunkedSHA256.update(chunk)
            offset = end
        }
        guard chunkedMD5.finalizeHex()
                == "7707d6ae4e027c70eea2a935c2296f21" else {
            fatalError("MD5 chunked million-a vector failed")
        }
        guard chunkedSHA256.finalizeHex()
                == "cdc76e5c9914fb9281a1c7e284d73e67"
                 + "f1809a48a497200e046d39ccc7112cd0" else {
            fatalError("SHA-256 chunked million-a vector failed")
        }
    }
}
