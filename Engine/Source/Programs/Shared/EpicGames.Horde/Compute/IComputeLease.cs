// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Full-duplex channel for sending and reciving messages
	/// </summary>
	public interface IComputeLease : IAsyncDisposable
	{
		/// <summary>
		/// The control channel, used to transmit messages before additional channels have been created
		/// </summary>
		IComputeChannel DefaultChannel { get; }

		/// <summary>
		/// Resources assigned to this lease
		/// </summary>
		IReadOnlyDictionary<string, int> AssignedResources { get; }

		/// <summary>
		/// Opens a new channel with the given identifier.
		/// </summary>
		/// <param name="id">The pipe identifier. These values are arbitrary, as long as both ends agree on a channel to communicate on. Channel 0 is reserved for the control channel.</param>
		/// <returns>New channel instance</returns>
		IComputeChannel OpenChannel(int id);
	}
}
