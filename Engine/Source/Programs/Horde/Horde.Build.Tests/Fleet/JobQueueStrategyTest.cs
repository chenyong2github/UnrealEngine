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
	public class JobQueueTest : TestSetup
	{
		[TestMethod]
		public async Task GetPoolQueueSizes()
		{
			(JobQueueStrategy Strategy, PoolSizeData PoolSizeData) = await SetUpJobsAsync(1, 5);
			Dictionary<PoolId, int> PoolQueueSizes = await Strategy.GetPoolQueueSizesAsync(Clock.UtcNow - TimeSpan.FromHours(2));
			Assert.AreEqual(1, PoolQueueSizes.Count);
			Assert.AreEqual(5, PoolQueueSizes[PoolSizeData.Pool.Id]);
		}
		
		[TestMethod]
		public async Task JobQueueWith5Jobs()
		{
			(JobQueueStrategy Strategy, PoolSizeData PoolSizeData) = await SetUpJobsAsync(1, 5);
			Dictionary<PoolId, PoolSizeData> PoolSizeDatas = new ();
			PoolSizeDatas[PoolSizeData.Pool.Id] = PoolSizeData;
			await Strategy.CalcDesiredPoolSizesAsync(PoolSizeDatas);
			PoolSizeData = PoolSizeDatas[PoolSizeData.Pool.Id];
			Assert.AreEqual(1, PoolSizeDatas.Count);
			Assert.AreEqual(PoolSizeData.Agents.Count + 0, PoolSizeData.DesiredAgentCount);
		}
		
		[TestMethod]
		public async Task JobQueueWith15Jobs()
		{
			(JobQueueStrategy Strategy, PoolSizeData PoolSizeData) = await SetUpJobsAsync(1, 15);
			Dictionary<PoolId, PoolSizeData> PoolSizeDatas = new ();
			PoolSizeDatas[PoolSizeData.Pool.Id] = PoolSizeData;
			await Strategy.CalcDesiredPoolSizesAsync(PoolSizeDatas);
			PoolSizeData = PoolSizeDatas[PoolSizeData.Pool.Id];
			Assert.AreEqual(1, PoolSizeDatas.Count);
			Assert.AreEqual(PoolSizeData.Agents.Count + 1, PoolSizeData.DesiredAgentCount);
		}
		
		[TestMethod]
		public async Task JobQueueWith25Jobs()
		{
			(JobQueueStrategy Strategy, PoolSizeData PoolSizeData) = await SetUpJobsAsync(1, 25);
			Dictionary<PoolId, PoolSizeData> PoolSizeDatas = new ();
			PoolSizeDatas[PoolSizeData.Pool.Id] = PoolSizeData;
			await Strategy.CalcDesiredPoolSizesAsync(PoolSizeDatas);
			PoolSizeData = PoolSizeDatas[PoolSizeData.Pool.Id];
			Assert.AreEqual(1, PoolSizeDatas.Count);
			Assert.AreEqual(PoolSizeData.Agents.Count + 2, PoolSizeData.DesiredAgentCount);
		}
	
		/// <summary>
		/// Set up a fixture for job queue tests, ensuring a certain number of job batches are in running or waiting state
		/// </summary>
		/// <param name="NumBatchesRunning">Num of job batches that should be in state running</param>
		/// <param name="NumBatchesWaiting">Num of job batches that should be in state waiting</param>
		private async Task<(JobQueueStrategy, PoolSizeData)> SetUpJobsAsync(int NumBatchesRunning, int NumBatchesWaiting)
		{
			IPool Pool1 = await PoolService.CreatePoolAsync("bogusPool1", null, true, 0, 0);
			IAgent Agent1 = await CreateAgentAsync(Pool1);
			IAgent Agent2 = await CreateAgentAsync(Pool1);
			PoolSizeData PoolSize = new (Pool1, new List<IAgent>() { Agent1, Agent2 }, null);
			
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
			
			for (int i = 0; i < NumBatchesWaiting; i++)
			{
				IJob Job = await AddPlaceholderJob(Graph, Stream.Id, NodeForAgentType1);
				await JobCollection.TryUpdateBatchAsync(Job, Graph, Job.Batches[0].Id, null, JobStepBatchState.Waiting, null);
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