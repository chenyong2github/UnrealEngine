// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;

namespace EpicGames.Horde.Compute.Buffers
{
	/// <summary>
	/// Implementation of <see cref="MemoryBuffer"/> that owns a rented memory resource
	/// </summary>
	class RentedLocalBuffer : MemoryBuffer, IDisposable
	{
		readonly IMemoryOwner<byte> _memoryOwner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="capacity">Size of data to allocate</param>
		public RentedLocalBuffer(int capacity)
			: this(MemoryPool<byte>.Shared.Rent(capacity))
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="memoryOwner">Memory to manage</param>
		public RentedLocalBuffer(IMemoryOwner<byte> memoryOwner)
			: base(memoryOwner.Memory)
		{
			_memoryOwner = memoryOwner;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_memoryOwner.Dispose();
		}
	}
}
