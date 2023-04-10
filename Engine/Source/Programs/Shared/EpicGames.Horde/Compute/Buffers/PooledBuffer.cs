// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
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
		class Chunk : ChunkBase, IDisposable
		{
			readonly IMemoryOwner<byte> _memoryOwner;
			ulong _stateValue;

			/// <inheritdoc/>
			protected override ref ulong StateValue => ref _stateValue;

			public Chunk(IMemoryOwner<byte> memoryOwner)
				: base(memoryOwner.Memory)
			{
				_memoryOwner = memoryOwner;
			}

			public void Dispose() => _memoryOwner.Dispose();
		}

		readonly Chunk[] _chunks;
		readonly AsyncEvent _writerEvent = new AsyncEvent();
		readonly AsyncEvent _readerEvent = new AsyncEvent();

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
		public PooledBuffer(int numChunks, int chunkLength)
			: this(CreateChunks(numChunks, chunkLength), 1)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		private PooledBuffer(Chunk[] chunks, int readerIdx)
			: base(chunks, readerIdx)
		{
			_chunks = chunks;
		}

		static Chunk[] CreateChunks(int numChunks, int chunkLength)
		{
			Chunk[] chunks = new Chunk[numChunks];
			for (int chunkIdx = 0; chunkIdx < numChunks; chunkIdx++)
			{
				Chunk chunk = new Chunk(MemoryPool<byte>.Shared.Rent(chunkLength));
				chunks[chunkIdx] = chunk;
			}
			return chunks;
		}

		/// <inheritdoc/>
		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				for (int idx = 0; idx < _chunks.Length; idx++)
				{
					_chunks[idx].Dispose();
				}
			}
		}

		/// <inheritdoc/>
		protected override void SetReadEvent(int readerIdx) => _readerEvent.Set();

		/// <inheritdoc/>
		protected override void ResetReadEvent(int readerIdx) => _readerEvent.Reset();

		/// <inheritdoc/>
		protected override Task WaitForReadEvent(int readerIdx, CancellationToken cancellationToken) => _readerEvent.Task.WaitAsync(cancellationToken);

		/// <inheritdoc/>
		protected override void SetWriteEvent() => _writerEvent.Set();

		/// <inheritdoc/>
		protected override void ResetWriteEvent() => _writerEvent.Reset();

		/// <inheritdoc/>
		protected override Task WaitForWriteEvent(CancellationToken cancellationToken) => _writerEvent.Task.WaitAsync(cancellationToken);
	}
}
