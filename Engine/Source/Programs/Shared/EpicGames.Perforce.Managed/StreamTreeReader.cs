// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Serialization;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Interface for 
	/// </summary>
	public abstract class StreamTreeReader
	{
		/// <summary>
		/// Reads a node of the tree
		/// </summary>
		/// <param name="reference"></param>
		/// <returns></returns>
		public abstract Task<StreamTree> ReadAsync(StreamTreeRef @reference);
	}

	/// <summary>
	/// Implements a <see cref="StreamTreeReader"/> using a contiguous block of memory
	/// </summary>
	public class StreamTreeMemoryReader : StreamTreeReader
	{
		/// <summary>
		/// Map from hash to encoded CB tree object
		/// </summary>
		readonly Dictionary<IoHash, CbObject> _hashToTree;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Root"></param>
		/// <param name="hashToTree"></param>
		public StreamTreeMemoryReader(Dictionary<IoHash, CbObject> hashToTree)
		{
			_hashToTree = hashToTree;
		}

		/// <inheritdoc/>
		public override Task<StreamTree> ReadAsync(StreamTreeRef @reference)
		{
			return Task.FromResult(new StreamTree(@reference.Path, _hashToTree[@reference.Hash]));
		}
	}
}
