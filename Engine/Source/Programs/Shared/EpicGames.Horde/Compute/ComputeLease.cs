// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Implementation of a compute lease using sockets to transfer data
	/// </summary>
	public sealed class ComputeLease : IComputeLease, IAsyncDisposable
	{
		/// <inheritdoc/>
		public IReadOnlyDictionary<string, int> AssignedResources { get; }

		/// <inheritdoc/>
		public IComputeSocket Socket { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="assignedResources">Resources assigned to this lease</param>
		/// <param name="socket">Socket for communication</param>
		public ComputeLease(IReadOnlyDictionary<string, int> assignedResources, IComputeSocket socket)
		{
			AssignedResources = assignedResources;
			Socket = socket;
		}

		/// <inheritdoc/>
		public ValueTask DisposeAsync() => Socket.DisposeAsync();
	}
}
