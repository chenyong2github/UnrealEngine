// Copyright Epic Games, Inc. All Rights Reserved.

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
		public ComputeTaskResultMessage(CbObjectAttachment Result)
		{
			this.Result = Result;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeTaskResultMessage(ComputeTaskOutcome Outcome, string? Detail = null)
		{
			this.Outcome = Outcome;
			this.Detail = Detail;
		}
	}
}
