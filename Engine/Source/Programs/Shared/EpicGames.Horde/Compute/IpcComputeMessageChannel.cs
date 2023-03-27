// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Buffers;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Message channel for communicating with child processes without using a socket
	/// </summary>
	public class IpcComputeMessageChannel : ComputeMessageChannel
	{
		readonly IntPtr _parentProcessHandle;
		readonly SharedMemoryBuffer _recvBuffer;
		readonly SharedMemoryBuffer _sendBuffer;

		static IntPtr s_currentProcessHandle;

		/// <summary>
		/// 
		/// </summary>
		/// <param name="capacity"></param>
		/// <param name="logger"></param>
		public IpcComputeMessageChannel(long capacity, ILogger logger)
			: this(IntPtr.Zero, new SharedMemoryBuffer(capacity), new SharedMemoryBuffer(capacity), logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		internal IpcComputeMessageChannel(IntPtr parentProcessHandle, SharedMemoryBuffer recvBuffer, SharedMemoryBuffer sendBuffer, ILogger logger)
			: base(recvBuffer.Reader, sendBuffer.Writer, logger)
		{
			_parentProcessHandle = parentProcessHandle;
			_recvBuffer = recvBuffer;
			_sendBuffer = sendBuffer;
		}

		/// <inheritdoc/>
		protected override ValueTask DisposeAsync(bool disposing)
		{
			_recvBuffer.Dispose();
			_sendBuffer.Dispose();
			return new ValueTask();
		}

		/// <summary>
		/// Creates an IPC channel from a string handle
		/// </summary>
		/// <param name="handle">Handle to parse</param>
		/// <param name="logger">Logger for the new channel</param>
		public static IpcComputeMessageChannel FromStringHandle(string handle, ILogger logger)
		{
			string[] handles = handle.Split('/');
			if (handles.Length != 3)
			{
				throw new ArgumentException($"Invalid handle for IPC channel: {handle}", nameof(handle));
			}

			IntPtr parentProcessHandle = new IntPtr((long)UInt64.Parse(handles[0], NumberStyles.None, null));
			SharedMemoryBuffer recvBuffer = SharedMemoryBuffer.OpenIpcHandle(handles[1]);
			SharedMemoryBuffer sendBuffer = SharedMemoryBuffer.OpenIpcHandle(handles[2]);

			return new IpcComputeMessageChannel(parentProcessHandle, sendBuffer, recvBuffer, logger); // Note: Swapping send/recv buffer here
		}

		/// <summary>
		/// Gets a string handle that can be used to open this channel in another process
		/// </summary>
		public string GetStringHandle()
		{
			if (s_currentProcessHandle == IntPtr.Zero)
			{
				IntPtr currentProcessHandle = Native.GetCurrentProcess();
				Native.DuplicateHandle(currentProcessHandle, currentProcessHandle, currentProcessHandle, out s_currentProcessHandle, 0, true, Native.DUPLICATE_SAME_ACCESS);
			}
			return $"{(ulong)s_currentProcessHandle.ToInt64()}/{_recvBuffer.GetIpcHandle()}/{_sendBuffer.GetIpcHandle()}";
		}

		/// <summary>
		/// Force the stream closed
		/// </summary>
		public void ForceComplete() => _recvBuffer.FinishWriting();

		/// <summary>
		/// Registers a receive buffer with the agent process
		/// </summary>
		public async ValueTask AttachRecvBufferAsync(int channelId, SharedMemoryBuffer buffer, CancellationToken cancellationToken)
		{
			string handle = buffer.GetIpcHandle(_parentProcessHandle);

			using IComputeMessageBuilder message = await CreateMessageAsync(ComputeMessageType.AttachRecvBuffer, handle.Length + 20, cancellationToken);
			message.WriteInt32(channelId);
			message.WriteString(handle);
			message.Send();
		}

		/// <summary>
		/// Parse a <see cref="AttachRecvBufferRequest"/> message
		/// </summary>
		public static AttachRecvBufferRequest ParseAttachRecvBuffer(IComputeMessage message)
		{
			int channelId = message.ReadInt32();
			string handle = message.ReadString();
			return new AttachRecvBufferRequest(channelId, handle);
		}

		/// <summary>
		/// Registers a send buffer with the agent process
		/// </summary>
		public async ValueTask AttachSendBufferAsync(int channelId, SharedMemoryBuffer buffer, CancellationToken cancellationToken)
		{
			string handle = buffer.GetIpcHandle(_parentProcessHandle);

			using IComputeMessageBuilder message = await CreateMessageAsync(ComputeMessageType.AttachSendBuffer, handle.Length + 20, cancellationToken);
			message.WriteInt32(channelId);
			message.WriteString(handle);
			message.Send();
		}

		/// <summary>
		/// Parse a <see cref="AttachSendBufferRequest"/> message
		/// </summary>
		public static AttachSendBufferRequest ParseAttachSendBuffer(IComputeMessage message)
		{
			int channelId = message.ReadInt32();
			string handle = message.ReadString();
			return new AttachSendBufferRequest(channelId, handle);
		}
	}
}
