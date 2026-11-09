// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

import Foundation

// Chunk lists from the CDN for delta updates, so big files only download the parts that changed
enum CourierChunks
{
	struct Chunk: Codable
	{
		let size: UInt32
		let hash: String
	}

	// One step of the delta plan, local copies from the old file at sourceOffset,
	// remote downloads from the new file on the CDN at offset.
	enum DeltaPart
	{
		case local(sourceOffset: UInt64, size: UInt64)
		case remote(offset: UInt64, size: UInt64)

		var size: UInt64
		{
			switch self
			{
			case .local(_, let size),
			.remote(_, let size): size
			}
		}
	}

	struct ChunkKey: Hashable
	{
		let size: UInt32
		let hash: String
	}

	struct ChunkList: Codable
	{
		// If the plan has to download more than this much of the file, then just download the whole thing instead.
		static let maxDeltaDownloadRatio = 0.8

		let minSize: UInt32
		let avgSize: UInt32
		let maxSize: UInt32
		let chunks: [Chunk]

		func validate(fileSize: UInt64) throws
		{
			if !(FastCDC.minimumMin...FastCDC.minimumMax).contains(minSize)
					   || !(FastCDC.averageMin...FastCDC.averageMax).contains(avgSize)
					   || !(FastCDC.maximumMin...FastCDC.maximumMax).contains(maxSize)
					   || minSize > avgSize
					   || avgSize > maxSize
			{
				throw Err("Chunk list has invalid chunking parameters.")
			}

			let total = try chunks.reduce(UInt64(0))
			{
				total, chunk in
				guard chunk.size > 0 && chunk.size <= maxSize
				else
				{
					throw Err("Chunk list contains an invalid chunk size.")
				}

				return total + UInt64(chunk.size)
			}

			if total != fileSize
			{
				throw Err("Chunk list covers \(total) bytes, expected \(fileSize) bytes.")
			}
		}

		// Chunks the old file with the CDN's settings, then goes through the new file's chunks in order.
		func makeDeltaPlan(localPath: String, onScan: (UInt64) throws -> Void) throws -> [DeltaPart]
		{
			var localChunks: [ChunkKey: UInt64] = [:]
			try FastCDC.chunkFile(atPath: localPath, minSize: Int(minSize), averageSize: Int(avgSize), maxSize: Int(maxSize))
			{
				chunk in
				let key = ChunkKey(size: chunk.size, hash: chunk.hash)
				if localChunks[key] == nil
				{
					localChunks[key] = chunk.offset
				}
				try onScan(UInt64(chunk.size))
			}
			var plan: [DeltaPart] = []
			var offset: UInt64 = 0

			for chunk in chunks
			{
				let size = UInt64(chunk.size)
				if let sourceOffset = localChunks[ChunkKey(size: chunk.size, hash: chunk.hash.lowercased())]
				{
					if case .local(let previousOffset, let previousSize) = plan.last,
					   previousOffset + previousSize == sourceOffset
					{
						plan[plan.count - 1] = .local(sourceOffset: previousOffset, size: previousSize + size)
					}
					else
					{
						plan.append(.local(sourceOffset: sourceOffset, size: size))
					}
				}
				else
				{
					if case .remote(let previousOffset, let previousSize) = plan.last
					{
						plan[plan.count - 1] = .remote(offset: previousOffset, size: previousSize + size)
					}
					else
					{
						plan.append(.remote(offset: offset, size: size))
					}
				}
				offset += size
			}
			return plan
		}

		// Throws if the chunk list is broken, returns nil if it's not worth it (does a full download instead).
		static func loadDeltaPlan(json: Data, expectedSize: UInt64, localPath: String, onScan: (UInt64) throws -> Void) throws -> [DeltaPart]?
		{
			let chunkList: ChunkList
			let decoder = JSONDecoder()
			decoder.keyDecodingStrategy = .convertFromSnakeCase
			do
			{
				try chunkList = decoder.decode(ChunkList.self, from: json)
			}
			catch
			{
				throw Err("Invalid chunk list JSON: \(error)")
			}
			try chunkList.validate(fileSize: expectedSize)
			let plan = try chunkList.makeDeltaPlan(localPath: localPath, onScan: onScan)
			// Add up the size of all the parts that has to be downloaded
			let remoteSize = plan.filter { if case .remote = $0 { true } else { false } }.reduce(0) { $0 + $1.size }
			let notWorthIt = Double(remoteSize) > Double(expectedSize) * maxDeltaDownloadRatio

			if notWorthIt { return nil }
			return plan
		}
	}
}