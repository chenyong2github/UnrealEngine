// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Manages a set of readers and writers to buffers across a transport layer
	/// </summary>
	public class RemoteComputeSocket : IComputeSocket, IAsyncDisposable
	{
		enum ControlMessageType
		{
			Attach = -1,
			Detach = -2,
		}

		readonly object _lockObject = new object();

		bool _complete;

		readonly IComputeTransport _transport;
		readonly ILogger _logger;

		readonly CancellationTokenSource _cancellationSource = new CancellationTokenSource();

		readonly BackgroundTask _recvTask;
		readonly Dictionary<int, IComputeBufferWriter> _recvBufferWriters = new Dictionary<int, IComputeBufferWriter>();

		readonly SemaphoreSlim _sendSemaphore = new SemaphoreSlim(1, 1);
		readonly Dictionary<int, Task> _sendTasks = new Dictionary<int, Task>();
		readonly Dictionary<IComputeBufferReader, int> _sendBufferReaders = new Dictionary<IComputeBufferReader, int>();

		readonly HashSet<int> _remoteRecvBuffers = new HashSet<int>();
		readonly AsyncEvent _remoteBuffersChanged = new AsyncEvent();

		readonly BackgroundTask _attachTask;
		readonly Channel<int> _attachChannel = Channel.CreateUnbounded<int>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="logger">Logger for trace output</param>
		public RemoteComputeSocket(IComputeTransport transport, ILogger logger)
		{
			_transport = transport;
			_logger = logger;
			_recvTask = BackgroundTask.StartNew(ctx => RunReaderAsync(transport, ctx));
			_attachTask = BackgroundTask.StartNew(ctx => SendAttachNotificationsAsync(ctx));
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
				_sendBufferReaders.Clear();
			}

			// Wait for all the individual send tasks to complete
			await Task.WhenAll(sendTasks);
			cancellationToken.ThrowIfCancellationRequested();

			// Send a final message indicating that the lease is done. This will allow the senders on the remote end to terminate, and trigger
			// a shutdown event to be sent back to our read task allowing it to shut down gracefully.
			await _transport.MarkCompleteAsync(cancellationToken);

			// Stop sending 
			_attachChannel.Writer.Complete();
			await _attachTask.Task.WaitAsync(TimeSpan.FromSeconds(5), cancellationToken);
			await _attachTask.StopAsync();

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
			await _attachTask.DisposeAsync();
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
					if (size >= 0)
					{
						IComputeBufferWriter writer = GetReceiveBuffer(cachedWriters, id);
						await ReadPacketAsync(transport, id, size, writer, cancellationToken);
					}
					else if (size == (int)ControlMessageType.Attach)
					{
						_logger.LogTrace("Attaching recv buffer {Id}", id);
						AttachRemoteRecvBuffer(id);
					}
					else if (size == (int)ControlMessageType.Detach)
					{
						_logger.LogTrace("Detaching recv buffer {Id}", id);
						DetachRecvBuffer(cachedWriters, id);
					}
					else
					{
						_logger.LogWarning("Unrecognized control message: {Message}", size);
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
				foreach (IComputeBuffer buffer in _recvBufferWriters.Values)
				{
					buffer.Writer.MarkComplete();
				}
			}

			_logger.LogTrace("Closing reader");
		}

		async Task ReadPacketAsync(IComputeTransport transport, int id, int size, IComputeBufferWriter writer, CancellationToken cancellationToken)
		{
			Memory<byte> memory = writer.GetWriteBuffer();
			while (memory.Length < size)
			{
				_logger.LogTrace("No space in buffer {Id}, flushing", id);
				await writer.WaitToWriteAsync(size, cancellationToken);
				memory = writer.GetWriteBuffer();
			}

			for (int offset = 0; offset < size;)
			{
				int read = await transport.ReadPartialAsync(memory.Slice(offset, size - offset), cancellationToken);
				offset += read;
			}

			writer.AdvanceWritePosition(size);
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
				IComputeBufferWriter? recvBufferWriter;
				if (_recvBufferWriters.TryGetValue(id, out recvBufferWriter))
				{
					writer = recvBufferWriter;
					cachedWriters.Add(id, writer);
					return writer;
				}
			}

			throw new ComputeInternalException($"No buffer is attached to channel {id}");
		}

		/// <inheritdoc/>
		public async ValueTask WaitForAttachAsync(int channelId, CancellationToken cancellationToken = default)
		{
			for (; ; )
			{
				Task task = _remoteBuffersChanged.Task;
				lock (_lockObject)
				{
					if (_remoteRecvBuffers.Contains(channelId))
					{
						return;
					}
				}
				await task.WaitAsync(cancellationToken);
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

		readonly byte[] _header = new byte[8];
		readonly SendSegment _headerSegment = new SendSegment();
		readonly SendSegment _bodySegment = new SendSegment();

		/// <inheritdoc/>
		public async ValueTask SendAsync(int id, ReadOnlyMemory<byte> memory, CancellationToken cancellationToken = default)
		{
			if (memory.Length > 0)
			{
				await SendInternalAsync(id, memory.Length, memory, cancellationToken);
			}
		}

		/// <inheritdoc/>
		public ValueTask MarkCompleteAsync(int id, CancellationToken cancellationToken = default) => SendInternalAsync(id, (int)ControlMessageType.Detach, ReadOnlyMemory<byte>.Empty, cancellationToken);

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
		public async void AttachRecvBuffer(int channelId, IComputeBufferWriter recvBufferWriter)
		{
			bool complete;
			lock (_lockObject)
			{
				complete = _complete;
				if (!complete)
				{
					_recvBufferWriters.Add(channelId, recvBufferWriter.AddRef());
				}
			}

			if (recvBufferWriter != null && complete)
			{
				recvBufferWriter.MarkComplete();
			}

			if (!complete)
			{
				await _attachChannel.Writer.WriteAsync(channelId);
			}
		}

		async Task SendAttachNotificationsAsync(CancellationToken cancellationToken)
		{
			while (await _attachChannel.Reader.WaitToReadAsync(cancellationToken))
			{
				int channelId;
				if (_attachChannel.Reader.TryRead(out channelId))
				{
					await SendInternalAsync(channelId, (int)ControlMessageType.Attach, ReadOnlyMemory<byte>.Empty, cancellationToken);
				}
			}
		}

		void AttachRemoteRecvBuffer(int id)
		{
			lock (_lockObject)
			{
				_remoteRecvBuffers.Add(id);
			}
			_remoteBuffersChanged.Pulse();
		}

		void DetachRecvBuffer(Dictionary<int, IComputeBufferWriter> cachedWriters, int id)
		{
			cachedWriters.Remove(id);

			IComputeBufferWriter? recvBufferWriter;
			lock (_lockObject)
			{
#pragma warning disable CA2000 // Dispose objects before losing scope
				_recvBufferWriters.Remove(id, out recvBufferWriter);
#pragma warning restore CA2000 // Dispose objects before losing scope
			}
			if (recvBufferWriter != null)
			{
				recvBufferWriter.MarkComplete();
				recvBufferWriter.Dispose();
			}
		}

		/// <inheritdoc/>
		public void AttachSendBuffer(int channelId, IComputeBufferReader sendBufferReader)
		{
			sendBufferReader = sendBufferReader.AddRef();
			lock (_lockObject)
			{
				_sendTasks.Add(channelId, Task.Run(() => SendFromBufferAsync(channelId, sendBufferReader, _cancellationSource.Token), CancellationToken.None)); // No cancellation token; need to ensure dispose runs
				_sendBufferReaders.Add(sendBufferReader, channelId);
			}
		}

		async Task SendFromBufferAsync(int channelId, IComputeBufferReader reader, CancellationToken cancellationToken)
		{
			using IComputeBufferReader _ = reader;

			while (!cancellationToken.IsCancellationRequested)
			{
				ReadOnlyMemory<byte> memory = reader.GetReadBuffer();
				if (memory.Length > 0)
				{
					await SendAsync(channelId, memory, cancellationToken);
					reader.AdvanceReadPosition(memory.Length);
				}
				if (reader.IsComplete)
				{
					await MarkCompleteAsync(channelId, cancellationToken);
					break;
				}
				await reader.WaitToReadAsync(1, cancellationToken);
			}

			lock (_lockObject)
			{
				_sendTasks.Remove(channelId);
				_sendBufferReaders.Remove(reader);
			}
		}
	}
}
