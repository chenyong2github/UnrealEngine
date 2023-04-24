// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using EpicGames.Horde.Compute.Buffers;

namespace EpicGames.Horde.Compute
{
	internal enum IpcMessage
	{
		AttachRecvBuffer = 0,
		AttachSendBuffer = 1,
	}

	/// <summary>
	/// Provides functionality for attaching buffers for compute workers 
	/// </summary>
	public sealed class WorkerComputeSocket : IDisposable
	{
		/// <summary>
		/// Name of the environment variable for passing the name of the compute channel
		/// </summary>
		public const string IpcEnvVar = "UE_HORDE_COMPUTE_IPC";

		readonly SharedMemoryBuffer _commandBuffer;

		/// <summary>
		/// Creates a socket for a worker
		/// </summary>
		public WorkerComputeSocket()
		{
			string? baseName = Environment.GetEnvironmentVariable(IpcEnvVar);
			if (baseName == null)
			{
				throw new InvalidOperationException($"Environment variable {IpcEnvVar} is not defined; cannot connect as worker.");
			}

			_commandBuffer = SharedMemoryBuffer.OpenExisting(baseName);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_commandBuffer.Dispose();
		}

		/// <summary>
		/// Attaches a new buffer for receiving data
		/// </summary>
		/// <param name="channelId">Channel id for the buffer</param>
		/// <param name="buffer">Buffer to attach</param>
		public void AttachRecvBuffer(int channelId, SharedMemoryBuffer buffer) => AttachBuffer(IpcMessage.AttachRecvBuffer, channelId, buffer);

		/// <summary>
		/// Attaches a new buffer for sending data
		/// </summary>
		/// <param name="channelId">Channel id for the buffer</param>
		/// <param name="buffer">Buffer to attach</param>
		public void AttachSendBuffer(int channelId, SharedMemoryBuffer buffer) => AttachBuffer(IpcMessage.AttachSendBuffer, channelId, buffer);

		void AttachBuffer(IpcMessage message, int channelId, SharedMemoryBuffer buffer)
		{
			MemoryWriter writer = new MemoryWriter(_commandBuffer.Writer.GetMemory());
			writer.WriteUnsignedVarInt((int)message);
			writer.WriteUnsignedVarInt(channelId);
			writer.WriteString(buffer.Name);
			_commandBuffer.Writer.Advance(writer.Length);
		}
	}

	/// <summary>
	/// Extension methods for worker sockets
	/// </summary>
	public static class WorkerComputeSocketExtensions
	{
		/// <summary>
		/// Creates a channel using a socket and receive buffer
		/// </summary>
		/// <param name="socket">Socket to use for sending data</param>
		/// <param name="channelId">Channel id to send and receive data</param>
		/// <param name="recvBuffer">Buffer for receiving data</param>
		/// <param name="sendBuffer">Buffer for sending data</param>
		public static IComputeChannel CreateChannel(this WorkerComputeSocket socket, int channelId, SharedMemoryBuffer recvBuffer, SharedMemoryBuffer sendBuffer)
		{
			socket.AttachRecvBuffer(channelId, recvBuffer);
			socket.AttachSendBuffer(channelId, sendBuffer);
			return new ComputeChannel.BufferedReaderWriterChannel(recvBuffer, sendBuffer);
		}
	}
}
