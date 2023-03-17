// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of a compute lease using sockets to transfer data
	/// </summary>
	public sealed class ComputeLease : IComputeLease, IAsyncDisposable
	{
		record struct Packet(int Id, ReadOnlyMemory<byte> Data, TaskCompletionSource? Fence);

		class Buffer : ComputeBuffer
		{
			readonly ComputeLease _owner;

			public Buffer(ComputeLease owner, int id, IMemoryOwner<byte> memoryOwner)
				: base(id, memoryOwner)
			{
				_owner = owner;
			}

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				if (disposing)
				{
					_owner.ReleaseBuffer(this);
				}
			}

			public override async ValueTask FlushAsync(CancellationToken cancellationToken)
			{
				TaskCompletionSource tcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
				using (IDisposable disposable = cancellationToken.Register(() => tcs.SetCanceled(cancellationToken)))
				{
					_owner._writeQueue.Writer.TryWrite(new Packet(Id, ReadOnlyMemory<byte>.Empty, tcs));
					await tcs.Task;
				}
			}

			public override async ValueTask ResetWritePositionAsync(CancellationToken cancellationToken)
			{
				// Flush any pending writes
				await FlushAsync(cancellationToken);

				// Write an empty packet telling the remote to flush any reads before continuing
				_owner._writeQueue.Writer.TryWrite(new Packet(Id, ReadOnlyMemory<byte>.Empty, null));

				// Execute the regular logic to make sure we don't have any readers
				ResetWritePosition();
			}

			public async ValueTask ClientResetWritePositionAsync(CancellationToken cancellationToken)
			{
				await WaitForAllReadDataAsync(cancellationToken);
				ResetWritePosition();
			}

			public override void AdvanceWritePosition(long size)
			{
				if (size > 0)
				{
					ReadOnlyMemory<byte> data = Data.Slice((int)WritePosition, (int)size);
					_owner._writeQueue.Writer.TryWrite(new Packet(Id, data, null));
				}

				base.AdvanceWritePosition(size);
			}

			public void AdvanceWritePositionAfterRecv(long size)
			{
				base.AdvanceWritePosition(size);
			}
		}

		/// <inheritdoc/>
		public IReadOnlyDictionary<string, int> AssignedResources { get; }

		readonly object _lockObject = new object();

		bool _complete;
		readonly Task _readTask;
		readonly Task _writeTask;
		readonly CancellationTokenSource _cts = new CancellationTokenSource();

		readonly Dictionary<int, Buffer> _inputBuffers = new Dictionary<int, Buffer>();
		readonly Dictionary<int, Buffer> _outputBuffers = new Dictionary<int, Buffer>();

		readonly Channel<Packet> _writeQueue = Channel.CreateUnbounded<Packet>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="transport">Transport to communicate with the remote</param>
		/// <param name="assignedResources">Resources assigned to this lease</param>
		public ComputeLease(IComputeTransport transport, IReadOnlyDictionary<string, int> assignedResources)
		{
			AssignedResources = assignedResources;

			_readTask = RunReaderAsync(transport, _cts.Token);
			_writeTask = RunWriterAsync(transport, _cts.Token);
		}

		/// <summary>
		/// Attempt to gracefully close the current connection and shutdown both ends of the transport
		/// </summary>
		public async ValueTask CloseAsync(CancellationToken cancellationToken)
		{
			_writeQueue.Writer.TryComplete();

			Task cancellationTask = Task.Delay(-1, cancellationToken);

			await Task.WhenAny(_writeTask, cancellationTask);
			cancellationToken.ThrowIfCancellationRequested();

			await Task.WhenAny(_readTask, cancellationTask);
			cancellationToken.ThrowIfCancellationRequested();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			_writeQueue.Writer.TryComplete();

			_cts.Cancel();

			await _writeTask;
			await _readTask;

			_cts.Dispose();
		}

		async Task RunReaderAsync(IComputeTransport transport, CancellationToken cancellationToken)
		{
			byte[] header = new byte[8];
			try
			{
				// Maintain a local cache of buffers to be able to query for them without having to acquire a global lock
				Dictionary<int, Buffer> buffers = new Dictionary<int, Buffer>();

				// Process messages from the remote
				for(; ;)
				{
					await transport.ReadAsync(header, cancellationToken);

					int id = BinaryPrimitives.ReadInt32LittleEndian(header);
					int size = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(4));
					if (size < 0)
					{
						break;
					}

					Buffer? buffer;
					if (!buffers.TryGetValue(id, out buffer))
					{
						lock (_lockObject)
						{
							if (!_inputBuffers.TryGetValue(id, out buffer))
							{
								throw new NotImplementedException();
							}
							buffers.Add(id, buffer);
						}
					}

					if (size == 0)
					{
						// We're being asked to reset the read position back to zero, so we need to know that the read offset is equal to length
						await buffer.ClientResetWritePositionAsync(cancellationToken);
					}
					else
					{
						await transport.ReadAsync(buffer.Data.Slice((int)buffer.WritePosition, size), cancellationToken);
						buffer.AdvanceWritePositionAfterRecv(size);
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
				foreach (Buffer buffer in _outputBuffers.Values)
				{
					buffer.MarkComplete();
				}
			}
		}

		async Task RunWriterAsync(IComputeTransport transport, CancellationToken cancellationToken)
		{
			// Parse the queue of write requests
			byte[] header = new byte[8];
			try
			{
				while (await _writeQueue.Reader.WaitToReadAsync(cancellationToken))
				{
					Packet packet = await _writeQueue.Reader.ReadAsync(cancellationToken);
					if (packet.Fence != null)
					{
						packet.Fence.SetResult();
						continue;
					}

					BinaryPrimitives.WriteInt32LittleEndian(header, packet.Id);
					BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(4), packet.Data.Length);
					await transport.WriteAsync(header, cancellationToken);

					if (packet.Data.Length > 0)
					{
						await transport.WriteAsync(packet.Data, cancellationToken);
					}
				}

				// Send a close packet to the remote
				BinaryPrimitives.WriteInt32LittleEndian(header, 0);
				BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(4), -1);
				await transport.WriteAsync(header, cancellationToken);
			}
			catch (OperationCanceledException)
			{
			}
		}

		/// <inheritdoc/>
		public IComputeInputBuffer CreateInputBuffer(int id, IMemoryOwner<byte> memoryOwner)
		{
			Buffer inputBuffer = new Buffer(this, id, memoryOwner);
			lock (_lockObject)
			{
				if (_complete)
				{
					inputBuffer.MarkComplete();
				}
				_inputBuffers.Add(id, inputBuffer);
			}
			return inputBuffer;
		}

		/// <inheritdoc/>
		public IComputeOutputBuffer CreateOutputBuffer(int id, IMemoryOwner<byte> memoryOwner)
		{
			Buffer outputBuffer = new Buffer(this, id, memoryOwner);
			lock (_lockObject)
			{
				if (_complete)
				{
					outputBuffer.MarkComplete();
				}
				_outputBuffers.Add(id, outputBuffer);
			}
			return outputBuffer;
		}

		void ReleaseBuffer(Buffer buffer)
		{
			lock (_lockObject)
			{
				Buffer? existingBuffer;
				if (_inputBuffers.TryGetValue(buffer.Id, out existingBuffer) && ReferenceEquals(existingBuffer, buffer))
				{
					_inputBuffers.Remove(buffer.Id);
				}
				if (_outputBuffers.TryGetValue(buffer.Id, out existingBuffer) && ReferenceEquals(existingBuffer, buffer))
				{
					_outputBuffers.Remove(buffer.Id);
				}
			}
		}
	}
}
