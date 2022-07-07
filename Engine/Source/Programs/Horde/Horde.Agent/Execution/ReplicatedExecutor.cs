// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Horde.Agent.Utility;
using HordeCommon.Rpc;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Execution
{
	class ReplicatedExecutor : BuildGraphExecutor
	{
		protected readonly DirectoryReference _rootDir;
		protected readonly IStorageClient _storageClient;
		protected readonly QualifiedRefId _contentRef;
		protected DirectoryReference _workspaceDir = null!;
		protected Dictionary<string, string> _envVars = new Dictionary<string, string>();

		public ReplicatedExecutor(IRpcConnection rpcConnection, string jobId, string batchId, string agentTypeName, IStorageClient storageClient, QualifiedRefId contentRef, DirectoryReference rootDir)
			: base(rpcConnection, jobId, batchId, agentTypeName)
		{
			_rootDir = rootDir;
			_storageClient = storageClient;
			_contentRef = contentRef;
		}

		public override async Task InitializeAsync(ILogger logger, CancellationToken cancellationToken)
		{
			await base.InitializeAsync(logger, cancellationToken);

			_workspaceDir = DirectoryReference.Combine(_rootDir, "Replicated");
			FileUtils.ForceDeleteDirectoryContents(_workspaceDir);

			throw new NotImplementedException("Needs refactoring for new bundle/store logic");

/*
			using (IMemoryCache cache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 1024 * 1024 * 1000 }))
			{
				BucketId bucketId = new BucketId(_job.StreamId.ToString());
				using Bundle<DirectoryNode> bundle = await Bundle.ReadAsync<DirectoryNode>(_storageClient, _contentRef.NamespaceId, _contentRef.BucketId, _contentRef.RefId, new BundleOptions(), cache);
				await bundle.Root.CopyToDirectoryAsync(bundle, _workspaceDir.ToDirectoryInfo(), logger);
			}

			// Get all the environment variables for jobs
			_envVars["IsBuildMachine"] = "1";*/
		}

		protected override Task<bool> SetupAsync(BeginStepResponse step, ILogger logger, CancellationToken cancellationToken)
		{
			return SetupAsync(step, _workspaceDir, null, _envVars, logger, cancellationToken);
		}

		protected override Task<bool> ExecuteAsync(BeginStepResponse step, ILogger logger, CancellationToken cancellationToken)
		{
			return ExecuteAsync(step, _workspaceDir, null, _envVars, logger, cancellationToken);
		}

		public override Task FinalizeAsync(ILogger logger, CancellationToken cancellationToken)
		{
			return Task.CompletedTask;
		}
	}
}
