// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// In-process buffer used to store compute messages
	/// </summary>
	public sealed class PooledBuffer : ComputeBufferBase
	{
		class Resources : ResourcesBase
		{
			public HeaderPtr _headerPtr;
			readonly GCHandle _headerHandle;

			public Memory<byte>[] _chunks;
			readonly GCHandle[] _chunkHandles;

			readonly AsyncEvent _writerEvent = new AsyncEvent();
			readonly AsyncEvent _readerEvent = new AsyncEvent();

			public unsafe Resources(int numChunks, int chunkLength, int numReaders)
			{
				_chunks = new Memory<byte>[numChunks];
				_chunkHandles = new GCHandle[numChunks];

				for (int idx = 0; idx < numChunks; idx++)
				{
					byte[] data = new byte[chunkLength];
					_chunks[idx] = data;
					_chunkHandles[idx] = GCHandle.Alloc(data, GCHandleType.Pinned);
				}

				byte[] header = new byte[HeaderSize];
				_headerHandle = GCHandle.Alloc(header, GCHandleType.Pinned);
				_headerPtr = new HeaderPtr((ulong*)_headerHandle.AddrOfPinnedObject().ToPointer(), numReaders, numChunks, chunkLength);
			}

			public override void Dispose()
			{
				for (int idx = 0; idx < _chunkHandles.Length; idx++)
				{
					_chunkHandles[idx].Free();
				}
				_headerHandle.Free();
			}

			/// <inheritdoc/>
			public override void SetReadEvent(int readerIdx) => _readerEvent.Set();

			/// <inheritdoc/>
			public override void ResetReadEvent(int readerIdx) => _readerEvent.Reset();

			/// <inheritdoc/>
			public override Task WaitForReadEvent(int readerIdx, CancellationToken cancellationToken) => _readerEvent.Task.WaitAsync(cancellationToken);

			/// <inheritdoc/>
			public override void SetWriteEvent() => _writerEvent.Set();

			/// <inheritdoc/>
			public override void ResetWriteEvent() => _writerEvent.Reset();

			/// <inheritdoc/>
			public override Task WaitForWriteEvent(CancellationToken cancellationToken) => _writerEvent.Task.WaitAsync(cancellationToken);
		}

		readonly Resources _resources;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="capacity">Total capacity of the buffer</param>
		public PooledBuffer(int capacity)
			: this(2, capacity / 2)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="numChunks">Number of chunks in the buffer</param>
		/// <param name="chunkLength">Length of each chunk</param>
		/// <param name="numReaders">Number of readers for this buffer</param>
		public PooledBuffer(int numChunks, int chunkLength, int numReaders = 1)
			: this(new Resources(numChunks, chunkLength, numReaders))
		{
		}

		private PooledBuffer(Resources resources)
			: base(resources._headerPtr, resources._chunks, resources)
		{
			_resources = resources;
		}

		/// <inheritdoc/>
		public override IComputeBuffer AddRef()
		{
			_resources.AddRef();
			return new PooledBuffer(_resources);
		}
	}
}
