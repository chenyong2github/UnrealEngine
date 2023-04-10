// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Socket for sending and reciving data using a "push" model. The application can attach multiple writers to accept received data.
	/// </summary>
	public interface IComputeSocket : IAsyncDisposable
	{
		/// <summary>
		/// Sends data to a remote channel
		/// </summary>
		/// <param name="channelId">Channel to write to</param>
		/// <param name="memory">Memory to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask SendAsync(int channelId, ReadOnlyMemory<byte> memory, CancellationToken cancellationToken = default);

		/// <summary>
		/// Marks a channel as complete
		/// </summary>
		/// <param name="channelId">Channel to mark as complete</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask MarkCompleteAsync(int channelId, CancellationToken cancellationToken = default);

		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="writer">Writer for the received data</param>
		void AttachRecvBuffer(int channelId, IComputeBufferWriter writer);

		/// <summary>
		/// Attaches a buffer to send data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="reader">Reader for the data to send</param>
		void AttachSendBuffer(int channelId, IComputeBufferReader reader);

		/// <summary>
		/// Attempt to gracefully close the current connection and shutdown both ends of the transport
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask CloseAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Static methods for sockets
	/// </summary>
	public static class ComputeSocket
	{
		/// <summary>
		/// Create a new compute socket
		/// </summary>
		/// <param name="transport">Transport to use for the socket</param>
		/// <param name="logger">Logger for diagnostic output</param>
		public static IComputeSocket Create(IComputeTransport transport, ILogger logger) => new DefaultComputeSocket(transport, logger);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeMessageChannel"/>
	/// </summary>
	public static class ComputeSocketExtensions
	{
		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="logger">Logger for the channel</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, ILogger logger, CancellationToken cancellationToken = default) 
			=> socket.CreateMessageChannel(channelId, 65536, logger, cancellationToken);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="bufferSize">Size of the send and receive buffer</param>
		/// <param name="logger">Logger for the channel</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, int bufferSize, ILogger logger, CancellationToken cancellationToken = default)
			=> socket.CreateMessageChannel(channelId, bufferSize, bufferSize, logger, cancellationToken);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="sendBufferSize">Size of the send buffer</param>
		/// <param name="recvBufferSize">Size of the recieve buffer</param>
		/// <param name="logger">Logger for the channel</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, int sendBufferSize, int recvBufferSize, ILogger logger, CancellationToken cancellationToken = default)
		{
			PooledBuffer sendBuffer = new PooledBuffer(sendBufferSize);
			PooledBuffer recvBuffer = new PooledBuffer(recvBufferSize);
			return new ComputeMessageChannel(socket, channelId, sendBuffer, recvBuffer, logger);
		}
	}
}
