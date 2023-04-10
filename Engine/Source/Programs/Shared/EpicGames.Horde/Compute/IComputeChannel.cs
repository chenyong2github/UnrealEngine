// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Conventional TCP-like interface for writing data to a socket. Sends are "push", receives are "pull".
	/// </summary>
	public interface IComputeChannel : IDisposable
	{
		/// <summary>
		/// Sends data to a remote channel
		/// </summary>
		/// <param name="memory">Memory to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask SendAsync(ReadOnlyMemory<byte> memory, CancellationToken cancellationToken = default);

		/// <summary>
		/// Marks a channel as complete
		/// </summary>
		/// <param name="buffer">Buffer to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Opens a channel for compute workers
	/// </summary>
	public static class ComputeChannel
	{
		class BufferedReaderChannel : IComputeChannel
		{
			readonly IComputeSocket _socket;
			readonly int _channelId;
			readonly IComputeBuffer _recvBuffer;

			public BufferedReaderChannel(IComputeSocket socket, int channelId, IComputeBuffer recvBuffer)
			{
				_socket = socket;
				_channelId = channelId;
				_recvBuffer = recvBuffer;

				_socket.AttachRecvBuffer(_channelId, recvBuffer.Writer);
			}

			public void Dispose()
			{
				_recvBuffer.Dispose();
			}

			public ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) => _recvBuffer.Reader.ReadAsync(buffer, cancellationToken);

			public ValueTask SendAsync(ReadOnlyMemory<byte> memory, CancellationToken cancellationToken = default) => _socket.SendAsync(_channelId, memory, cancellationToken);
		}

		class BufferedReaderWriterChannel : IComputeChannel
		{
			readonly IComputeSocket _socket;
			readonly int _channelId;
			readonly IComputeBuffer _recvBuffer;
			readonly IComputeBuffer _sendBuffer;

			public BufferedReaderWriterChannel(IComputeSocket socket, int channelId, IComputeBuffer recvBuffer, IComputeBuffer sendBuffer)
			{
				_socket = socket;
				_channelId = channelId;
				_recvBuffer = recvBuffer;
				_sendBuffer = sendBuffer;

				_socket.AttachRecvBuffer(_channelId, recvBuffer.Writer);
				_socket.AttachSendBuffer(_channelId, sendBuffer.Reader);
			}

			public void Dispose()
			{
				_recvBuffer.Dispose();
				_sendBuffer.Dispose();
			}

			public ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) => _recvBuffer.Reader.ReadAsync(buffer, cancellationToken);

			public ValueTask SendAsync(ReadOnlyMemory<byte> memory, CancellationToken cancellationToken = default) => _sendBuffer.Writer.WriteAsync(memory, cancellationToken);
		}

		class WorkerChannel : IComputeChannel
		{
			readonly IComputeBuffer _recvBuffer;
			readonly IComputeBuffer _sendBuffer;

			public WorkerChannel(IComputeBuffer recvBuffer, IComputeBuffer sendBuffer)
			{
				_recvBuffer = recvBuffer;
				_sendBuffer = sendBuffer;
			}

			public void Dispose() { }

			public ValueTask<int> ReceiveAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) => _recvBuffer.Reader.ReadAsync(buffer, cancellationToken);

			public ValueTask SendAsync(ReadOnlyMemory<byte> memory, CancellationToken cancellationToken = default) => _sendBuffer.Writer.WriteAsync(memory, cancellationToken);
		}

		/// <summary>
		/// Name of the environment variable for passing the name of the compute channel
		/// </summary>
		public const string IpcEnvVar = "UE_HORDE_COMPUTE_IPC";

		internal static string GetSendBufferName(string baseName) => $"{baseName}_SEND";
		internal static string GetRecvBufferName(string baseName) => $"{baseName}_RECV";


		/// <summary>
		/// Creates a socket for a worker
		/// </summary>
		public static IComputeChannel ConnectAsWorker()
		{
			string? baseName = Environment.GetEnvironmentVariable(IpcEnvVar);
			if (baseName == null)
			{
				throw new InvalidOperationException($"Environment variable {IpcEnvVar} is not defined; cannot connect as worker.");
			}

			SharedMemoryBuffer recvBuffer = SharedMemoryBuffer.OpenExisting(GetRecvBufferName(baseName));
			SharedMemoryBuffer sendBuffer = SharedMemoryBuffer.OpenExisting(GetSendBufferName(baseName));
			return new WorkerChannel(recvBuffer, sendBuffer);
		}

		/// <summary>
		/// Creates a channel using a socket and receive buffer
		/// </summary>
		/// <param name="socket">Socket to use for sending data</param>
		/// <param name="channelId">Channel id to send and receive data</param>
		/// <param name="recvBuffer">Buffer for receiving data</param>
		public static IComputeChannel CreateChannel(this IComputeSocket socket, int channelId, IComputeBuffer recvBuffer) => new BufferedReaderChannel(socket, channelId, recvBuffer);

		/// <summary>
		/// Creates a channel using a socket and receive buffer
		/// </summary>
		/// <param name="socket">Socket to use for sending data</param>
		/// <param name="channelId">Channel id to send and receive data</param>
		/// <param name="recvBuffer">Buffer for receiving data</param>
		/// <param name="sendBuffer">Buffer for sending data</param>
		public static IComputeChannel CreateChannel(this IComputeSocket socket, int channelId, IComputeBuffer recvBuffer, IComputeBuffer sendBuffer) => new BufferedReaderWriterChannel(socket, channelId, recvBuffer, sendBuffer);
	}

	/// <summary>
	/// Extension methods for <see cref="IComputeChannel"/>
	/// </summary>
	public static class ComputeChannelExtensions
	{
		/// <summary>
		/// Reads a complete message from the given socket, retrying reads until the buffer is full.
		/// </summary>
		/// <param name="channel">Channel to read from</param>
		/// <param name="buffer">Buffer to store the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask ReceiveMessageAsync(this IComputeChannel channel, Memory<byte> buffer, CancellationToken cancellationToken = default)
		{
			if (!await TryReceiveMessageAsync(channel, buffer, cancellationToken))
			{
				throw new EndOfStreamException();
			}
		}

		/// <summary>
		/// Reads either a full message or end of stream from the channel
		/// </summary>
		/// <param name="channel">Channel to read from</param>
		/// <param name="buffer">Buffer to store the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async ValueTask<bool> TryReceiveMessageAsync(this IComputeChannel channel, Memory<byte> buffer, CancellationToken cancellationToken = default)
		{
			for (int offset = 0; offset < buffer.Length;)
			{
				int read = await channel.ReceiveAsync(buffer.Slice(offset), cancellationToken);
				if (read == 0)
				{
					return false;
				}
				offset += read;
			}
			return true;
		}
	}
}
