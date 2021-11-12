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
using HordeServer.Utilities;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	[TestClass]
	public class JobServiceTests : TestSetup
	{
		[TestMethod]
		public async Task TestChainedJobs()
		{
			ProjectId ProjectId = new ProjectId("ue5");
			IProject? Project = await ProjectService.Collection.AddOrUpdateAsync(ProjectId, "", "", 0, new ProjectConfig { Name = "UE5" });
			Assert.IsNotNull(Project);

			ITemplate Template = await TemplateCollection.AddAsync("Test template", null, false, null, null, new List<string>(), new List<Parameter>());
			IGraph Graph = await GraphCollection.AddAsync(Template);

			TemplateRefId TemplateRefId1 = new TemplateRefId("template1");
			TemplateRefId TemplateRefId2 = new TemplateRefId("template2");

			StreamConfig StreamConfig = new StreamConfig();
			StreamConfig.Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId1.ToString(), Name = "Test Template", ChainedJobs = new List<CreateChainedJobTemplateRequest> { new CreateChainedJobTemplateRequest { TemplateId = TemplateRefId2.ToString(), Trigger = "Setup Build" } } });
			StreamConfig.Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId2.ToString(), Name = "Test Template" });
			StreamConfig.Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { TemplateRefId1.ToString(), TemplateRefId2.ToString() } });

			StreamId StreamId = new StreamId("ue5-main");
			IStream? Stream = await StreamService.GetStreamAsync(StreamId);
			Stream = await StreamService.StreamCollection.TryCreateOrReplaceAsync(new StreamId("ue5-main"), Stream, String.Empty, String.Empty, ProjectId, StreamConfig);

			IJob Job = await JobService.CreateJobAsync(null, Stream!, TemplateRefId1, Template.Id, Graph, "Hello", 1234, 1233, 999, null, null, null, null, null, Stream!.Templates[TemplateRefId1].ChainedJobs, true, true, null, null, new List<string>());
			Assert.AreEqual(1, Job.ChainedJobs.Count);

			Job = Deref(await JobService.UpdateBatchAsync(Job, Job.Batches[0].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			Job = Deref(await JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Running));
			Job = Deref(await JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Assert.IsNotNull(Job.ChainedJobs[0].JobId);

			IJob? ChainedJob = await JobCollection.GetAsync(Job.ChainedJobs[0].JobId!.Value);
			Assert.IsNotNull(ChainedJob);
			Assert.AreEqual(ChainedJob!.Id, Job!.ChainedJobs[0].JobId);

			Assert.AreEqual(ChainedJob!.Change, Job!.Change);
			Assert.AreEqual(ChainedJob!.CodeChange, Job!.CodeChange);
			Assert.AreEqual(ChainedJob!.PreflightChange, Job!.PreflightChange);
			Assert.AreEqual(ChainedJob!.StartedByUserId, Job!.StartedByUserId);
		}

		[TestMethod]
		public async Task StopAnyDuplicateJobsByPreflight()
		{
			Fixture Fixture = await CreateFixtureAsync();

			string[] Args = {"-Target=bogus"};
			IJob OrgJob = await CreatePreflightJob(Fixture, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, Args);
			IJob NewJob = await CreatePreflightJob(Fixture, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, Args);
			IJob DifferentTplRef = await CreatePreflightJob(Fixture, "tpl-ref-other", "tpl-hash-1", "elvis", 1000, Args);
			IJob DifferentTplHash = await CreatePreflightJob(Fixture, "tpl-ref-1", "tpl-hash-other", "elvis", 1000, Args);
			IJob DifferentUserName = await CreatePreflightJob(Fixture, "tpl-ref-1", "tpl-hash-1", "julia", 1000, Args);
			IJob DifferentArgs = await CreatePreflightJob(Fixture, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, new []{"-Target=other"});
			
			OrgJob = (await JobService.GetJobAsync(OrgJob.Id))!;
			NewJob = (await JobService.GetJobAsync(NewJob.Id))!;
			DifferentTplRef = (await JobService.GetJobAsync(DifferentTplRef.Id))!;
			DifferentTplHash = (await JobService.GetJobAsync(DifferentTplHash.Id))!;
			DifferentUserName = (await JobService.GetJobAsync(DifferentUserName.Id))!;
			DifferentArgs = (await JobService.GetJobAsync(DifferentArgs.Id))!;
			
			Assert.AreEqual(KnownUsers.System, OrgJob.AbortedByUserId);
			Assert.IsNull(NewJob.AbortedByUserId);
			Assert.IsNull(DifferentTplRef.AbortedByUserId);
			Assert.IsNull(DifferentTplHash.AbortedByUserId);
			Assert.IsNull(DifferentUserName.AbortedByUserId);
			Assert.IsNull(DifferentArgs.AbortedByUserId);
		}

		private async Task<IJob> CreatePreflightJob(Fixture Fixture, string TemplateRefId, string TemplateHash, string StartedByUserName, int PreflightChange, string[] Arguments)
		{
			IUser User = await UserCollection.FindOrAddUserByLoginAsync(StartedByUserName);
			return await JobService.CreateJobAsync(
				JobId: JobId.GenerateNewId(),
				Stream: Fixture!.Stream!,
				TemplateRefId: new TemplateRefId(TemplateRefId),
				TemplateHash: new ContentHash(Encoding.ASCII.GetBytes(TemplateHash)),
				Graph: Fixture!.Graph,
				Name: "hello1",
				Change: 1000001,
				CodeChange: 1000002,
				PreflightChange: PreflightChange,
				ClonedPreflightChange: null,
				StartedByUserId: User.Id,
				Priority: Priority.Normal,
				null,
				null,
				null,
				false,
				false,
				null,
				null,
				Arguments: new List<string>(Arguments)
			);
		}
		
		// Only test for cancelled preflights
		// [TestMethod]
		// public async Task StopAnyDuplicateJobsByChange()
		// {
		// 	TestSetup TestSetup = await GetTestSetup();
		// 	Fixture Fix = Fixture!;
		// 	IJob Job1 = Fix.Job1;
		// 	IJob Job2 = Fix.Job2;
		// 	
		// 	Assert.AreEqual(JobState.Waiting, Job1.GetState());
		// 	Assert.IsTrue(await JobService.UpdateBatchAsync(Job1, Job1.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
		//
		// 	Job1 = (await JobService.GetJobAsync(Job1.Id))!;
		// 	Job2 = (await JobService.GetJobAsync(Job2.Id))!;
		// 	Assert.AreEqual(JobState.Running, Job1.GetState());
		// 	Assert.AreEqual(JobState.Complete, Job2.GetState());
		// 	
		// 	IJob Job3 = await JobService.CreateJobAsync(null, Fix.Stream!.Id, Fix.TemplateRefId1, Fix.Template.Id, Fix.Graph, "Hello", Job1.Change, Job1.CodeChange, null, "joe", null, null, true, true, null, null, new List<string>());
		// 	await DispatchService.TickOnlyForTestingAsync();
		// 	Job1 = (await JobService.GetJobAsync(Job1.Id))!;
		// 	Assert.AreEqual(JobState.Complete, Job1.GetState());
		// 	Assert.AreEqual(JobState.Waiting, Job3.GetState());
		// }

		[TestMethod]
		public async Task TestRunEarly()
		{
			IProject? Project = await ProjectService.Collection.AddOrUpdateAsync(new ProjectId("ue5"), "", "", 0, new ProjectConfig { Name = "UE5" });
			Assert.IsNotNull(Project);

			StreamId StreamId = new StreamId("ue5-main");
			IStream? Stream = await StreamCollection.GetAsync(StreamId);
			Stream = await StreamCollection.TryCreateOrReplaceAsync(StreamId, Stream, "", "", Project!.Id, new StreamConfig { Name = "//UE5/Main" });

			ITemplate Template = await TemplateCollection.AddAsync("Test template", null, false, null, null, new List<string>(), new List<Parameter>());
			IGraph Graph = await GraphCollection.AddAsync(Template);

			NewGroup GroupA = new NewGroup("win", new List<NewNode>());
			GroupA.Nodes.Add(new NewNode("Compile"));

			NewGroup GroupB = new NewGroup("win", new List<NewNode>());
			GroupB.Nodes.Add(new NewNode("Cook", RunEarly: true));
			GroupB.Nodes.Add(new NewNode("Middle"));
			GroupB.Nodes.Add(new NewNode("Pak", InputDependencies: new List<string> { "Compile", "Cook" }));

			Graph = await GraphCollection.AppendAsync(Graph, new List<NewGroup> { GroupA, GroupB });

			IJob Job = await JobService.CreateJobAsync(null, Stream!, new TemplateRefId("temp"), Template.Id, Graph, "Hello", 1234, 1233, 999, null, null, null, null, null, null, true, true, null, null, new List<string> { "-Target=Pak" });

			Job = Deref(await JobService.UpdateBatchAsync(Job, Job.Batches[0].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Running));
			Assert.IsNotNull(await JobService.UpdateStepAsync(Job, Job.Batches[0].Id, Job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Job = Deref(await JobService.UpdateBatchAsync(Job, Job.Batches[1].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await JobService.UpdateStepAsync(Job, Job.Batches[1].Id, Job.Batches[1].Steps[0].Id, JobStepState.Running));

			Job = Deref(await JobService.UpdateBatchAsync(Job, Job.Batches[2].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await JobService.UpdateStepAsync(Job, Job.Batches[2].Id, Job.Batches[2].Steps[0].Id, JobStepState.Running));
			Assert.IsNotNull(await JobService.UpdateStepAsync(Job, Job.Batches[2].Id, Job.Batches[2].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Job = (await JobService.GetJobAsync(Job.Id))!;

			Assert.AreEqual(JobStepState.Waiting, Job.Batches[2].Steps[1].State);
		}
	}
}
