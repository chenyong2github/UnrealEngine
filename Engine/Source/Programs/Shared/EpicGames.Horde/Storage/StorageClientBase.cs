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

		readonly TreeStore _treeStore;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="cache"></param>
		protected StorageClientBase(IMemoryCache? cache)
		{
			_treeStore = new TreeStore(this, cache);
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
			return new RefValue(refTarget, await this.ReadBundleAsync(refTarget.Locator, cancellationToken));
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
