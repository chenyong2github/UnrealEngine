// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
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
