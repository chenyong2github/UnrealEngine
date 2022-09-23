// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
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

		readonly IMemoryCache? _cache;
		readonly TreeStore _treeStore;
		readonly object _lockObject = new object();

		// Active read tasks at any moment. If a BundleObject is not available in the cache, we start a read and add an entry to this dictionary
		// so that other threads can also await it.
		readonly Dictionary<string, Task<Bundle>> _readTasks = new Dictionary<string, Task<Bundle>>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="cache"></param>
		protected StorageClientBase(IMemoryCache? cache)
		{
			_cache = cache;
			_treeStore = new TreeStore(this);
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

		/// <inheritdoc/>
		public abstract Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default);

		#endregion

		#region Bundles

		static string GetBundleCacheKey(BlobId blobId) => $"bundle:{blobId}";

		/// <summary>
		/// Adds a bundle to the cache
		/// </summary>
		/// <param name="blobId">Blob id for the bundle</param>
		/// <param name="bundle">The bundle data</param>
		void AddBundleToCache(BlobId blobId, Bundle bundle)
		{
			if (_cache != null)
			{
				string cacheKey = GetBundleCacheKey(blobId);
				using (ICacheEntry entry = _cache.CreateEntry(cacheKey))
				{
					int length = bundle.Packets.Sum(x => x.Length);
					entry.SetSize(length);
					entry.SetValue(bundle);
				}
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
			string cacheKey = GetBundleCacheKey(locator.BlobId);
			if (_cache == null || !_cache.TryGetValue<Bundle>(cacheKey, out Bundle? bundle))
			{
				Task<Bundle>? readTask;
				lock (_lockObject)
				{
					if (!_readTasks.TryGetValue(cacheKey, out readTask))
					{
						readTask = Task.Run(() => ReadBundleInternalAsync(cacheKey, locator, cancellationToken), cancellationToken);
						_readTasks.Add(cacheKey, readTask);
					}
				}
				bundle = await readTask;
			}
			return bundle;
		}

		async Task<Bundle> ReadBundleInternalAsync(string cacheKey, BlobLocator locator, CancellationToken cancellationToken)
		{
			// Perform another (sequenced) check whether an object has been added to the cache, to counteract the race between a read task being added and a task completing.
			lock (_lockObject)
			{
				if (_cache != null && _cache.TryGetValue<Bundle>(cacheKey, out Bundle? cachedObject))
				{
					return cachedObject;
				}
			}

			// Read the data from storage
			Bundle bundle;
			using (Stream stream = await ReadBlobAsync(locator, cancellationToken))
			{
				bundle = await Bundle.FromStreamAsync(stream, cancellationToken);
			}

			// Add the object to the cache
			AddBundleToCache(locator.BlobId, bundle);

			// Remove this object from the list of read tasks
			lock (_lockObject)
			{
				_readTasks.Remove(cacheKey);
			}
			return bundle;
		}

		/// <inheritdoc/>
		public Task<Stream> CreateBundleStreamAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			return ReadBlobAsync(locator, cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<BundleHeader> ReadBundleHeaderAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			Bundle bundle = await ReadBundleAsync(locator, cancellationToken);
			return bundle.Header;
		}

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>> ReadBundlePacketAsync(BlobLocator locator, int packetIdx, CancellationToken cancellationToken = default)
		{
			Bundle bundle = await ReadBundleAsync(locator, cancellationToken);
			return DecodePacket(locator, packetIdx, bundle.Header.Packets[packetIdx], bundle.Header.CompressionFormat, bundle.Packets[packetIdx]);
		}

		/// <summary>
		/// Gets a decoded block from the store
		/// </summary>
		/// <param name="locator">Information about the bundle</param>
		/// <param name="packetIdx">Index of the packet</param>
		/// <param name="packet">The decoded block location and size</param>
		/// <param name="format">Compression type in the bundle</param>
		/// <param name="encodedPacket">The encoded packet data</param>
		/// <returns>The decoded data</returns>
		ReadOnlyMemory<byte> DecodePacket(BlobLocator locator, int packetIdx, BundlePacket packet, BundleCompressionFormat format, ReadOnlyMemory<byte> encodedPacket)
		{
			if (_cache == null)
			{
				return DecodePacketUncached(packet, format, encodedPacket);
			}
			else
			{
				string cacheKey = $"bundle-packet:{locator.BlobId}#{packetIdx}";
				return _cache.GetOrCreate<ReadOnlyMemory<byte>>(cacheKey, entry =>
				{
					ReadOnlyMemory<byte> decodedPacket = DecodePacketUncached(packet, format, encodedPacket);
					entry.SetSize(packet.DecodedLength);
					return decodedPacket;
				});
			}
		}

		static ReadOnlyMemory<byte> DecodePacketUncached(BundlePacket packet, BundleCompressionFormat format, ReadOnlyMemory<byte> encodedPacket)
		{
			byte[] decodedPacket = new byte[packet.DecodedLength];
			BundleData.Decompress(format, encodedPacket, decodedPacket);
			return decodedPacket;
		}

		/// <inheritdoc/>
		public async Task<BlobLocator> WriteBundleAsync(Bundle bundle, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			using ReadOnlySequenceStream stream = new ReadOnlySequenceStream(bundle.AsSequence());
			return await WriteBlobAsync(stream, prefix, cancellationToken);
		}

		#endregion

		#region Trees

		/// <inheritdoc/>
		public ITreeWriter CreateTreeWriter(TreeOptions options, Utf8String prefix = default)
		{
			return _treeStore.CreateTreeWriter(options, prefix);
		}

		/// <inheritdoc/>
		public virtual Task<ITreeBlob?> TryReadTreeAsync(RefName name, TimeSpan maxAge = default, CancellationToken cancellationToken = default)
		{
			return _treeStore.TryReadTreeAsync(name, maxAge, cancellationToken);
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public abstract Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<RefValue?> TryReadRefValueAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			RefTarget? refTarget = await TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (refTarget == null)
			{
				return null;
			}
			return new RefValue(refTarget, await ReadBundleAsync(refTarget.Locator, cancellationToken));
		}

		/// <inheritdoc/>
		public abstract Task<RefTarget?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <inheritdoc/>
		public virtual async Task<RefTarget> WriteRefValueAsync(RefName name, Bundle bundle, int exportIdx = 0, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			BlobLocator locator = await this.WriteBundleAsync(bundle, prefix, cancellationToken);
			RefTarget target = new RefTarget(locator, exportIdx);
			await WriteRefTargetAsync(name, target, cancellationToken);
			return target;
		}

		/// <inheritdoc/>
		public abstract Task WriteRefTargetAsync(RefName name, RefTarget target, CancellationToken cancellationToken = default);

		#endregion
	}
}
