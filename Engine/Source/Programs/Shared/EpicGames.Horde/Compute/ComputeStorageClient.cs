// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Storage client which can read bundles over a compute channel
	/// </summary>
	public sealed class ComputeStorageClient : StorageClientBase, IDisposable
	{
		readonly IComputeChannel _channel;
		readonly SemaphoreSlim _semaphore;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="channel"></param>
		public ComputeStorageClient(IComputeChannel channel)
		{
			_channel = channel;
			_semaphore = new SemaphoreSlim(1);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_semaphore.Dispose();
		}

		#region Blobs

		sealed class BlobStream : ReadOnlyMemoryStream
		{
			readonly IComputeMessage _message;

			public BlobStream(IComputeMessage message)
				: base(message.Data)
			{
				_message = message;
			}

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				if (disposing)
				{
					_message.Dispose();
				}
			}
		}

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobAsync(BlobLocator locator, CancellationToken cancellationToken = default)
		{
			return await ReadBlobRangeAsync(locator, 0, 0, cancellationToken);
		}

		/// <inheritdoc/>
		public override async Task<Stream> ReadBlobRangeAsync(BlobLocator locator, int offset, int length, CancellationToken cancellationToken = default)
		{
			await _semaphore.WaitAsync(cancellationToken);
			try
			{
				await _channel.CppBlobReadAsync(locator, offset, length, cancellationToken);

				IComputeMessage response = await _channel.ReadAsync(cancellationToken);
				if (response.Type != ComputeMessageType.CppBlobData)
				{
					response.Dispose();
					throw new NotImplementedException();
				}

				return new BlobStream(response);
			}
			finally
			{
				_semaphore.Release();
			}
		}

		/// <inheritdoc/>
		public override Task<BlobLocator> WriteBlobAsync(Stream stream, Utf8String prefix = default, CancellationToken cancellationToken = default)
		{
			throw new NotSupportedException();
		}

		#endregion

		#region Nodes

		/// <inheritdoc/>
		public override IAsyncEnumerable<NodeHandle> FindNodesAsync(Utf8String name, CancellationToken cancellationToken = default)
		{
			throw new NotSupportedException();
		}

		#endregion

		#region Refs

		/// <inheritdoc/>
		public override Task DeleteRefAsync(RefName name, CancellationToken cancellationToken = default)
		{
			throw new NotSupportedException();
		}

		/// <inheritdoc/>
		public override Task<NodeHandle?> TryReadRefTargetAsync(RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			throw new NotSupportedException();
		}

		/// <inheritdoc/>
		public override Task WriteRefTargetAsync(RefName name, NodeHandle target, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			throw new NotSupportedException();
		}

		#endregion
	}
}
