// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of a compute channel
	/// </summary>
	class ComputeChannel : IComputeChannel, IAsyncDisposable
	{
		// Length of a message header. Consists of a 1 byte type field, followed by 4 byte length field.
		const int HeaderLength = 5;

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
		class MessageBuilder : IComputeMessageBuilder
		{
			readonly ComputeChannel _channel;
			readonly IComputeBufferWriter _sendBufferWriter;
			readonly ComputeMessageType _type;
			int _length;

			/// <inheritdoc/>
			public int Length => _length;

			public MessageBuilder(ComputeChannel channel, IComputeBufferWriter sendBufferWriter, ComputeMessageType type)
			{
				_channel = channel;
				_sendBufferWriter = sendBufferWriter;
				_type = type;
				_length = 0;
			}

			public void Dispose()
			{
				if (_channel._currentBuilder == this)
				{
					_channel._currentBuilder = null;
				}
			}

			public void Send()
			{
				Span<byte> header = _sendBufferWriter.GetMemory().Span;
				header[0] = (byte)_type;
				BinaryPrimitives.WriteInt32LittleEndian(header.Slice(1, 4), _length);

				_sendBufferWriter.Advance(HeaderLength + _length);
				_length = 0;
			}

			/// <inheritdoc/>
			public void Advance(int count) => _length += count;

			/// <inheritdoc/>
			public Memory<byte> GetMemory(int sizeHint = 0) => _sendBufferWriter.GetMemory().Slice(HeaderLength + _length);

			/// <inheritdoc/>
			public Span<byte> GetSpan(int sizeHint = 0) => GetMemory(sizeHint).Span;
		}

		readonly IComputeBufferReader _receiveBufferReader;
		readonly IComputeBufferWriter _sendBufferWriter;

		MessageBuilder? _currentBuilder;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="receiveBufferReader">Reader for incoming messages</param>
		/// <param name="sendBufferWriter">Writer for outgoing messages</param>
		public ComputeChannel(IComputeBufferReader receiveBufferReader, IComputeBufferWriter sendBufferWriter)
		{
			_receiveBufferReader = receiveBufferReader;
			_sendBufferWriter = sendBufferWriter;
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			await DisposeAsync(true);
			GC.SuppressFinalize(this);
		}

		/// <summary>
		/// Overridable dispose method
		/// </summary>
		protected virtual async ValueTask DisposeAsync(bool disposing)
		{
			_currentBuilder?.Dispose();
			_sendBufferWriter.MarkComplete();
			await _sendBufferWriter.FlushAsync(CancellationToken.None);
		}

		/// <inheritdoc/>
		public async ValueTask<IComputeMessage> ReceiveAsync(CancellationToken cancellationToken)
		{
			while (!_receiveBufferReader.IsComplete)
			{
				ReadOnlyMemory<byte> memory = _receiveBufferReader.GetMemory();
				if (memory.Length >= HeaderLength)
				{
					int length = BinaryPrimitives.ReadInt32LittleEndian(memory.Span.Slice(1, 4));
					if (memory.Length >= HeaderLength + length)
					{
						ComputeMessageType type = (ComputeMessageType)memory.Span[0];
						Message message = new Message(type, memory.Slice(HeaderLength, length));
						_receiveBufferReader.Advance(HeaderLength + length);
						return message;
					}
				}
				await _receiveBufferReader.WaitAsync(memory.Length, cancellationToken);
			}
			return new Message(ComputeMessageType.None, ReadOnlyMemory<byte>.Empty);
		}

		/// <inheritdoc/>
		public IComputeMessageBuilder CreateMessage(ComputeMessageType type, int sizeHint = 0)
		{
			if (_currentBuilder != null)
			{
				throw new InvalidOperationException("Only one writer can be active at a time. Dispose of the previous writer first.");
			}

			_currentBuilder = new MessageBuilder(this, _sendBufferWriter, type);
			return _currentBuilder;
		}
	}
}
