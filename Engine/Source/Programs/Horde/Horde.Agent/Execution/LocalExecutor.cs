// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Agent.Utility;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Execution
{
	class LocalExecutor : BuildGraphExecutor
	{
		private readonly LocalExecutorSettings _settings;
		private readonly DirectoryReference _localWorkspaceDir;

		public LocalExecutor(IRpcConnection rpcConnection, string jobId, string batchId, string agentTypeName, LocalExecutorSettings settings) 
			: base(rpcConnection, jobId, batchId, agentTypeName)
		{
			_settings = settings;
			if(settings.WorkspaceDir == null)
			{
				throw new Exception("Missing LocalWorkspaceDir from settings");
			}
			_localWorkspaceDir = new DirectoryReference(settings.WorkspaceDir);
		}

		protected override Task<bool> SetupAsync(BeginStepResponse step, ILogger logger, CancellationToken cancellationToken)
		{
			return SetupAsync(step, _localWorkspaceDir, null, logger, cancellationToken);
		}

		protected override Task<bool> ExecuteAsync(BeginStepResponse step, ILogger logger, CancellationToken cancellationToken)
		{
			if (_settings.RunSteps)
			{
				return ExecuteAsync(step, _localWorkspaceDir, null,  logger, cancellationToken);
			}
			else
			{
				logger.LogInformation("**** SKIPPING NODE {StepName} ****", step.Name);
				return Task.FromResult(true);
			}
		}
	}
}
