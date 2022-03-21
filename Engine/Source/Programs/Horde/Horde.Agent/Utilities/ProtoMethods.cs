// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Compute;

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
		public ComputeTaskResultMessage(RefIdWrapper resultRefId)
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
