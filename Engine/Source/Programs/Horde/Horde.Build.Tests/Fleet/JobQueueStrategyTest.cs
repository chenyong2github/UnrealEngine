// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Fleet.Autoscale;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
using HordeServerTests;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests.Fleet
{
	using PoolId = StringId<IPool>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	
	[TestClass]
	public class JobQueueStrategyTest : TestSetup
	{
		[TestMethod]
		public async Task GetPoolQueueSizes()
		{
			(JobQueueStrategy Strategy, PoolSizeData PoolSizeData) = await SetUpJobsAsync(1, 5);
			await Clock.AdvanceAsync(Strategy.ReadyTimeThreshold + TimeSpan.FromSeconds(5));
			Dictionary<PoolId, int> PoolQueueSizes = await Strategy.GetPoolQueueSizesAsync(Clock.UtcNow - TimeSpan.FromHours(2));
			Assert.AreEqual(1, PoolQueueSizes.Count);
			Assert.AreEqual(5, PoolQueueSizes[PoolSizeData.Pool.Id]);
		}
		
		[TestMethod]
		public async Task EmptyJobQueue()
		{
			await AssertAgentCount(0, -1, false);
		}
		
		[TestMethod]
		public async Task BelowMinQueueSizeForScaleOut()
		{
			await AssertAgentCount(2, 0);
		}
		
		[TestMethod]
		public async Task NoAgentsInPool()
		{
			await AssertAgentCount(1, 1, true, 0);
		}
		
		[TestMethod]
		public async Task BatchesNotWaitingLongEnough()
		{
			await AssertAgentCount(3, -1, false);
		}
		
		[TestMethod]
		public async Task NumQueuedJobs3()
		{
			await AssertAgentCount(3, 1, true);
		}
		
		[TestMethod]
		public async Task NumQueuedJobs6()
		{
			await AssertAgentCount(6, 2);
		}
		
		[TestMethod]
		public async Task NumQueuedJobs25()
		{
			await AssertAgentCount(25, 6);
		}

		public async Task AssertAgentCount(int NumBatchesReady, int ExpectedAgentDelta, bool WaitedBeyondThreshold = true, int NumAgents = 10)
		{
			(JobQueueStrategy Strategy, PoolSizeData PoolSizeData) = await SetUpJobsAsync(1, NumBatchesReady, NumAgents);
			TimeSpan TimeToWait = WaitedBeyondThreshold
				? Strategy.ReadyTimeThreshold + TimeSpan.FromSeconds(5)
				: TimeSpan.FromSeconds(15);
			
			await Clock.AdvanceAsync(TimeToWait);

			List<PoolSizeData> Result = await Strategy.CalcDesiredPoolSizesAsync(new() { PoolSizeData });
			Assert.AreEqual(1, Result.Count);
			Assert.AreEqual(Result[0].Agents.Count + ExpectedAgentDelta, Result[0].DesiredAgentCount);
		}
	
		/// <summary>
		/// Set up a fixture for job queue tests, ensuring a certain number of job batches are in running or waiting state
		/// </summary>
		/// <param name="NumBatchesRunning">Num of job batches that should be in state running</param>
		/// <param name="NumBatchesReady">Num of job batches that should be in state waiting</param>
		private async Task<(JobQueueStrategy, PoolSizeData)> SetUpJobsAsync(int NumBatchesRunning, int NumBatchesReady, int NumAgents = 10)
		{
			IPool Pool1 = await PoolService.CreatePoolAsync("bogusPool1", null, true, 0, 0);
			List<IAgent> Agents = new();
			for (int i = 0; i < NumAgents; i++)
			{
				Agents.Add(await CreateAgentAsync(Pool1));
			}
			
			PoolSizeData PoolSize = new (Pool1, Agents, null);
			
			string AgentTypeName1 = "bogusAgentType1";
			Dictionary<string, CreateAgentTypeRequest> AgentTypes = new() { {AgentTypeName1, new() { Pool = Pool1.Name} }, };
			IStream Stream = (await StreamCollection.TryCreateOrReplaceAsync(
				new StreamId("ue5-main"),
				null,
				"",
				"",
				new ProjectId("does-not-exist"),
				new StreamConfig { Name = "//UE5/Main", AgentTypes = AgentTypes}
			))!;

			string NodeForAgentType1 = "bogusNodeOnAgentType1";
			IGraph Graph = await GraphCollection.AppendAsync(null, new()
			{
				new NewGroup(AgentTypeName1, new List<NewNode>
				{
					new (NodeForAgentType1),
				})
			});

			for (int i = 0; i < NumBatchesRunning; i++)
			{
				IJob Job = await AddPlaceholderJob(Graph, Stream.Id, NodeForAgentType1);
				await JobCollection.TryUpdateBatchAsync(Job, Graph, Job.Batches[0].Id, null, JobStepBatchState.Running, null);
			}
			
			for (int i = 0; i < NumBatchesReady; i++)
			{
				IJob Job = await AddPlaceholderJob(Graph, Stream.Id, NodeForAgentType1);
				await JobCollection.TryUpdateBatchAsync(Job, Graph, Job.Batches[0].Id, null, JobStepBatchState.Ready, null);
			}
			
			return (new (JobCollection, GraphCollection, StreamService, Clock), PoolSize);
		}
		
		private async Task<IJob> AddPlaceholderJob(IGraph Graph, StreamId StreamId, string NodeNameToExecute)
		{
			IJob Job = await JobCollection.AddAsync(ObjectId<IJob>.GenerateNewId(), StreamId,
				new StringId<TemplateRef>("bogusTemplateRefId"), ContentHash.Empty, Graph, "bogusJobName",
				1000, 1000, null, null, null, null, null, null, null, false,
				false, null, null, new List<string> { "-Target=" + NodeNameToExecute });

			return Job;
		}
	}
}