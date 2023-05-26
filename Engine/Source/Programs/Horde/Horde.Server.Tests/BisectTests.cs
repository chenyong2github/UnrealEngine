// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Server.Jobs;
using Horde.Server.Jobs.Graphs;
using Horde.Server.Jobs.Bisect;
using Horde.Server.Jobs.Templates;
using Horde.Server.Logs;
using Horde.Server.Projects;
using Horde.Server.Streams;
using Horde.Server.Users;
using HordeCommon;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

namespace Horde.Server.Tests
{
	[TestClass]
	public class BisectTests : TestSetup
	{
		StreamId StreamId { get; } = new StreamId("ue4-main");
		TemplateId TemplateId { get; } = new TemplateId("test-build");

		[TestMethod]
		public async Task TestStates()
		{
			IUser user = await UserCollection.FindOrAddUserByLoginAsync("Bob");

			PerforceService.AddChange(StreamId, 10, user, "", new[] { "Foo.cpp" });

			PerforceService.AddChange(StreamId, 11, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 12, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 13, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 14, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 15, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 16, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 17, user, "", new[] { "Bar.cpp" });
			PerforceService.AddChange(StreamId, 20, user, "", new[] { "Bar.cpp" });

			ProjectConfig project = new ProjectConfig();
			project.Id = new ProjectId("ue4");
			project.Streams.Add(new StreamConfig { Id = StreamId, Templates = new List<TemplateRefConfig> { new TemplateRefConfig { Id = TemplateId } } });

			UpdateConfig(x => x.Projects.Add(project));

			// Create the graph
			List<NewNode> nodes = new List<NewNode>();
			nodes.Add(new NewNode("CompileEditor"));

			List<NewGroup> groups = new List<NewGroup>();
			groups.Add(new NewGroup("Foo", nodes));

			Mock<ITemplate> templateMock = new Mock<ITemplate>(MockBehavior.Strict);
			templateMock.SetupGet(x => x.InitialAgentType).Returns((string?)null);

			IGraph graph = await GraphCollection.AddAsync(templateMock.Object, null);
			graph = await GraphCollection.AppendAsync(graph, groups);

			// Create two job runs, one which fails, one that succeeds.
			CreateJobOptions options = new CreateJobOptions();
			options.Arguments.Add($"{IJob.TargetArgumentPrefix}CompileEditor");

			IJob succeededJob = await CreateJob(10, graph, JobStepOutcome.Success, options);
			IJob failedJob = await CreateJob(20, graph, JobStepOutcome.Failure, options);

			IBisectTaskCollection bisectTaskCollection = ServiceProvider.GetRequiredService<IBisectTaskCollection>();

			IBisectTask bisectTask = await bisectTaskCollection.CreateAsync(failedJob, "CompileEditor", JobStepOutcome.Failure, UserId.Anonymous);
			Assert.AreEqual(failedJob.TemplateId, bisectTask.TemplateId);
			Assert.AreEqual(failedJob.StreamId, bisectTask.StreamId);
			Assert.AreEqual("CompileEditor", bisectTask.NodeName);
			Assert.AreEqual(JobStepOutcome.Failure, bisectTask.Outcome);
			Assert.AreEqual(UserId.Anonymous, bisectTask.OwnerId);
			Assert.AreEqual(failedJob.Change, bisectTask.InitialChange);
			Assert.AreEqual(failedJob.Id, bisectTask.InitialJobId);
			Assert.AreEqual(failedJob.Change, bisectTask.CurrentChange);
			Assert.AreEqual(failedJob.Id, bisectTask.CurrentJobId);

			List<IJob> jobs = await JobCollection.FindBisectTaskJobsAsync(bisectTask.Id, null).ToListAsync();
			Assert.AreEqual(0, jobs.Count);

			BisectService bisectService = ServiceProvider.GetRequiredService<BisectService>();
			await bisectService.StartAsync(CancellationToken.None);

			await Clock.AdvanceAsync(TimeSpan.FromMinutes(30));

			bisectTask = Deref(await bisectTaskCollection.GetAsync(bisectTask.Id));
			Assert.AreEqual(BisectTaskState.Running, bisectTask.State);
			jobs = await JobCollection.FindBisectTaskJobsAsync(bisectTask.Id, running: true).ToListAsync();
			Assert.AreEqual(1, jobs.Count);
			Assert.AreEqual(14, jobs[0].Change);
			await SetJobOutcome(jobs[0], graph, JobStepOutcome.Failure);

			await Clock.AdvanceAsync(TimeSpan.FromMinutes(30));

			bisectTask = Deref(await bisectTaskCollection.GetAsync(bisectTask.Id));
			Assert.AreEqual(BisectTaskState.Running, bisectTask.State);
			jobs = await JobCollection.FindBisectTaskJobsAsync(bisectTask.Id, running: true).ToListAsync();
			Assert.AreEqual(1, jobs.Count);
			Assert.AreEqual(12, jobs[0].Change);
			await SetJobOutcome(jobs[0], graph, JobStepOutcome.Success);

			await Clock.AdvanceAsync(TimeSpan.FromMinutes(30));

			bisectTask = Deref(await bisectTaskCollection.GetAsync(bisectTask.Id));
			Assert.AreEqual(BisectTaskState.Running, bisectTask.State);
			jobs = await JobCollection.FindBisectTaskJobsAsync(bisectTask.Id, running: true).ToListAsync();
			Assert.AreEqual(1, jobs.Count);
			Assert.AreEqual(13, jobs[0].Change);
			await SetJobOutcome(jobs[0], graph, JobStepOutcome.Success);

			await Clock.AdvanceAsync(TimeSpan.FromMinutes(30));

			bisectTask = Deref(await bisectTaskCollection.GetAsync(bisectTask.Id));
			Assert.AreEqual(BisectTaskState.Succeeded, bisectTask.State);
			Assert.AreEqual(20, bisectTask.InitialChange);
			Assert.AreEqual(14, bisectTask.CurrentChange);
			jobs = await JobCollection.FindBisectTaskJobsAsync(bisectTask.Id, running: true).ToListAsync();
			Assert.AreEqual(0, jobs.Count);
		}

		async Task<IJob> CreateJob(int change, IGraph graph, JobStepOutcome outcome, CreateJobOptions options)
		{
			IJob job = await JobCollection.AddAsync(JobId.GenerateNewId(), StreamId, TemplateId, ContentHash.SHA1("hello"), graph, "Test job", change, change, options);
			return await SetJobOutcome(job, graph, outcome);
		}

		async Task<IJob> SetJobOutcome(IJob job, IGraph graph, JobStepOutcome outcome)
		{
			job = Deref(await JobCollection.TryUpdateGraphAsync(job, graph));

			job = Deref(await JobCollection.TryUpdateStepAsync(job, graph, job.Batches[0].Id, job.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));
			job = Deref(await JobCollection.TryUpdateBatchAsync(job, graph, job.Batches[0].Id, null, JobStepBatchState.Complete, null));

			IJobStepBatch batch = job.Batches[^1];
			IJobStep step = batch.Steps[^1];

			job = Deref(await JobCollection.TryUpdateStepAsync(job, graph, batch.Id, step.Id, JobStepState.Completed, outcome));
			job = Deref(await JobCollection.TryUpdateBatchAsync(job, graph, batch.Id, null, JobStepBatchState.Complete, null));

			INodeGroup group = graph.Groups[batch.GroupIdx];
			INode node = group.Nodes[step.NodeIdx];
			await JobStepRefCollection.InsertOrReplaceAsync(new JobStepRefId(job.Id, batch.Id, step.Id), job.Name, node.Name, job.StreamId, job.TemplateId, job.Change, LogId.GenerateNewId(), null, null, outcome, false, null, null, 0.0f, 0.0f, DateTime.MinValue, DateTime.MinValue, DateTime.MinValue);

			return job;
		}
	}
}
