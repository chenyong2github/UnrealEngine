// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServerTests
{
	[TestClass]
	public class JobServiceTests : DatabaseIntegrationTest
	{
		[TestMethod]
		public async Task TestChainedJobs()
		{
			TestSetup TestSetup = await GetTestSetup();

			ProjectId ProjectId = new ProjectId("ue5");
			IProject? Project = await TestSetup.ProjectService.TryCreateProjectAsync(ProjectId, "UE5", null, null, null);
			Assert.IsNotNull(Project);

			ITemplate Template = await TestSetup.TemplateService.CreateTemplateAsync("Test template", null, false, null, null, new List<TemplateCounter>(), new List<string>(), new List<Parameter>());
			IGraph Graph = await TestSetup.GraphCollection.AddAsync(Template);

			TemplateRefId TemplateRefId1 = new TemplateRefId("template1");
			TemplateRefId TemplateRefId2 = new TemplateRefId("template2");

			StreamConfig StreamConfig = new StreamConfig();
			StreamConfig.Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId1.ToString(), Name = "Test Template", ChainedJobs = new List<CreateChainedJobTemplateRequest> { new CreateChainedJobTemplateRequest { TemplateId = TemplateRefId2.ToString(), Trigger = "Setup Build" } } });
			StreamConfig.Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId2.ToString(), Name = "Test Template" });
			StreamConfig.Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { TemplateRefId1.ToString(), TemplateRefId2.ToString() } });

			StreamId StreamId = new StreamId("ue5-main");
			IStream? Stream = await TestSetup.StreamService.GetStreamAsync(StreamId);
			Stream = await TestSetup.StreamService.StreamCollection.TryCreateOrReplaceAsync(new StreamId("ue5-main"), Stream, String.Empty, ProjectId, StreamConfig);

			IJob Job = await TestSetup.JobService.CreateJobAsync(null, Stream!.Id, TemplateRefId1, Template.Id, Graph, "Hello", 1234, 1233, 999, null, null, "joe", null, null, Stream.Templates[TemplateRefId1].ChainedJobs, true, true, null, null, null, Template.Counters, new List<string>());
			Assert.AreEqual(1, Job.ChainedJobs.Count);

			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job, Job.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Assert.IsNotNull(Job.ChainedJobs[0].JobId);

			IJob? ChainedJob = await TestSetup.JobCollection.GetAsync(Job.ChainedJobs[0].JobId!.Value);
			Assert.IsNotNull(ChainedJob);
			Assert.AreEqual(ChainedJob!.Id, Job!.ChainedJobs[0].JobId);

			Assert.AreEqual(ChainedJob!.Change, Job!.Change);
			Assert.AreEqual(ChainedJob!.CodeChange, Job!.CodeChange);
			Assert.AreEqual(ChainedJob!.PreflightChange, Job!.PreflightChange);
			Assert.AreEqual(ChainedJob!.StartedByUser, Job!.StartedByUser);
		}

		[TestMethod]
		public async Task StopAnyDuplicateJobsByPreflight()
		{
			TestSetup TestSetup = await GetTestSetup();

			string[] Args = {"-Target=bogus"};
			IJob OrgJob = await CreatePreflightJob(TestSetup, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, Args);
			IJob NewJob = await CreatePreflightJob(TestSetup, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, Args);
			IJob DifferentTplRef = await CreatePreflightJob(TestSetup, "tpl-ref-other", "tpl-hash-1", "elvis", 1000, Args);
			IJob DifferentTplHash = await CreatePreflightJob(TestSetup, "tpl-ref-1", "tpl-hash-other", "elvis", 1000, Args);
			IJob DifferentUserName = await CreatePreflightJob(TestSetup, "tpl-ref-1", "tpl-hash-1", "julia", 1000, Args);
			IJob DifferentArgs = await CreatePreflightJob(TestSetup, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, new []{"-Target=other"});
			
			OrgJob = (await TestSetup.JobService.GetJobAsync(OrgJob.Id))!;
			NewJob = (await TestSetup.JobService.GetJobAsync(NewJob.Id))!;
			DifferentTplRef = (await TestSetup.JobService.GetJobAsync(DifferentTplRef.Id))!;
			DifferentTplHash = (await TestSetup.JobService.GetJobAsync(DifferentTplHash.Id))!;
			DifferentUserName = (await TestSetup.JobService.GetJobAsync(DifferentUserName.Id))!;
			DifferentArgs = (await TestSetup.JobService.GetJobAsync(DifferentArgs.Id))!;
			
			Assert.AreEqual("horde.duplicated.by.newer.CL", OrgJob.AbortedByUser);
			Assert.IsNull(NewJob.AbortedByUser);
			Assert.IsNull(DifferentTplRef.AbortedByUser);
			Assert.IsNull(DifferentTplHash.AbortedByUser);
			Assert.IsNull(DifferentUserName.AbortedByUser);
			Assert.IsNull(DifferentArgs.AbortedByUser);
		}

		private Task<IJob> CreatePreflightJob(TestSetup TestSetup, string TemplateRefId, string TemplateHash, string StartedByUserName, int PreflightChange, string[] Arguments)
		{
			return TestSetup.JobService.CreateJobAsync(
				JobId: ObjectId.GenerateNewId(),
				StreamId: TestSetup.Fixture!.Stream!.Id,
				TemplateRefId: new TemplateRefId(TemplateRefId),
				TemplateHash: new ContentHash(Encoding.ASCII.GetBytes(TemplateHash)),
				Graph: TestSetup.Fixture!.Graph,
				Name: "hello1",
				Change: 1000001,
				CodeChange: 1000002,
				PreflightChange: PreflightChange,
				ClonedPreflightChange: null,
				StartedByUserId: null,
				StartedByUserName: StartedByUserName,
				Priority: Priority.Normal,
				null,
				null,
				false,
				false,
				null,
				null,
				null,
				TestSetup.Fixture!.Template.Counters,
				Arguments: new List<string>(Arguments)
			);
		}
		
		// Only test for cancelled preflights
		// [TestMethod]
		// public async Task StopAnyDuplicateJobsByChange()
		// {
		// 	TestSetup TestSetup = await GetTestSetup();
		// 	Fixture Fix = TestSetup.Fixture!;
		// 	IJob Job1 = Fix.Job1;
		// 	IJob Job2 = Fix.Job2;
		// 	
		// 	Assert.AreEqual(JobState.Waiting, Job1.GetState());
		// 	Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job1, Job1.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
		//
		// 	Job1 = (await TestSetup.JobService.GetJobAsync(Job1.Id))!;
		// 	Job2 = (await TestSetup.JobService.GetJobAsync(Job2.Id))!;
		// 	Assert.AreEqual(JobState.Running, Job1.GetState());
		// 	Assert.AreEqual(JobState.Complete, Job2.GetState());
		// 	
		// 	IJob Job3 = await TestSetup.JobService.CreateJobAsync(null, Fix.Stream!.Id, Fix.TemplateRefId1, Fix.Template.Id, Fix.Graph, "Hello", Job1.Change, Job1.CodeChange, null, "joe", null, null, true, true, null, null, Fix.Template.Counters, new List<string>());
		// 	await TestSetup.DispatchService.TickOnlyForTestingAsync();
		// 	Job1 = (await TestSetup.JobService.GetJobAsync(Job1.Id))!;
		// 	Assert.AreEqual(JobState.Complete, Job1.GetState());
		// 	Assert.AreEqual(JobState.Waiting, Job3.GetState());
		// }

		[TestMethod]
		public async Task TestRunEarly()
		{
			TestSetup TestSetup = await GetTestSetup();
			
			IProject? Project = await TestSetup.ProjectService.TryCreateProjectAsync(new ProjectId("ue5"), "UE5", null, null, null);
			Assert.IsNotNull(Project);

			StreamId StreamId = new StreamId("ue5-main");
			await TestSetup.StreamCollection.TryCreateOrReplaceAsync(new StreamId("ue5-main"), null, "", Project!.Id, new StreamConfig { Name = "//UE5/Main" });

			ITemplate Template = await TestSetup.TemplateService.CreateTemplateAsync("Test template", null, false, null, null, new List<TemplateCounter>(), new List<string>(), new List<Parameter>());
			IGraph Graph = await TestSetup.GraphCollection.AddAsync(Template);

			CreateGroupRequest GroupA = new CreateGroupRequest("win", new List<CreateNodeRequest>());
			GroupA.Nodes.Add(new CreateNodeRequest("Compile"));

			CreateGroupRequest GroupB = new CreateGroupRequest("win", new List<CreateNodeRequest>());
			GroupB.Nodes.Add(new CreateNodeRequest("Cook", RunEarly: true));
			GroupB.Nodes.Add(new CreateNodeRequest("Middle"));
			GroupB.Nodes.Add(new CreateNodeRequest("Pak", InputDependencies: new List<string> { "Compile", "Cook" }));

			Graph = await TestSetup.GraphCollection.AppendAsync(Graph, new List<CreateGroupRequest> { GroupA, GroupB });

			IJob Job = await TestSetup.JobService.CreateJobAsync(null, StreamId, new TemplateRefId("temp"), Template.Id, Graph, "Hello", 1234, 1233, 999, null, null, "joe", null, null, null, true, true, null, null, null, Template.Counters, new List<string> { "-Target=Pak" });

			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job, Job.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job, Job.Batches[1].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[1].Id, Job.Batches[1].Steps[0].Id, JobStepState.Running));

			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job, Job.Batches[2].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[2].Id, Job.Batches[2].Steps[0].Id, JobStepState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job, Job.Batches[2].Id, Job.Batches[2].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Job = (await TestSetup.JobService.GetJobAsync(Job.Id))!;

			Assert.AreEqual(JobStepState.Waiting, Job.Batches[2].Steps[1].State);
		}
	}
}
