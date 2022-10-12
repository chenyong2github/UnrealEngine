// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using System.Threading.Tasks;
using Horde.Agent.Services;
using HordeCommon;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Execution.Interfaces
{
	interface IExecutor
	{
		Task InitializeAsync(ILogger logger, CancellationToken cancellationToken);
		Task<JobStepOutcome> RunAsync(BeginStepResponse step, ILogger logger, CancellationToken cancellationToken);
		Task FinalizeAsync(ILogger logger, CancellationToken cancellationToken);
	}

	interface IExecutorFactory
	{
		IExecutor CreateExecutor(ISession session, ExecuteJobTask executeJobTask, BeginBatchResponse beginBatchResponse);
	}
}
