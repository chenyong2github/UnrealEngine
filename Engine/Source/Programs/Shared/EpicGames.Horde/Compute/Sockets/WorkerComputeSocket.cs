// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute.Sockets
{
	sealed class WorkerComputeSocket : IComputeSocket
	{
		/// <summary>
		/// Environment variable for channel info for communicating with the worker process
		/// </summary>
		public const string IpcEnvVar = "UE_HORDE_COMPUTE_IPC";

		readonly IntPtr _parentProcessHandle;
		readonly SharedMemoryBuffer _ipcBuffer;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="parentProcessHandle">Handle to the parent process</param>
		/// <param name="ipcBuffer">Channel for IPC messages</param>
		/// <param name="logger">Logger for trace output</param>
		public WorkerComputeSocket(IntPtr parentProcessHandle, SharedMemoryBuffer ipcBuffer, ILogger logger)
		{
			_parentProcessHandle = parentProcessHandle;
			_ipcBuffer = ipcBuffer;
			_logger = logger;
		}

		public static WorkerComputeSocket FromEnvironment(ILogger logger)
		{
			string? handle = Environment.GetEnvironmentVariable(IpcEnvVar);
			if (handle == null)
			{
				throw new InvalidOperationException($"Environment variable {IpcEnvVar} is not defined; cannot connect as worker.");
			}

			IntPtr[] handles = handle.Split('.').Select(x => new IntPtr((long)UInt64.Parse(x, NumberStyles.None, null))).ToArray();

			IntPtr parentProcessHandle = handles[0];
			SharedMemoryBuffer ipcBuffer = new SharedMemoryBuffer(handles[1], handles[2], handles[3]);

			return new WorkerComputeSocket(parentProcessHandle, ipcBuffer, logger);
		}

		static IntPtr s_currentProcessHandle;

		public static string GetIpcEnvVarValue(SharedMemoryBuffer ipcBuffer)
		{
			if (s_currentProcessHandle == IntPtr.Zero)
			{
				IntPtr currentProcessHandle = Native.GetCurrentProcess();
				Native.DuplicateHandle(currentProcessHandle, currentProcessHandle, currentProcessHandle, out s_currentProcessHandle, 0, true, Native.DUPLICATE_SAME_ACCESS);
			}

			IntPtr[] handles = new IntPtr[4];
			handles[0] = s_currentProcessHandle;
			ipcBuffer.GetHandles(out handles[1], out handles[2], out handles[3]);

			return String.Join(".", handles.Select(x => ((ulong)x.ToInt64()).ToString()));
		}

		/// <inheritdoc/>
		public ValueTask DisposeAsync()
		{
			_ipcBuffer.Dispose();
			return new ValueTask();
		}

		/// <inheritdoc/>
		public IComputeBuffer CreateBuffer(long capacity) => new SharedMemoryBuffer(capacity);

		/// <inheritdoc/>
		public async ValueTask AttachRecvBufferAsync(int channelId, IComputeBufferWriter writer, CancellationToken cancellationToken)
		{
			SharedMemoryBuffer buffer = (SharedMemoryBuffer)writer.Buffer;

			buffer.DuplicateHandles(_parentProcessHandle, out IntPtr targetMemoryHandle, out IntPtr targetReaderEventHandle, out IntPtr targetWriterEventHandle);
			IpcAttachBufferRequest request = new IpcAttachBufferRequest(IpcMessageType.AttachRecvBuffer, channelId, targetMemoryHandle, targetReaderEventHandle, targetWriterEventHandle);
			request.Write(_ipcBuffer);
			_logger.LogTrace("Attached recv buffer to channel {ChannelId}", channelId);

			await _ipcBuffer.FlushAsync(cancellationToken);
		}

		/// <inheritdoc/>
		public async ValueTask AttachSendBufferAsync(int channelId, IComputeBufferReader reader, CancellationToken cancellationToken)
		{
			SharedMemoryBuffer buffer = (SharedMemoryBuffer)reader.Buffer;

			buffer.DuplicateHandles(_parentProcessHandle, out IntPtr targetMemoryHandle, out IntPtr targetReaderEventHandle, out IntPtr targetWriterEventHandle);
			IpcAttachBufferRequest request = new IpcAttachBufferRequest(IpcMessageType.AttachSendBuffer, channelId, targetMemoryHandle, targetReaderEventHandle, targetWriterEventHandle);
			request.Write(_ipcBuffer);
			_logger.LogTrace("Attached send buffer to channel {ChannelId}", channelId);

			await _ipcBuffer.FlushAsync(cancellationToken);
		}
	}
}
