import Foundation

protocol StreamingFileHasher
{
	mutating func update(_ data: Data)
	mutating func finalizeHex() -> String
}

struct MD5: StreamingFileHasher
{
	private var a0: UInt32 = 0x6745_2301
	private var b0: UInt32 = 0xefcd_ab89
	private var c0: UInt32 = 0x98ba_dcfe
	private var d0: UInt32 = 0x1032_5476

	private var buffer = [UInt8]()
	private var totalLength: UInt64 = 0

	private static let s: [UInt32] =
	[
		7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
		5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
		4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
		6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
	]

	private static let k: [UInt32] =
	[
		0xd76a_a478, 0xe8c7_b756, 0x2420_70db, 0xc1bd_ceee,
		0xf57c_0faf, 0x4787_c62a, 0xa830_4613, 0xfd46_9501,
		0x6980_98d8, 0x8b44_f7af, 0xffff_5bb1, 0x895c_d7be,
		0x6b90_1122, 0xfd98_7193, 0xa679_438e, 0x49b4_0821,
		0xf61e_2562, 0xc040_b340, 0x265e_5a51, 0xe9b6_c7aa,
		0xd62f_105d, 0x0244_1453, 0xd8a1_e681, 0xe7d3_fbc8,
		0x21e1_cde6, 0xc337_07d6, 0xf4d5_0d87, 0x455a_14ed,
		0xa9e3_e905, 0xfcef_a3f8, 0x676f_02d9, 0x8d2a_4c8a,
		0xfffa_3942, 0x8771_f681, 0x6d9d_6122, 0xfde5_380c,
		0xa4be_ea44, 0x4bde_cfa9, 0xf6bb_4b60, 0xbebf_bc70,
		0x289b_7ec6, 0xeaa1_27fa, 0xd4ef_3085, 0x0488_1d05,
		0xd9d4_d039, 0xe6db_99e5, 0x1fa2_7cf8, 0xc4ac_5665,
		0xf429_2244, 0x432a_ff97, 0xab94_23a7, 0xfc93_a039,
		0x655b_59c3, 0x8f0c_cc92, 0xffef_f47d, 0x8584_5dd1,
		0x6fa8_7e4f, 0xfe2c_e6e0, 0xa301_4314, 0x4e08_11a1,
		0xf753_7e82, 0xbd3a_f235, 0x2ad7_d2bb, 0xeb86_d391
	]

	@inline(__always)
	private static func rotl(_ x: UInt32, _ c: UInt32) -> UInt32
	{
		(x << c) | (x >> (32 - c))
	}

	mutating func update(_ data: Data)
	{
		totalLength = totalLength &+ UInt64(data.count)
		buffer.append(contentsOf: data)

		var offset = 0

		while buffer.count - offset >= 64
		{
			processBlock(Array(buffer[offset..<offset + 64]))
			offset += 64
		}

		if offset > 0
		{
			buffer.removeFirst(offset)
		}
	}

	private mutating func processBlock(_ block: [UInt8])
	{
		var m = [UInt32](repeating: 0, count: 16)

		for i in 0..<16
		{
			let j = i * 4
			m[i] = UInt32(block[j])
			| (UInt32(block[j + 1]) << 8)
			| (UInt32(block[j + 2]) << 16)
			| (UInt32(block[j + 3]) << 24)
		}

		var a = a0, b = b0, c = c0, d = d0

		for i in 0..<64
		{
			var f: UInt32
			var g: Int
			switch i
			{
				case 0..<16:
					f = (b & c) | (~b & d); g = i
				case 16..<32:
					f = (d & b) | (~d & c); g = (5 * i + 1) % 16
				case 32..<48:
					f = b ^ c ^ d;          g = (3 * i + 5) % 16
				default:
					f = c ^ (b | ~d);       g = (7 * i) % 16
			}
			f = f &+ a &+ MD5.k[i] &+ m[g]
			a = d
			d = c
			c = b
			b = b &+ MD5.rotl(f, MD5.s[i])
		}

		a0 = a0 &+ a
		b0 = b0 &+ b
		c0 = c0 &+ c
		d0 = d0 &+ d
	}

	mutating func finalizeHex() -> String
	{
		let bitLength = totalLength &* 8

		buffer.append(0x80)

		while buffer.count % 64 != 56
		{
			buffer.append(0)
		}
		for i in 0..<8
		{
			buffer.append(UInt8((bitLength >> (8 * UInt64(i))) & 0xff))
		}

		var offset = 0

		while offset < buffer.count
		{
			processBlock(Array(buffer[offset..<offset + 64]))
			offset += 64
		}

		var digest = [UInt8]()

		for word in [a0, b0, c0, d0]
		{
			digest.append(UInt8(word & 0xff))
			digest.append(UInt8((word >> 8) & 0xff))
			digest.append(UInt8((word >> 16) & 0xff))
			digest.append(UInt8((word >> 24) & 0xff))
		}

		return digest.map { String(format: "%02x", $0) }.joined()
	}
}

struct SHA256: StreamingFileHasher
{
	private var state: [UInt32] = [
		0x6a09_e667, 0xbb67_ae85, 0x3c6e_f372, 0xa54f_f53a,
		0x510e_527f, 0x9b05_688c, 0x1f83_d9ab, 0x5be0_cd19
	]
	private var buffer = [UInt8]()
	private var totalLength: UInt64 = 0

	private static let constants: [UInt32] = [
		0x428a_2f98, 0x7137_4491, 0xb5c0_fbcf, 0xe9b5_dba5,
		0x3956_c25b, 0x59f1_11f1, 0x923f_82a4, 0xab1c_5ed5,
		0xd807_aa98, 0x1283_5b01, 0x2431_85be, 0x550c_7dc3,
		0x72be_5d74, 0x80de_b1fe, 0x9bdc_06a7, 0xc19b_f174,
		0xe49b_69c1, 0xefbe_4786, 0x0fc1_9dc6, 0x240c_a1cc,
		0x2de9_2c6f, 0x4a74_84aa, 0x5cb0_a9dc, 0x76f9_88da,
		0x983e_5152, 0xa831_c66d, 0xb003_27c8, 0xbf59_7fc7,
		0xc6e0_0bf3, 0xd5a7_9147, 0x06ca_6351, 0x1429_2967,
		0x27b7_0a85, 0x2e1b_2138, 0x4d2c_6dfc, 0x5338_0d13,
		0x650a_7354, 0x766a_0abb, 0x81c2_c92e, 0x9272_2c85,
		0xa2bf_e8a1, 0xa81a_664b, 0xc24b_8b70, 0xc76c_51a3,
		0xd192_e819, 0xd699_0624, 0xf40e_3585, 0x106a_a070,
		0x19a4_c116, 0x1e37_6c08, 0x2748_774c, 0x34b0_bcb5,
		0x391c_0cb3, 0x4ed8_aa4a, 0x5b9c_ca4f, 0x682e_6ff3,
		0x748f_82ee, 0x78a5_636f, 0x84c8_7814, 0x8cc7_0208,
		0x90be_fffa, 0xa450_6ceb, 0xbef9_a3f7, 0xc671_78f2
	]

	@inline(__always)
	private static func rotateRight(_ value: UInt32, by count: UInt32) -> UInt32
	{
		(value >> count) | (value << (32 - count))
	}

	mutating func update(_ data: Data)
	{
		totalLength = totalLength &+ UInt64(data.count)
		buffer.append(contentsOf: data)
		var offset = 0
		while buffer.count - offset >= 64
		{
			processBlock(Array(buffer[offset..<offset + 64]))
			offset += 64
		}
		if offset > 0 { buffer.removeFirst(offset) }
	}

	private mutating func processBlock(_ block: [UInt8])
	{
		var words = [UInt32](repeating: 0, count: 64)
		for index in 0..<16
		{
			let offset = index * 4
			words[index] = (
				(UInt32(block[offset]) << 24)
					| (UInt32(block[offset + 1]) << 16)
					| (UInt32(block[offset + 2]) << 8)
					| UInt32(block[offset + 3])
			)
		}
		for index in 16..<64
		{
			let previous15 = words[index - 15]
			let previous2 = words[index - 2]
			let sigma0 = (
				SHA256.rotateRight(previous15, by: 7)
					^ SHA256.rotateRight(previous15, by: 18)
					^ (previous15 >> 3)
			)
			let sigma1 = (
				SHA256.rotateRight(previous2, by: 17)
					^ SHA256.rotateRight(previous2, by: 19)
					^ (previous2 >> 10)
			)
			words[index] = (
				words[index - 16] &+ sigma0
					&+ words[index - 7] &+ sigma1
			)
		}

		var a = state[0], b = state[1], c = state[2], d = state[3]
		var e = state[4], f = state[5], g = state[6], h = state[7]
		for index in 0..<64
		{
			let sum1 = (
				SHA256.rotateRight(e, by: 6)
					^ SHA256.rotateRight(e, by: 11)
					^ SHA256.rotateRight(e, by: 25)
			)
			let choice = (e & f) ^ (~e & g)
			let temporary1 = (
				h &+ sum1 &+ choice
					&+ SHA256.constants[index] &+ words[index]
			)
			let sum0 = (
				SHA256.rotateRight(a, by: 2)
					^ SHA256.rotateRight(a, by: 13)
					^ SHA256.rotateRight(a, by: 22)
			)
			let majority = (a & b) ^ (a & c) ^ (b & c)
			let temporary2 = sum0 &+ majority

			h = g
			g = f
			f = e
			e = d &+ temporary1
			d = c
			c = b
			b = a
			a = temporary1 &+ temporary2
		}

		state[0] = state[0] &+ a
		state[1] = state[1] &+ b
		state[2] = state[2] &+ c
		state[3] = state[3] &+ d
		state[4] = state[4] &+ e
		state[5] = state[5] &+ f
		state[6] = state[6] &+ g
		state[7] = state[7] &+ h
	}

	mutating func finalizeHex() -> String
	{
		let bitLength = totalLength &* 8
		buffer.append(0x80)
		while buffer.count % 64 != 56
		{
			buffer.append(0)
		}
		for shift in stride(from: 56, through: 0, by: -8)
		{
			buffer.append(UInt8((bitLength >> UInt64(shift)) & 0xff))
		}
		while !buffer.isEmpty
		{
			processBlock(Array(buffer.prefix(64)))
			buffer.removeFirst(64)
		}

		var digest = [UInt8]()
		digest.reserveCapacity(32)
		for word in state
		{
			digest.append(UInt8((word >> 24) & 0xff))
			digest.append(UInt8((word >> 16) & 0xff))
			digest.append(UInt8((word >> 8) & 0xff))
			digest.append(UInt8(word & 0xff))
		}
		return digest.map { String(format: "%02x", $0) }.joined()
	}
}
