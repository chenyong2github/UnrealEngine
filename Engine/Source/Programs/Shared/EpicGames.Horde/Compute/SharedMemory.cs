// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32.SafeHandles;
using System;
using System.Buffers;
using System.IO.MemoryMappedFiles;

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
		{
			_file = MemoryMappedFile.CreateNew(null, capacity);

			_viewAccessor = _file.CreateViewAccessor(0, capacity);
			_viewAccessor.SafeMemoryMappedViewHandle.AcquirePointer(ref _data);

			MemoryWrapper wrapper = new MemoryWrapper(_data, (int)capacity);
			Memory = wrapper.Memory;
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
