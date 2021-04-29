// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// Interface for a particular blob type
	/// </summary>
	interface IBlobType
	{
		/// <summary>
		/// Name of the type
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Gets references from the given blob
		/// </summary>
		/// <param name="Data">The blob data</param>
		/// <returns>References</returns>
		public IEnumerable<BlobRef> GetRefs(ReadOnlyMemory<byte> Data);
	}

	/// <summary>
	/// Leaf blob type definition
	/// </summary>
	sealed class LeafBlob : IBlobType
	{
		/// <inheritdoc/>
		public string Name => "Leaf";

		/// <inheritdoc/>
		public IEnumerable<BlobRef> GetRefs(ReadOnlyMemory<byte> Data)
		{
			yield break;
		}
	}
}
