// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of a compute channel
	/// </summary>
	public class ComputeMessageChannel : IComputeMessageChannel, IAsyncDisposable
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
			int _position;

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

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> GetMemory(int minSize = 1) => Data.Slice(_position);

			/// <inheritdoc/>
			public void Advance(int length) => _position += length;
		}

		/// <summary>
		/// Allows creating new messages in rented memory
		/// </summary>
		class MessageBuilder : IComputeMessageBuilder
		{
			readonly ComputeMessageChannel _channel;
			readonly IComputeBufferWriter _sendBufferWriter;
			readonly ComputeMessageType _type;
			int _length;

			/// <inheritdoc/>
			public int Length => _length;

			public MessageBuilder(ComputeMessageChannel channel, IComputeBufferWriter sendBufferWriter, ComputeMessageType type)
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

				if (_channel._logger.IsEnabled(LogLevel.Trace))
				{
					_channel.LogMessageInfo("SEND", _type, _sendBufferWriter.GetMemory().Slice(HeaderLength, _length).Span);
				}

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

		// Can lock chunked memory writer to acuqire pointer
		readonly ILogger _logger;

#pragma warning disable CA2213
		MessageBuilder? _currentBuilder;
#pragma warning restore CA2213

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="receiveBufferReader">Reader for incoming messages</param>
		/// <param name="sendBufferWriter">Writer for outgoing messages</param>
		/// <param name="logger">Logger for diagnostic output</param>
		public ComputeMessageChannel(IComputeBufferReader receiveBufferReader, IComputeBufferWriter sendBufferWriter, ILogger logger)
		{
			_receiveBufferReader = receiveBufferReader;
			_sendBufferWriter = sendBufferWriter;
			_logger = logger;
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
			_sendBufferWriter.Dispose();
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
						if (_logger.IsEnabled(LogLevel.Trace))
						{
							LogMessageInfo("RECV", message.Type, message.Data.Span);
						}
						_receiveBufferReader.Advance(HeaderLength + length);
						return message;
					}
					if (_receiveBufferReader.Buffer.Length < HeaderLength + length)
					{
						throw new Exception($"Receive buffer is too small for message; size is {_receiveBufferReader.Buffer.Length}, need {HeaderLength + length}");
					}
				}
				await _receiveBufferReader.WaitForDataAsync(memory.Length, cancellationToken);
			}
			return new Message(ComputeMessageType.None, ReadOnlyMemory<byte>.Empty);
		}

		void LogMessageInfo(string verb, ComputeMessageType type, ReadOnlySpan<byte> data)
		{
			StringBuilder bytes = new StringBuilder();
			for (int offset = 0; offset < 16 && offset < data.Length; offset++)
			{
				bytes.Append($" {data[offset]:X2}");
			}
			if (data.Length > 16)
			{
				bytes.Append("..");
			}
			_logger.LogTrace("{Verb} {Type,-22} [{Length,10:n0}] = {Bytes}", verb, type, data.Length, bytes.ToString());
		}

		/// <inheritdoc/>
		public async ValueTask<IComputeMessageBuilder> CreateMessageAsync(ComputeMessageType type, int maxSize, CancellationToken cancellationToken)
		{
			if (_currentBuilder != null)
			{
				throw new InvalidOperationException("Only one writer can be active at a time. Dispose of the previous writer first.");
			}

			for(; ;)
			{
				Memory<byte> memory = _sendBufferWriter.GetMemory();
				if (memory.Length >= maxSize)
				{
					_currentBuilder = new MessageBuilder(this, _sendBufferWriter, type);
					return _currentBuilder;
				}

				long totalLength = _sendBufferWriter.Buffer.Length;
				if (totalLength < maxSize)
				{
					throw new ComputeInternalException($"Insufficient space in buffer to write message of {maxSize} bytes; buffer only has {totalLength} remaining");
				}
				else
				{
					await _sendBufferWriter.FlushAsync(cancellationToken);
				}
			}
		}
	}
}
