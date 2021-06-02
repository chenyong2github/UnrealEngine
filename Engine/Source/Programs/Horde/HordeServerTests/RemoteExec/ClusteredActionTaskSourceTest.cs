// Copyright Epic Games, Inc. All Rights Reserved.

extern alias HordeAgent;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Build.Bazel.Remote.Execution.V2;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using RemoteExecOperation = HordeCommon.Rpc.Tasks.RemoteExecOperation;

namespace HordeServerTests.RemoteExec
{
	public class FakeAgent : IAgent
	{
		public AgentId Id { get; set; }
		public ObjectId? SessionId { get; set; }
		public DateTime? SessionExpiresAt { get; set; }
		public AgentStatus Status { get; set; }
		public bool Enabled { get; set; }
		public string? Comment { get; set; }
		public bool Ephemeral { get; set; }
		public bool Deleted { get; set; }
		public StringId<IAgentSoftwareCollection>? Version { get; set; }
		public StringId<AgentSoftwareChannels>? Channel { get; set; }
		public string? LastUpgradeVersion { get; set; }
		public DateTime? LastUpgradeTime { get; set; }
		public IReadOnlyList<StringId<IPool>> ExplicitPools { get; set; } = new List<StringId<IPool>>();
		public bool RequestConform { get; set; }
		public bool RequestRestart { get; set; }
		public bool RequestShutdown { get; set; }
		public IReadOnlyList<AgentWorkspace> Workspaces { get; set; } = new List<AgentWorkspace>();
		public DateTime LastConformTime { get; set; }
		public int? ConformAttemptCount { get; set; }
		public AgentCapabilities Capabilities { get; set; } = new AgentCapabilities();
		public IReadOnlyList<AgentLease> Leases { get; set; } = new List<AgentLease>();
		public Acl? Acl { get; set; }
		public DateTime UpdateTime { get; set; }
		public uint UpdateIndex { get; set; }
	}
	
	[TestClass]
	public class ActionRedisTaskSourceTest : DatabaseIntegrationTest
	{
		private readonly TestSetup TestSetup;

		public ActionRedisTaskSourceTest()
		{
			TestSetup = GetTestSetup().Result;
		}

		[TestMethod]
		public async Task ScheduleActionOnSeparateServer()
		{
			ClusteredActionTaskSource Server1 = CreateTaskSource();
			ClusteredActionTaskSource Server2 = CreateTaskSource();

			FakeAgent Agent = new FakeAgent();
			await Server2.SubscribeAsync(Agent);
			
			ExecuteRequest ExecuteRequest = new ExecuteRequest();
			ExecuteRequest.SkipCacheLookup = true;
			ExecuteRequest.ActionDigest = new Digest { Hash = "my-bogus-hash-digest", SizeBytes = 20 };

			// Schedule task server 1
			RemoteExecOperation Op = await Server1.ExecuteAsync(ExecuteRequest);
			
			// Force server 2 to pick it up by ticking it
			Task Server2UpdateTask = Server2.UpdateOperations();
			Assert.IsTrue(await WaitForOperationToExistAsync(Server2, ObjectId.Parse(Op.Id)));
			int ExitCode = 11122233;
			Assert.IsTrue(Server2.SetResultForActiveOperation(ObjectId.Parse(Op.Id), new ActionResult { ExitCode = ExitCode }));
			await Server2UpdateTask;
			
			// Verify result can fetched from server 1
			RemoteExecOperation Op2 = (await Server1.GetOperationAsync(Op.Id))!;
			Assert.IsNotNull(Op2);
			Assert.AreEqual(ExitCode, Op2.Response.Result.ExitCode);
		}

		private ClusteredActionTaskSource CreateTaskSource()
		{
			ILogFileService LogFileService = (TestSetup.ServiceProvider.GetService(typeof(ILogFileService)) as ILogFileService)!;
			return new ClusteredActionTaskSource(null!, GetRedisDatabase(), null!, LogFileService, null!, TestSetup.ServerSettingsMon);
		}

		private async Task<bool> WaitForOperationToExistAsync(ClusteredActionTaskSource TaskSource, ObjectId OperationId, int MaxWaitSecs = 5)
		{
			DateTimeOffset ExpireAt = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(MaxWaitSecs);
			while (DateTimeOffset.UtcNow < ExpireAt)
			{
				if (TaskSource.CheckIfActiveOperationExists(OperationId)) return true;
				await Task.Delay(20);
			}

			return false;
		}
	}
}