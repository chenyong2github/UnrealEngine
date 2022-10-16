// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Agent.Services;
using Horde.Agent.Utility;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Execution
{
	class LocalExecutor : JobExecutor
	{
		private readonly LocalExecutorSettings _settings;
		private readonly DirectoryReference _localWorkspaceDir;

		public LocalExecutor(ISession session, string jobId, string batchId, string agentTypeName, LocalExecutorSettings settings, IHttpClientFactory httpClientFactory) 
			: base(session, jobId, batchId, agentTypeName, httpClientFactory)
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

	class LocalExecutorFactory : JobExecutorFactory
	{
		readonly LocalExecutorSettings _settings;
		readonly IHttpClientFactory _httpClientFactory;

		public LocalExecutorFactory(IOptions<LocalExecutorSettings> settings, IHttpClientFactory httpClientFactory)
		{
			_settings = settings.Value;
			_httpClientFactory = httpClientFactory;
		}

		public override JobExecutor CreateExecutor(ISession session, ExecuteJobTask executeJobTask, BeginBatchResponse beginBatchResponse)
		{
			return new LocalExecutor(session, executeJobTask.JobId, executeJobTask.BatchId, beginBatchResponse.AgentType, _settings, _httpClientFactory);
		}
	}
}
