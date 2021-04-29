// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extension methods for working with streams
	/// </summary>
	static class StreamExtensions
	{
		/// <summary>
		/// Reads data of a fixed size into the buffer. Throws an exception if the whole block cannot be read.
		/// </summary>
		/// <param name="Stream">The stream to read from</param>
		/// <param name="Data">Buffer to receive the read data</param>
		/// <param name="Offset">Offset within the buffer to read the new data</param>
		/// <param name="Length">Length of the data to read</param>
		/// <returns>Async task</returns>
		public static Task ReadFixedSizeDataAsync(this Stream Stream, byte[] Data, int Offset, int Length)
		{
			return ReadFixedSizeDataAsync(Stream, Data.AsMemory(Offset, Length));
		}

		/// <summary>
		/// Reads data of a fixed size into the buffer. Throws an exception if the whole block cannot be read.
		/// </summary>
		/// <param name="Stream">The stream to read from</param>
		/// <param name="Data">Buffer to receive the read data</param>
		/// <returns>Async task</returns>
		public static async Task ReadFixedSizeDataAsync(this Stream Stream, Memory<byte> Data)
		{
			while (Data.Length > 0)
			{
				int Count = await Stream.ReadAsync(Data);
				if (Count == 0)
				{
					throw new EndOfStreamException();
				}
				Data = Data.Slice(Count);
			}
		}
	}
}