// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;

namespace HordeCommon
{
	/// <summary>
	/// Implements an unmarshlled view of a memory mapped file
	/// </summary>
	unsafe class MemoryMappedFileView : IDisposable
	{
		sealed unsafe class MemoryWrapper : MemoryManager<byte>
		{
			private readonly byte* Pointer;
			private readonly int Length;

			public MemoryWrapper(byte* Pointer, int Length)
			{
				this.Pointer = Pointer;
				this.Length = Length;
			}

			/// <inheritdoc/>
			public override Span<byte> GetSpan() => new Span<byte>(Pointer, Length);

			/// <inheritdoc/>
			public override MemoryHandle Pin(int elementIndex) => new MemoryHandle(Pointer + elementIndex);

			/// <inheritdoc/>
			public override void Unpin() { }

			/// <inheritdoc/>
			protected override void Dispose(bool disposing) { }
		}

		MemoryMappedViewAccessor MemoryMappedViewAccessor;
		byte* Data;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="MemoryMappedViewAccessor"></param>
		public MemoryMappedFileView(MemoryMappedViewAccessor MemoryMappedViewAccessor)
		{
			this.MemoryMappedViewAccessor = MemoryMappedViewAccessor;
			MemoryMappedViewAccessor.SafeMemoryMappedViewHandle.AcquirePointer(ref Data);
		}

		/// <summary>
		/// Gets a memory object for the given range
		/// </summary>
		/// <param name="Offset"></param>
		/// <param name="Length"></param>
		/// <returns></returns>
		public Memory<byte> GetMemory(long Offset, int Length)
		{
			MemoryWrapper Wrapper = new MemoryWrapper(Data + Offset, Length);
			return Wrapper.Memory;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			if (Data != null)
			{
				MemoryMappedViewAccessor.SafeMemoryMappedViewHandle.ReleasePointer();
				Data = null;
			}
		}
	}
}
