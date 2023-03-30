// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using EpicGames.Horde.Compute.Buffers;

namespace EpicGames.Horde.Compute
{
	enum IpcMessageType
	{
		Finish,
		AttachSendBuffer,
		AttachRecvBuffer,
	}

	record class IpcAttachBufferRequest(IpcMessageType Type, int ChannelId, IntPtr MemoryHandle, IntPtr ReaderEventHandle, IntPtr WriterEventHandle)
	{
		public static IpcAttachBufferRequest Read(SharedMemoryBuffer buffer)
		{
			ReadOnlySpan<byte> span = buffer.GetReadMemory().Span;

			IpcMessageType type = (IpcMessageType)span[0];
			int ChannelId = BinaryPrimitives.ReadInt32LittleEndian(span.Slice(4));
			IntPtr MemoryMappedFileHandle = new IntPtr(BinaryPrimitives.ReadInt64LittleEndian(span.Slice(8)));
			IntPtr ReaderEventHandle = new IntPtr(BinaryPrimitives.ReadInt64LittleEndian(span.Slice(16)));
			IntPtr WriterEventHandle = new IntPtr(BinaryPrimitives.ReadInt64LittleEndian(span.Slice(24)));
			buffer.AdvanceReadPosition(32);

			return new IpcAttachBufferRequest(type, ChannelId, MemoryMappedFileHandle, ReaderEventHandle, WriterEventHandle);
		}

		public void Write(SharedMemoryBuffer buffer)
		{
			Span<byte> span = buffer.GetWriteMemory().Span;

			span[0] = (byte)Type;
			BinaryPrimitives.WriteInt32LittleEndian(span.Slice(4), ChannelId);
			BinaryPrimitives.WriteInt64LittleEndian(span.Slice(8), MemoryHandle.ToInt64());
			BinaryPrimitives.WriteInt64LittleEndian(span.Slice(16), ReaderEventHandle.ToInt64());
			BinaryPrimitives.WriteInt64LittleEndian(span.Slice(24), WriterEventHandle.ToInt64());

			buffer.AdvanceWritePosition(32);
		}
	}
}
