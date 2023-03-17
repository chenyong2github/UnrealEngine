// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Base implementation of <see cref="IComputeInputBuffer"/> and <see cref="IComputeOutputBuffer"/>. Can be derived from to intercept write calls and add special handling for change events.
	/// </summary>
	class ComputeBuffer : IComputeInputBuffer, IComputeOutputBuffer, IDisposable
	{
		/// <inheritdoc/>
		public int Id => _id;

		/// <inheritdoc/>
		public Memory<byte> Data { get; }

		/// <inheritdoc/>
		ReadOnlyMemory<byte> IComputeInputBuffer.Data => Data;

		/// <inheritdoc/>
		public long ReadPosition => _readPosition;

		/// <inheritdoc/>
		public long WritePosition => _writePosition;

		readonly int _id;
		readonly IMemoryOwner<byte> _memoryOwner;
		readonly object _lockObject = new object();

		long _readPosition;
		long _writePosition;
		bool _complete;
		readonly List<TaskCompletionSource> _waiters = new List<TaskCompletionSource>();
		TaskCompletionSource? _consumedTcs;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Id for the buffer</param>
		/// <param name="memoryOwner">The underlying block of memory</param>
		public ComputeBuffer(int id, IMemoryOwner<byte> memoryOwner)
		{
			_id = id;
			_memoryOwner = memoryOwner;

			Data = _memoryOwner.Memory;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		/// <summary>
		/// Standard dispose pattern
		/// </summary>
		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
				_memoryOwner.Dispose();
			}
		}

		/// <inheritdoc/>
		public virtual ValueTask FlushAsync(CancellationToken cancellationToken) => new ValueTask();

		/// <inheritdoc/>
		public virtual async ValueTask ResetWritePositionAsync(CancellationToken cancellationToken)
		{
			await FlushAsync(cancellationToken);
			ResetWritePosition();
		}

		void ReleaseWaiters()
		{
			for (int idx = 0; idx < _waiters.Count; idx++)
			{
				_waiters[idx].TrySetResult();
			}
			_waiters.Clear();
		}

		/// <summary>
		/// Marks this buffer as complete, indicating that no more data will be written to it.
		/// </summary>
		public void MarkComplete()
		{
			lock (_lockObject)
			{
				_complete = true;
				ReleaseWaiters();
			}
		}

		/// <summary>
		/// Reset the current write position.
		/// </summary>
		public void ResetWritePosition()
		{
			_readPosition = 0;
			_writePosition = 0;
		}

		/// <inheritdoc/>
		public void AdvanceReadPosition(long size)
		{
			lock (_lockObject)
			{
				_readPosition += size;
				if (_readPosition == _writePosition && _consumedTcs != null)
				{
					_consumedTcs.SetResult();
				}
			}
		}

		/// <inheritdoc/>
		public virtual void AdvanceWritePosition(long size)
		{
			if (size < 0)
			{
				throw new ArgumentException("New length cannot be less than current length");
			}

			if (size > 0)
			{
				lock (_lockObject)
				{
					if (_complete)
					{
						throw new InvalidOperationException("Cannot add data to a buffer after it is marked complete");
					}

					_writePosition += size;
					ReleaseWaiters();
				}
			}
		}

		/// <summary>
		/// Waits for all data to be read from the buffer
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		protected async ValueTask WaitForAllReadDataAsync(CancellationToken cancellationToken)
		{
			TaskCompletionSource tcs;
			lock (_lockObject)
			{
				if (_complete || _readPosition == _writePosition)
				{
					return;
				}
				else
				{
					tcs = _consumedTcs ??= new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
				}
			}

			using (IDisposable disposable = cancellationToken.Register(() => tcs.SetCanceled(cancellationToken)))
			{
				await tcs.Task;
			}
		}

		/// <inheritdoc/>
		public async ValueTask WaitForWrittenDataAsync(long currentLength, CancellationToken cancellationToken)
		{
			if (_writePosition != currentLength || _complete)
			{
				return;
			}

			TaskCompletionSource tcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
			lock (_lockObject)
			{
				if (_writePosition != currentLength)
				{
					return;
				}
				else
				{
					_waiters.Add(tcs);
				}
			}

			using (IDisposable disposable = cancellationToken.Register(tcs.SetCanceled))
			{
				await tcs.Task;
			}
		}
	}
}
