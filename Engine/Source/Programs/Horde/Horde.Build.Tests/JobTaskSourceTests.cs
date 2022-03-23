// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Horde.Build.Api;
using Horde.Build.Models;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests
{
	[TestClass]
	public class JobTaskSourceTests : TestSetup
	{
		private bool _eventReceived;
		private bool? _eventPoolHasAgentsOnline;
		
		[TestMethod]
		public async Task UpdateJobQueueNormal()
		{
			Fixture fixture = await SetupPoolWithAgentAsync(isPoolAutoScaled: true, shouldCreateAgent: true, isAgentEnabled: true);

			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(1, JobTaskSource.GetQueueForTesting().Count);
			Assert.AreEqual(fixture.Job1.Id, JobTaskSource.GetQueueForTesting().Min!.Id.Item1);
			Assert.AreEqual(JobStepBatchState.Ready, JobTaskSource.GetQueueForTesting().Min!.Batch.State);
			
			Assert.IsTrue(_eventReceived);
			Assert.IsTrue(_eventPoolHasAgentsOnline!.Value);
		}
		
		[TestMethod]
		public async Task UpdateJobQueueWithNoAgentsInPool()
		{
			Fixture fixture = await SetupPoolWithAgentAsync(isPoolAutoScaled: true, shouldCreateAgent: false, isAgentEnabled: false);
			
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);

			IJob job = (await JobService.GetJobAsync(fixture.Job1.Id))!;
			Assert.AreEqual(JobStepBatchError.NoAgentsInPool, job.Batches[0].Error);
			
			Assert.IsFalse(_eventReceived);
		}
		
		[TestMethod]
		public async Task UpdateJobQueueWithNoAgentsOnlineInPool()
		{
			Fixture fixture = await SetupPoolWithAgentAsync(isPoolAutoScaled: false, shouldCreateAgent: true, isAgentEnabled: false);
			
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);

			IJob job = (await JobService.GetJobAsync(fixture.Job1.Id))!;
			Assert.AreEqual(JobStepBatchError.NoAgentsOnline, job.Batches[0].Error);
			
			Assert.IsFalse(_eventReceived);
		}
		
		[TestMethod]
		public async Task UpdateJobQueueWithNoAgentsOnlineInAutoScaledPool()
		{
			Fixture fixture = await SetupPoolWithAgentAsync(isPoolAutoScaled: true, shouldCreateAgent: true, isAgentEnabled: false);
			
			Assert.AreEqual(0, JobTaskSource.GetQueueForTesting().Count);
			await JobTaskSource.TickAsync(CancellationToken.None);
			Assert.AreEqual(1, JobTaskSource.GetQueueForTesting().Count);

			Assert.AreEqual(fixture.Job1.Id, JobTaskSource.GetQueueForTesting().Min!.Id.Item1);
			Assert.AreEqual(JobStepBatchState.Ready, JobTaskSource.GetQueueForTesting().Min!.Batch.State);

			Assert.IsTrue(_eventReceived);
			Assert.IsFalse(_eventPoolHasAgentsOnline!.Value);
		}

		private async Task<Fixture> SetupPoolWithAgentAsync(bool isPoolAutoScaled, bool shouldCreateAgent, bool isAgentEnabled)
		{
			Fixture fixture = await CreateFixtureAsync();
			IPool pool = await PoolService.CreatePoolAsync(Fixture.PoolName, null, isPoolAutoScaled, 0, 0);

			if (shouldCreateAgent)
			{
				IAgent? agent = await AgentService.CreateAgentAsync("TestAgent", isAgentEnabled, null, new List<StringId<IPool>> { pool.Id });
				await AgentService.CreateSessionAsync(agent, AgentStatus.Ok, new List<string>(), new Dictionary<string, int>(), null);
			}
			
			JobTaskSource.OnJobScheduled += (pool, poolHasAgentsOnline, job, graph, batchId) =>
			{
				_eventReceived = true;
				_eventPoolHasAgentsOnline = poolHasAgentsOnline;
			};

			return fixture;
		}
	}
}
