// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Execution
{
	class LocalExecutor : BuildGraphExecutor
	{
		LocalExecutorSettings Settings;
		DirectoryReference LocalWorkspaceDir;

		public LocalExecutor(IRpcConnection RpcConnection, string JobId, string BatchId, string AgentTypeName, LocalExecutorSettings Settings) 
			: base(RpcConnection, JobId, BatchId, AgentTypeName)
		{
			this.Settings = Settings;
			if(Settings.WorkspaceDir == null)
			{
				throw new Exception("Missing LocalWorkspaceDir from settings");
			}
			LocalWorkspaceDir = new DirectoryReference(Settings.WorkspaceDir);
		}

		protected override Task<bool> SetupAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			Dictionary<string, string> EnvVars = new Dictionary<string, string>();
			return SetupAsync(Step, LocalWorkspaceDir, null, EnvVars, Logger, CancellationToken);
		}

		protected override Task<bool> ExecuteAsync(BeginStepResponse Step, ILogger Logger, CancellationToken CancellationToken)
		{
			if (Settings.RunSteps)
			{
				Dictionary<string, string> EnvVars = new Dictionary<string, string>();
				return ExecuteAsync(Step, LocalWorkspaceDir, null, EnvVars, Logger, CancellationToken);
			}
			else
			{
				Logger.LogInformation("**** SKIPPING NODE {StepName} ****", Step.Name);
				return Task.FromResult(true);
			}
		}
	}
}
