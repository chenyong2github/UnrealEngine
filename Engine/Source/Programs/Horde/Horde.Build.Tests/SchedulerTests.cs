// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using HordeCommon;
using Horde.Build.Collections.Impl;
using Horde.Build.Models;
using Horde.Build.Services;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using PoolId = Horde.Build.Utilities.StringId<Horde.Build.Models.IPool>;
using StreamId = Horde.Build.Utilities.StringId<Horde.Build.Models.IStream>;
using ProjectId = Horde.Build.Utilities.StringId<Horde.Build.Models.IProject>;
using TemplateRefId = Horde.Build.Utilities.StringId<Horde.Build.Models.TemplateRef>;
using Horde.Build.Api;
using MongoDB.Bson;
using System.Linq;
using Horde.Build.Tests.Stubs.Services;
using Horde.Build.Collections;
using Horde.Build.Utilities;
using System.Threading;

namespace Horde.Build.Tests
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	[TestClass]
    public class SchedulerTests : TestSetup
	{
		ProjectId ProjectId { get; } = new ProjectId("ue5");
		StreamId StreamId { get; } = new StreamId("ue5-main");
		TemplateRefId TemplateRefId { get; } = new TemplateRefId("template1");

		ITemplate Template;
		HashSet<JobId> InitialJobIds;

		public SchedulerTests()
		{
			IUser Bob = UserCollection.FindOrAddUserByLoginAsync("Bob").Result;

			IProject ? Project = ProjectService.Collection.AddOrUpdateAsync(ProjectId, "", "", 0, new ProjectConfig { Name = "UE5" }).Result;
			Assert.IsNotNull(Project);

			Template = TemplateCollection.AddAsync("Test template").Result;

			InitialJobIds = new HashSet<JobId>(JobCollection.FindAsync().Result.Select(x => x.Id));

			PerforceService.Changes.Clear();
			PerforceService.AddChange("//UE5/Main", 100, Bob, "", new[] { "code.cpp" });
			PerforceService.AddChange("//UE5/Main", 101, Bob, "", new[] { "content.uasset" });
			PerforceService.AddChange("//UE5/Main", 102, Bob, "", new[] { "content.uasset" });
		}

		async Task<IStream> SetScheduleAsync(CreateScheduleRequest Schedule)
		{
			await ScheduleService.ResetAsync();

			IStream? Stream = await StreamService.GetStreamAsync(StreamId);

			StreamConfig Config = new StreamConfig();
			Config.Name = "//UE5/Main";
			Config.Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { TemplateRefId.ToString() } });
			Config.Templates.Add(new CreateTemplateRefRequest { Id = TemplateRefId.ToString(), Name = "Test", Schedule = Schedule });

			return (await StreamService.StreamCollection.TryCreateOrReplaceAsync(StreamId, Stream, "", "", ProjectId, Config))!; 
		}

		public async Task<List<IJob>> FileTestHelperAsync(params string[] Files)
		{
			IUser Bob = UserCollection.FindOrAddUserByLoginAsync("Bob", "Bob").Result;

			PerforceService.Changes.Clear();
			PerforceService.AddChange("//UE5/Main", 100, Bob, "", new[] { "code.cpp" });
			PerforceService.AddChange("//UE5/Main", 101, Bob, "", new[] { "content.uasset" });
			PerforceService.AddChange("//UE5/Main", 102, Bob, "", new[] { "content.uasset" });
			PerforceService.AddChange("//UE5/Main", 103, Bob, "", new[] { "foo/code.cpp" });
			PerforceService.AddChange("//UE5/Main", 104, Bob, "", new[] { "bar/code.cpp" });
			PerforceService.AddChange("//UE5/Main", 105, Bob, "", new[] { "foo/bar/content.uasset" });
			PerforceService.AddChange("//UE5/Main", 106, Bob, "", new[] { "bar/foo/content.uasset" });

			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.MaxChanges = 10;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { Interval = 1 });
			Schedule.Files = Files.ToList();
			await SetScheduleAsync(Schedule);

			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			return await GetNewJobs();
		}

		[TestMethod]
		public async Task FileTest1Async()
		{
			List<IJob> Jobs = await FileTestHelperAsync("....cpp");
			Assert.AreEqual(3, Jobs.Count);
			Assert.AreEqual(100, Jobs[0].Change);
			Assert.AreEqual(103, Jobs[1].Change);
			Assert.AreEqual(104, Jobs[2].Change);
		}

		[TestMethod]
		public async Task FileTest2Async()
		{
			List<IJob> Jobs = await FileTestHelperAsync("/foo/...");
			Assert.AreEqual(2, Jobs.Count);
			Assert.AreEqual(103, Jobs[0].Change);
			Assert.AreEqual(105, Jobs[1].Change);
		}

		[TestMethod]
		public async Task FileTest3Async()
		{
			List<IJob> Jobs = await FileTestHelperAsync("....uasset", "-/bar/...");
			Assert.AreEqual(3, Jobs.Count);
			Assert.AreEqual(101, Jobs[0].Change);
			Assert.AreEqual(102, Jobs[1].Change);
			Assert.AreEqual(105, Jobs[2].Change);
		}

		[TestMethod]
		public void DayScheduleTest()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Utc); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(Clock.UtcNow, RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(new List<DayOfWeek> { DayOfWeek.Friday, DayOfWeek.Sunday }, 13 * 60, null, null));

			DateTime? NextTime = Schedule.GetNextTriggerTimeUtc(StartTime, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0 * 2.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0 * 7.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0 * 9.0), NextTime!.Value);
		}

		[TestMethod]
		public void MulticheduleTest()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Utc); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(Clock.UtcNow, RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(null, 13 * 60, 14 * 60, 15));

			DateTime? NextTime = Schedule.GetNextTriggerTimeUtc(StartTime, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.25), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.5), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.75), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(2.0), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(1.0 + 24.0), NextTime!.Value);
		}

		[TestMethod]
		public void MultiPatternTest()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 0, 0, 0, DateTimeKind.Utc); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			Schedule Schedule = new Schedule(Clock.UtcNow, RequireSubmittedChange: false);
			Schedule.Patterns.Add(new SchedulePattern(null, 11 * 60, 0, 0));
			Schedule.Patterns.Add(new SchedulePattern(null, 19 * 60, 0, 0));

			DateTime? NextTime = Schedule.GetNextTriggerTimeUtc(StartTime, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(11), NextTime!.Value);

			NextTime = Schedule.GetNextTriggerTimeUtc(NextTime.Value, TimeZoneInfo.Utc);
			Assert.AreEqual(StartTime + TimeSpan.FromHours(19), NextTime!.Value);
		}

		[TestMethod]
		public async Task NoSubmittedChangeTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
//			Schedule.LastTriggerTime = StartTime;
			await SetScheduleAsync(Schedule);

			// Initial tick
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger a job
			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);
		}

		[TestMethod]
		public async Task RequireSubmittedChangeTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			IStream Stream = await SetScheduleAsync(Schedule);

			// Initial tick
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger a job
			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);

			IStream Stream2 = (await StreamCollection.GetAsync(Stream.Id))!;
			Schedule Schedule2 = Stream2.Templates.First().Value.Schedule!;
			Assert.AreEqual(102, Schedule2.LastTriggerChange);
			Assert.AreEqual(Clock.UtcNow, Schedule2.LastTriggerTime);

			// Trigger another job
			await Clock.AdvanceAsync(TimeSpan.FromHours(0.5));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);
		}

		[TestMethod]
		public async Task MultipleJobsTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			Schedule.MaxChanges = 2;
			Schedule.Filter = new List<ChangeContentFlags> { ChangeContentFlags.ContainsCode };
			await SetScheduleAsync(Schedule);

			// Initial tick
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger some jobs
			IUser Bob = UserCollection.FindOrAddUserByLoginAsync("Bob").Result;
			PerforceService.AddChange("//UE5/Main", 103, Bob, "", new string[] { "foo.cpp" });
			PerforceService.AddChange("//UE5/Main", 104, Bob, "", new string[] { "foo.cpp" });
			PerforceService.AddChange("//UE5/Main", 105, Bob, "", new string[] { "foo.uasset" });
			PerforceService.AddChange("//UE5/Main", 106, Bob, "", new string[] { "foo.cpp" });

			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(2, Jobs2.Count);
			Assert.AreEqual(104, Jobs2[0].Change);
			Assert.AreEqual(104, Jobs2[0].CodeChange);
			Assert.AreEqual(106, Jobs2[1].Change);
			Assert.AreEqual(106, Jobs2[1].CodeChange);
		}

		[TestMethod]
		public async Task SkipCiTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			Schedule.MaxChanges = 2;
			Schedule.Filter = new List<ChangeContentFlags> { ChangeContentFlags.ContainsCode };
			await SetScheduleAsync(Schedule);

			// Initial tick
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(0, Jobs1.Count);

			// Trigger some jobs
			IUser Bob = UserCollection.FindOrAddUserByLoginAsync("Bob").Result;
			PerforceService.AddChange("//UE5/Main", 103, Bob, "", new string[] { "foo.cpp" });
			PerforceService.AddChange("//UE5/Main", 104, Bob, "Don't build this change!\n#skipci", new string[] { "foo.cpp" });
			PerforceService.AddChange("//UE5/Main", 105, Bob, "", new string[] { "foo.uasset" });
			PerforceService.AddChange("//UE5/Main", 106, Bob, "", new string[] { "foo.cpp" });

			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(2, Jobs2.Count);
			Assert.AreEqual(103, Jobs2[0].Change);
			Assert.AreEqual(103, Jobs2[0].CodeChange);
			Assert.AreEqual(106, Jobs2[1].Change);
			Assert.AreEqual(106, Jobs2[1].CodeChange);
		}

		[TestMethod]
		public async Task MaxActiveTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.RequireSubmittedChange = false;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			Schedule.MaxActive = 1;
			await SetScheduleAsync(Schedule);

			// Trigger a job
			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);

			// Test that another job does not trigger
			await Clock.AdvanceAsync(TimeSpan.FromHours(0.5));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);

			// Mark the original job as complete
			await JobService.UpdateJobAsync(Jobs2[0], AbortedByUserId: KnownUsers.System);

			// Test that another job does not trigger
			await Clock.AdvanceAsync(TimeSpan.FromHours(0.5));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs4 = await GetNewJobs();
			Assert.AreEqual(1, Jobs4.Count);
			Assert.AreEqual(102, Jobs4[0].Change);
			Assert.AreEqual(100, Jobs4[0].CodeChange);
		}

		[TestMethod]
		public async Task CreateNewChangeTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			IStream Stream = await SetScheduleAsync(Schedule);

			// Trigger a job
			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(1, Jobs2.Count);
			Assert.AreEqual(102, Jobs2[0].Change);
			Assert.AreEqual(100, Jobs2[0].CodeChange);

			// Check another job does not trigger due to the change above
			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);
		}

		[TestMethod]
		public async Task GateTestAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			// Create two templates, the second dependent on the first
			ITemplate? NewTemplate1 = await TemplateCollection.AddAsync("Test template 1");
			TemplateRef NewTemplateRef1 = new TemplateRef(NewTemplate1);
			TemplateRefId NewTemplateRefId1 = new TemplateRefId("new-template-1");

			ITemplate? NewTemplate2 = await TemplateCollection.AddAsync("Test template 2");
			TemplateRef NewTemplateRef2 = new TemplateRef(NewTemplate2);
			NewTemplateRef2.Schedule = new Schedule(Clock.UtcNow);
			NewTemplateRef2.Schedule.Gate = new ScheduleGate(NewTemplateRefId1, "TriggerNext");
			NewTemplateRef2.Schedule.Patterns.Add(new SchedulePattern(null, 0, null, 10));
			NewTemplateRef2.Schedule.LastTriggerTime = StartTime;
			TemplateRefId NewTemplateRefId2 = new TemplateRefId("new-template-2");

			IStream? Stream = await StreamService.GetStreamAsync(StreamId);

			StreamConfig Config = new StreamConfig();
			Config.Name = "//UE5/Main";
			Config.Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { NewTemplateRefId1.ToString(), NewTemplateRefId2.ToString() } });

			Stream = (await StreamService.StreamCollection.TryCreateOrReplaceAsync(StreamId, Stream, "", "", ProjectId, Config))!;

			// Create the TriggerNext step and mark it as complete
			IGraph GraphA = await GraphCollection.AddAsync(NewTemplate1);
			NewGroup GroupA = new NewGroup("win", new List<NewNode> { new NewNode("TriggerNext") });
			GraphA = await GraphCollection.AppendAsync(GraphA, new List<NewGroup> { GroupA });

			// Tick the schedule and make sure it doesn't trigger
			await ScheduleService.TickForTestingAsync();
			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(0, Jobs2.Count);

			// Create a job and fail it
			IJob Job1 = await JobService.CreateJobAsync(null, Stream, NewTemplateRefId1, Template.Id, GraphA, "Hello", 1234, 1233, 999, null, null, null, null, null, null, null, true, true, null, null, new List<string> { "-Target=TriggerNext" });
			SubResourceId BatchId1 = Job1.Batches[0].Id;
			SubResourceId StepId1 = Job1.Batches[0].Steps[0].Id;
			Job1 = Deref(await JobService.UpdateBatchAsync(Job1, BatchId1, LogId.GenerateNewId(), JobStepBatchState.Running));
			Job1 = Deref(await JobService.UpdateStepAsync(Job1, BatchId1, StepId1, JobStepState.Completed, JobStepOutcome.Failure));
			Job1 = Deref(await JobService.UpdateBatchAsync(Job1, BatchId1, LogId.GenerateNewId(), JobStepBatchState.Complete));
			await GetNewJobs();

			// Tick the schedule and make sure it doesn't trigger
			await Clock.AdvanceAsync(TimeSpan.FromMinutes(30.0));
			await ScheduleService.TickForTestingAsync();
			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(0, Jobs3.Count);

			// Create a job and make it succeed
			IJob Job2 = await JobService.CreateJobAsync(null, Stream, NewTemplateRefId1, Template.Id, GraphA, "Hello", 1234, 1233, 999, null, null, null, null, null, null, null, true, true, null, null, new List<string> { "-Target=TriggerNext" });
			SubResourceId BatchId2 = Job2.Batches[0].Id;
			SubResourceId StepId2 = Job2.Batches[0].Steps[0].Id;
			Job2 = Deref(await JobService.UpdateBatchAsync(Job2, BatchId2, LogId.GenerateNewId(), JobStepBatchState.Running));
			Job2 = Deref(await JobService.UpdateStepAsync(Job2, BatchId2, StepId2, JobStepState.Completed, JobStepOutcome.Success));

			// Tick the schedule and make sure it does trigger
			await ScheduleService.TickForTestingAsync();
			List<IJob> Jobs4 = await GetNewJobs();
			Assert.AreEqual(1, Jobs4.Count);
			Assert.AreEqual(1234, Jobs4[0].Change);
			Assert.AreEqual(1233, Jobs4[0].CodeChange);
		}

		[TestMethod]
		public async Task GateTest2Async()
		{
			IUser Bob = await UserCollection.FindOrAddUserByLoginAsync("Bob");

			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			PerforceService.Changes.Clear();
			PerforceService.AddChange("//UE5/Main", 1230, Bob, "", new[] { "code.cpp" });
			PerforceService.AddChange("//UE5/Main", 1231, Bob, "", new[] { "content.uasset" });
			PerforceService.AddChange("//UE5/Main", 1232, Bob, "", new[] { "content.uasset" });
			PerforceService.AddChange("//UE5/Main", 1233, Bob, "", new[] { "code.cpp" });

			// Create two templates, the second dependent on the first
			TemplateRefId NewTemplateRefId1 = new TemplateRefId("new-template-1");
			CreateTemplateRefRequest NewTemplate1 = new CreateTemplateRefRequest();
			NewTemplate1.Id = NewTemplateRefId1.ToString();

			TemplateRefId NewTemplateRefId2 = new TemplateRefId("new-template-2");
			CreateTemplateRefRequest NewTemplate2 = new CreateTemplateRefRequest();
			NewTemplate2.Id = NewTemplateRefId2.ToString();
			NewTemplate2.Name = "Test template 2";
			NewTemplate2.Schedule = new CreateScheduleRequest();
			NewTemplate2.Schedule.MaxChanges = 4;
			NewTemplate2.Schedule.Filter = new List<ChangeContentFlags> { ChangeContentFlags.ContainsCode };
			NewTemplate2.Schedule.Gate = new CreateScheduleGateRequest { TemplateId = NewTemplateRefId1.ToString(), Target = "TriggerNext" };
			NewTemplate2.Schedule.Patterns.Add(new CreateSchedulePatternRequest { Interval = 10 });// (null, 0, null, 10));
//			NewTemplate2.Schedule.LastTriggerTime = StartTime;

			IStream? Stream = await StreamService.GetStreamAsync(StreamId);

			StreamConfig Config = new StreamConfig();
			Config.Name = "//UE5/Main";
			Config.Tabs.Add(new CreateJobsTabRequest { Title = "foo", Templates = new List<string> { NewTemplateRefId1.ToString(), NewTemplateRefId2.ToString() } });
			Config.Templates.Add(NewTemplate1);
			Config.Templates.Add(NewTemplate2);

			Stream = (await StreamService.StreamCollection.TryCreateOrReplaceAsync(StreamId, Stream, "", "", ProjectId, Config))!;

			ITemplate Template1 = (await TemplateCollection.GetAsync(Stream.Templates[NewTemplateRefId1].Hash))!;
			ITemplate Template2 = (await TemplateCollection.GetAsync(Stream.Templates[NewTemplateRefId2].Hash))!;

			// Create the graph
			IGraph GraphA = await GraphCollection.AddAsync(Template1);
			NewGroup GroupA = new NewGroup("win", new List<NewNode> { new NewNode("TriggerNext") });
			GraphA = await GraphCollection.AppendAsync(GraphA, new List<NewGroup> { GroupA });

			// Create successful jobs for all the changes we added above
			for (int Change = 1230; Change <= 1233; Change++)
			{
				int CodeChange = (Change < 1233) ? 1230 : 1233;

				IJob Job1 = await JobService.CreateJobAsync(null, Stream, NewTemplateRefId1, Template.Id, GraphA, "Hello", Change, CodeChange, null, null, null, null, null, false, null, null, true, true, null, null, new List<string> { "-Target=TriggerNext" });
				for (int BatchIdx = 0; BatchIdx < Job1.Batches.Count; BatchIdx++)
				{
					SubResourceId BatchId1 = Job1.Batches[BatchIdx].Id;
					Job1 = Deref(await JobService.UpdateBatchAsync(Job1, BatchId1, LogId.GenerateNewId(), JobStepBatchState.Running));
					for (int StepIdx = 0; StepIdx < Job1.Batches[BatchIdx].Steps.Count; StepIdx++)
					{
						SubResourceId StepId1 = Job1.Batches[BatchIdx].Steps[StepIdx].Id;
						Job1 = Deref(await JobService.UpdateStepAsync(Job1, BatchId1, StepId1, JobStepState.Completed, JobStepOutcome.Success, NewLogId: LogId.GenerateNewId()));
					}
					Job1 = Deref(await JobService.UpdateBatchAsync(Job1, BatchId1, LogId.GenerateNewId(), JobStepBatchState.Complete));
				}
			}
			await GetNewJobs();

			// Tick the schedule and make sure it doesn't trigger
			await Clock.AdvanceAsync(TimeSpan.FromMinutes(30.0));
			await ScheduleService.TriggerAsync(StreamId, NewTemplateRefId2, Clock.UtcNow, default);
			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(2, Jobs3.Count);
			Assert.AreEqual(1230, Jobs3[0].Change);
			Assert.AreEqual(1233, Jobs3[1].Change);
		}

		[TestMethod]
		public async Task UpdateConfigAsync()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Local); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.RequireSubmittedChange = false;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			Schedule.MaxActive = 2;
			await SetScheduleAsync(Schedule);

			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs1 = await GetNewJobs();
			Assert.AreEqual(1, Jobs1.Count);
			Assert.AreEqual(102, Jobs1[0].Change);
			Assert.AreEqual(100, Jobs1[0].CodeChange);

			// Make sure the job is registered
			IStream? Stream1 = await StreamService.GetStreamAsync(StreamId);
			TemplateRef TemplateRef1 = Stream1!.Templates.First().Value;
			Assert.AreEqual(1, TemplateRef1.Schedule!.ActiveJobs.Count);
			Assert.AreEqual(Jobs1[0].Id, TemplateRef1.Schedule!.ActiveJobs[0]);

			// Test that another job does not trigger
			await SetScheduleAsync(Schedule);

			// Make sure the job is still registered
			IStream? Stream2 = await StreamService.GetStreamAsync(StreamId);
			TemplateRef TemplateRef2 = Stream2!.Templates.First().Value;
			Assert.AreEqual(1, TemplateRef2.Schedule!.ActiveJobs.Count);
			Assert.AreEqual(Jobs1[0].Id, TemplateRef2.Schedule!.ActiveJobs[0]);
		}

		[TestMethod]
		public async Task StreamPausing()
		{
			DateTime StartTime = new DateTime(2021, 1, 1, 12, 0, 0, DateTimeKind.Utc); // Friday Jan 1, 2021 
			Clock.UtcNow = StartTime;

			CreateScheduleRequest Schedule = new CreateScheduleRequest();
			Schedule.Enabled = true;
			Schedule.Patterns.Add(new CreateSchedulePatternRequest { MinTime = 13 * 60, MaxTime = 14 * 60, Interval = 15 });
			IStream Stream = await SetScheduleAsync(Schedule);

			IStreamCollection StreamCollection = ServiceProvider.GetRequiredService<IStreamCollection>();
			await StreamCollection.TryUpdatePauseStateAsync(Stream, NewPausedUntil: StartTime.AddHours(5), NewPauseComment: "testing");

			// Try trigger a job. No job should be scheduled as the stream is paused
			await Clock.AdvanceAsync(TimeSpan.FromHours(1.25));
			await ScheduleService.TickForTestingAsync();
			List<IJob> Jobs2 = await GetNewJobs();
			Assert.AreEqual(0, Jobs2.Count);

			// Advance time beyond the pause period. A build should now trigger
			await Clock.AdvanceAsync(TimeSpan.FromHours(5.25));
			await ScheduleService.TickForTestingAsync();

			List<IJob> Jobs3 = await GetNewJobs();
			Assert.AreEqual(1, Jobs3.Count);
			Assert.AreEqual(102, Jobs3[0].Change);
			Assert.AreEqual(100, Jobs3[0].CodeChange);
		}

		async Task<List<IJob>> GetNewJobs()
		{
			List<IJob> Jobs = await JobCollection.FindAsync();
			Jobs.RemoveAll(x => InitialJobIds.Contains(x.Id));
			InitialJobIds.UnionWith(Jobs.Select(x => x.Id));
			return Jobs.OrderBy(x => x.Change).ToList();
		}
	}
}