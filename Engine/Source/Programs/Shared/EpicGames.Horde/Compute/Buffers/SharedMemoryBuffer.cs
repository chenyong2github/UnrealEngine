// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using Microsoft.Win32.SafeHandles;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.IO;
using System.Buffers.Binary;
using System.Threading.Tasks;
using System.Threading;
using System.Linq;
using System.Globalization;
using System.ComponentModel;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// Implementation of <see cref="IComputeBuffer"/> suitable for cross-process communication
	/// </summary>
	public sealed class SharedMemoryBuffer : ComputeBufferBase
	{
		const int HeaderLength = 32;

		long ReadPosition
		{
			get => BinaryPrimitives.ReadInt64LittleEndian(_header.Span.Slice(8));
			set => BinaryPrimitives.WriteInt64LittleEndian(_header.Span.Slice(8), value);
		}

		long WritePosition
		{
			get => BinaryPrimitives.ReadInt64LittleEndian(_header.Span.Slice(16));
			set => BinaryPrimitives.WriteInt64LittleEndian(_header.Span.Slice(16), value);
		}

		bool FinishedWriting
		{
			get => _header.Span[24] != 0;
			set => _header.Span[24] = value ? (byte)1 : (byte)0;
		}

		const int ChunkOffsetPow2 = 10;

		readonly MemoryMappedFile _memoryMappedFile;
		readonly MemoryMappedViewAccessor _memoryMappedViewAccessor;
		readonly MemoryMappedView _memoryMappedView;
		readonly long _length;
		readonly Memory<byte> _header;
		readonly Memory<byte> _memory; // TODO: DEPRECATE
		readonly Memory<byte>[] _chunks;

		readonly Native.EventHandle _writtenEvent;
		readonly Native.EventHandle _flushedEvent;

		/// <inheritdoc/>
		public override long Length => _length;

		/// <summary>
		/// Creates a new shared memory buffer with the given capacity
		/// </summary>
		/// <param name="capacity">Capacity of the buffer</param>
		public SharedMemoryBuffer(long capacity)
			: this(MemoryMappedFile.CreateNew(null, capacity, MemoryMappedFileAccess.ReadWrite, MemoryMappedFileOptions.None, HandleInheritability.Inheritable))
		{
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="buffer"></param>
		public SharedMemoryBuffer(MemoryMappedFile buffer)
			: this(buffer, new Native.EventHandle(HandleInheritability.Inheritable), new Native.EventHandle(HandleInheritability.Inheritable))
		{
		}

		/// <summary>
		/// Creates a shared memory buffer from a memory mapped file
		/// </summary>
		internal SharedMemoryBuffer(MemoryMappedFile memoryMappedFile, Native.EventHandle writtenEvent, Native.EventHandle flushedEvent)
		{
			_memoryMappedFile = memoryMappedFile;
			_memoryMappedViewAccessor = memoryMappedFile.CreateViewAccessor();
			_memoryMappedView = new MemoryMappedView(_memoryMappedViewAccessor);

			_writtenEvent = writtenEvent;
			_flushedEvent = flushedEvent;

			_header = _memoryMappedView.GetMemory(0, HeaderLength);
			_length = _memoryMappedViewAccessor.Capacity;

			_memory = _memoryMappedView.GetMemory(HeaderLength, (int)(_length - HeaderLength)); // TODO: REMOVE

			int chunkCount = (int)((_length + ((1 << ChunkOffsetPow2) - 1)) >> ChunkOffsetPow2);
			_chunks = new Memory<byte>[chunkCount];

			for (int chunkIdx = 0; chunkIdx < chunkCount; chunkIdx++)
			{
				long offset = (chunkIdx << ChunkOffsetPow2) + HeaderLength;
				int length = (int)Math.Min(Length - offset, Int32.MaxValue);
				_chunks[chunkIdx] = _memoryMappedView.GetMemory(offset, length);
			}
		}

		/// <summary>
		/// Opens an IpcBuffer from a string passed in from another process
		/// </summary>
		/// <param name="handle">Descriptor for the buffer to open</param>
		public static MemoryMappedFile OpenMemoryMappedFileFromHandle(IntPtr handle)
		{
			MethodInfo? setHandleInfo = typeof(SafeMemoryMappedFileHandle).GetMethod("SetHandle", BindingFlags.Instance | BindingFlags.NonPublic, new[] { typeof(IntPtr) });
			if (setHandleInfo == null)
			{
				throw new InvalidOperationException("Cannot find SetHandle method for SafeMemoryMappedFileHandle");
			}

			ConstructorInfo? constructorInfo = typeof(MemoryMappedFile).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(SafeMemoryMappedFileHandle) }, null);
			if (constructorInfo == null)
			{
				throw new InvalidOperationException("Cannot find private constructor for memory mapped file");
			}

			SafeMemoryMappedFileHandle safeHandle = new SafeMemoryMappedFileHandle();
			setHandleInfo.Invoke(safeHandle, new object[] { handle });

			return (MemoryMappedFile)constructorInfo.Invoke(new object[] { safeHandle });
		}

		/// <summary>
		/// Opens a <see cref="SharedMemoryBuffer"/> from handles returned by <see cref="GetIpcHandle()"/>
		/// </summary>
		public static SharedMemoryBuffer OpenIpcHandle(string handle)
		{
			IntPtr[] handles = handle.Split('.').Select(x => new IntPtr((long)UInt64.Parse(x, NumberStyles.None, null))).ToArray();
			if (handles.Length != 3)
			{
				throw new ArgumentException($"Malformed ipc handle string: {handle}", nameof(handle));
			}

			MemoryMappedFile memoryMappedFile = OpenMemoryMappedFileFromHandle(handles[0]);
			Native.EventHandle writtenEvent = new Native.EventHandle(handles[1], true);
			Native.EventHandle flushedEvent = new Native.EventHandle(handles[2], true);

			return new SharedMemoryBuffer(memoryMappedFile, writtenEvent, flushedEvent);
		}

		/// <summary>
		/// Gets a string that can be used to open the same buffer in another process
		/// </summary>
		public string GetIpcHandle()
		{
			IntPtr memoryHandle = _memoryMappedFile.SafeMemoryMappedFileHandle.DangerousGetHandle();
			IntPtr writtenEventHandle = _writtenEvent.SafeWaitHandle.DangerousGetHandle();
			IntPtr flushedEventHandle = _flushedEvent.SafeWaitHandle.DangerousGetHandle();
			return GetIpcHandle(memoryHandle, writtenEventHandle, flushedEventHandle);
		}

		/// <summary>
		/// Duplicates handles for this buffer into the given process, and returns a string that the target process can use to open it.
		/// </summary>
		/// <param name="targetProcessHandle">Handle to the target process</param>
		/// <returns>String that the target process can pass into a call to <see cref="OpenIpcHandle(String)"/> to open the buffer.</returns>
		public string GetIpcHandle(IntPtr targetProcessHandle)
		{
			IntPtr sourceProcessHandle = Native.GetCurrentProcess();

			IntPtr targetMemoryHandle;
			if (!Native.DuplicateHandle(sourceProcessHandle, _memoryMappedFile.SafeMemoryMappedFileHandle.DangerousGetHandle(), targetProcessHandle, out targetMemoryHandle, 0, false, Native.DUPLICATE_SAME_ACCESS))
			{
				throw new Win32Exception();
			}

			IntPtr targetWrittenEventHandle;
			if (!Native.DuplicateHandle(sourceProcessHandle, _writtenEvent.SafeWaitHandle.DangerousGetHandle(), targetProcessHandle, out targetWrittenEventHandle, 0, false, Native.DUPLICATE_SAME_ACCESS))
			{
				throw new Win32Exception();
			}

			IntPtr targetFlushedEventHandle;
			if(!Native.DuplicateHandle(sourceProcessHandle, _flushedEvent.SafeWaitHandle.DangerousGetHandle(), targetProcessHandle, out targetFlushedEventHandle, 0, false, Native.DUPLICATE_SAME_ACCESS))
			{
				throw new Win32Exception();
			}

			return GetIpcHandle(targetMemoryHandle, targetWrittenEventHandle, targetFlushedEventHandle);
		}

		static string GetIpcHandle(IntPtr memoryHandle, IntPtr writtenEventHandle, IntPtr flushedEventHandle)
		{
			return $"{(ulong)memoryHandle.ToInt64()}.{(ulong)writtenEventHandle.ToInt64()}.{(ulong)flushedEventHandle.ToInt64()}";
		}

		/// <inheritdoc/>
		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				_flushedEvent.Dispose();
				_writtenEvent.Dispose();
				_memoryMappedView.Dispose();
				_memoryMappedViewAccessor.Dispose();
				_memoryMappedFile.Dispose();
			}
		}

		/// <inheritdoc/>
		public override Memory<byte> GetMemory(long offset, int length)
		{
			int chunkIdx = (int)(offset >> ChunkOffsetPow2);
			long baseOffset = offset - (chunkIdx << ChunkOffsetPow2);
			return _chunks[chunkIdx].Slice((int)(offset - baseOffset), length);
		}

		#region Reader

		/// <inheritdoc/>
		public override bool FinishedReading() => FinishedWriting && ReadPosition == WritePosition;

		/// <inheritdoc/>
		public override void AdvanceReadPosition(int size)
		{
			ReadPosition += size;

			if (ReadPosition == WritePosition)
			{
				_flushedEvent.Set();
			}
		}

		/// <inheritdoc/>
		public override ReadOnlyMemory<byte> GetReadMemory() => _memory.Slice((int)ReadPosition, (int)(WritePosition - ReadPosition));

		/// <inheritdoc/>
		public override async ValueTask WaitForDataAsync(int currentLength, CancellationToken cancellationToken)
		{
			long initialWritePosition = ReadPosition + currentLength;
			for (; ; )
			{
				if (FinishedWriting || WritePosition > initialWritePosition)
				{
					return;
				}
				else
				{
					await _writtenEvent.WaitOneAsync(cancellationToken);
				}
			}
		}

		#endregion

		#region Writer

		/// <inheritdoc/>
		public override void FinishWriting()
		{
			FinishedWriting = true;
			_writtenEvent.Set();
		}

		/// <inheritdoc/>
		public override void AdvanceWritePosition(int size)
		{
			if (FinishedWriting)
			{
				throw new InvalidOperationException("Cannot update write position after marking as complete");
			}

			WritePosition += size;
			_writtenEvent.Set();
		}

		/// <inheritdoc/>
		public override Memory<byte> GetWriteMemory() => _memory.Slice((int)WritePosition);

		/// <inheritdoc/>
		public override async ValueTask FlushWritesAsync(CancellationToken cancellationToken)
		{
			while (ReadPosition < WritePosition)
			{
				await _flushedEvent.WaitOneAsync(cancellationToken);
			}
		}

		#endregion
	}
}
