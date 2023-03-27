// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
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
		public async ValueTask AttachRecvBufferAsync(int channelId, IComputeBufferWriter writer, CancellationToken cancellationToken)
		{
			SharedMemoryBuffer buffer = (SharedMemoryBuffer)writer.Buffer;
			await _ipcMessageChannel.AttachRecvBufferAsync(channelId, buffer, cancellationToken);
			_logger.LogTrace("Attached recv buffer to channel {ChannelId}", channelId);
		}

		/// <inheritdoc/>
		public async ValueTask AttachSendBufferAsync(int channelId, IComputeBufferReader reader, CancellationToken cancellationToken)
		{
			SharedMemoryBuffer buffer = (SharedMemoryBuffer)reader.Buffer;
			await _ipcMessageChannel.AttachSendBufferAsync(channelId, buffer, cancellationToken);
			_logger.LogTrace("Attached send buffer to channel {ChannelId}", channelId);
		}
	}
}
