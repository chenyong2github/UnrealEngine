// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// In-process buffer used to store compute messages
	/// </summary>
	class PooledBuffer : ComputeBufferBase
	{
		readonly IMemoryOwner<byte> _memoryOwner;
		readonly Memory<byte> _memory;

		enum State
		{
			Normal,
			ReaderWaitingForData, // Once new data is available, set _readerTcs
			WriterWaitingToFlush, // Once flush has been complete, set _writerTcs
			Finished
		}

		int _readPosition;
		int _readLength;
		int _writePosition;
		int _state;
		TaskCompletionSource _readerTcs = new TaskCompletionSource(); // Used when reader is waiting for writer. Reset by the reader.
		TaskCompletionSource _writerTcs = new TaskCompletionSource(); // Used when writer is waiting for reader. Reset by the writer.

		/// <inheritdoc/>
		public override long Length => _memory.Length;

		// Accessor for the value of read length. 
		int GetInterlockedReadLength() => Interlocked.CompareExchange(ref _readLength, 0, 0);

		/// <summary>
		/// Creates a local buffer with the given capacity
		/// </summary>
		/// <param name="capacity">Capacity of the buffer</param>
		public PooledBuffer(int capacity)
		{
			_memoryOwner = MemoryPool<byte>.Shared.Rent(capacity);
			_memory = _memoryOwner.Memory.Slice(0, capacity);
		}

		/// <inheritdoc/>
		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				_memoryOwner.Dispose();
			}
		}

		/// <inheritdoc/>
		public override Memory<byte> GetMemory(long offset, int length)
		{
			if (offset > Int32.MaxValue)
			{
				throw new ArgumentException("Offset is out of range", nameof(offset));
			}
			return _memory.Slice((int)offset, length);
		}

		#region Reader

		/// <inheritdoc/>
		public override bool FinishedReading() => (State)_state == State.Finished && GetInterlockedReadLength() == 0;

		/// <inheritdoc/>
		public override void AdvanceReadPosition(int size)
		{
			_readPosition += size;
			Interlocked.Add(ref _readLength, -size);
		}

		/// <inheritdoc/>
		public override ReadOnlyMemory<byte> GetReadMemory() => _memory.Slice(_readPosition, GetInterlockedReadLength());

		/// <inheritdoc/>
		public override async ValueTask WaitForDataAsync(int currentLength, CancellationToken cancellationToken)
		{
			try
			{
				while (GetInterlockedReadLength() == currentLength)
				{
					int state = _state;
					if (state == (int)State.Finished)
					{
						break;
					}
					else if (state == (int)State.Normal)
					{
						TaskCompletionSource readerTcs = _readerTcs;
						if (readerTcs.Task.IsCompleted)
						{
							Interlocked.CompareExchange(ref _readerTcs, new TaskCompletionSource(), readerTcs);
						}
						Interlocked.CompareExchange(ref _state, (int)State.ReaderWaitingForData, state);
					}
					else if (state == (int)State.ReaderWaitingForData)
					{
						await _readerTcs.Task.WaitAsync(cancellationToken);
					}
					else if (state == (int)State.WriterWaitingToFlush)
					{
						Compact();
						Interlocked.CompareExchange(ref _state, (int)State.Normal, state);
						_writerTcs.TrySetResult();
					}
					else
					{
						throw new NotImplementedException();
					}
					cancellationToken.ThrowIfCancellationRequested();
				}
			}
			finally
			{
				Interlocked.CompareExchange(ref _state, (int)State.Normal, (int)State.ReaderWaitingForData);
			}
		}

		#endregion

		#region Writer

		/// <inheritdoc/>
		public override void FinishWriting()
		{
			for (; ; )
			{
				int state = _state;
				if (Interlocked.CompareExchange(ref _state, (int)State.Finished, state) == state)
				{
					_readerTcs.TrySetResult();
					break;
				}
			}
		}

		/// <inheritdoc/>
		public override void AdvanceWritePosition(int size)
		{
			if (_state == (int)State.Finished)
			{
				throw new InvalidOperationException("Cannot update write position after marking as complete");
			}

			_writePosition += size;
			Interlocked.Add(ref _readLength, size);

			if (Interlocked.CompareExchange(ref _state, (int)State.Normal, (int)State.ReaderWaitingForData) == (int)State.ReaderWaitingForData)
			{
				_readerTcs.TrySetResult();
			}
		}

		/// <inheritdoc/>
		public override Memory<byte> GetWriteMemory()
		{
			return _memory.Slice(_writePosition);
		}

		/// <inheritdoc/>
		public override async ValueTask FlushAsync(CancellationToken cancellationToken)
		{
			try
			{
				while (_readPosition > 0)
				{
					int state = _state;
					if (state == (int)State.Finished)
					{
						break;
					}
					else if (state == (int)State.Normal)
					{
						TaskCompletionSource writerTcs = _writerTcs;
						if (writerTcs.Task.IsCompleted)
						{
							Interlocked.CompareExchange(ref _writerTcs, new TaskCompletionSource(), writerTcs);
						}
						Interlocked.CompareExchange(ref _state, (int)State.WriterWaitingToFlush, state);
					}
					else if (state == (int)State.ReaderWaitingForData)
					{
						Compact();
						break;
					}
					else if (state == (int)State.WriterWaitingToFlush)
					{
						await _writerTcs.Task.WaitAsync(cancellationToken);
					}
				}
			}
			finally
			{
				Interlocked.CompareExchange(ref _state, (int)State.Normal, (int)State.WriterWaitingToFlush);
			}
		}

		void Compact()
		{
			_memory.Slice(_readPosition, _readLength).CopyTo(_memory);
			_readPosition = 0;
			_writePosition = _readLength;
		}

		#endregion
	}
}
