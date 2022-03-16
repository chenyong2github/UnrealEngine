// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Security.Claims;
using System.Threading.Tasks;
using HordeCommon;
using Horde.Build.Api;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Horde.Build.Models;
using Horde.Build.Services;
using Microsoft.AspNetCore.Mvc;
using AgentSoftwareVersion = Horde.Build.Utilities.StringId<Horde.Build.Collections.IAgentSoftwareCollection>;
using Horde.Build.Utilities;
using MongoDB.Bson;

namespace Horde.Build.Tests
{
    /// <summary>
    /// Testing the agent service
    /// </summary>
	[TestClass]
    public class AgentServiceTest : TestSetup
    {
        [TestMethod]
        public async Task GetJobs()
        {
			Fixture Fixture = await CreateFixtureAsync();

			ActionResult<List<object>> Res = await JobsController.FindJobsAsync();
	        Assert.AreEqual(2, Res.Value!.Count);
	        Assert.AreEqual("hello2", (Res.Value[0] as GetJobResponse)!.Name);
	        Assert.AreEqual("hello1", (Res.Value[1] as GetJobResponse)!.Name);
	        
	        Res = await JobsController.FindJobsAsync(IncludePreflight: false);
	        Assert.AreEqual(1, Res.Value!.Count);
	        Assert.AreEqual("hello2", (Res.Value[0] as GetJobResponse)!.Name);
        }
        
        [TestMethod]
        public async Task CreateSessionTest()
        {
			Fixture Fixture = await CreateFixtureAsync();

	        IAgent Agent = await AgentService.CreateSessionAsync(Fixture.Agent1, AgentStatus.Ok, new List<string>(), new Dictionary<string, int>(),
		        "test");
	        
	        Assert.IsTrue(AgentService.AuthorizeSession(Agent, GetUser(Agent)));
	        await Clock.AdvanceAsync(TimeSpan.FromMinutes(20));
	        Assert.IsFalse(AgentService.AuthorizeSession(Agent, GetUser(Agent)));
        }

        private ClaimsPrincipal GetUser(IAgent Agent)
        {
	        return new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
	        {
		        new Claim(HordeClaimTypes.AgentSessionId, Agent.SessionId.ToString()!),
	        }, "TestAuthType"));
        }
    }
}