// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Win32.SafeHandles;
using System.Runtime.Versioning;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.IO;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// Implementation of <see cref="IComputeBuffer"/> suitable for cross-process communication
	/// </summary>
	[SupportedOSPlatform("windows")]
	public sealed class IpcBuffer : ComputeBuffer, IDisposable
	{
		[StructLayout(LayoutKind.Sequential)]
		class SECURITY_ATTRIBUTES
		{
			public int nLength;
			public IntPtr lpSecurityDescriptor;
			public int bInheritHandle;
		}

		class NativeEvent : WaitHandle
		{
			public NativeEvent(HandleInheritability handleInheritability)
			{
				SECURITY_ATTRIBUTES securityAttributes = new SECURITY_ATTRIBUTES();
				securityAttributes.nLength = Marshal.SizeOf(securityAttributes);
				securityAttributes.bInheritHandle = (handleInheritability == HandleInheritability.Inheritable)? 1 : 0;
				SafeWaitHandle = CreateEvent(securityAttributes, false, false, null);
			}

			public NativeEvent(IntPtr handle, bool ownsHandle)
			{
				SafeWaitHandle = new SafeWaitHandle(handle, ownsHandle);
			}

			public void Set() => SetEvent(SafeWaitHandle);

			[DllImport("kernel32.dll")]
			static extern SafeWaitHandle CreateEvent(SECURITY_ATTRIBUTES lpEventAttributes, bool bManualReset, bool bInitialState, string? lpName);

			[DllImport("kernel32.dll")]
			static extern bool SetEvent(SafeWaitHandle hEvent);
		}

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
			get => _header.Span[0] != 0;
			set => _header.Span[0] = value? (byte)1 : (byte)0;
		}

		readonly MemoryMappedFile _memoryMappedFile;
		readonly MemoryMappedViewAccessor _memoryMappedViewAccessor;
		readonly MemoryMappedView _memoryMappedView;

		readonly Memory<byte> _header;
		readonly Memory<byte> _memory;

		readonly NativeEvent _writtenEvent;
		readonly NativeEvent _flushedEvent;

		private IpcBuffer(MemoryMappedFile memoryMappedFile, NativeEvent writtenEvent, NativeEvent flushedEvent)
		{
			_memoryMappedFile = memoryMappedFile;
			_memoryMappedViewAccessor = _memoryMappedFile.CreateViewAccessor();
			_memoryMappedView = new MemoryMappedView(_memoryMappedViewAccessor);

			Memory<byte> memory = _memoryMappedView.GetMemory(0, (int)_memoryMappedViewAccessor.Capacity);
			_header = memory.Slice(0, HeaderLength);
			_memory = memory.Slice(HeaderLength);

			_writtenEvent = writtenEvent;
			_flushedEvent = flushedEvent;
		}

		/// <summary>
		/// Creates a new IPC buffer with the given capacity
		/// </summary>
		/// <param name="capacity">Capacity of the buffer</param>
		public static IpcBuffer CreateNew(long capacity)
		{
			MemoryMappedFile memoryMappedFile = MemoryMappedFile.CreateNew(null, HeaderLength + capacity, MemoryMappedFileAccess.ReadWrite, MemoryMappedFileOptions.None, HandleInheritability.Inheritable);

			NativeEvent writtenEvent = new NativeEvent(HandleInheritability.Inheritable);
			NativeEvent flushedEvent = new NativeEvent(HandleInheritability.Inheritable);

			return new IpcBuffer(memoryMappedFile, writtenEvent, flushedEvent);
		}

		/// <summary>
		/// Opens an IpcBuffer from a string passed in from another process
		/// </summary>
		/// <param name="handle">Descriptor for the buffer to open</param>
		public static IpcBuffer OpenExisting(string handle)
		{
			string[] values = handle.Split(',');
			if (values.Length != 3)
			{
				throw new ArgumentException("Expected three handle values for IPC buffer", nameof(handle));
			}

			IntPtr memoryHandle = new IntPtr((long)UInt64.Parse(values[0], NumberStyles.None));
			MemoryMappedFile memoryMappedFile = OpenMemoryMappedFile(memoryHandle);

			IntPtr writtenEventHandle = new IntPtr((long)UInt64.Parse(values[1], NumberStyles.None));
			NativeEvent writtenEvent = new NativeEvent(writtenEventHandle, true);

			IntPtr flushedEventHandle = new IntPtr((long)UInt64.Parse(values[2], NumberStyles.None));
			NativeEvent flushedEvent = new NativeEvent(flushedEventHandle, true);

			return new IpcBuffer(memoryMappedFile, writtenEvent, flushedEvent);
		}

		static MemoryMappedFile OpenMemoryMappedFile(IntPtr handle)
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
		/// Gets the descriptor for the current buffer. This can be used for calls to <see cref="OpenExisting(String)"/> by newly spawned processes that inherit handles.
		/// </summary>
		/// <returns>Descriptor string for the buffer</returns>
		public string GetHandle()
		{
			ulong targetMemoryHandle = (ulong)_memoryMappedFile.SafeMemoryMappedFileHandle.DangerousGetHandle().ToInt64();
			ulong targetWrittenEventHandle = (ulong)_writtenEvent.SafeWaitHandle.DangerousGetHandle();
			ulong targetFlushedEventHandle = (ulong)_flushedEvent.SafeWaitHandle.DangerousGetHandle();
			return $"{targetMemoryHandle},{targetWrittenEventHandle},{targetFlushedEventHandle}";
		}

		/// <summary>
		/// Duplicates handles for this buffer into the given process, and returns a string that the target process can use to open it.
		/// </summary>
		/// <param name="targetProcessHandle">Handle to the target process</param>
		/// <returns>String that the target process can pass into a call to <see cref="OpenExisting(String)"/> to open the buffer.</returns>
		public string DuplicateIntoProcess(IntPtr targetProcessHandle)
		{
			IntPtr sourceProcessHandle = GetCurrentProcess();

			IntPtr targetMemoryHandle;
			DuplicateHandle(sourceProcessHandle, _memoryMappedFile.SafeMemoryMappedFileHandle.DangerousGetHandle(), targetProcessHandle, out targetMemoryHandle, 0, false, DUPLICATE_SAME_ACCESS);

			IntPtr targetWrittenEventHandle;
			DuplicateHandle(sourceProcessHandle, _writtenEvent.SafeWaitHandle.DangerousGetHandle(), targetProcessHandle, out targetWrittenEventHandle, 0, false, DUPLICATE_SAME_ACCESS);

			IntPtr targetFlushedEventHandle;
			DuplicateHandle(sourceProcessHandle, _flushedEvent.SafeWaitHandle.DangerousGetHandle(), targetProcessHandle, out targetFlushedEventHandle, 0, false, DUPLICATE_SAME_ACCESS);

			return $"{(ulong)targetMemoryHandle.ToInt64()},{(ulong)targetWrittenEventHandle.ToInt64()},{(ulong)targetFlushedEventHandle.ToInt64()}";
		}

		const uint DUPLICATE_SAME_ACCESS = 2;

		[DllImport("kernel32.dll", SetLastError = true)]
		static extern IntPtr GetCurrentProcess();

		[DllImport("kernel32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		static extern bool DuplicateHandle(IntPtr hSourceProcessHandle, IntPtr hSourceHandle, IntPtr hTargetProcessHandle, out IntPtr lpTargetHandle, uint dwDesiredAccess, [MarshalAs(UnmanagedType.Bool)] bool bInheritHandle, uint dwOptions);

		/// <inheritdoc/>
		public void Dispose()
		{
			_flushedEvent.Dispose();
			_writtenEvent.Dispose();
			_memoryMappedView.Dispose();
			_memoryMappedViewAccessor.Dispose();
			_memoryMappedFile.Dispose();
		}

		#region Reader

		/// <inheritdoc/>
		protected override bool FinishedReading() => FinishedWriting && ReadPosition == WritePosition;

		/// <inheritdoc/>
		protected override void AdvanceReadPosition(int size)
		{
			ReadPosition += size;

			if (ReadPosition == WritePosition)
			{
				_flushedEvent.Set();
			}
		}

		/// <inheritdoc/>
		protected override ReadOnlyMemory<byte> GetReadMemory() => _memory.Slice((int)ReadPosition, (int)(WritePosition - ReadPosition));

		/// <inheritdoc/>
		protected override async ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken)
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
		protected override void FinishWriting()
		{
			FinishedWriting = true;
			_writtenEvent.Set();
		}

		/// <inheritdoc/>
		protected override void AdvanceWritePosition(int size)
		{
			if (FinishedWriting)
			{
				throw new InvalidOperationException("Cannot update write position after marking as complete");
			}

			WritePosition += size;
			_writtenEvent.Set();
		}

		/// <inheritdoc/>
		protected override Memory<byte> GetWriteMemory() => _memory.Slice((int)WritePosition);

		/// <inheritdoc/>
		protected override async ValueTask FlushWritesAsync(CancellationToken cancellationToken)
		{
			while (ReadPosition < WritePosition)
			{
				await _flushedEvent.WaitOneAsync(cancellationToken);
			}
		}

		#endregion
	}
}
