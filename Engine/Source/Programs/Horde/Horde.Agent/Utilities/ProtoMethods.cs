// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;

namespace HordeCommon.Rpc.Tasks
{
	/// <summary>
	/// 
	/// </summary>
	partial class ComputeTaskResultMessage
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeTaskResultMessage(RefId resultRefId)
		{
			ResultRefId = resultRefId;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeTaskResultMessage(ComputeTaskOutcome outcome, string? detail = null)
		{
			Outcome = (int)outcome;
			Detail = detail;
		}
	}
}
