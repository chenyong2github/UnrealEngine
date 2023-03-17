// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Full-duplex channel for sending and reciving messages
	/// </summary>
	public interface IComputeLease : IAsyncDisposable
	{
		/// <summary>
		/// Resources assigned to this lease
		/// </summary>
		IReadOnlyDictionary<string, int> AssignedResources { get; }

		/// <summary>
		/// Closes the underlying transport stream gracefully
		/// </summary>
		ValueTask CloseAsync(CancellationToken cancellationToken);

		/// <summary>
		/// Creates an input buffer from the given memory. A corresponding output buffer with the same size and id must be created at the remote end.
		/// </summary>
		/// <param name="id">Identifier for the buffer. This much match up with an output buffer on the remote for a connection to be established.</param>
		/// <param name="memory">Memory to read from</param>
		IComputeInputBuffer CreateInputBuffer(int id, IMemoryOwner<byte> memory);

		/// <summary>
		/// Creates an output buffer from the given memory. A corresponding output buffer with the same size and id must be created at the remote end.
		/// </summary>
		/// <param name="id">Identifier for the buffer. This much match up with an input buffer on the remote for a connection to be established.</param>
		/// <param name="memory">Memory to read from</param>
		IComputeOutputBuffer CreateOutputBuffer(int id, IMemoryOwner<byte> memory);
	}
}
