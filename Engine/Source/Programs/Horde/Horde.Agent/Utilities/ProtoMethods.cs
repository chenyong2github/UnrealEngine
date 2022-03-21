// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Common;
using EpicGames.Horde.Compute;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Text;

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
