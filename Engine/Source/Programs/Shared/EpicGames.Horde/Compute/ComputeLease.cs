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
		public IReadOnlyList<string> Properties { get; }

		/// <inheritdoc/>
		public IReadOnlyDictionary<string, int> AssignedResources { get; }

		/// <inheritdoc/>
		public IComputeSocket Socket { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="properties">Properties of the assigned agent</param>
		/// <param name="assignedResources">Resources assigned to this lease</param>
		/// <param name="socket">Socket for communication</param>
		public ComputeLease(IReadOnlyList<string> properties, IReadOnlyDictionary<string, int> assignedResources, IComputeSocket socket)
		{
			Properties = properties;
			AssignedResources = assignedResources;
			Socket = socket;
		}

		/// <inheritdoc/>
		public ValueTask DisposeAsync() => Socket.DisposeAsync();
	}
}
