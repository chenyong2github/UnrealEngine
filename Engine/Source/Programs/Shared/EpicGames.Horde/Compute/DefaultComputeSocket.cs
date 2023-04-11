// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Manages a set of readers and writers to buffers across a transport layer
	/// </summary>
	class DefaultComputeSocket : IComputeSocket, IAsyncDisposable
	{
		readonly object _lockObject = new object();

		bool _complete;

		readonly IComputeTransport _transport;
		readonly ILogger _logger;

		readonly CancellationTokenSource _cancellationSource = new CancellationTokenSource();

		readonly BackgroundTask _recvTask;
		readonly Dictionary<int, IComputeBuffer> _recvBuffers = new Dictionary<int, IComputeBuffer>();

		readonly SemaphoreSlim _sendSemaphore = new SemaphoreSlim(1, 1);
		readonly Dictionary<int, Task> _sendTasks = new Dictionary<int, Task>();
		readonly Dictionary<IComputeBuffer, int> _sendBuffers = new Dictionary<IComputeBuffer, int>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="logger">Logger for trace output</param>
		public DefaultComputeSocket(IComputeTransport transport, ILogger logger)
		{
			_transport = transport;
			_logger = logger;
			_recvTask = BackgroundTask.StartNew(ctx => RunReaderAsync(transport, ctx));
		}

		/// <summary>
		/// Attempt to gracefully close the current connection and shutdown both ends of the transport
		/// </summary>
		public async ValueTask CloseAsync(CancellationToken cancellationToken)
		{
			// Make sure we close all buffers that are attached, otherwise we'll lock up waiting for send tasks to complete
			Task[] sendTasks;
			lock (_lockObject)
			{
				sendTasks = _sendTasks.Values.ToArray();
				foreach ((IComputeBuffer sendBuffer, int channelId) in _sendBuffers)
				{
					if (sendBuffer.Writer.MarkComplete())
					{
						_logger.LogWarning("Buffer attached to channel {ChannelId} was not marked as complete before socket was closed.", channelId);
					}
				}
				_sendBuffers.Clear();
			}

			// Wait for all the individual send tasks to complete
			await Task.WhenAll(sendTasks);
			cancellationToken.ThrowIfCancellationRequested();

			// Send a final message indicating that the lease is done. This will allow the senders on the remote end to terminate, and trigger
			// a shutdown event to be sent back to our read task allowing it to shut down gracefully.
			await _transport.MarkCompleteAsync(cancellationToken);

			// Wait for the reader to stop
			await _recvTask.Task.WaitAsync(TimeSpan.FromSeconds(5), cancellationToken);
			await _recvTask.StopAsync();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			_cancellationSource.Cancel();
			await Task.WhenAll(_sendTasks.Values);
			await _recvTask.DisposeAsync();
			_sendSemaphore.Dispose();
			_cancellationSource.Dispose();
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

					// Dispatch it to the correct place
					if (size < 0)
					{
						_logger.LogTrace("Detaching buffer {Id}", id);
						DetachRecvBuffer(cachedWriters, id);
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
				foreach (IComputeBufferWriter writer in _recvBuffers.Values)
				{
					writer.MarkComplete();
				}
			}

			_logger.LogTrace("Closing reader");
		}

		async Task ReadPacketAsync(IComputeTransport transport, int id, int size, IComputeBufferWriter writer, CancellationToken cancellationToken)
		{
			Memory<byte> memory = writer.GetMemory();
			while (memory.Length < size)
			{
				_logger.LogTrace("No space in buffer {Id}, flushing", id);
				await writer.WaitToWriteAsync(memory.Length, cancellationToken);
				memory = writer.GetMemory();
			}

			for (int offset = 0; offset < size;)
			{
				int read = await transport.ReadPartialAsync(memory.Slice(offset, size - offset), cancellationToken);
				offset += read;
			}

			writer.Advance(size);
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
				IComputeBuffer? recvBuffer;
				if (_recvBuffers.TryGetValue(id, out recvBuffer))
				{
					writer = recvBuffer.Writer;
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

		readonly byte[] _header = new byte[8];
		readonly SendSegment _headerSegment = new SendSegment();
		readonly SendSegment _bodySegment = new SendSegment();

		/// <inheritdoc/>
		public async ValueTask SendAsync(int id, ReadOnlyMemory<byte> memory, CancellationToken cancellationToken)
		{
			if (memory.Length > 0)
			{
				await SendInternalAsync(id, memory.Length, memory, cancellationToken);
			}
		}

		/// <inheritdoc/>
		public ValueTask MarkCompleteAsync(int id, CancellationToken cancellationToken) => SendInternalAsync(id, -1, ReadOnlyMemory<byte>.Empty, cancellationToken);

		async ValueTask SendInternalAsync(int id, int size, ReadOnlyMemory<byte> memory, CancellationToken cancellationToken)
		{
			await _sendSemaphore.WaitAsync(cancellationToken);
			try
			{
				BinaryPrimitives.WriteInt32LittleEndian(_header, id);
				BinaryPrimitives.WriteInt32LittleEndian(_header.AsSpan(4), size);
				_headerSegment.Set(_header, _bodySegment, 0);
				_bodySegment.Set(memory, null, _header.Length);

				ReadOnlySequence<byte> sequence = new ReadOnlySequence<byte>(_headerSegment, 0, _bodySegment, memory.Length);
				await _transport.WriteAsync(sequence, cancellationToken);
			}
			finally
			{
				_sendSemaphore.Release();
			}
		}

		/// <inheritdoc/>
		public void AttachRecvBuffer(int channelId, IComputeBuffer recvBuffer)
		{
			IComputeBuffer shared = recvBuffer.AddRef();

			bool complete;
			lock (_lockObject)
			{
				complete = _complete;
				if (!complete)
				{
					_recvBuffers.Add(channelId, shared);
				}
			}

			if (shared != null && complete)
			{
				shared.Writer.MarkComplete();
				shared.Dispose();
			}
		}

		void DetachRecvBuffer(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			cachedWriters.Remove(id);

			IComputeBuffer? buffer;
			lock (_lockObject)
			{
#pragma warning disable CA2000 // Dispose objects before losing scope
				_recvBuffers.Remove(id, out buffer);
#pragma warning restore CA2000 // Dispose objects before losing scope
			}
			if (buffer != null)
			{
				buffer.Writer.MarkComplete();
				buffer.Dispose();
			}
		}

		/// <inheritdoc/>
		public void AttachSendBuffer(int channelId, IComputeBuffer buffer)
		{
			IComputeBuffer shared = buffer.AddRef();
			lock (_lockObject)
			{
				_sendTasks.Add(channelId, Task.Run(() => SendFromBufferAsync(channelId, shared, _cancellationSource.Token), _cancellationSource.Token));
				_sendBuffers.Add(shared, channelId);
			}
		}

		async Task SendFromBufferAsync(int channelId, IComputeBuffer buffer, CancellationToken cancellationToken)
		{
			using IComputeBuffer _ = buffer;

			IComputeBufferReader reader = buffer.Reader;
			while (!cancellationToken.IsCancellationRequested)
			{
				ReadOnlyMemory<byte> memory = reader.GetMemory();
				if (memory.Length > 0)
				{
					await SendAsync(channelId, memory, cancellationToken);
					reader.Advance(memory.Length);
				}
				if (reader.IsComplete)
				{
					await MarkCompleteAsync(channelId, cancellationToken);
					break;
				}
				await reader.WaitToReadAsync(0, cancellationToken);
			}

			lock (_lockObject)
			{
				_sendTasks.Remove(channelId);
				_sendBuffers.Remove(buffer);
			}
		}
	}
}
