// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute.Sockets;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Socket for sending and reciving messages
	/// </summary>
	public interface IComputeSocket : IAsyncDisposable
	{
		/// <summary>
		/// Creates a new buffer of the given capacity, suitable for this socket type
		/// </summary>
		/// <param name="capacity">Capacity of the buffer</param>
		IComputeBuffer CreateBuffer(long capacity);

		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="writer">Writer for the received data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask AttachRecvBufferAsync(int channelId, IComputeBufferWriter writer, CancellationToken cancellationToken);

		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="reader">Reader for incoming data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask AttachSendBufferAsync(int channelId, IComputeBufferReader reader, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Utility methods for creating sockets
	/// </summary>
	public static class ComputeSocket
	{
		/// <summary>
		/// Creates a socket for a worker
		/// </summary>
		/// <param name="logger">Logger for diagnostic messages</param>
		public static IComputeSocket ConnectAsWorker(ILogger logger) => WorkerComputeSocket.FromEnvironment(logger);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeMessageChannel"/>
	/// </summary>
	public static class ComputeSocketExtensions
	{
		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="capacity">Capacity for the buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static ValueTask<IComputeBufferReader> AttachRecvBufferAsync(this IComputeSocket socket, int channelId, long capacity, CancellationToken cancellationToken = default)
		{
			return AttachRecvBufferAsync(socket, channelId, socket.CreateBuffer(capacity), cancellationToken);
		}

		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="buffer">Buffer to attach to the socket</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask<IComputeBufferReader> AttachRecvBufferAsync(this IComputeSocket socket, int channelId, IComputeBuffer buffer, CancellationToken cancellationToken = default)
		{
			(IComputeBufferReader reader, IComputeBufferWriter writer) = buffer.ToShared();
			await socket.AttachRecvBufferAsync(channelId, writer, cancellationToken);
			return reader;
		}

		/// <summary>
		/// Attaches a buffer from which to send data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="capacity">Capacity for the buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static ValueTask<IComputeBufferWriter> AttachSendBufferAsync(this IComputeSocket socket, int channelId, long capacity, CancellationToken cancellationToken = default) 
			=> AttachSendBufferAsync(socket, channelId, socket.CreateBuffer(capacity), cancellationToken);

		/// <summary>
		/// Attaches a buffer from which to send data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="buffer">Buffer for the data to send</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask<IComputeBufferWriter> AttachSendBufferAsync(this IComputeSocket socket, int channelId, IComputeBuffer buffer, CancellationToken cancellationToken = default)
		{
			(IComputeBufferReader reader, IComputeBufferWriter writer) = buffer.ToShared();
			await socket.AttachSendBufferAsync(channelId, reader, cancellationToken);
			return writer;
		}

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="logger">Logger for the channel</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static ValueTask<IComputeMessageChannel> CreateMessageChannelAsync(this IComputeSocket socket, int channelId, ILogger logger, CancellationToken cancellationToken = default) 
			=> socket.CreateMessageChannelAsync(channelId, 65536, logger, cancellationToken);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="bufferSize">Size of the send and receive buffer</param>
		/// <param name="logger">Logger for the channel</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static ValueTask<IComputeMessageChannel> CreateMessageChannelAsync(this IComputeSocket socket, int channelId, long bufferSize, ILogger logger, CancellationToken cancellationToken = default)
			=> socket.CreateMessageChannelAsync(channelId, bufferSize, bufferSize, logger, cancellationToken);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="sendBufferSize">Size of the send buffer</param>
		/// <param name="recvBufferSize">Size of the recieve buffer</param>
		/// <param name="logger">Logger for the channel</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask<IComputeMessageChannel> CreateMessageChannelAsync(this IComputeSocket socket, int channelId, long sendBufferSize, long recvBufferSize, ILogger logger, CancellationToken cancellationToken = default)
		{
			IComputeBufferReader reader = await socket.AttachRecvBufferAsync(channelId, recvBufferSize, cancellationToken);
			IComputeBufferWriter writer = await socket.AttachSendBufferAsync(channelId, sendBufferSize, cancellationToken);
			return new ComputeMessageChannel(reader, writer, logger);
		}
	}
}
