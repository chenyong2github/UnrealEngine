// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Compute;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Text;

namespace HordeCommon.Rpc.Tasks
{
	partial class ComputeTaskResultMessage
	{
		public ComputeTaskResultMessage(CbObjectAttachment Result)
		{
			this.Result = Result;
		}

		public ComputeTaskResultMessage(ComputeTaskOutcome Outcome, string? Detail = null)
		{
			this.Outcome = Outcome;
			this.Detail = Detail;
		}
	}
}
