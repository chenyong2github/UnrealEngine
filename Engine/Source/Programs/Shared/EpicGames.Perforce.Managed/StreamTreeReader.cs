// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

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
		/// <param name="Ref"></param>
		/// <returns></returns>
		public abstract Task<StreamTree> ReadAsync(StreamTreeRef Ref);
	}

	/// <summary>
	/// Implements a <see cref="StreamTreeReader"/> using a contiguous block of memory
	/// </summary>
	public class StreamTreeMemoryReader : StreamTreeReader
	{
		/// <summary>
		/// Map from hash to encoded CB tree object
		/// </summary>
		Dictionary<IoHash, CbObject> HashToTree;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Root"></param>
		/// <param name="HashToTree"></param>
		public StreamTreeMemoryReader(Dictionary<IoHash, CbObject> HashToTree)
		{
			this.HashToTree = HashToTree;
		}

		/// <inheritdoc/>
		public override Task<StreamTree> ReadAsync(StreamTreeRef Ref)
		{
			return Task.FromResult(new StreamTree(Ref.Path, HashToTree[Ref.Hash]));
		}
	}
}
