// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Caching.Memory;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for an implementation of <see cref="IStorageClient"/>, providing implementations for some common functionality.
	/// </summary>
	public abstract class StorageClientBase : IStorageClient
	{
		class RangedReadStream : Stream, IDisposable
		{
			readonly Stream _inner;
			readonly int _length;

			int _position;

			public RangedReadStream(Stream inner, int length)
			{
				_inner = inner;
				_length = length;
			}

			/// <inheritdoc/>
			public override bool CanRead => true;

			/// <inheritdoc/>
			public override bool CanSeek => false;

			/// <inheritdoc/>
			public override bool CanWrite => false;

			/// <inheritdoc/>
			public override long Length => throw new NotSupportedException();

			/// <inheritdoc/>
			public override long Position { get => _position; set => throw new NotSupportedException(); }

			/// <inheritdoc/>
			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				if (disposing)
				{
					_inner.Dispose();
				}
			}

			/// <inheritdoc/>
			public override void Flush() => _inner.Flush();

			/// <inheritdoc/>
			public override int Read(Span<byte> buffer)
			{
				buffer = buffer.Slice(0, Math.Min(buffer.Length, _length - _position));
				int read = _inner.Read(buffer);
				_position += read;
				return read;
			}

			/// <inheritdoc/>
			public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));

			/// <inheritdoc/>
			public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
			{
				buffer = buffer.Slice(0, Math.Min(buffer.Length, _length - _position));
				int read = await _inner.ReadAsync(buffer, cancellationToken);
				_position += read;
				return read;
			}

			/// <inheritdoc/>
			public override async Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken) => await ReadAsync(buffer.AsMemory(offset, count), cancellationToken);

			/// <inheritdoc/>
			public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();

			/// <inheritdoc/>
			public override void SetLength(long value) => throw new NotSupportedException();

			/// <inheritdoc/>
			public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
		}

		/// <summary>
		/// Describes a bundle export
		/// </summary>
		/// <param name="PacketIdx">Index of the packet within a bundle</param>
		/// <param name="Offset">Offset within the packet</param>
		record struct ExportInfo(int PacketIdx, int Offset);

		/// <summary>
		/// Computed information about a bundle
		/// </summary>
		class BundleInfo
		{
			public readonly BlobLocator Locator;
			public readonly BundleHeader Header;
			public readonly int[] PacketOffsets;
			public readonly ExportInfo[] Exports;
			public readonly (IoHash, NodeLocator)[] References;

			public BundleInfo(BlobLocator locator, BundleHeader header, int headerLength)
			{
				Locator = locator;
				Header = header;

				PacketOffsets = new int[header.Packets.Count];
				Exports = new ExportInfo[header.Exports.Count];

				int exportIdx = 0;
				int packetOffset = headerLength;
				for (int packetIdx = 0; packetIdx < header.Packets.Count; packetIdx++)
				{
					BundlePacket packet = header.Packets[packetIdx];
					PacketOffsets[packetIdx] = packetOffset;

					int nodeOffset = 0;
					for (; exportIdx < header.Exports.Count && nodeOffset + header.Exports[exportIdx].Length <= packet.DecodedLength; exportIdx++)
					{
						Exports[exportIdx] = new ExportInfo(packetIdx, nodeOffset);
						nodeOffset += header.Exports[exportIdx].Length;
					}

					packetOffset += packet.EncodedLength;
				}

				References = new (IoHash, NodeLocator)[header.Imports.Sum(x => x.Exports.Count) + header.Exports.Count];

				int referenceIdx = 0;
				foreach (BundleImport import in header.Imports)
				{
					foreach ((int importExportIdx, IoHash importExportHash) in import.Exports)
					{
						NodeLocator importLocator = new NodeLocator(import.Locator, importExportIdx);
						References[referenceIdx++] = (importExportHash, importLocator);
					}
				}
				for(int idx = 0; idx < header.Exports.Count; idx++)
				{
					References[referenceIdx++] = (header.Exports[idx].Hash, new NodeLocator(locator, idx));
				}
			}
		}

		readonly IMemoryCache _cache;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="cache"></param>
		protected StorageClientBase(IMemoryCache cache)
		{
			_cache = cache;
		}

		#region Blobs

		/// <inheritdoc/>
		public abstract Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default)
		{
			Stream stream = await ReadBlobAsync(locator, cancellationToken);
			stream.Seek(offset, SeekOrigin.Begin);
			return new RangedReadStream(stream, Math.Min((int)(stream.Length - offset), length));
		}

		async Task<Memory<byte>> ReadBlobRangeAsync(BlobLocator locator, int offset, Memory<byte> memory, CancellationToken cancellationToken = default)
		{
			using (Stream stream = await ReadBlobRangeAsync(locator, offset, memory.Length, cancellationToken))
			{
				int length = 0;
				while (length < memory.Length)
				{
					int readBytes = await stream.ReadAsync(memory.Slice(length), cancellationToken);
					if (readBytes == 0)
					{
						break;
					}
					length += readBytes;
				}
				return memory.Slice(0, length);
			}
		}

		/// <inheritdoc/>
		public abstract Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Bundles

		static string GetBundleInfoCacheKey(BlobId blobId) => $"bundle:{blobId}";
		static string GetEncodedPacketCacheKey(BlobId blobId, int packetIdx) => $"encoded-packet:{blobId}#{packetIdx}";
		static string GetDecodedPacketCacheKey(BlobId blobId, int packetIdx) => $"decoded-packet:{blobId}#{packetIdx}";

		void AddToCache(string cacheKey, object value, int size)
		{
			using (ICacheEntry entry = _cache.CreateEntry(cacheKey))
			{
				entry.SetValue(value);
				entry.SetSize(size);
			}
		}

		/// <summary>
		/// Reads a bundle from the given blob id, or retrieves it from the cache
		/// </summary>
		/// <param name="locator"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<Bundle> ReadBundleAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			using (Stream stream = await ReadBlobAsync(locator, cancellationToken))
			{
				return await Bundle.FromStreamAsync(stream, cancellationToken);
			}
		}

		/// <summary>
		/// Reads the header and structural metadata about the bundle
		/// </summary>
		/// <param name="locator">The bundle location</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about the bundle</returns>
		async ValueTask<BundleInfo> GetBundleInfoAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			string cacheKey = GetBundleInfoCacheKey(locator.BlobId);
			if (!_cache.TryGetValue(cacheKey, out BundleInfo bundleInfo))
			{
				int prefetchSize = 1024 * 1024;
				for (; ; )
				{
					using (IMemoryOwner<byte> owner = MemoryPool<byte>.Shared.Rent(prefetchSize))
					{
						// Read the prefetch size from the blob
						Memory<byte> memory = owner.Memory.Slice(0, prefetchSize);
						memory = await ReadBlobRangeAsync(locator, 0, memory, cancellationToken);

						// Make sure it's large enough to hold the header
						int headerSize = BundleHeader.ReadPrelude(memory);
						if (headerSize > prefetchSize)
						{
							prefetchSize = headerSize;
							continue;
						}

						// Parse the header and construct the bundle info from it
						BundleHeader header = new BundleHeader(new MemoryReader(memory));
						bundleInfo = new BundleInfo(locator, header, headerSize);

						// Add the info to the cache
						AddToCache(cacheKey, bundleInfo, headerSize);

						// Also add any encoded packets we prefetched
						int packetOffset = headerSize;
						for (int packetIdx = 0; packetIdx < header.Packets.Count && packetOffset + header.Packets[packetIdx].EncodedLength < memory.Length; packetIdx++)
						{
							int packetLength = header.Packets[packetIdx].EncodedLength;
							ReadOnlyMemory<byte> packetData = memory.Slice(packetOffset, packetLength).ToArray();
							AddToCache(GetEncodedPacketCacheKey(locator.BlobId, packetIdx), packetData, packetData.Length);
							packetOffset += packetLength;
						}

						break;
					}
				}
			}
			return bundleInfo;
		}

		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="bundleInfo">Information about the bundle</param>
		/// <param name="packetIdx">Index of the packet</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The decoded data</returns>
		async ValueTask<ReadOnlyMemory<byte>> ReadBundlePacketAsync(BundleInfo bundleInfo, int packetIdx, CancellationToken cancellationToken)
		{
			string cacheKey = GetDecodedPacketCacheKey(bundleInfo.Locator.BlobId, packetIdx);
			if (!_cache.TryGetValue(cacheKey, out ReadOnlyMemory<byte> decodedPacket))
			{
				BundlePacket packet = bundleInfo.Header.Packets[packetIdx];
				byte[] decodedPacketBuffer = new byte[packet.DecodedLength];

				ReadOnlyMemory<byte> encodedPacket;
				if (_cache.TryGetValue(GetEncodedPacketCacheKey(bundleInfo.Locator.BlobId, packetIdx), out encodedPacket))
				{
					BundleData.Decompress(bundleInfo.Header.CompressionFormat, encodedPacket, decodedPacketBuffer);
				}
				else
				{
					using IMemoryOwner<byte> owner = MemoryPool<byte>.Shared.Rent(packet.EncodedLength);

					Memory<byte> buffer = owner.Memory.Slice(0, packet.EncodedLength);
					buffer = await ReadBlobRangeAsync(bundleInfo.Locator, bundleInfo.PacketOffsets[packetIdx], buffer, cancellationToken);

					BundleData.Decompress(bundleInfo.Header.CompressionFormat, buffer, decodedPacketBuffer);
				}

				decodedPacket = decodedPacketBuffer;
				AddToCache(cacheKey, decodedPacket, decodedPacket.Length);
			}
			return decodedPacket;
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using ReadOnlySequenceStream stream = new ReadOnlySequenceStream(bundle.AsSequence());
			return await WriteBlobAsync(stream, prefix, cancellationToken);
		}

		#endregion

		#region Nodes

		class NodeReader : MemoryReader, ITreeNodeReader
		{
			readonly IStorageClient _store;
			readonly IReadOnlyList<(IoHash, NodeLocator)> _refs;

			public NodeReader(IStorageClient store, ReadOnlyMemory<byte> data, IReadOnlyList<(IoHash, NodeLocator)> refs)
				: base(data)
			{
				_store = store;
				_refs = refs;
			}

			public void ReadRef(TreeNodeRef treeNodeRef)
			{
				treeNodeRef._store = _store;
				(treeNodeRef._hash, treeNodeRef._locator) = _refs[(int)this.ReadUnsignedVarInt()];
			}
		}

		/// <inheritdoc/>
		public async ValueTask<TNode> ReadNodeAsync<TNode>(NodeLocator locator, CancellationToken cancellationToken = default) where TNode : TreeNode
		{
			BundleInfo bundleInfo = await GetBundleInfoAsync(locator.Blob, cancellationToken);
			BundleExport export = bundleInfo.Header.Exports[locator.ExportIdx];

			ExportInfo exportInfo = bundleInfo.Exports[locator.ExportIdx];
			ReadOnlyMemory<byte> packetData = await ReadBundlePacketAsync(bundleInfo, exportInfo.PacketIdx, cancellationToken);

			(IoHash, NodeLocator)[] refs = new (IoHash, NodeLocator)[export.References.Count];
			for (int idx = 0; idx < export.References.Count; idx++)
			{
				refs[idx] = bundleInfo.References[export.References[idx]];
			}

			ReadOnlyMemory<byte> nodeData = packetData.Slice(exportInfo.Offset, export.Length);
			return TreeNode.Deserialize<TNode>(new NodeReader(this, nodeData, refs));
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public abstract Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<TNode?> TryReadNodeAsync<TNode>(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) where TNode : TreeNode
		{
			NodeLocator refTarget = await TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (!refTarget.IsValid())
			{
				return null;
			}
			return await ReadNodeAsync<TNode>(refTarget, cancellationToken);
		}

		/// <inheritdoc/>
		public abstract Task<NodeLocator> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<NodeLocator> WriteRefAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await WriteBundleAsync(bundle, prefix, cancellationToken);
			NodeLocator target = new NodeLocator(locator, exportIdx);
			await WriteRefTargetAsync(name, target, cancellationToken);
			return target;
		}

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, NodeLocator target, CancellationToken cancellationToken = default);

		#endregion
	}
}
