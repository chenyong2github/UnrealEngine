// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using HordeServer.Utilities;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	[TestClass]
	public class JobTaskSourceTests : TestSetup
	{
		private bool EventReceived;
		private IPool? EventPool;
		private bool? EventPoolHasAgentsOnline;
		
		[TestMethod]
		public async Task UpdateJobQueueNormal()
		{
			Fixture Fixture = await SetupPoolWithAgentAsync(IsPoolAutoScaled: true, ShouldCreateAgent: true, IsAgentEnabled: true);

			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(1, JobTaskSource.GetQueueForTesting().Count);
			Assert.AreEqual(Fixture.Job1.Id, JobTaskSource.GetQueueForTesting().Min!.Id.Item1);
			Assert.AreEqual(JobStepBatchState.Ready, JobTaskSource.GetQueueForTesting().Min!.Batch.State);
			
			Assert.IsTrue(EventReceived);
			Assert.IsTrue(EventPoolHasAgentsOnline!.Value);
		}
		
		[TestMethod]
		public async Task UpdateJobQueueWithNoAgentsInPool()
		{
			Fixture Fixture = await SetupPoolWithAgentAsync(IsPoolAutoScaled: true, ShouldCreateAgent: false, IsAgentEnabled: false);
			
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);

			IJob Job = (await JobService.GetJobAsync(Fixture.Job1.Id))!;
			Assert.AreEqual(JobStepBatchError.NoAgentsInPool, Job.Batches[0].Error);
			
			Assert.IsFalse(EventReceived);
		}
		
		[TestMethod]
		public async Task UpdateJobQueueWithNoAgentsOnlineInPool()
		{
			Fixture Fixture = await SetupPoolWithAgentAsync(IsPoolAutoScaled: false, ShouldCreateAgent: true, IsAgentEnabled: false);
			
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);

			IJob Job = (await JobService.GetJobAsync(Fixture.Job1.Id))!;
			Assert.AreEqual(JobStepBatchError.NoAgentsOnline, Job.Batches[0].Error);
			
			Assert.IsFalse(EventReceived);
		}
		
		[TestMethod]
		public async Task UpdateJobQueueWithNoAgentsOnlineInAutoScaledPool()
		{
			Fixture Fixture = await SetupPoolWithAgentAsync(IsPoolAutoScaled: true, ShouldCreateAgent: true, IsAgentEnabled: false);
			
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(1, JobTaskSource.GetQueueForTesting().Count);

			Assert.AreEqual(Fixture.Job1.Id, JobTaskSource.GetQueueForTesting().Min!.Id.Item1);
			Assert.AreEqual(JobStepBatchState.Ready, JobTaskSource.GetQueueForTesting().Min!.Batch.State);

			Assert.IsTrue(EventReceived);
			Assert.IsFalse(EventPoolHasAgentsOnline!.Value);
		}

		private async Task<Fixture> SetupPoolWithAgentAsync(bool IsPoolAutoScaled, bool ShouldCreateAgent, bool IsAgentEnabled)
		{
			Fixture Fixture = await CreateFixtureAsync();
			IPool Pool = await PoolService.CreatePoolAsync(Fixture.PoolName, null, IsPoolAutoScaled, 0, 0);

			if (ShouldCreateAgent)
			{
				IAgent? Agent = await AgentService.CreateAgentAsync("TestAgent", IsAgentEnabled, null, new List<StringId<IPool>> { Pool.Id });
				await AgentService.CreateSessionAsync(Agent, AgentStatus.Ok, new List<string>(), new Dictionary<string, int>(), null);
			}
			
			JobTaskSource.OnJobScheduled += (Pool, PoolHasAgentsOnline, Job, Graph, BatchId) =>
			{
				EventReceived = true;
				EventPool = Pool;
				EventPoolHasAgentsOnline = PoolHasAgentsOnline;
			};

			return Fixture;
		}
	}
}
