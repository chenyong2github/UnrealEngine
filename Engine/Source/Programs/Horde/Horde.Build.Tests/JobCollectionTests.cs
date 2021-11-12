// Copyright Epic Games, Inc. All Rights Reserved.

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
using HordeServer.Utilities;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;

	[TestClass]
	public class JobCollectionTests : TestSetup
	{
		NewGroup AddGroup(List<NewGroup> Groups)
		{
			NewGroup Group = new NewGroup("win64", new List<NewNode>());
			Groups.Add(Group);
			return Group;
		}

		NewNode AddNode(NewGroup Group, string Name, string[]? InputDependencies, Action<NewNode>? Action = null)
		{
			NewNode Node = new NewNode(Name, InputDependencies?.ToList(), InputDependencies?.ToList(), null, null, null, null, null, null);
			if (Action != null)
			{
				Action.Invoke(Node);
			}
			Group.Nodes.Add(Node);
			return Node;
		}

		async Task<IJob> StartBatch(IJob Job, IGraph Graph, int BatchIdx)
		{
			Assert.AreEqual(JobStepBatchState.Ready, Job.Batches[BatchIdx].State);
			Job = Deref(await JobCollection.TryUpdateBatchAsync(Job, Graph, Job.Batches[BatchIdx].Id, null, JobStepBatchState.Running, null));
			Assert.AreEqual(JobStepBatchState.Running, Job.Batches[BatchIdx].State);
			return Job;
		}

		async Task<IJob> RunStep(IJob Job, IGraph Graph, int BatchIdx, int StepIdx, JobStepOutcome Outcome)
		{
			Assert.AreEqual(JobStepState.Ready, Job.Batches[BatchIdx].Steps[StepIdx].State);
			Job = Deref(await JobCollection.TryUpdateStepAsync(Job, Graph, Job.Batches[BatchIdx].Id, Job.Batches[BatchIdx].Steps[StepIdx].Id, JobStepState.Running, JobStepOutcome.Success));
			Assert.AreEqual(JobStepState.Running, Job.Batches[BatchIdx].Steps[StepIdx].State);
			Job = Deref(await JobCollection.TryUpdateStepAsync(Job, Graph, Job.Batches[BatchIdx].Id, Job.Batches[BatchIdx].Steps[StepIdx].Id, JobStepState.Completed, Outcome));
			Assert.AreEqual(JobStepState.Completed, Job.Batches[BatchIdx].Steps[StepIdx].State);
			Assert.AreEqual(Outcome, Job.Batches[BatchIdx].Steps[StepIdx].Outcome);
			return Job;
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

			IJob Job = await JobCollection.AddAsync(JobId.GenerateNewId(), new StreamId("ue4-main"), new TemplateRefId("test-build"), ContentHash.SHA1("hello"), BaseGraph, "Test job", 123, 123, null, null, null, null, null, null, null, false, false, null, null, Arguments);

			Job = await StartBatch(Job, BaseGraph, 0);
			Job = await RunStep(Job, BaseGraph, 0, 0, JobStepOutcome.Success); // Setup Build

			List<NewGroup> NewGroups = new List<NewGroup>();

			NewGroup InitialGroup = AddGroup(NewGroups);
			AddNode(InitialGroup, "Update Version Files", null);
			AddNode(InitialGroup, "Compile Editor", new[] { "Update Version Files" });

			NewGroup CompileGroup = AddGroup(NewGroups);
			AddNode(CompileGroup, "Compile Client", new[] { "Update Version Files" });

			NewGroup PublishGroup = AddGroup(NewGroups);
			AddNode(PublishGroup, "Cook Client", new[] { "Compile Editor" }, x => x.RunEarly = true);
			AddNode(PublishGroup, "Publish Client", new[] { "Compile Client", "Cook Client" });
			AddNode(PublishGroup, "Post-Publish Client", null, x => x.OrderDependencies = new List<string> { "Publish Client" });

			IGraph Graph = await GraphCollection.AppendAsync(BaseGraph, NewGroups, null, null);
			Job = Deref(await JobCollection.TryUpdateGraphAsync(Job, Graph));

			Job = await StartBatch(Job, Graph, 1);
			Job = await RunStep(Job, Graph, 1, 0, JobStepOutcome.Success); // Update Version Files
			Job = await RunStep(Job, Graph, 1, 1, JobStepOutcome.Success); // Compile Editor

			Job = await StartBatch(Job, Graph, 2);
			Job = await RunStep(Job, Graph, 2, 0, JobStepOutcome.Success); // Compile Client

			Job = await StartBatch(Job, Graph, 3);
			Job = await RunStep(Job, Graph, 3, 0, JobStepOutcome.Failure); // Cook Client
			Assert.AreEqual(JobStepState.Skipped, Job.Batches[3].Steps[1].State); // Publish Client
			Assert.AreEqual(JobStepState.Skipped, Job.Batches[3].Steps[2].State); // Post-Publish Client
		}

		[TestMethod]
		public async Task TryAssignLeaseTest()
		{
			Fixture Fixture = await CreateFixtureAsync();

			await JobCollection.TryAssignLeaseAsync(Fixture.Job1, 0, new PoolId("foo"), Fixture.Agent1.Id,
				ObjectId.GenerateNewId(), LeaseId.GenerateNewId(), LogId.GenerateNewId());
			
			IJob Job = (await JobCollection.GetAsync(Fixture.Job1.Id))!;
			await JobCollection.TryAssignLeaseAsync(Job, 0, new PoolId("foo"), Fixture.Agent1.Id,
				ObjectId.GenerateNewId(), LeaseId.GenerateNewId(), LogId.GenerateNewId());
			
			// Manually verify the log output
		}

		[TestMethod]
		public Task LostLeaseTestWithDependency()
		{
			return LostLeaseTestInternal(true);
		}

		[TestMethod]
		public Task LostLeaseTestWithoutDependency()
		{
			return LostLeaseTestInternal(false);
		}

		public async Task LostLeaseTestInternal(bool HasDependency)
		{
			Mock<ITemplate> TemplateMock = new Mock<ITemplate>(MockBehavior.Strict);
			TemplateMock.SetupGet(x => x.InitialAgentType).Returns((string?)null);

			IGraph BaseGraph = await GraphCollection.AddAsync(TemplateMock.Object);

			List<string> Arguments = new List<string>();
			Arguments.Add("-Target=Step 1");
			Arguments.Add("-Target=Step 3");

			IJob Job = await JobCollection.AddAsync(JobId.GenerateNewId(), new StreamId("ue4-main"), new TemplateRefId("test-build"), ContentHash.SHA1("hello"), BaseGraph, "Test job", 123, 123, null, null, null, null, null, null, null, false, false, null, null, Arguments);

			Job = await StartBatch(Job, BaseGraph, 0);
			Job = await RunStep(Job, BaseGraph, 0, 0, JobStepOutcome.Success); // Setup Build

			List<NewGroup> NewGroups = new List<NewGroup>();

			NewGroup InitialGroup = AddGroup(NewGroups);
			AddNode(InitialGroup, "Step 1", null);
			AddNode(InitialGroup, "Step 2", HasDependency? new[] { "Step 1" } : null);
			AddNode(InitialGroup, "Step 3", new[] { "Step 2" });

			IGraph Graph = await GraphCollection.AppendAsync(BaseGraph, NewGroups, null, null);
			Job = Deref(await JobCollection.TryUpdateGraphAsync(Job, Graph));

			Job = await StartBatch(Job, Graph, 1);
			Job = await RunStep(Job, Graph, 1, 0, JobStepOutcome.Success); // Step 1
			Job = await RunStep(Job, Graph, 1, 1, JobStepOutcome.Success); // Step 2

			// Force an error executing the batch
			Job = Deref(await JobCollection.TryUpdateBatchAsync(Job, Graph, Job.Batches[1].Id, null, JobStepBatchState.Complete, JobStepBatchError.Incomplete));

			// Check that it restarted all three nodes
			IJob NewJob = (await JobCollection.GetAsync(Job.Id))!;
			Assert.AreEqual(3, NewJob.Batches.Count);
			Assert.AreEqual(1, NewJob.Batches[2].GroupIdx);

			if (HasDependency)
			{
				Assert.AreEqual(3, NewJob.Batches[2].Steps.Count);

				Assert.AreEqual(0, NewJob.Batches[2].Steps[0].NodeIdx);
				Assert.AreEqual(1, NewJob.Batches[2].Steps[1].NodeIdx);
				Assert.AreEqual(2, NewJob.Batches[2].Steps[2].NodeIdx);
			}
			else
			{
				Assert.AreEqual(2, NewJob.Batches[2].Steps.Count);

				Assert.AreEqual(1, NewJob.Batches[2].Steps[0].NodeIdx);
				Assert.AreEqual(2, NewJob.Batches[2].Steps[1].NodeIdx);
			}
		}

		[TestMethod]
		public async Task IncompleteBatchAsync()
		{
			Mock<ITemplate> TemplateMock = new Mock<ITemplate>(MockBehavior.Strict);
			TemplateMock.SetupGet(x => x.InitialAgentType).Returns((string?)null);

			IGraph BaseGraph = await GraphCollection.AddAsync(TemplateMock.Object);

			List<string> Arguments = new List<string>();
			Arguments.Add("-Target=Step 1");
			Arguments.Add("-Target=Step 3");

			IJob Job = await JobCollection.AddAsync(JobId.GenerateNewId(), new StreamId("ue4-main"), new TemplateRefId("test-build"), ContentHash.SHA1("hello"), BaseGraph, "Test job", 123, 123, null, null, null, null, null, null, null, false, false, null, null, Arguments);
			Assert.AreEqual(1, Job.Batches.Count);

			Job = await StartBatch(Job, BaseGraph, 0);
			Job = Deref(await JobCollection.TryUpdateBatchAsync(Job, BaseGraph, Job.Batches[0].Id, null, JobStepBatchState.Complete, JobStepBatchError.Incomplete));

			Job = (await JobCollection.GetAsync(Job.Id))!;
			Assert.AreEqual(2, Job.Batches.Count);
			Assert.AreEqual(JobStepBatchState.Complete, Job.Batches[0].State);
			Assert.AreEqual(0, Job.Batches[0].Steps.Count);
			Assert.AreEqual(JobStepBatchState.Ready, Job.Batches[1].State);
			Assert.AreEqual(1, Job.Batches[1].Steps.Count);

			Job = Deref(await JobCollection.TryUpdateBatchAsync(Job, BaseGraph, Job.Batches[1].Id, null, JobStepBatchState.Complete, JobStepBatchError.Incomplete));

			Job = (await JobCollection.GetAsync(Job.Id))!;
			Assert.AreEqual(3, Job.Batches.Count);
			Assert.AreEqual(JobStepBatchState.Complete, Job.Batches[0].State);
			Assert.AreEqual(0, Job.Batches[0].Steps.Count);
			Assert.AreEqual(JobStepBatchState.Complete, Job.Batches[1].State);
			Assert.AreEqual(0, Job.Batches[1].Steps.Count);
			Assert.AreEqual(JobStepBatchState.Ready, Job.Batches[2].State);
			Assert.AreEqual(1, Job.Batches[2].Steps.Count);

			Job = Deref(await JobCollection.TryUpdateBatchAsync(Job, BaseGraph, Job.Batches[2].Id, null, JobStepBatchState.Complete, JobStepBatchError.Incomplete));

			Job = (await JobCollection.GetAsync(Job.Id))!;
			Assert.AreEqual(3, Job.Batches.Count);
			Assert.AreEqual(JobStepBatchState.Complete, Job.Batches[0].State);
			Assert.AreEqual(0, Job.Batches[0].Steps.Count);
			Assert.AreEqual(JobStepBatchState.Complete, Job.Batches[1].State);
			Assert.AreEqual(0, Job.Batches[1].Steps.Count);
			Assert.AreEqual(JobStepBatchState.Complete, Job.Batches[2].State);
			Assert.AreEqual(1, Job.Batches[2].Steps.Count);
			Assert.AreEqual(JobStepState.Skipped, Job.Batches[2].Steps[0].State);
		}
	}
}
