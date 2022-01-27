// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Horde.Build.Fleet.Autoscale;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Models;
using HordeServer.Utilities;
using HordeServerTests;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests.Fleet
{
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;
	
	[TestClass]
	public class LeaseUtilizationStrategyTest : TestSetup
	{
		private IAgent Agent1 = null!;
		private IAgent Agent2 = null!;
		private IAgent Agent3 = null!;
		private IAgent Agent4 = null!;
		private List<IAgent> PoolAgents = null!;

		public void CreateAgents(IPool Pool)
		{
			Agent1 = CreateAgentAsync(Pool).Result;
			Agent2 = CreateAgentAsync(Pool).Result;
			Agent3 = CreateAgentAsync(Pool).Result;
			Agent4 = CreateAgentAsync(Pool).Result;
			PoolAgents = new() { Agent1, Agent2, Agent3, Agent4 };
		}

		[TestMethod]
		public async Task UtilizationFull()
		{
			IPool Pool = await PoolService.CreatePoolAsync("AutoscalePool1", null, true, 0, 0);
			CreateAgents(Pool);
			
			await AddPlaceholderLease(Agent1, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent2, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent3, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent4, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AssertPoolSizeAsync(Pool, 4);
		}
		
		[TestMethod]
		public async Task UtilizationHalf()
		{
			IPool Pool = await PoolService.CreatePoolAsync("AutoscalePool1", null, true, 0, 0);
			CreateAgents(Pool);
			
			await AddPlaceholderLease(Agent1, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent2, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AssertPoolSizeAsync(Pool, 2);
		}
		
		[TestMethod]
		public async Task UtilizationZero()
		{
			IPool Pool = await PoolService.CreatePoolAsync("AutoscalePool1", null, true, 0, 0);
			CreateAgents(Pool);

			await AssertPoolSizeAsync(Pool, 0);
		}
		
		[TestMethod]
		public async Task ReserveAgents()
		{
			IPool Pool = await PoolService.CreatePoolAsync("AutoscalePool1", null, true, 0, 5);
			CreateAgents(Pool);
			
			await AddPlaceholderLease(Agent1, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent2, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent3, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			await AddPlaceholderLease(Agent4, Pool, Clock.UtcNow - TimeSpan.FromMinutes(120), TimeSpan.FromMinutes(120));
			
			// Full utilization should mean all agents plus the reserve agents
			await AssertPoolSizeAsync(Pool, 9);
		}
		
		[TestMethod]
		public async Task MinAgents()
		{
			IPool Pool = await PoolService.CreatePoolAsync("AutoscalePool1", null, true, 2, 0);
			CreateAgents(Pool);
			
			// Even with no utilization, expect at least the min number of agents
			await AssertPoolSizeAsync(Pool, 2);
		}

		private async Task AssertPoolSizeAsync(IPool Pool, int ExpectedNumAgents)
		{
			LeaseUtilizationStrategy Strategy = new (AgentCollection, PoolCollection, LeaseCollection, Clock);
			List<PoolSizeData> Output = await Strategy.CalcDesiredPoolSizesAsync(new() { new(Pool, PoolAgents, null) });
			Assert.AreEqual(1, Output.Count);
			Assert.AreEqual(ExpectedNumAgents, Output[0].DesiredAgentCount);
		}
		
		private async Task<ILease> AddPlaceholderLease(IAgent Agent, IPool Pool, DateTime StartTime, TimeSpan Duration)
		{
			Assert.IsNotNull(Agent.SessionId);
			
			ExecuteJobTask PlaceholderJobTask = new();
			PlaceholderJobTask.JobName = "placeholderJobName";
			byte[] Payload = Any.Pack(PlaceholderJobTask).ToByteArray();

			ILease Lease = await LeaseCollection.AddAsync(ObjectId<ILease>.GenerateNewId(), "placeholderLease", Agent.Id, Agent.SessionId!.Value, new StreamId("placeholderStream"), Pool.Id, null, StartTime, Payload);
			bool WasModified = await LeaseCollection.TrySetOutcomeAsync(Lease.Id, StartTime + Duration, LeaseOutcome.Success, null);
			Assert.IsTrue(WasModified);
			return Lease;
		}
	}
}