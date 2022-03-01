// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
using HordeServerTests;
using Microsoft.AspNetCore.Mvc;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests
{
	/// <summary>
	/// Database-only integration test for LeasesController
	/// </summary>
	[TestClass]
	public class LeasesControllerDbTest : TestSetup
	{
		[TestMethod]
		public async Task FindLeases()
		{
			DateTimeOffset MinTime = Clock.UtcNow - TimeSpan.FromMinutes(5);
			DateTimeOffset MaxTime = Clock.UtcNow;

			ILease Lease1 = await CreateLease(Clock.UtcNow - TimeSpan.FromMinutes(10), TimeSpan.FromMinutes(6));
			ILease Lease2 = await CreateLease(Clock.UtcNow - TimeSpan.FromMinutes(7), TimeSpan.FromMinutes(3.1));
			ILease OutOfTimeWindow = await CreateLease(Clock.UtcNow - TimeSpan.FromMinutes(7), TimeSpan.FromMinutes(25));
			
			ActionResult<List<object>> Res = await LeasesController.FindLeasesAsync(null, null, null, null, MinTime, MaxTime);
			Assert.AreEqual(2, Res.Value!.Count);
			Assert.AreEqual(Lease2.Id.ToString(), (Res.Value[0] as GetAgentLeaseResponse)!.Id);
			Assert.AreEqual(Lease1.Id.ToString(), (Res.Value[1] as GetAgentLeaseResponse)!.Id);
		}

		private async Task<ILease> CreateLease(DateTime StartTime, TimeSpan Duration)
		{
			ObjectId<ILease> Id = ObjectId<ILease>.GenerateNewId();
			ObjectId<ISession> SessionId = ObjectId<ISession>.GenerateNewId();
			ILease Lease = await LeaseCollection.AddAsync(Id, "myLease", new AgentId("agent-1"), SessionId, null, null, null, StartTime, new byte[] { });
			await LeaseCollection.TrySetOutcomeAsync(Id, StartTime + Duration, LeaseOutcome.Success, null);
			return Lease;
		}
	}
}