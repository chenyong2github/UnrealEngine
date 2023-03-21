// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// In-process buffer used for compute messages
	/// </summary>
	public class HeapBuffer : ComputeBuffer
	{
		readonly IMemoryOwner<byte> _memoryOwner;
		readonly Memory<byte> _memory;

		int _readPosition;
		int _writePosition;
		bool _finishedWriting;
		TaskCompletionSource? _writtenTcs;
		TaskCompletionSource? _flushedTcs;

		/// <summary>
		/// Creates a local buffer with the given capacity
		/// </summary>
		/// <param name="capacity">Capacity of the buffer</param>
		public HeapBuffer(int capacity)
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

		#region Reader

		/// <inheritdoc/>
		protected override bool FinishedReading() => _finishedWriting && _readPosition == _writePosition;

		/// <inheritdoc/>
		protected override void AdvanceReadPosition(int size)
		{
			_readPosition += size;

			if (_readPosition == _writePosition)
			{
				_flushedTcs?.TrySetResult();
			}
		}

		/// <inheritdoc/>
		protected override ReadOnlyMemory<byte> GetReadMemory() => _memory.Slice(_readPosition, _writePosition - _readPosition);

		/// <inheritdoc/>
		protected override async ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken)
		{
			int initialWritePosition = _readPosition + currentLength;

			// Early out if we're already past this
			if (_finishedWriting || _writePosition > initialWritePosition)
			{
				return;
			}

			TaskCompletionSource tcs = new TaskCompletionSource();
			using (IDisposable registration = cancellationToken.Register(x => tcs.SetCanceled((CancellationToken)x!), cancellationToken))
			{
				try
				{
					_writtenTcs = tcs;
					for (; ; )
					{
						if (_finishedWriting || _writePosition > initialWritePosition)
						{
							break;
						}
						else
						{
							await _writtenTcs.Task;
						}
					}
				}
				finally
				{
					_writtenTcs = null;
				}
			}
		}

		#endregion

		#region Writer

		/// <inheritdoc/>
		protected override void FinishWriting()
		{
			_finishedWriting = true;
			_writtenTcs?.TrySetResult();
		}

		/// <inheritdoc/>
		protected override void AdvanceWritePosition(int size)
		{
			if (_finishedWriting)
			{
				throw new InvalidOperationException("Cannot update write position after marking as complete");
			}

			_writePosition += size;
			_writtenTcs?.TrySetResult();
		}

		/// <inheritdoc/>
		protected override Memory<byte> GetWriteMemory() => _memory.Slice(_writePosition);

		/// <inheritdoc/>
		protected override async ValueTask FlushWritesAsync(CancellationToken cancellationToken)
		{
			if (_readPosition < _writePosition)
			{
				TaskCompletionSource tcs = new TaskCompletionSource();
				using (_ = cancellationToken.Register(x => tcs.SetCanceled((CancellationToken)x!), cancellationToken, false))
				{
					TaskCompletionSource? originalTcs = Interlocked.CompareExchange(ref _flushedTcs, tcs, null);
					if (originalTcs != null)
					{
						_ = originalTcs.Task.ContinueWith(x => tcs.TrySetResult(), cancellationToken, TaskContinuationOptions.None, TaskScheduler.Default);
					}
				}
				if (_readPosition < _writePosition)
				{
					await tcs.Task;
				}
			}
		}

		#endregion
	}
}
