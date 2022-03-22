// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using HordeCommon;
using Horde.Build.Api;
using Horde.Build.Models;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using ProjectId = Horde.Build.Utilities.StringId<Horde.Build.Models.IProject>;
using StreamId = Horde.Build.Utilities.StringId<Horde.Build.Models.IStream>;
using TemplateRefId = Horde.Build.Utilities.StringId<Horde.Build.Models.TemplateRef>;
using Horde.Build.Utilities;

namespace Horde.Build.Tests
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	[TestClass]
	public class JobServiceTests : TestSetup
	{
		[TestMethod]
		public async Task TestChainedJobs()
		{
			ProjectId projectId = new ProjectId("ue5");
			IProject? project = await ProjectService.Collection.AddOrUpdateAsync(projectId, "", "", 0, new ProjectConfig { Name = "UE5" });
			Assert.IsNotNull(project);

			ITemplate template = await TemplateCollection.AddAsync("Test template");
			IGraph graph = await GraphCollection.AddAsync(template);

			TemplateRefId templateRefId1 = new TemplateRefId("template1");
			TemplateRefId templateRefId2 = new TemplateRefId("template2");

			StreamConfig streamConfig = new StreamConfig();
			streamConfig.Templates.Add(new CreateTemplateRefRequest { Id = templateRefId1.ToString(), Name = "Test Template", ChainedJobs = new List<CreateChainedJobTemplateRequest> { new CreateChainedJobTemplateRequest { TemplateId = templateRefId2.ToString(), Trigger = "Setup Build" } } });
			streamConfig.Templates.Add(new CreateTemplateRefRequest { Id = templateRefId2.ToString(), Name = "Test Template" });
			streamConfig.Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { templateRefId1.ToString(), templateRefId2.ToString() } });

			StreamId streamId = new StreamId("ue5-main");
			IStream? stream = await StreamService.GetStreamAsync(streamId);
			stream = await StreamService.StreamCollection.TryCreateOrReplaceAsync(new StreamId("ue5-main"), stream, String.Empty, String.Empty, projectId, streamConfig);

			IJob job = await JobService.CreateJobAsync(null, stream!, templateRefId1, template.Id, graph, "Hello", 1234, 1233, 999, null, null, null, null, null, null, stream!.Templates[templateRefId1].ChainedJobs, true, true, null, null, new List<string>());
			Assert.AreEqual(1, job.ChainedJobs.Count);

			job = Deref(await JobService.UpdateBatchAsync(job, job.Batches[0].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[0].Id, job.Batches[0].Steps[0].Id, JobStepState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[0].Id, job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Assert.IsNotNull(job.ChainedJobs[0].JobId);

			IJob? chainedJob = await JobCollection.GetAsync(job.ChainedJobs[0].JobId!.Value);
			Assert.IsNotNull(chainedJob);
			Assert.AreEqual(chainedJob!.Id, job!.ChainedJobs[0].JobId);

			Assert.AreEqual(chainedJob!.Change, job!.Change);
			Assert.AreEqual(chainedJob!.CodeChange, job!.CodeChange);
			Assert.AreEqual(chainedJob!.PreflightChange, job!.PreflightChange);
			Assert.AreEqual(chainedJob!.StartedByUserId, job!.StartedByUserId);
		}

		[TestMethod]
		public async Task StopAnyDuplicateJobsByPreflight()
		{
			Fixture fixture = await CreateFixtureAsync();

			string[] args = {"-Target=bogus"};
			IJob orgJob = await CreatePreflightJob(fixture, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, args);
			IJob newJob = await CreatePreflightJob(fixture, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, args);
			IJob differentTplRef = await CreatePreflightJob(fixture, "tpl-ref-other", "tpl-hash-1", "elvis", 1000, args);
			IJob differentTplHash = await CreatePreflightJob(fixture, "tpl-ref-1", "tpl-hash-other", "elvis", 1000, args);
			IJob differentUserName = await CreatePreflightJob(fixture, "tpl-ref-1", "tpl-hash-1", "julia", 1000, args);
			IJob differentArgs = await CreatePreflightJob(fixture, "tpl-ref-1", "tpl-hash-1", "elvis", 1000, new []{"-Target=other"});
			
			orgJob = (await JobService.GetJobAsync(orgJob.Id))!;
			newJob = (await JobService.GetJobAsync(newJob.Id))!;
			differentTplRef = (await JobService.GetJobAsync(differentTplRef.Id))!;
			differentTplHash = (await JobService.GetJobAsync(differentTplHash.Id))!;
			differentUserName = (await JobService.GetJobAsync(differentUserName.Id))!;
			differentArgs = (await JobService.GetJobAsync(differentArgs.Id))!;
			
			Assert.AreEqual(KnownUsers.System, orgJob.AbortedByUserId);
			Assert.IsNull(newJob.AbortedByUserId);
			Assert.IsNull(differentTplRef.AbortedByUserId);
			Assert.IsNull(differentTplHash.AbortedByUserId);
			Assert.IsNull(differentUserName.AbortedByUserId);
			Assert.IsNull(differentArgs.AbortedByUserId);
		}

		private async Task<IJob> CreatePreflightJob(Fixture fixture, string templateRefId, string templateHash, string startedByUserName, int preflightChange, string[] arguments)
		{
			IUser user = await UserCollection.FindOrAddUserByLoginAsync(startedByUserName);
			return await JobService.CreateJobAsync(
				JobId: JobId.GenerateNewId(),
				Stream: fixture!.Stream!,
				TemplateRefId: new TemplateRefId(templateRefId),
				TemplateHash: new ContentHash(Encoding.ASCII.GetBytes(templateHash)),
				Graph: fixture!.Graph,
				Name: "hello1",
				Change: 1000001,
				CodeChange: 1000002,
				PreflightChange: preflightChange,
				ClonedPreflightChange: null,
				StartedByUserId: user.Id,
				Priority: Priority.Normal,
				null,
				null,
				null,
				null,
				false,
				false,
				null,
				null,
				Arguments: new List<string>(arguments)
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
			IAgent? agent = await AgentService.CreateAgentAsync("TestAgent", true, null, new List<StringId<IPool>> { new StringId<IPool>("win") });
			await AgentService.CreateSessionAsync(agent, AgentStatus.Ok, new List<string>(), new Dictionary<string, int>(), null);

			IProject? project = await ProjectService.Collection.AddOrUpdateAsync(new ProjectId("ue5"), "", "", 0, new ProjectConfig { Name = "UE5" });
			Assert.IsNotNull(project);

			StreamId streamId = new StreamId("ue5-main");
			IStream? stream = await StreamCollection.GetAsync(streamId);
			stream = await StreamCollection.TryCreateOrReplaceAsync(streamId, stream, "", "", project!.Id, new StreamConfig { Name = "//UE5/Main" });

			ITemplate template = await TemplateCollection.AddAsync("Test template");
			IGraph graph = await GraphCollection.AddAsync(template);

			NewGroup groupA = new NewGroup("win", new List<NewNode>());
			groupA.Nodes.Add(new NewNode("Compile"));

			NewGroup groupB = new NewGroup("win", new List<NewNode>());
			groupB.Nodes.Add(new NewNode("Cook", RunEarly: true));
			groupB.Nodes.Add(new NewNode("Middle"));
			groupB.Nodes.Add(new NewNode("Pak", InputDependencies: new List<string> { "Compile", "Cook" }));

			graph = await GraphCollection.AppendAsync(graph, new List<NewGroup> { groupA, groupB });

			IJob job = await JobService.CreateJobAsync(null, stream!, new TemplateRefId("temp"), template.Id, graph, "Hello", 1234, 1233, 999, null, null, null, null, null, null, null, true, true, null, null, new List<string> { "-Target=Pak" });

			job = Deref(await JobService.UpdateBatchAsync(job, job.Batches[0].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[0].Id, job.Batches[0].Steps[0].Id, JobStepState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[0].Id, job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			job = Deref(await JobService.UpdateBatchAsync(job, job.Batches[1].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[1].Id, job.Batches[1].Steps[0].Id, JobStepState.Running));

			job = Deref(await JobService.UpdateBatchAsync(job, job.Batches[2].Id, LogId.GenerateNewId(), JobStepBatchState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[2].Id, job.Batches[2].Steps[0].Id, JobStepState.Running));
			job = Deref(await JobService.UpdateStepAsync(job, job.Batches[2].Id, job.Batches[2].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			Assert.AreEqual(JobStepState.Waiting, job.Batches[2].Steps[1].State);
		}
	}
}
