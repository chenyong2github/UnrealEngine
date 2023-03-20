// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of a compute lease using sockets to transfer data
	/// </summary>
	public sealed class ComputeLease : IComputeLease, IAsyncDisposable
	{
		readonly object _lockObject = new object();

		bool _complete;

		readonly IComputeTransport _transport;
		readonly Task _readTask;
		readonly CancellationTokenSource _cancellationSource = new CancellationTokenSource();

		readonly Dictionary<int, IComputeBufferWriter> _receiveBuffers = new Dictionary<int, IComputeBufferWriter>();
		readonly AsyncEvent _receiveBuffersChangedEvent = new AsyncEvent();

		readonly SemaphoreSlim _sendSemaphore = new SemaphoreSlim(1, 1);
		readonly Dictionary<int, IComputeBufferReader> _sendBuffers = new Dictionary<int, IComputeBufferReader>();
		readonly Dictionary<int, Task> _sendTasks = new Dictionary<int, Task>();

		/// <inheritdoc/>
		public IReadOnlyDictionary<string, int> AssignedResources { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="assignedResources">Resources assigned to this lease</param>
		public ComputeLease(IComputeTransport transport, IReadOnlyDictionary<string, int> assignedResources)
		{
			AssignedResources = assignedResources;

			_transport = transport;
			_readTask = RunReaderAsync(transport, _cancellationSource.Token);
		}

		/// <summary>
		/// Attempt to gracefully close the current connection and shutdown both ends of the transport
		/// </summary>
		public async ValueTask CloseAsync(CancellationToken cancellationToken)
		{
			Task cancellationTask = Task.Delay(-1, cancellationToken);

			// Wait for all the individual send tasks to complete
			await Task.WhenAll(_sendTasks.Values);
			cancellationToken.ThrowIfCancellationRequested();

			// Send a final message indicating that the lease is done. This will allow the senders on the remote end to terminate, and trigger
			// a message to be sent back to our read task allowing it to shut down gracefully.
			byte[] header = new byte[8];
			await _transport.WriteAsync(header, cancellationToken);

			// Wait for the reader to stop
			await Task.WhenAny(_readTask, cancellationTask);
			cancellationToken.ThrowIfCancellationRequested();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			_cancellationSource.Cancel();

			await Task.WhenAll(_sendTasks.Values);
			await _readTask;

			_cancellationSource.Dispose();
			_sendSemaphore.Dispose();
		}

		async Task RunReaderAsync(IComputeTransport transport, CancellationToken cancellationToken)
		{
			byte[] header = new byte[8];
			try
			{
				// Maintain a local cache of buffers to be able to query for them without having to acquire a global lock
				Dictionary<int, IComputeBufferWriter> cachedWriters = new Dictionary<int, IComputeBufferWriter>();

				// Process messages from the remote
				for(; ;)
				{
					// Read the next packet header
					await transport.ReadAsync(header, cancellationToken);

					// Parse the target buffer and packet size
					int id = BinaryPrimitives.ReadInt32LittleEndian(header);
					int size = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(4));

					// If the size if negative, we're closing the entire connection
					if (size == 0)
					{
						break;
					}
					
					// Dispatch it to the correct place
					IComputeBufferWriter writer = await GetReceiveBufferAsync(cachedWriters, id);
					if (size < 0)
					{
						writer.MarkComplete();
						DetachReceiveBuffer(cachedWriters, id);
					}
					else
					{
						await transport.ReadAsync(writer.GetMemory().Slice(0, size), cancellationToken);
						writer.Advance(size);
					}
				}
			}
			catch (OperationCanceledException)
			{
			}

			// Mark all buffers as complete
			lock (_lockObject)
			{
				_complete = true;
				foreach (IComputeBufferWriter writer in _receiveBuffers.Values)
				{
					writer.MarkComplete();
				}
			}
		}

		async Task<IComputeBufferWriter> GetReceiveBufferAsync(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			IComputeBufferWriter? writer;
			if (cachedWriters.TryGetValue(id, out writer))
			{
				return writer;
			}

			for(; ;)
			{
				Task waitTask;
				lock (_lockObject)
				{
					if (_receiveBuffers.TryGetValue(id, out writer))
					{
						cachedWriters.Add(id, writer);
						return writer;
					}
					waitTask = _receiveBuffersChangedEvent.Task;
				}
				await waitTask;
			}
		}

		class SendSegment : ReadOnlySequenceSegment<byte>
		{
			public void Set(ReadOnlyMemory<byte> memory, ReadOnlySequenceSegment<byte>? next, long runningIndex)
			{
				Memory = memory;
				Next = next;
				RunningIndex = runningIndex;
			}
		}

		async Task SendFromBufferAsync(int id, IComputeBufferReader reader, CancellationToken cancellationToken)
		{
			byte[] header = new byte[8];

			BinaryPrimitives.WriteInt32LittleEndian(header, id);

			SendSegment headerSegment = new SendSegment();
			SendSegment bodySegment = new SendSegment();
			headerSegment.Set(header, bodySegment, 0);

			for (; ; )
			{
				// Wait for something to read
				await reader.WaitAsync(0, cancellationToken);

				// Acquire the semaphore
				await _sendSemaphore.WaitAsync(cancellationToken);
				try
				{
					ReadOnlyMemory<byte> memory = reader.GetMemory();
					if (memory.Length > 0)
					{
						BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(4), memory.Length);

						bodySegment.Set(memory, null, header.Length);
						ReadOnlySequence<byte> sequence = new ReadOnlySequence<byte>(headerSegment, 0, bodySegment, memory.Length);

						await _transport.WriteAsync(sequence, cancellationToken);

						reader.Advance(memory.Length);
					}
					else if (reader.IsComplete)
					{
						BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(4), -1);
						await _transport.WriteAsync(header, cancellationToken);
						break;
					}
				}
				finally
				{
					_sendSemaphore.Release();
				}
			}

			lock (_lockObject)
			{
				_sendBuffers.Remove(id);
				_sendTasks.Remove(id);
			}
		}

		/// <inheritdoc/>
		public void AttachSendBuffer(int id, IComputeBufferReader reader)
		{
			lock (_lockObject)
			{
				_sendBuffers.Add(id, reader);
				_sendTasks.Add(id, Task.Run(() => SendFromBufferAsync(id, reader, _cancellationSource.Token), _cancellationSource.Token));
			}
		}

		/// <inheritdoc/>
		public void AttachReceiveBuffer(int id, IComputeBufferWriter writer)
		{
			bool complete;
			lock (_lockObject)
			{
				complete = _complete;

				if(!complete)
				{
					_receiveBuffers.Add(id, writer);
				}
			}

			if (complete)
			{
				writer.MarkComplete();
			}
			else
			{
				_receiveBuffersChangedEvent.Set();
			}
		}

		void DetachReceiveBuffer(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			cachedWriters.Remove(id);
			lock (_lockObject)
			{
				_receiveBuffers.Remove(id);
			}
		}
	}
}
