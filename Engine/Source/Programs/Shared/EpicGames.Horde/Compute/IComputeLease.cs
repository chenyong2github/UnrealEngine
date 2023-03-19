// Copyright Epic Games, Inc. All Rights Reserved.

using System;
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
		/// Attaches a send buffer to this lease. Data will be read from this buffer and replicated a receive buffer attached with the same id on the remote.
		/// </summary>
		/// <param name="id">Identifier for the buffer</param>
		/// <param name="buffer">The buffer to attach</param>
		void AttachSendBuffer(int id, IComputeBufferReader buffer);

		/// <summary>
		/// Attaches a receive buffer to this lease. Data will be read into this buffer from the other end of the lease.
		/// </summary>
		/// <param name="id">Identifier for the buffer</param>
		/// <param name="buffer">The buffer to attach</param>
		void AttachReceiveBuffer(int id, IComputeBufferWriter buffer);
	}
}
