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
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="recvBufferWriter">Writer for the buffer to store received data</param>
		void AttachRecvBuffer(int channelId, IComputeBufferWriter recvBufferWriter);

		/// <summary>
		/// Attaches a buffer to send data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="sendBufferReader">Reader for the buffer to send data from</param>
		void AttachSendBuffer(int channelId, IComputeBufferReader sendBufferReader);

		/// <summary>
		/// Attempt to gracefully close the current connection and shutdown both ends of the transport
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask CloseAsync(CancellationToken cancellationToken = default);
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
			using IComputeBuffer sendBuffer = new PooledBuffer(sendBufferSize);
			using IComputeBuffer recvBuffer = new PooledBuffer(recvBufferSize);
			return new ComputeMessageChannel(socket, channelId, sendBuffer, recvBuffer, logger);
		}
	}
}
