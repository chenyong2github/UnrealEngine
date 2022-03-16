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
		Task InitializeAsync(ILogger Logger, CancellationToken CancellationToken);
		Task<JobStepOutcome> RunAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken);
		Task FinalizeAsync(ILogger Logger, CancellationToken CancellationToken);
	}
}
