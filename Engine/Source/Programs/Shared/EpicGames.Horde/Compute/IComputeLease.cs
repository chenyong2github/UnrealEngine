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
		/// Resources assigned to this lease
		/// </summary>
		IReadOnlyDictionary<string, int> AssignedResources { get; }

		/// <summary>
		/// Socket to communicate with the remote
		/// </summary>
		IComputeSocket Socket { get; }
	}
}
