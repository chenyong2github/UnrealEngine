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
	/// Implementation of a compute lease using sockets to transfer data
	/// </summary>
	public sealed class ComputeSocket : IComputeSocket, IAsyncDisposable
	{
		readonly object _lockObject = new object();
		
		bool _complete;
		int _nextChannelId;

		readonly Queue<int> _freeChannelIds = new Queue<int>();
		readonly IComputeTransport _transport;
		readonly ILoggerFactory _loggerFactory;
		readonly ILogger _logger;
		readonly Task _readTask;
		CancellationTokenSource _cancellationSource = new CancellationTokenSource();

		readonly Dictionary<int, IComputeBufferWriter> _receiveBuffers = new Dictionary<int, IComputeBufferWriter>();
		readonly AsyncEvent _receiveBuffersChangedEvent = new AsyncEvent();

		readonly SemaphoreSlim _sendSemaphore = new SemaphoreSlim(1, 1);
		readonly Dictionary<int, IComputeBufferReader> _sendBuffers = new Dictionary<int, IComputeBufferReader>();
		readonly Dictionary<int, Task> _sendTasks = new Dictionary<int, Task>();

		int Endpoint => (_nextChannelId & 1);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="isRemote">Whether this socket is the remote end of the connection</param>
		/// <param name="loggerFactory">Logger for trace output</param>
		public ComputeSocket(IComputeTransport transport, bool isRemote, ILoggerFactory loggerFactory)
		{
			_transport = transport;
			_loggerFactory = loggerFactory;
			_logger = loggerFactory.CreateLogger<ComputeSocket>();
			_nextChannelId = isRemote ? 1 : 2;
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
			_logger.LogTrace("{Endpoint}: Sent shutdown packet", Endpoint);

			// Wait for the reader to stop
			await Task.WhenAny(_readTask, cancellationTask);
			cancellationToken.ThrowIfCancellationRequested();

			_logger.LogTrace("{Endpoint}: Closed", Endpoint);
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
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
		}

		async Task RunReaderAsync(IComputeTransport transport, CancellationToken cancellationToken)
		{
			_logger.LogTrace("{Endpoint}: Started reader", Endpoint);

			byte[] header = new byte[8];
			try
			{
				// Maintain a local cache of buffers to be able to query for them without having to acquire a global lock
				Dictionary<int, IComputeBufferWriter> cachedWriters = new Dictionary<int, IComputeBufferWriter>();

				// Process messages from the remote
				for (; ; )
				{
					// Read the next packet header
					int bytesRead = await transport.ReadAsync(header, cancellationToken);
					if (bytesRead == 0)
					{
						_logger.LogTrace("{Endpoint}: End of socket", Endpoint);
						break;
					}

					// Parse the target buffer and packet size
					int id = BinaryPrimitives.ReadInt32LittleEndian(header);
					int size = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(4));
					_logger.LogTrace("{Endpoint}: Read {ChannelId} -> {Size} bytes", Endpoint, id, size);

					// If the size if negative, we're closing the entire connection
					if (size == 0)
					{
						_logger.LogTrace("{Endpoint}: Received shutdown packet", Endpoint);
						break;
					}

					// Dispatch it to the correct place
					if (size < 0)
					{
						_logger.LogTrace("{Endpoint}: Detaching buffer {Id}", Endpoint, id);
						DetachReceiveBuffer(cachedWriters, id);
					}
					else
					{
						IComputeBufferWriter writer = await GetReceiveBufferAsync(cachedWriters, id);
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

			_logger.LogTrace("{Endpoint}: Closing reader", Endpoint);
		}

		async Task<IComputeBufferWriter> GetReceiveBufferAsync(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			IComputeBufferWriter? writer;
			if (cachedWriters.TryGetValue(id, out writer))
			{
				return writer;
			}

			int delayTime = 1;
			Task? delayTask = null;
			Stopwatch? timer = null;

			for (; ; )
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

				timer ??= Stopwatch.StartNew();
				delayTask ??= Task.Delay(TimeSpan.FromSeconds(delayTime));

				while (!waitTask.IsCompleted)
				{
					if (await Task.WhenAny(waitTask, delayTask) == delayTask)
					{
						_logger.LogWarning("Socket stalled for {Time}s waiting for a buffer to be attached to channel {ChannelId}", (int)timer.Elapsed.TotalSeconds, id);
						delayTime = Math.Min(15, delayTime + 2);
						delayTask = Task.Delay(TimeSpan.FromSeconds(delayTime));
					}
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
						_logger.LogTrace("{Endpoint}: Sending complete packet for channel {ChannelId}", Endpoint, id);
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
		public void AttachBuffers(int channelId, IComputeBufferReader? sendBufferReader, IComputeBufferWriter? receiveBufferWriter)
		{
			bool complete;
			lock (_lockObject)
			{
				complete = _complete;
				if (sendBufferReader != null)
				{
					_sendBuffers.Add(channelId, sendBufferReader);
					_sendTasks.Add(channelId, Task.Run(() => SendFromBufferAsync(channelId, sendBufferReader, _cancellationSource.Token), _cancellationSource.Token));
				}
				if (receiveBufferWriter != null && !complete)
				{
					_receiveBuffers.Add(channelId, receiveBufferWriter);
				}
			}

			if (receiveBufferWriter != null)
			{
				if (complete)
				{
					receiveBufferWriter.MarkComplete();
				}
				else
				{
					_receiveBuffersChangedEvent.Set();
				}
			}
		}

		/// <inheritdoc/>
		public IComputeChannel AttachMessageChannel(int channelId, IComputeBuffer sendBuffer, IComputeBuffer receiveBuffer)
		{
			AttachBuffers(channelId, sendBuffer.Reader, receiveBuffer.Writer);
			return new ComputeChannel(receiveBuffer.Reader, sendBuffer.Writer, _loggerFactory.CreateLogger<ComputeChannel>());
		}

		void DetachReceiveBuffer(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
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
			}
		}
	}
}
