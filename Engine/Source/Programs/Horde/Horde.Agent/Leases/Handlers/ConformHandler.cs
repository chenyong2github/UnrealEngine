// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Horde.Agent.Execution;
using Horde.Agent.Parser;
using Horde.Agent.Services;
using Horde.Agent.Utility;
using HordeCommon.Rpc;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Leases.Handlers
{
	class ConformHandler : LeaseHandler<ConformTask>
	{
		readonly AgentSettings _settings;
		readonly ILogger _defaultLogger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ConformHandler(IOptions<AgentSettings> settings, ILogger<ConformHandler> defaultLogger)
		{
			_settings = settings.Value;
			_defaultLogger = defaultLogger;
		}

		/// <inheritdoc/>
		public override async Task<LeaseResult> ExecuteAsync(ISession session, string leaseId, ConformTask conformTask, CancellationToken cancellationToken)
		{
			await using JsonRpcLogger conformLogger = new JsonRpcLogger(session.RpcConnection, conformTask.LogId, _defaultLogger);

			conformLogger.LogInformation("Conforming, lease {LeaseId}", leaseId);
			await session.TerminateProcessesAsync(conformLogger, cancellationToken);

			bool removeUntrackedFiles = conformTask.RemoveUntrackedFiles;
			IList<AgentWorkspace> pendingWorkspaces = conformTask.Workspaces;
			for (; ; )
			{
				// Run the conform task
				if (_settings.Executor == ExecutorType.Perforce && _settings.PerforceExecutor.RunConform)
				{
					await PerforceExecutor.ConformAsync(session.WorkingDir, pendingWorkspaces, removeUntrackedFiles, conformLogger, cancellationToken);
				}
				else
				{
					conformLogger.LogInformation("Skipping due to Settings.RunConform flag");
				}

				// Update the new set of workspaces
				UpdateAgentWorkspacesRequest request = new UpdateAgentWorkspacesRequest();
				request.AgentId = session.AgentId;
				request.Workspaces.AddRange(pendingWorkspaces);
				request.RemoveUntrackedFiles = removeUntrackedFiles;

				UpdateAgentWorkspacesResponse response = await session.RpcConnection.InvokeAsync(x => x.UpdateAgentWorkspacesAsync(request, null, null, cancellationToken), new RpcContext(), cancellationToken);
				if (!response.Retry)
				{
					conformLogger.LogInformation("Conform finished");
					break;
				}

				conformLogger.LogInformation("Pending workspaces have changed - running conform again...");
				pendingWorkspaces = response.PendingWorkspaces;
				removeUntrackedFiles = response.RemoveUntrackedFiles;
			}

			return LeaseResult.Success;
		}
	}
}

