// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Collections.Impl;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using HordeServer.Api;
using MongoDB.Bson;
using System.Linq;
using HordeServerTests.Stubs.Services;

namespace HordeServerTests
{
    [TestClass]
    public class SchedulerTests : DatabaseIntegrationTest
	{
		StreamId StreamId { get; } = new StreamId("ue5-main");
		TemplateRefId TemplateRefId { get; } = new TemplateRefId("template1");

		IServiceProvider ServiceProvider;
		TestSetup TestSetup;
		IStream Stream;
		ITemplate Template;
		HashSet<ObjectId> InitialJobIds;
		ScheduleService ScheduleService;
		PerforceServiceStub PerforceService;

		public SchedulerTests()
		{
			TestSetup = GetTestSetup().Result;

			IProject ? Project = TestSetup.ProjectService.TryCreateProjectAsync(new ProjectId("ue5"), "UE5", null, null, null).Result;
			Assert.IsNotNull(Project);

			Template = TestSetup.TemplateService.CreateTemplateAsync("Test template", null, false, null, null, new List<TemplateCounter>(), new List<string>(), new List<Parameter>()).Result;

			Stream = TestSetup.StreamCollection.GetAsync(StreamId).Result!;
			if (Stream == null)
			{
				Stream = TestSetup.StreamService.TryCreateStreamAsync(new StreamId("ue5-main"), "//UE5/Main", Project!.Id).Result!;
			}

			InitialJobIds = new HashSet<ObjectId>(TestSetup.JobCollection.FindAsync().Result.Select(x => x.Id));

			ServiceProvider = TestSetup.ServiceProvider;
			ScheduleService = ServiceProvider.GetRequiredService<ScheduleService>();

			PerforceService = (PerforceServiceStub)TestSetup.PerforceService;
			PerforceService.Changes.Clear();
			PerforceService.AddChange("//UE5/Main", 100, "Bob", "", new[] { "code.cpp" });
			PerforceService.AddChange("//UE5/Main", 101, "Bob", "", new[] { "content.uasset" });
			PerforceService.AddChange("//UE5/Main", 102, "Bob", "", new[] { "content.uasset" });
		}

		async Task SetSchedule(Schedule Schedule)
		{
			TemplateRef TemplateRef1 = new TemplateRef(Template, Schedule: Schedule);
			List<StreamTab> Tabs = new List<StreamTab>();
			Tabs.Add(new JobsTab("foo", true, new List<TemplateRefId> { TemplateRefId }, new List<string>(), new List<JobsTabColumn>()));

			IStream? Stream = await TestSetup.StreamService.GetStreamAsync(StreamId);
			Assert.IsNotNull(Stream);

			StreamService StreamService = ServiceProvider.GetRequiredService<StreamService>();
			await StreamService.UpdateStreamAsync(Stream, NewTabs: Tabs, NewTemplateRefs: new Dictionary<TemplateRefId, TemplateRef> { { TemplateRefId, TemplateRef1 } });
		}

		[TestMethod]
		public void DayScheduleTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Utc); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(new List<DayOfWeek> { DayOfWeek.Friday, DayOfWeek.Sunday }, 13 * 60, null, null));

			DateTimeOffset? NextTime = Schedule.GetNextTriggerTime(StartTime, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0 * 2.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0 * 7.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0 * 9.0), NextTime!.Value);
		}

		[TestMethod]
		public void MulticheduleTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Utc); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));

			DateTimeOffset? NextTime = Schedule.GetNextTriggerTime(StartTime, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.25), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.5), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.75), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(2.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTime(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0), NextTime!.Value);
		}

		[TestMethod]
		public async Task NoSubmittedChangeTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));
			Schedule.LastTriggerTime = StartTime;
			await SetSchedule(Schedule);

			// Initial tick
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger a job
			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);

			IStream Stream2 = (await TestSetup.StreamCollection.GetAsync(Stream.Id))!;
			Schedule Schedule2 = Stream2.Templates.First().Value.Schedule!;
			Assert.AreEqual(102, Schedule2.LastTriggerChange);
			Assert.AreEqual(TestSetup.Clock.UtcNow, Schedule2.LastTriggerTime);

			// Tick a little later (interval not elapsed)
			await ScheduleService.TickSharedOnlyForTestingAsync();
			Assert.AreEqual(0, (await GetNewJobs()).Count);

			// Trigger another job
			TestSetup.Clock.Advance(TimeSpan.FromHours(0.5));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(1, Jobs3.Count);
			Assert.AreEqual(102, Jobs3[0].Change);
			Assert.AreEqual(100, Jobs3[0].CodeChange);
		}

		[TestMethod]
		public async Task RequireSubmittedChangeTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule();
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));
			Schedule.LastTriggerTime = StartTime;
			await SetSchedule(Schedule);

			// Initial tick
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger a job
			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);

			IStream Stream2 = (await TestSetup.StreamCollection.GetAsync(Stream.Id))!;
			Schedule Schedule2 = Stream2.Templates.First().Value.Schedule!;
			Assert.AreEqual(102, Schedule2.LastTriggerChange);
			Assert.AreEqual(TestSetup.Clock.UtcNow, Schedule2.LastTriggerTime);

			// Trigger another job
			TestSetup.Clock.Advance(TimeSpan.FromHours(0.5));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);
		}

		[TestMethod]
		public async Task MultipleJobsTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule();
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));
			Schedule.LastTriggerTime = StartTime;
			Schedule.MaxChanges = 2;
			Schedule.Filter = new List<ChangeContentFlags> { ChangeContentFlags.ContainsCode };
			await SetSchedule(Schedule);

			// Initial tick
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger some jobs
			PerforceService.AddChange("//UE5/Main", 103, "Bob", "", new string[] { "foo.cpp" });
			PerforceService.AddChange("//UE5/Main", 104, "Bob", "", new string[] { "foo.cpp" });
			PerforceService.AddChange("//UE5/Main", 105, "Bob", "", new string[] { "foo.uasset" });
			PerforceService.AddChange("//UE5/Main", 106, "Bob", "", new string[] { "foo.cpp" });

			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(2, Jobs2.Count);
			Assert.AreEqual(104, Jobs2[0].Change);
			Assert.AreEqual(104, Jobs2[0].CodeChange);
			Assert.AreEqual(106, Jobs2[1].Change);
			Assert.AreEqual(106, Jobs2[1].CodeChange);
		}

		[TestMethod]
		public async Task MaxActiveTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));
			Schedule.LastTriggerTime = StartTime;
			Schedule.MaxActive = 1;
			await SetSchedule(Schedule);

			// Trigger a job
			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);

			// Test that another job does not trigger
			TestSetup.Clock.Advance(TimeSpan.FromHours(0.5));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);

			// Mark the original job as complete
			await TestSetup.JobService.UpdateJobAsync(Jobs2[0], AbortedByUser: "me");

			// Test that another job does not trigger
			TestSetup.Clock.Advance(TimeSpan.FromHours(0.5));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs4 = await GetNewJobs();
			Assert.AreEqual(1, Jobs4.Count);
			Assert.AreEqual(102, Jobs4[0].Change);
			Assert.AreEqual(100, Jobs4[0].CodeChange);
		}

		[TestMethod]
		public async Task CreateNewChangeTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule();
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));
			Schedule.LastTriggerTime = StartTime;

			ITemplate? NewTemplate = await TestSetup.TemplateService.CreateTemplateAsync("Test template", SubmitNewChange: "foo.cpp");
			TemplateRef NewTemplateRef = new TemplateRef(NewTemplate, Schedule: Schedule);

			IStream? Stream = await TestSetup.StreamService.GetStreamAsync(StreamId);
			Assert.IsNotNull(Stream);

			StreamService StreamService = ServiceProvider.GetRequiredService<StreamService>();
			await StreamService.UpdateStreamAsync(Stream, NewTemplateRefs: new Dictionary<TemplateRefId, TemplateRef> { { TemplateRefId, NewTemplateRef } });

			// Trigger a job
			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(103, Jobs2[0].Change);
			Assert.AreEqual(103, Jobs2[0].CodeChange);

			// Check another job does not trigger due to the change above
			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);
		}

		[TestMethod]
		public async Task GateTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;

			// Create two templates, the second dependent on the first
			ITemplate? NewTemplate1 = await TestSetup.TemplateService.CreateTemplateAsync("Test template 1");
			TemplateRef NewTemplateRef1 = new TemplateRef(NewTemplate1);
			TemplateRefId NewTemplateRefId1 = new TemplateRefId("new-template-1");

			ITemplate? NewTemplate2 = await TestSetup.TemplateService.CreateTemplateAsync("Test template 2");
			TemplateRef NewTemplateRef2 = new TemplateRef(NewTemplate2);
			NewTemplateRef2.Schedule = new Schedule();
			NewTemplateRef2.Schedule.Gate = new ScheduleGate(NewTemplateRefId1, "TriggerNext");
			NewTemplateRef2.Schedule.Patterns.Add(new SchedulePattern(null, 0, null, 10));
			NewTemplateRef2.Schedule.LastTriggerTime = StartTime;
			TemplateRefId NewTemplateRefId2 = new TemplateRefId("new-template-2");

			List<StreamTab> Tabs = new List<StreamTab>();
			Tabs.Add(new JobsTab("foo", true, new List<TemplateRefId> { NewTemplateRefId1, NewTemplateRefId2 }, new List<string>(), new List<JobsTabColumn>()));
			await TestSetup.StreamService.UpdateStreamAsync(Stream, NewTabs: Tabs, NewTemplateRefs: new Dictionary<TemplateRefId, TemplateRef> { { NewTemplateRefId1, NewTemplateRef1 }, { NewTemplateRefId2, NewTemplateRef2 } });

			// Create the TriggerNext step and mark it as complete
			IGraph GraphA = await TestSetup.GraphCollection.AddAsync(NewTemplate1);
			CreateGroupRequest GroupA = new CreateGroupRequest("win", new List<CreateNodeRequest> { new CreateNodeRequest("TriggerNext") });
			GraphA = await TestSetup.GraphCollection.AppendAsync(GraphA, new List<CreateGroupRequest> { GroupA });

			// Tick the schedule and make sure it doesn't trigger
			await ScheduleService.TickSharedOnlyForTestingAsync();
			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(0, Jobs2.Count);

			// Create a job and fail it
			IJob Job1 = await TestSetup.JobService.CreateJobAsync(null, StreamId, NewTemplateRefId1, Template.Id, GraphA, "Hello", 1234, 1233, 999, null, null, "joe", null, null, null, true, true, null, null, null, Template.Counters, new List<string> { "-Target=TriggerNext" });
			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job1, Job1.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job1, Job1.Batches[0].Id, Job1.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Failure));
			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job1, Job1.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Complete));
			await GetNewJobs();

			// Tick the schedule and make sure it doesn't trigger
			TestSetup.Clock.Advance(TimeSpan.FromMinutes(30.0));
			await ScheduleService.TickSharedOnlyForTestingAsync();
			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);

			// Create a job and make it succeed
			IJob Job2 = await TestSetup.JobService.CreateJobAsync(null, StreamId, NewTemplateRefId1, Template.Id, GraphA, "Hello", 1234, 1233, 999, null, null, "joe", null, null, null, true, true, null, null, null, Template.Counters, new List<string> { "-Target=TriggerNext" });
			Assert.IsTrue(await TestSetup.JobService.UpdateBatchAsync(Job2, Job2.Batches[0].Id, ObjectId.GenerateNewId(), JobStepBatchState.Running));
			Assert.IsNotNull(await TestSetup.JobService.UpdateStepAsync(Job2, Job2.Batches[0].Id, Job2.Batches[0].Steps[0].Id, JobStepState.Completed, JobStepOutcome.Success));

			// Tick the schedule and make sure it does trigger
			await ScheduleService.TickSharedOnlyForTestingAsync();
			List<IJob> Jobs4 = await GetNewJobs();
			Assert.AreEqual(1, Jobs4.Count);
			Assert.AreEqual(1234, Jobs4[0].Change);
			Assert.AreEqual(1233, Jobs4[0].CodeChange);
		}

		[TestMethod]
		public async Task StreamPausing()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			TestSetup.Clock.UtcNow = StartTime;
			
			Schedule Schedule = new Schedule();
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));
			Schedule.LastTriggerTime = StartTime;
			
			ITemplate NewTemplate = await TestSetup.TemplateService.CreateTemplateAsync("Test template", SubmitNewChange: "fooAAA.cpp");
			TemplateRef NewTemplateRef = new TemplateRef(NewTemplate, Schedule: Schedule);
			
			StreamService StreamService = ServiceProvider.GetRequiredService<StreamService>();
			await StreamService.UpdateStreamAsync(Stream,
				NewTemplateRefs: new Dictionary<TemplateRefId, TemplateRef> { { TemplateRefId, NewTemplateRef } },
				UpdatePauseFields: true, NewPausedUntil: StartTime.AddHours(5), NewPauseComment: "testing");
			
			// Try trigger a job. No job should be scheduled as the stream is paused
			TestSetup.Clock.Advance(TimeSpan.FromHours(1.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();
			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(0, Jobs2.Count);

			// Advance time beyond the pause period. A build should now trigger
			TestSetup.Clock.Advance(TimeSpan.FromHours(5.25));
			await ScheduleService.TickSharedOnlyForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(1, Jobs3.Count);
			Assert.AreEqual(103, Jobs3[0].Change);
			Assert.AreEqual(103, Jobs3[0].CodeChange);
		}

		async Task<List<IJob>> GetNewJobs()
		{
			List<IJob> Jobs = await TestSetup.JobCollection.FindAsync();
			Jobs.RemoveAll(x => InitialJobIds.Contains(x.Id));
			InitialJobIds.UnionWith(Jobs.Select(x => x.Id));
			return Jobs.OrderBy(x => x.Change).ToList();
		}
	}
}