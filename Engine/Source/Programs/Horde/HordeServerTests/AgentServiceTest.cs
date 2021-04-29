using System;
using System.Collections.Generic;
using System.Security.Claims;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Api;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.AspNetCore.Mvc;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;
using HordeServer.Utilities;

namespace HordeServerTests
{
    /// <summary>
    /// Testing the agent service
    /// </summary>
	[TestClass]
    public class AgentServiceTest : DatabaseIntegrationTest
    {
        [TestMethod]
        public async Task GetJobs()
        {
	        TestSetup TestSetup = await GetTestSetup();

	        ActionResult<List<object>> Res = await TestSetup.JobsController.FindJobsAsync();
	        Assert.AreEqual(2, Res.Value.Count);
	        Assert.AreEqual("hello2", (Res.Value[0] as GetJobResponse)!.Name);
	        Assert.AreEqual("hello1", (Res.Value[1] as GetJobResponse)!.Name);
	        
	        Res = await TestSetup.JobsController.FindJobsAsync(IncludePreflight: false);
	        Assert.AreEqual(1, Res.Value.Count);
	        Assert.AreEqual("hello2", (Res.Value[0] as GetJobResponse)!.Name);
        }
        
        [TestMethod]
        public async Task CreateSessionTest()
        {
	        TestSetup TestSetup = await GetTestSetup();
	        AgentService AgentService = TestSetup.AgentService;

	        IAgent Agent = await AgentService.CreateSessionAsync(TestSetup.Fixture!.Agent1, AgentStatus.Ok, new AgentCapabilities(),
		        AgentSoftwareVersion.Sanitize("test"));
	        
	        Assert.IsTrue(AgentService.AuthorizeSession(Agent, GetUser(Agent)));
	        TestSetup.Clock.Advance(TimeSpan.FromMinutes(20));
	        Assert.IsFalse(AgentService.AuthorizeSession(Agent, GetUser(Agent)));
        }

        private ClaimsPrincipal GetUser(IAgent Agent)
        {
	        return new ClaimsPrincipal(new ClaimsIdentity(new List<Claim>
	        {
		        new Claim(HordeClaimTypes.AgentSessionId, Agent.SessionId.ToString()),
	        }, "TestAuthType"));
        }
    }
}