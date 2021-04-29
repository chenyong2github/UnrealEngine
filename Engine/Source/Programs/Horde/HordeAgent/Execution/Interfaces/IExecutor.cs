// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Execution.Interfaces
{
	interface IExecutor
	{
		Task InitializeAsync(ILogger Logger, CancellationToken CancellationToken);
		Task<JobStepOutcome> RunAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken);
		Task FinalizeAsync(ILogger Logger, CancellationToken CancellationToken);
	}
}
