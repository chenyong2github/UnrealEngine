// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading.Tasks;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute.Sockets
{
	sealed class WorkerComputeSocket : IComputeSocket
	{
		readonly IpcComputeMessageChannel _ipcMessageChannel;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ipcMessageChannel">Channel for IPC messages</param>
		/// <param name="logger">Logger for trace output</param>
		public WorkerComputeSocket(IpcComputeMessageChannel ipcMessageChannel, ILogger logger)
		{
			_ipcMessageChannel = ipcMessageChannel;
			_logger = logger;
		}

		/// <inheritdoc/>
		public ValueTask DisposeAsync() => _ipcMessageChannel.DisposeAsync();

		/// <inheritdoc/>
		public IComputeBuffer CreateBuffer(long capacity) => new SharedMemoryBuffer(capacity);

		/// <inheritdoc/>
		public void AttachRecvBuffer(int channelId, IComputeBufferWriter writer)
		{
			SharedMemoryBuffer buffer = (SharedMemoryBuffer)writer.Buffer;
			_ipcMessageChannel.AttachRecvBuffer(channelId, buffer);
			_logger.LogTrace("Attached recv buffer to channel {ChannelId}", channelId);
		}

		/// <inheritdoc/>
		public void AttachSendBuffer(int channelId, IComputeBufferReader reader)
		{
			SharedMemoryBuffer buffer = (SharedMemoryBuffer)reader.Buffer;
			_ipcMessageChannel.AttachSendBuffer(channelId, buffer);
			_logger.LogTrace("Attached send buffer to channel {ChannelId}", channelId);
		}
	}
}
