// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute.Buffers;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Socket for sending and reciving messages
	/// </summary>
	public interface IComputeSocket : IAsyncDisposable
	{
		/// <summary>
		/// Attaches buffers to a channel.
		/// </summary>
		/// <param name="channelId">Identifier of the channel</param>
		/// <param name="sendBufferReader">The reader to attach</param>
		/// <param name="receiveBufferWriter">The writer to attach</param>
		/// <returns>Identifier for the message channel</returns>
		void AttachBuffers(int channelId, IComputeBufferReader? sendBufferReader, IComputeBufferWriter? receiveBufferWriter);

		/// <summary>
		/// Creates a channel to receive data on the given channel id
		/// </summary>
		/// <param name="channelId">Identifier of the channel</param>
		/// <param name="sendBuffer">Buffer for sending messages</param>
		/// <param name="receiveBuffer">Buffer for receiving messages</param>
		IComputeChannel AttachMessageChannel(int channelId, IComputeBuffer sendBuffer, IComputeBuffer receiveBuffer);
	}

	/// <summary>
	/// Extension methods for sockets
	/// </summary>
	public static class ComputeSocketExtensions
	{
		class HeapBufferChannel : IComputeChannel
		{
			readonly IComputeChannel _inner;
			readonly HeapBuffer _receiveBuffer;
			readonly HeapBuffer _sendBuffer;

			public HeapBufferChannel(IComputeChannel inner, HeapBuffer receiveBuffer, HeapBuffer sendBuffer)
			{
				_inner = inner;
				_sendBuffer = sendBuffer;
				_receiveBuffer = receiveBuffer;
			}

			/// <inheritdoc/>
			public async ValueTask DisposeAsync()
			{
				await _inner.DisposeAsync();

				_sendBuffer.Dispose();
				_receiveBuffer.Dispose();
			}

			/// <inheritdoc/>
			public ValueTask<IComputeMessage> ReceiveAsync(CancellationToken cancellationToken) => _inner.ReceiveAsync(cancellationToken);

			/// <inheritdoc/>
			public IComputeMessageBuilder CreateMessage(ComputeMessageType type, int sizeHint = 0) => _inner.CreateMessage(type, sizeHint);
		}

		/// <summary>
		/// Creates a channel to receive data on the given channel id
		/// </summary>
		/// <param name="socket">Socket to attach a channel on</param>
		/// <param name="channelId">Identifier of the channel</param>
		public static IComputeChannel AttachMessageChannel(this IComputeSocket socket, int channelId)
		{
			const int MaxMessageSize = 64 * 1024;
			const int BufferSize = MaxMessageSize * 3;

			HeapBuffer receiveBuffer = new HeapBuffer(BufferSize);
			HeapBuffer sendBuffer = new HeapBuffer(BufferSize);

			IComputeChannel inner = socket.AttachMessageChannel(channelId, sendBuffer, receiveBuffer);
			return new HeapBufferChannel(inner, receiveBuffer, sendBuffer);
		}

		/// <summary>
		/// Attaches a send buffer to a socket. Data will be read from this buffer and replicated a receive buffer attached with the same id on the remote.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Identifier for the buffer</param>
		/// <param name="reader">Source to read from</param>
		public static void AttachSendBuffer(this IComputeSocket socket, int channelId, IComputeBufferReader reader) => socket.AttachBuffers(channelId, reader, null);

		/// <summary>
		/// Attaches a receive buffer to a socket. Data will be read into this buffer from the other end of the lease.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Identifier for the buffer</param>
		/// <param name="writer">The buffer to attach</param>
		public static void AttachReceiveBuffer(this IComputeSocket socket, int channelId, IComputeBufferWriter writer) => socket.AttachBuffers(channelId, null, writer);
	}
}
