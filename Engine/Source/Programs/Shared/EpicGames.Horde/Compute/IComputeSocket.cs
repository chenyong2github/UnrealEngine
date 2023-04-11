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
		/// <param name="buffer">Buffer to receive data</param>
		void AttachRecvBuffer(int channelId, IComputeBuffer buffer);

		/// <summary>
		/// Attaches a buffer to send data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="buffer">Buffer to send data from</param>
		void AttachSendBuffer(int channelId, IComputeBuffer buffer);

		/// <summary>
		/// Attempt to gracefully close the current connection and shutdown both ends of the transport
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask CloseAsync(CancellationToken cancellationToken = default);
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
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, ILogger logger) 
			=> socket.CreateMessageChannel(channelId, 65536, logger);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="bufferSize">Size of the send and receive buffer</param>
		/// <param name="logger">Logger for the channel</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, int bufferSize, ILogger logger)
			=> socket.CreateMessageChannel(channelId, bufferSize, bufferSize, logger);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="sendBufferSize">Size of the send buffer</param>
		/// <param name="recvBufferSize">Size of the recieve buffer</param>
		/// <param name="logger">Logger for the channel</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, int sendBufferSize, int recvBufferSize, ILogger logger)
		{
			IComputeBuffer sendBuffer = new PooledBuffer(sendBufferSize).ToSharedInstance();
			IComputeBuffer recvBuffer = new PooledBuffer(recvBufferSize).ToSharedInstance();
			return new ComputeMessageChannel(socket, channelId, sendBuffer, recvBuffer, logger);
		}
	}
}
