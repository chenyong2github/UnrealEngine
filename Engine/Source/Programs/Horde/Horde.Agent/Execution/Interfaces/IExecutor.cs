// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Agent.Parser.Interfaces;
using Horde.Agent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Horde.Agent.Execution.Interfaces
{
	interface IExecutor
	{
		Task InitializeAsync(ILogger logger, CancellationToken cancellationToken);
		Task<JobStepOutcome> RunAsync(BeginStepResponse step, ILogger logger, CancellationToken cancellationToken);
		Task FinalizeAsync(ILogger logger, CancellationToken cancellationToken);
	}
}
