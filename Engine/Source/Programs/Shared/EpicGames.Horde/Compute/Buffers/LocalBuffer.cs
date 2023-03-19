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
	public class LocalBuffer : IComputeBuffer
	{
		class ReaderImpl : IComputeBufferReader
		{
			readonly LocalBuffer _owner;

			public ReaderImpl(LocalBuffer owner) => _owner = owner;

			/// <inheritdoc/>
			public bool IsComplete => _owner.FinishedReading();

			/// <inheritdoc/>
			public void Advance(int size) => _owner.AdvanceReadPosition(size);

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> GetMemory() => _owner.GetReadMemory();

			/// <inheritdoc/>
			public ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken) => _owner.WaitAsync(currentLength, cancellationToken);
		}

		class WriterImpl : IComputeBufferWriter
		{
			readonly LocalBuffer _owner;

			public WriterImpl(LocalBuffer owner) => _owner = owner;

			/// <inheritdoc/>
			public void MarkComplete() => _owner.FinishWriting();

			/// <inheritdoc/>
			public void Advance(int size) => _owner.AdvanceWritePosition(size);

			/// <inheritdoc/>
			public Memory<byte> GetMemory() => _owner.GetWriteMemory();

			/// <inheritdoc/>
			public ValueTask ResetAsync(CancellationToken cancellationToken) => _owner.ResetWritePositionAsync(cancellationToken);

			/// <inheritdoc/>
			public ValueTask FlushAsync(CancellationToken cancellationToken) => _owner.FlushWritesAsync(cancellationToken);
		}

		readonly Memory<byte> _memory;
		int _readPosition;
		int _writePosition;
		bool _finishedWriting;
		TaskCompletionSource? _writtenTcs;
		TaskCompletionSource? _flushedTcs;

		/// <inheritdoc/>
		public IComputeBufferReader Reader { get; }

		/// <inheritdoc/>
		public IComputeBufferWriter Writer { get; }

		/// <summary>
		/// Creates a local buffer with the given capacity
		/// </summary>
		/// <param name="memory"></param>
		public LocalBuffer(Memory<byte> memory)
		{
			_memory = memory;

			Reader = new ReaderImpl(this);
			Writer = new WriterImpl(this);
		}

		#region Reader

		bool FinishedReading() => _finishedWriting && _readPosition == _writePosition;

		void AdvanceReadPosition(int size)
		{
			_readPosition += size;

			if (_readPosition == _writePosition)
			{
				_flushedTcs?.TrySetResult();
			}
		}

		ReadOnlyMemory<byte> GetReadMemory() => _memory.Slice(_readPosition, _writePosition - _readPosition);

		async ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken)
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

		void FinishWriting()
		{
			_finishedWriting = true;
			_writtenTcs?.TrySetResult();
		}

		void AdvanceWritePosition(int size)
		{
			if (_finishedWriting)
			{
				throw new InvalidOperationException("Cannot update write position after marking as complete");
			}

			_writePosition += size;
			_writtenTcs?.TrySetResult();
		}

		Memory<byte> GetWriteMemory() => _memory.Slice(_writePosition);

		async ValueTask ResetWritePositionAsync(CancellationToken cancellationToken)
		{
			await FlushWritesAsync(cancellationToken);
			_writePosition = 0;
		}

		async ValueTask FlushWritesAsync(CancellationToken cancellationToken)
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

	/// <summary>
	/// Implementation of <see cref="LocalBuffer"/> that owns a rented memory resource
	/// </summary>
	class RentedLocalBuffer : LocalBuffer, IDisposable
	{
		readonly IMemoryOwner<byte> _memoryOwner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="capacity">Size of data to allocate</param>
		public RentedLocalBuffer(int capacity)
			: this(MemoryPool<byte>.Shared.Rent(capacity))
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="memoryOwner">Memory to manage</param>
		public RentedLocalBuffer(IMemoryOwner<byte> memoryOwner)
			: base(memoryOwner.Memory)
		{
			_memoryOwner = memoryOwner;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_memoryOwner.Dispose();
		}
	}
}
