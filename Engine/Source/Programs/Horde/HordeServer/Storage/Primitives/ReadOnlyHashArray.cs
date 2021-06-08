// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Security.Cryptography;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// Read-only array of blob hashes
	/// </summary>
	struct ReadOnlyHashArray : IReadOnlyList<IoHash>
	{
		/// <summary>
		/// Underlying storage for the array
		/// </summary>
		public ReadOnlyMemory<byte> Memory { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Storage for the array</param>
		public ReadOnlyHashArray(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;
		}

		/// <summary>
		/// Accessor for elements in the array
		/// </summary>
		/// <param name="Index">Index of the element to retrieve</param>
		/// <returns>Hash at the given index</returns>
		public IoHash this[int Index] => new IoHash(Memory.Slice(Index * IoHash.NumBytes, IoHash.NumBytes));

		/// <inheritdoc/>
		public int Count => Memory.Length / IoHash.NumBytes;

		/// <inheritdoc/>
		public IEnumerator<IoHash> GetEnumerator()
		{
			ReadOnlyMemory<byte> Remaining = Memory;
			while (!Remaining.IsEmpty)
			{
				yield return new IoHash(Remaining.Slice(0, IoHash.NumBytes));
				Remaining = Remaining.Slice(IoHash.NumBytes);
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}
}

