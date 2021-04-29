using HordeServer.Collections;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Text;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

using Microsoft.VisualStudio.TestTools.UnitTesting;
using EpicGames.Core;
using Moq;
using HordeServer.Models;
using HordeServer.Services;
using System.Threading.Tasks;
using HordeServer.Api;
using System.Linq;
using HordeCommon;
using HordeServer.Collections.Impl;
using Microsoft.Extensions.Logging.Abstractions;

namespace HordeServerTests
{
	[TestClass]
	public class JobCollectionTests : DatabaseIntegrationTest
	{
		IGraphCollection GraphCollection;
		IJobCollection JobCollection;

		public JobCollectionTests()
		{
			DatabaseService DatabaseService = GetDatabaseService();
			this.GraphCollection = new GraphCollection(DatabaseService);
			this.JobCollection = new JobCollection(DatabaseService, NullLogger<JobCollection>.Instance);
		}

		CreateGroupRequest AddGroup(List<CreateGroupRequest> Groups)
		{
			CreateGroupRequest Group = new CreateGroupRequest("win64", new List<CreateNodeRequest>());
			Groups.Add(Group);
			return Group;
		}

		CreateNodeRequest AddNode(CreateGroupRequest Group, string Name, string[]? InputDependencies, Action<CreateNodeRequest>? Action = null)
		{
			CreateNodeRequest Node = new CreateNodeRequest(Name, InputDependencies?.ToList(), InputDependencies?.ToList(), null, null, null, null, null, null);
			if (Action != null)
			{
				Action.Invoke(Node);
			}
			Group.Nodes.Add(Node);
			return Node;
		}

		async Task StartBatch(IJob Job, IGraph Graph, int BatchIdx)
		{
			Assert.AreEqual(JobStepBatchState.Ready, Job.Batches[BatchIdx].State);
			Assert.IsTrue(await JobCollection.TryUpdateBatchAsync(Job, Graph, Job.Batches[BatchIdx].Id, null, JobStepBatchState.Running, null));
			Assert.AreEqual(JobStepBatchState.Running, Job.Batches[BatchIdx].State);
		}

		async Task RunStep(IJob Job, IGraph Graph, int BatchIdx, int StepIdx, JobStepOutcome Outcome)
		{
			Assert.AreEqual(JobStepState.Ready, Job.Batches[BatchIdx].Steps[StepIdx].State);
			Assert.IsTrue(await JobCollection.TryUpdateStepAsync(Job, Graph, Job.Batches[BatchIdx].Id, Job.Batches[BatchIdx].Steps[StepIdx].Id, JobStepState.Running, JobStepOutcome.Success));
			Assert.AreEqual(JobStepState.Running, Job.Batches[BatchIdx].Steps[StepIdx].State);
			Assert.IsTrue(await JobCollection.TryUpdateStepAsync(Job, Graph, Job.Batches[BatchIdx].Id, Job.Batches[BatchIdx].Steps[StepIdx].Id, JobStepState.Completed, Outcome));
			Assert.AreEqual(JobStepState.Completed, Job.Batches[BatchIdx].Steps[StepIdx].State);
			Assert.AreEqual(Outcome, Job.Batches[BatchIdx].Steps[StepIdx].Outcome);
		}

		[TestMethod]
		public async Task TestStates()
		{
			Mock<ITemplate> TemplateMock = new Mock<ITemplate>(MockBehavior.Strict);
			TemplateMock.SetupGet(x => x.InitialAgentType).Returns((string?)null);

			IGraph BaseGraph = await GraphCollection.AddAsync(TemplateMock.Object);

			List<string> Arguments = new List<string>();
			Arguments.Add("-Target=Publish Client");
			Arguments.Add("-Target=Post-Publish Client");

			IJob Job = await JobCollection.AddAsync(ObjectId.GenerateNewId(), new StreamId("ue4-main"), new TemplateRefId("test-build"), ContentHash.SHA1("hello"), BaseGraph, "Test job", 123, 123, null, null, null, "Ben", null, null, null, false, false, null, null, null, Arguments);

			await StartBatch(Job, BaseGraph, 0);
			await RunStep(Job, BaseGraph, 0, 0, JobStepOutcome.Success); // Setup Build

			List<CreateGroupRequest> NewGroups = new List<CreateGroupRequest>();

			CreateGroupRequest InitialGroup = AddGroup(NewGroups);
			AddNode(InitialGroup, "Update Version Files", null);
			AddNode(InitialGroup, "Compile Editor", new[] { "Update Version Files" });

			CreateGroupRequest CompileGroup = AddGroup(NewGroups);
			AddNode(CompileGroup, "Compile Client", new[] { "Update Version Files" });

			CreateGroupRequest PublishGroup = AddGroup(NewGroups);
			AddNode(PublishGroup, "Cook Client", new[] { "Compile Editor" }, x => x.RunEarly = true);
			AddNode(PublishGroup, "Publish Client", new[] { "Compile Client", "Cook Client" });
			AddNode(PublishGroup, "Post-Publish Client", null, x => x.OrderDependencies = new List<string> { "Publish Client" });

			IGraph Graph = await GraphCollection.AppendAsync(BaseGraph, NewGroups, null, null);
			Assert.IsTrue(await JobCollection.TryUpdateGraphAsync(Job, Graph));

			await StartBatch(Job, Graph, 1);
			await RunStep(Job, Graph, 1, 0, JobStepOutcome.Success); // Update Version Files
			await RunStep(Job, Graph, 1, 1, JobStepOutcome.Success); // Compile Editor

			await StartBatch(Job, Graph, 2);
			await RunStep(Job, Graph, 2, 0, JobStepOutcome.Success); // Compile Client

			await StartBatch(Job, Graph, 3);
			await RunStep(Job, Graph, 3, 0, JobStepOutcome.Failure); // Cook Client
			Assert.AreEqual(JobStepState.Skipped, Job.Batches[3].Steps[1].State); // Publish Client
			Assert.AreEqual(JobStepState.Skipped, Job.Batches[3].Steps[2].State); // Post-Publish Client
		}

		[TestMethod]
		public async Task TryAssignLeaseTest()
		{
			TestSetup TestSetup = await GetTestSetup();
			await TestSetup.JobCollection.TryAssignLeaseAsync(TestSetup.Fixture!.Job1, 0, new PoolId("foo"), TestSetup.Fixture.Agent1.Id,
				ObjectId.GenerateNewId(), ObjectId.GenerateNewId(), ObjectId.GenerateNewId());
			
			IJob Job = (await TestSetup.JobCollection.GetAsync(TestSetup.Fixture!.Job1.Id))!;
			await TestSetup.JobCollection.TryAssignLeaseAsync(Job, 0, new PoolId("foo"), TestSetup.Fixture.Agent1.Id,
				ObjectId.GenerateNewId(), ObjectId.GenerateNewId(), ObjectId.GenerateNewId());
			
			// Manually verify the log output
		}
	}
}
