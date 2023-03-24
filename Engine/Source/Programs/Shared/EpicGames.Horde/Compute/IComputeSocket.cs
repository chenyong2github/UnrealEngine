// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Globalization;
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
		void AttachRecvBuffer(int channelId, IComputeBufferWriter writer);

		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="reader">Reader for incoming data</param>
		void AttachSendBuffer(int channelId, IComputeBufferReader reader);
	}

	/// <summary>
	/// Utility methods for creating sockets
	/// </summary>
	public static class ComputeSocket
	{
		/// <summary>
		/// Environment variable for channel info for communicating with the worker process
		/// </summary>
		public const string WorkerIpcEnvVar = "UE_HORDE_COMPUTE_IPC";

		/// <summary>
		/// Creates a socket for a worker
		/// </summary>
		/// <param name="logger">Logger for diagnostic messages</param>
		public static IComputeSocket ConnectAsWorker(ILogger logger)
		{
			string? handle = Environment.GetEnvironmentVariable(WorkerIpcEnvVar);
			if (handle == null)
			{
				throw new InvalidOperationException($"Environment variable {WorkerIpcEnvVar} is not defined; cannot connect as worker.");
			}

			try
			{
				IpcComputeMessageChannel channel = IpcComputeMessageChannel.FromStringHandle(handle, logger);
				return new WorkerComputeSocket(channel, logger);
			}
			catch(Exception ex)
			{
				throw new Exception($"While connecting using '{handle}'", ex);
			}
		}
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
		public static IComputeBufferReader AttachRecvBuffer(this IComputeSocket socket, int channelId, long capacity) => AttachRecvBuffer(socket, channelId, socket.CreateBuffer(capacity));

		/// <summary>
		/// Attaches a buffer to receive data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="buffer">Buffer to attach to the socket</param>
		public static IComputeBufferReader AttachRecvBuffer(this IComputeSocket socket, int channelId, IComputeBuffer buffer)
		{
			(IComputeBufferReader reader, IComputeBufferWriter writer) = buffer.ToShared();
			socket.AttachRecvBuffer(channelId, writer);
			return reader;
		}

		/// <summary>
		/// Attaches a buffer from which to send data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="capacity">Capacity for the buffer</param>
		public static IComputeBufferWriter AttachSendBuffer(this IComputeSocket socket, int channelId, long capacity) => AttachSendBuffer(socket, channelId, socket.CreateBuffer(capacity));

		/// <summary>
		/// Attaches a buffer from which to send data.
		/// </summary>
		/// <param name="socket">Socket to attach to</param>
		/// <param name="channelId">Channel to receive data on</param>
		/// <param name="buffer">Buffer for the data to send</param>
		public static IComputeBufferWriter AttachSendBuffer(this IComputeSocket socket, int channelId, IComputeBuffer buffer)
		{
			(IComputeBufferReader reader, IComputeBufferWriter writer) = buffer.ToShared();
			socket.AttachSendBuffer(channelId, reader);
			return writer;
		}

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="logger">Logger for the channel</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, ILogger logger) => socket.CreateMessageChannel(channelId, 65536, logger);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="bufferSize">Size of the send and receive buffer</param>
		/// <param name="logger">Logger for the channel</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, long bufferSize, ILogger logger) => CreateMessageChannel(socket, channelId, bufferSize, bufferSize, logger);

		/// <summary>
		/// Creates a message channel with the given identifier
		/// </summary>
		/// <param name="socket">Socket to create a channel for</param>
		/// <param name="channelId">Identifier for the channel</param>
		/// <param name="sendBufferSize">Size of the send buffer</param>
		/// <param name="recvBufferSize">Size of the recieve buffer</param>
		/// <param name="logger">Logger for the channel</param>
		public static IComputeMessageChannel CreateMessageChannel(this IComputeSocket socket, int channelId, long sendBufferSize, long recvBufferSize, ILogger logger)
		{
			IComputeBufferReader reader = socket.AttachRecvBuffer(channelId, recvBufferSize);
			IComputeBufferWriter writer = socket.AttachSendBuffer(channelId, sendBufferSize);
			return new ComputeMessageChannel(reader, writer, logger);
		}
	}
}
