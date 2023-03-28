// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Manages a set of readers and writers to buffers across a transport layer
	/// </summary>
	public abstract class ComputeSocketBase : IComputeSocket, IAsyncDisposable
	{
		readonly object _lockObject = new object();

		bool _complete;

		readonly IComputeTransport _transport;
		readonly ILogger _logger;
		readonly Task _readTask;
		CancellationTokenSource _cancellationSource = new CancellationTokenSource();

		readonly Dictionary<int, IComputeBufferWriter> _receiveBuffers = new Dictionary<int, IComputeBufferWriter>();

		readonly SemaphoreSlim _sendSemaphore = new SemaphoreSlim(1, 1);
		readonly Dictionary<int, IComputeBufferReader> _sendBuffers = new Dictionary<int, IComputeBufferReader>();
		readonly Dictionary<int, Task> _sendTasks = new Dictionary<int, Task>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="logger">Logger for trace output</param>
		protected ComputeSocketBase(IComputeTransport transport, ILogger logger)
		{
			_transport = transport;
			_logger = logger;
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
			_logger.LogTrace("Sent shutdown packet");

			// Wait for the reader to stop
			await Task.WhenAny(_readTask, cancellationTask);
			cancellationToken.ThrowIfCancellationRequested();

			_logger.LogTrace("Closed");
		}

		/// <inheritdoc/>
		public virtual async ValueTask DisposeAsync()
		{
			if (_cancellationSource != null)
			{
				_cancellationSource.Cancel();

				await Task.WhenAll(_sendTasks.Values);
				await _readTask;

				_cancellationSource.Dispose();
				_cancellationSource = null!;
			}

			_sendSemaphore.Dispose();
			GC.SuppressFinalize(this);
		}

		async Task RunReaderAsync(IComputeTransport transport, CancellationToken cancellationToken)
		{
			_logger.LogTrace("Started socket reader");

			byte[] header = new byte[8];
			try
			{
				// Maintain a local cache of buffers to be able to query for them without having to acquire a global lock
				Dictionary<int, IComputeBufferWriter> cachedWriters = new Dictionary<int, IComputeBufferWriter>();

				Memory<byte> last = Memory<byte>.Empty;

				// Process messages from the remote
				for (; ; )
				{
					// Read the next packet header
					if (!await transport.ReadOptionalAsync(header, cancellationToken))
					{
						_logger.LogTrace("End of socket");
						break;
					}

					// Parse the target buffer and packet size
					int id = BinaryPrimitives.ReadInt32LittleEndian(header);
					int size = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(4));

					// If the size if negative, we're closing the entire connection
					if (size == 0)
					{
						_logger.LogTrace("Received shutdown packet");
						break;
					}

					// Dispatch it to the correct place
					if (size < 0)
					{
						_logger.LogTrace("Detaching buffer {Id}", id);
						DetachRecvBufferWriter(cachedWriters, id);
					}
					else
					{
						IComputeBufferWriter writer = GetReceiveBuffer(cachedWriters, id);
						await ReadPacketAsync(transport, id, size, writer, cancellationToken);
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

			_logger.LogTrace("Closing reader");
		}

		async Task ReadPacketAsync(IComputeTransport transport, int id, int size, IComputeBufferWriter writer, CancellationToken cancellationToken)
		{
			for (int offset = 0; offset < size;)
			{
				Memory<byte> memory = writer.GetMemory();
				if (memory.Length == 0)
				{
					_logger.LogTrace("No space in buffer {Id}, flushing", id);
					await writer.FlushAsync(cancellationToken);
				}
				else
				{
					int maxRead = Math.Min(size - offset, memory.Length);
					int read = await transport.ReadPartialAsync(memory.Slice(0, maxRead), cancellationToken);
					writer.Advance(read);

					offset += read;
				}
			}
		}

		IComputeBufferWriter GetReceiveBuffer(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			IComputeBufferWriter? writer;
			if (cachedWriters.TryGetValue(id, out writer))
			{
				return writer;
			}

			lock (_lockObject)
			{
				if (_receiveBuffers.TryGetValue(id, out writer))
				{
					cachedWriters.Add(id, writer);
					return writer;
				}
			}

			throw new ComputeInternalException($"No buffer is attached to channel {id}");
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
				await reader.WaitForDataAsync(0, cancellationToken);

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
						_logger.LogTrace("Sending complete packet for channel {ChannelId}", id);
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
		public abstract IComputeBuffer CreateBuffer(long capacity);

		/// <inheritdoc/>
		public ValueTask AttachRecvBufferAsync(int channelId, IComputeBufferWriter recvBufferWriter, CancellationToken cancellationToken)
		{
			bool complete;
			lock (_lockObject)
			{
				complete = _complete;
				if (!complete)
				{
					_receiveBuffers.Add(channelId, recvBufferWriter);
				}
			}

			if (recvBufferWriter != null && complete)
			{
				recvBufferWriter.MarkComplete();
			}

			return new ValueTask();
		}

		void DetachRecvBufferWriter(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			cachedWriters.Remove(id);

			IComputeBufferWriter? writer;
			lock (_lockObject)
			{
#pragma warning disable CA2000 // Dispose objects before losing scope
				_receiveBuffers.Remove(id, out writer);
#pragma warning restore CA2000 // Dispose objects before losing scope
			}
			if (writer != null)
			{
				writer.MarkComplete();
				writer.Dispose();
			}
		}

		/// <summary>
		/// Registers the read end of an attached send buffer
		/// </summary>
		/// <param name="channelId">Channel id</param>
		/// <param name="sendBufferReader">Writer for the receive buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public ValueTask AttachSendBufferAsync(int channelId, IComputeBufferReader sendBufferReader, CancellationToken cancellationToken)
		{
			lock (_lockObject)
			{
				_sendBuffers.Add(channelId, sendBufferReader);
				_sendTasks.Add(channelId, Task.Run(() => SendFromBufferAsync(channelId, sendBufferReader, _cancellationSource.Token), _cancellationSource.Token));
			}
			return new ValueTask();
		}
	}
}
