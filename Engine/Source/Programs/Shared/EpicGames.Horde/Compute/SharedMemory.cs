// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32.SafeHandles;
using System;
using System.Buffers;
using System.IO.MemoryMappedFiles;
using System.Reflection;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Wraps the resources required for an unnamed memory mapped file and allows accessing it from C# as a <see cref="Memory{Byte}"/> object.
	/// </summary>
	public sealed unsafe class SharedMemory : IMemoryOwner<byte>
	{
		sealed unsafe class MemoryWrapper : MemoryManager<byte>
		{
			private readonly byte* _pointer;
			private readonly int _length;

			public MemoryWrapper(byte* pointer, int length)
			{
				_pointer = pointer;
				_length = length;
			}

			/// <inheritdoc/>
			public override Span<byte> GetSpan() => new Span<byte>(_pointer, _length);

			/// <inheritdoc/>
			public override MemoryHandle Pin(int elementIndex) => new MemoryHandle(_pointer + elementIndex);

			/// <inheritdoc/>
			public override void Unpin() { }

			/// <inheritdoc/>
			protected override void Dispose(bool disposing) { }
		}

		readonly MemoryMappedFile _file;
		readonly MemoryMappedViewAccessor _viewAccessor;
		byte* _data;

		/// <summary>
		/// Access the memory allocated through this buffer
		/// </summary>
		public Memory<byte> Memory { get; }

		/// <summary>
		/// Gets a handle for the underlying memory mapped file
		/// </summary>
		public SafeMemoryMappedFileHandle SafeMemoryMappedFileHandle => _file.SafeMemoryMappedFileHandle;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="capacity">Size of the memory to allocate. Data will not be committed until used.</param>
		public SharedMemory(long capacity)
			: this(MemoryMappedFile.CreateNew(null, capacity))
		{
		}

		/// <summary>
		/// Creates a memory mapped file from the given file object and capacity
		/// </summary>
		/// <param name="file">File to create from</param>
		private SharedMemory(MemoryMappedFile file)
		{
			_file = file;

			_viewAccessor = _file.CreateViewAccessor();
			_viewAccessor.SafeMemoryMappedViewHandle.AcquirePointer(ref _data);

			MemoryWrapper wrapper = new MemoryWrapper(_data, (int)_viewAccessor.Capacity);
			Memory = wrapper.Memory;
		}

		/// <summary>
		/// Opens a shared memory region from a handle
		/// </summary>
		/// <param name="handle">Handle to the underlying memory mapped file</param>
		public static SharedMemory FromHandle(SafeMemoryMappedFileHandle handle)
		{
			ConstructorInfo? constructorInfo = typeof(MemoryMappedFile).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(SafeMemoryMappedFileHandle) }, null);
			if (constructorInfo == null)
			{
				throw new InvalidOperationException("Cannot find private constructor for memory mapped file");
			}

			MemoryMappedFile file = (MemoryMappedFile)constructorInfo.Invoke(new object?[] { handle });
			return new SharedMemory(file);
		}

		/// <summary>
		/// Finalizer
		/// </summary>
		~SharedMemory()
		{
			Dispose(false);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		void Dispose(bool disposing)
		{
			if (_data != null)
			{
				_viewAccessor.SafeMemoryMappedViewHandle.ReleasePointer();
				_data = null;
			}

			if (disposing)
			{
				_viewAccessor.Dispose();
				_file.Dispose();
			}
		}
	}
}
