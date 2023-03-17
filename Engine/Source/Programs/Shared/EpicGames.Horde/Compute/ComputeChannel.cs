// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of a compute channel
	/// </summary>
	class ComputeChannel : IComputeChannel, IDisposable
	{
		/// <summary>
		/// Standard implementation of a message
		/// </summary>
		sealed class Message : IComputeMessage
		{
			/// <inheritdoc/>
			public ComputeMessageType Type { get; }

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> Data { get; }

			readonly IMemoryOwner<byte> _memoryOwner;

			public Message(ComputeMessageType type, ReadOnlyMemory<byte> data)
			{
				_memoryOwner = MemoryPool<byte>.Shared.Rent(data.Length);
				data.CopyTo(_memoryOwner.Memory);

				Type = type;
				Data = _memoryOwner.Memory.Slice(0, data.Length);
			}

			/// <inheritdoc/>
			public void Dispose()
			{
				_memoryOwner.Dispose();
			}
		}

		/// <summary>
		/// Allows creating new messages in rented memory
		/// </summary>
		class Writer : IComputeMessageWriter
		{
			readonly ComputeChannel _channel;
			readonly IComputeOutputBuffer _buffer;
			int _length;

			/// <inheritdoc/>
			public int Length => _length;

			public Writer(ComputeChannel channel, IComputeOutputBuffer buffer, ComputeMessageType type)
			{
				_channel = channel;
				_buffer = buffer;

				this.WriteUInt8((byte)type);
				this.WriteUInt32(0); // size placeholder
			}

			public void Dispose()
			{
				if (_channel._currentWriter == this)
				{
					_channel._currentWriter = null;
				}
			}

			public async ValueTask SendAsync(CancellationToken cancellationToken)
			{
				BinaryPrimitives.WriteInt32LittleEndian(_buffer.GetWriteSpan().Slice(1), _length);
				await _channel.SendAsync(_length, cancellationToken);
			}

			/// <inheritdoc/>
			public void Advance(int count) => _length += count;

			/// <inheritdoc/>
			public Memory<byte> GetMemory(int sizeHint = 0) => _buffer.Data.Slice((int)_buffer.WritePosition + _length);

			/// <inheritdoc/>
			public Span<byte> GetSpan(int sizeHint = 0) => GetMemory(sizeHint).Span;
		}

		/// <inheritdoc/>
		public int Id => _id;

		readonly int _id;
		readonly IComputeInputBuffer _inputBuffer;
		readonly IComputeOutputBuffer _outputBuffer;
		readonly long _flushLength;

		Writer? _currentWriter;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">The channel id</param>
		/// <param name="inputBuffer">Buffer to read incoming messages from</param>
		/// <param name="outputBuffer">Buffer to write outgoing messages to</param>
		/// <param name="maxMessageSize">Maximum size of a message</param>
		public ComputeChannel(int id, IComputeInputBuffer inputBuffer, IComputeOutputBuffer outputBuffer, int maxMessageSize = 1024 * 64)
		{
			_id = id;
			_inputBuffer = inputBuffer;
			_outputBuffer = outputBuffer;
			_flushLength = outputBuffer.Data.Length - maxMessageSize;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_currentWriter?.Dispose();
			_inputBuffer.Dispose();
			_outputBuffer.Dispose();
		}

		/// <inheritdoc/>
		public async ValueTask<IComputeMessage> ReadAsync(CancellationToken cancellationToken)
		{
			for (; ; )
			{
				long readPosition = _inputBuffer.ReadPosition;
				ReadOnlyMemory<byte> memory = _inputBuffer.GetReadMemory();

				const int HeaderLength = 5;
				if (memory.Length >= HeaderLength)
				{
					int length = BinaryPrimitives.ReadInt32LittleEndian(memory.Span.Slice(1, 4));
					if (memory.Length >= length)
					{
						ComputeMessageType type = (ComputeMessageType)memory.Span[0];
						Message message = new Message(type, memory.Slice(HeaderLength, length - HeaderLength));
						_inputBuffer.AdvanceReadPosition(length);
						return message;
					}
				}

				await _inputBuffer.WaitForWrittenDataAsync(readPosition + memory.Length, cancellationToken);
			}
		}

		async Task SendAsync(int length, CancellationToken cancellationToken)
		{
			_outputBuffer.AdvanceWritePosition(length);

			if (_outputBuffer.WritePosition > _flushLength)
			{
				await _outputBuffer.ResetWritePositionAsync(cancellationToken);
			}
		}

		/// <inheritdoc/>
		public IComputeMessageWriter CreateMessage(ComputeMessageType type, int sizeHint = 0)
		{
			if (_currentWriter != null)
			{
				throw new InvalidOperationException("Only one writer can be active at a time. Dispose of the previous writer first.");
			}

			_currentWriter = new Writer(this, _outputBuffer, type);
			return _currentWriter;
		}
	}
}
