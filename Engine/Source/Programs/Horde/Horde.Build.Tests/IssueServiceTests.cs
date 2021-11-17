// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser;
using HordeAgent.Utility;
using HordeCommon;
using HordeServer;
using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.IssueHandlers.Impl;
using HordeServer.Logs;
using HordeServer.Logs.Builder;
using HordeServer.Logs.Storage;
using HordeServer.Logs.Storage.Impl;
using HordeServer.Models;
using HordeServer.Notifications;
using HordeServer.Services;
using HordeServer.Services.Impl;
using HordeServer.Utilities;
using HordeServerTests.Stubs.Collections;
using HordeServerTests.Stubs.Services;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using MongoDB.Driver;
using Moq;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;
	using UserId = ObjectId<IUser>;

	[TestClass]
	public class IssueServiceTests : TestSetup
	{
		class TestJsonLogger : JsonLogger, IAsyncDisposable
		{
			ILogFileService LogFileService;
			LogId LogId;
			List<(LogLevel, byte[])> Events = new List<(LogLevel, byte[])>();

			public TestJsonLogger(ILogFileService LogFileService, LogId LogId)
				: base(null, NullLogger.Instance)
			{
				this.LogFileService = LogFileService;
				this.LogId = LogId;
			}

			public async ValueTask DisposeAsync()
			{
				foreach ((LogLevel Level, byte[] Line) in Events)
				{
					byte[] LineWithNewLine = new byte[Line.Length + 1];
					Array.Copy(Line, LineWithNewLine, Line.Length);
					LineWithNewLine[LineWithNewLine.Length - 1] = (byte)'\n';
					await WriteAsync(Level, LineWithNewLine);
				}
			}

			protected override void WriteFormattedEvent(LogLevel Level, byte[] Line)
			{
				Events.Add((Level, Line));
			}

			private async Task WriteAsync(LogLevel Level, byte[] Line)
			{
				ILogFile LogFile = (await LogFileService.GetLogFileAsync(LogId))!;
				LogMetadata Metadata = await LogFileService.GetMetadataAsync(LogFile);
				await LogFileService.WriteLogDataAsync(LogFile, Metadata.Length, Metadata.MaxLineIndex, Line, false);

				if (Level >= LogLevel.Warning)
				{
					LogEvent Event = ParseEvent(Line);
					if (Event.LineIndex == 0)
					{
						EventSeverity Severity = (Level == LogLevel.Warning) ? EventSeverity.Warning : EventSeverity.Error;
						await LogFileService.CreateEventsAsync(new List<NewLogEventData> { new NewLogEventData { LogId = LogId, LineIndex = Metadata.MaxLineIndex, LineCount = Event.LineCount, Severity = Severity } });
					}
				}
			}

			static LogEvent ParseEvent(byte[] Line)
			{
				Utf8JsonReader Reader = new Utf8JsonReader(Line.AsSpan());
				Reader.Read();
				return LogEvent.Read(ref Reader);
			}
		}

		const string MainStreamName = "//UE4/Main";
		StreamId MainStreamId = StreamId.Sanitize(MainStreamName);

		const string ReleaseStreamName = "//UE4/Release";
		StreamId ReleaseStreamId = StreamId.Sanitize(ReleaseStreamName);

		const string DevStreamName = "//UE4/Dev";
		StreamId DevStreamId = StreamId.Sanitize(DevStreamName);

		IGraph Graph;
		PerforceServiceStub Perforce;

		UserId TimId;
		UserId JerryId;
		UserId BobId;

		DirectoryReference WorkspaceDir;

		public IssueServiceTests()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				WorkspaceDir = new DirectoryReference("C:\\Horde");
			}
			else
			{
				WorkspaceDir = new DirectoryReference("/Horde");
			}

			IProject Project = ProjectCollection.AddOrUpdateAsync(new ProjectId("ue4"), "", "", 0, new ProjectConfig { Name = "UE4" }).Result!;

			IStream MainStream = StreamCollection.TryCreateOrReplaceAsync(MainStreamId, null, "", "", Project.Id, new StreamConfig { Name = MainStreamName }).Result!;
			IStream ReleaseStream = StreamCollection.TryCreateOrReplaceAsync(ReleaseStreamId, null, "", "", Project.Id, new StreamConfig { Name = ReleaseStreamName }).Result!;
			IStream DevStream = StreamCollection.TryCreateOrReplaceAsync(DevStreamId, null, "", "", Project.Id, new StreamConfig { Name = DevStreamName }).Result!;

			IUser Bill = UserCollection.FindOrAddUserByLoginAsync("Bill").Result;
			IUser Anne = UserCollection.FindOrAddUserByLoginAsync("Anne").Result;
			IUser Bob = UserCollection.FindOrAddUserByLoginAsync("Bob").Result;
			IUser Jerry = UserCollection.FindOrAddUserByLoginAsync("Jerry").Result;
			IUser Chris = UserCollection.FindOrAddUserByLoginAsync("Chris").Result;

			TimId = UserCollection.FindOrAddUserByLoginAsync("Tim").Result.Id;
			JerryId = UserCollection.FindOrAddUserByLoginAsync("Jerry").Result.Id;
			BobId = UserCollection.FindOrAddUserByLoginAsync("Bob").Result.Id;

			Perforce = PerforceService;
			Perforce.AddChange(MainStreamName, 100, Bill, "Description", new string[] { "a/b.cpp" });
			Perforce.AddChange(MainStreamName, 105, Anne, "Description", new string[] { "a/c.cpp" });
			Perforce.AddChange(MainStreamName, 110, Bob, "Description", new string[] { "a/d.cpp" });
			Perforce.AddChange(MainStreamName, 115, Jerry, "Description\n#ROBOMERGE-SOURCE: CL 75 in //UE4/Release/...", new string[] { "a/e.cpp", "a/foo.cpp" });
			Perforce.AddChange(MainStreamName, 120, Chris, "Description\n#ROBOMERGE-OWNER: Tim", new string[] { "a/f.cpp" });
			Perforce.AddChange(MainStreamName, 125, Chris, "Description", new string[] { "a/g.cpp" });

			List<INode> Nodes = new List<INode>();
			Nodes.Add(MockNode("Update Version Files"));
			Nodes.Add(MockNode("Compile UnrealHeaderTool Win64"));
			Nodes.Add(MockNode("Compile ShooterGameEditor Win64"));
			Nodes.Add(MockNode("Cook ShooterGame Win64"));

			Mock<INodeGroup> Group = new Mock<INodeGroup>(MockBehavior.Strict);
			Group.SetupGet(x => x.Nodes).Returns(Nodes);

			Mock<IGraph> GraphMock = new Mock<IGraph>(MockBehavior.Strict);
			GraphMock.SetupGet(x => x.Groups).Returns(new List<INodeGroup> { Group.Object });
			Graph = GraphMock.Object;
		}

		public static INode MockNode(string Name)
		{
			Mock<INode> Node = new Mock<INode>();
			Node.SetupGet(x => x.Name).Returns(Name);
			return Node.Object;
		}

		public IJob CreateJob(StreamId StreamId, int Change, string Name, IGraph Graph, TimeSpan Time = default)
		{
			JobId JobId = JobId.GenerateNewId();

			List<IJobStepBatch> Batches = new List<IJobStepBatch>();
			for (int GroupIdx = 0; GroupIdx < Graph.Groups.Count; GroupIdx++)
			{
				INodeGroup Group = Graph.Groups[GroupIdx];

				List<IJobStep> Steps = new List<IJobStep>();
				for (int NodeIdx = 0; NodeIdx < Group.Nodes.Count; NodeIdx++)
				{
					SubResourceId StepId = new SubResourceId((ushort)((GroupIdx * 100) + NodeIdx));

					ILogFile LogFile = LogFileService.CreateLogFileAsync(JobId, null, HordeServer.Api.LogType.Json).Result;

					Mock<IJobStep> Step = new Mock<IJobStep>(MockBehavior.Strict);
					Step.SetupGet(x => x.Id).Returns(StepId);
					Step.SetupGet(x => x.NodeIdx).Returns(NodeIdx);
					Step.SetupGet(x => x.LogId).Returns(LogFile.Id);
					Step.SetupGet(x => x.StartTimeUtc).Returns(DateTime.UtcNow + Time);

					Steps.Add(Step.Object);
				}

				SubResourceId BatchId = new SubResourceId((ushort)(GroupIdx * 100));

				Mock<IJobStepBatch> Batch = new Mock<IJobStepBatch>(MockBehavior.Strict);
				Batch.SetupGet(x => x.Id).Returns(BatchId);
				Batch.SetupGet(x => x.GroupIdx).Returns(GroupIdx);
				Batch.SetupGet(x => x.Steps).Returns(Steps);
				Batches.Add(Batch.Object);
			}

			Mock<IJob> Job = new Mock<IJob>(MockBehavior.Strict);
			Job.SetupGet(x => x.Id).Returns(JobId);
			Job.SetupGet(x => x.Name).Returns(Name);
			Job.SetupGet(x => x.StreamId).Returns(StreamId);
			Job.SetupGet(x => x.TemplateId).Returns(new TemplateRefId("test-template"));
			Job.SetupGet(x => x.Change).Returns(Change);
			Job.SetupGet(x => x.Batches).Returns(Batches);
			Job.SetupGet(x => x.ShowUgsBadges).Returns(true);
			Job.SetupGet(x => x.ShowUgsAlerts).Returns(true);
			Job.SetupGet(x => x.NotificationChannel).Returns("#devtools-horde-slack-testing");
			return Job.Object;
		}

		async Task UpdateCompleteStep(IJob Job, int BatchIdx, int StepIdx, JobStepOutcome Outcome)
		{
			IJobStepBatch Batch = Job.Batches[BatchIdx];
			IJobStep Step = Batch.Steps[StepIdx];
			await IssueService.UpdateCompleteStep(Job, Graph, Batch.Id, Step.Id);

			JobStepRefId JobStepRefId = new JobStepRefId(Job.Id, Batch.Id, Step.Id);
			string NodeName = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx].Name;
			await JobStepRefCollection.InsertOrReplaceAsync(JobStepRefId, "TestJob", NodeName, Job.StreamId, Job.TemplateId, Job.Change, Step.LogId, null, null, Outcome, 0.0f, 0.0f, Step.StartTimeUtc!.Value, Step.StartTimeUtc);
		}

		async Task AddEvent(IJob Job, int BatchIdx, int StepIdx, object Data, EventSeverity Severity = EventSeverity.Error)
		{
			LogId LogId = Job.Batches[BatchIdx].Steps[StepIdx].LogId!.Value;

			List<byte> Bytes = new List<byte>();
			Bytes.AddRange(JsonSerializer.SerializeToUtf8Bytes(Data));
			Bytes.Add((byte)'\n');

			ILogFile LogFile = (await LogFileService.GetLogFileAsync(LogId))!;
			LogMetadata Metadata = await LogFileService.GetMetadataAsync(LogFile);
			await LogFileService.WriteLogDataAsync(LogFile, Metadata.Length, Metadata.MaxLineIndex, Bytes.ToArray(), false);

			await LogFileService.CreateEventsAsync(new List<NewLogEventData> { new NewLogEventData { LogId = LogId, LineIndex = Metadata.MaxLineIndex, LineCount = 1, Severity = Severity } });
		}

		private async Task ParseEventsAsync(IJob Job, int BatchIdx, int StepIdx, string[] Lines)
		{
			LogId LogId = Job.Batches[BatchIdx].Steps[StepIdx].LogId!.Value;

			LogParserContext Context = new LogParserContext();
			Context.WorkspaceDir = WorkspaceDir;
			Context.PerforceStream = "//UE4/Main";
			Context.PerforceChange = 12345;

			await using (TestJsonLogger Logger = new TestJsonLogger(LogFileService, LogId))
			{
				using (LogParser Parser = new LogParser(Logger, Context, new List<string>()))
				{
					for (int Idx = 0; Idx < Lines.Length; Idx++)
					{
						Parser.WriteLine(Lines[Idx]);
					}
				}
			}
		}

		[TestMethod]
		public async Task DefaultIssueTest()
		{
			// #1
			// Scenario: Warning in first step
			// Expected: Default issues is created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				Assert.AreEqual("Warnings in Update Version Files", Issues[0].Summary);
			}

			// #2
			// Scenario: Errors in other steps on same job
			// Expected: Nodes are NOT added to issue
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await AddEvent(Job, 0, 1, new { });
				await UpdateCompleteStep(Job, 0, 1, JobStepOutcome.Failure);
				await AddEvent(Job, 0, 2, new { });
				await UpdateCompleteStep(Job, 0, 2, JobStepOutcome.Failure);

				List<IIssue> Issues = (await IssueService.FindIssuesAsync()).OrderBy(x => x.Summary).ToList();
				Assert.AreEqual(3, Issues.Count);

				Assert.AreEqual("Errors in Compile ShooterGameEditor Win64", Issues[0].Summary);
				Assert.AreEqual("Errors in Compile UnrealHeaderTool Win64", Issues[1].Summary);
				Assert.AreEqual("Warnings in Update Version Files", Issues[2].Summary);
			}

			// #3
			// Scenario: Subsequent jobs also error
			// Expected: Nodes are added to issue, but change outcome to error
			{
				IJob Job = CreateJob(MainStreamId, 110, "Test Build", Graph);
				await AddEvent(Job, 0, 0, new { });
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = (await IssueService.FindIssuesAsync()).OrderBy(x => x.Summary).ToList();
				Assert.AreEqual(3, Issues.Count);

				Assert.AreEqual("Errors in Compile ShooterGameEditor Win64", Issues[0].Summary);
				Assert.AreEqual("Errors in Compile UnrealHeaderTool Win64", Issues[1].Summary);
				Assert.AreEqual("Errors in Update Version Files", Issues[2].Summary);
			}

			// #4
			// Scenario: Subsequent jobs also error, but in different node
			// Expected: Additional error is created
			{
				IJob Job = CreateJob(MainStreamId, 110, "Test Build", Graph);
				await AddEvent(Job, 0, 3, new { });
				await UpdateCompleteStep(Job, 0, 3, JobStepOutcome.Warnings);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(4, Issues.Count);
			}

			// #5
			// Add a description to the issue
			{
				List<IIssue> Issues = await IssueService.FindIssuesAsync();

				IIssue Issue = Issues[0];
				await IssueService.UpdateIssueAsync(Issue.Id, Description: "Hello world!");
				IIssue? NewIssue = await IssueService.GetIssueAsync(Issue.Id);
				Assert.AreEqual(NewIssue?.Description, "Hello world!");
			}
		}

		[TestMethod]
		public async Task DefaultIssueTest2()
		{
			// #1
			// Scenario: Warning in first step
			// Expected: Default issues is created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, Issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", Issues[0].Summary);
			}

			// #2
			// Scenario: Same step errors
			// Expected: Issue state changes to error
			{
				IJob Job = CreateJob(MainStreamId, 110, "Test Build", Graph);
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Error) }, EventSeverity.Error);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);
				Assert.AreEqual(IssueSeverity.Error, Issues[0].Severity);

				Assert.AreEqual("Errors in Update Version Files", Issues[0].Summary);
			}
		}

		[TestMethod]
		public async Task AutoSdkWarningTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync(Resolved: false);
				Assert.AreEqual(0, OpenIssues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 in AutoSDK file
			// Expected: Creates issue, identifies source file correctly
			{
				string[] Lines =
				{
					@"  D:\build\AutoSDK\Sync\HostWin64\GDK\200604\Microsoft GDK\200604\GRDK\ExtensionLibraries\Xbox.Game.Chat.2.Cpp.API\DesignTime\CommonConfiguration\Neutral\Include\GameChat2Impl.h(90): warning C5043: 'xbox::services::game_chat_2::chat_manager::set_memory_callbacks': exception specification does not match previous declaration",
					@"  D:\build\AutoSDK\Sync\HostWin64\GDK\200604\Microsoft GDK\200604\GRDK\ExtensionLibraries\Xbox.Game.Chat.2.Cpp.API\DesignTime\CommonConfiguration\Neutral\Include\GameChat2.h(2083): note: see declaration of 'xbox::services::game_chat_2::chat_manager::set_memory_callbacks'",
				};

				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				ILogFile? Log = await LogFileService.GetLogFileAsync(Job.Batches[0].Steps[0].LogId!.Value);
				List<ILogEvent> Events = await LogFileService.FindLogEventsAsync(Log!);
				Assert.AreEqual(1, Events.Count);
				Assert.AreEqual(2, Events[0].LineCount);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(1, Spans.Count);
				Assert.AreEqual(1, Issue.Fingerprints.Count);
				Assert.AreEqual("Compile", Issue.Fingerprints[0].Type);
				Assert.AreEqual("Compile warnings in GameChat2Impl.h", Issue.Summary);
			}
		}

		[TestMethod]
		public async Task GenericErrorTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync(Resolved: false);
				Assert.AreEqual(0, OpenIssues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "fog.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(1, Spans.Count);
				Assert.AreEqual(1, Issue.Fingerprints.Count);
				Assert.AreEqual("Compile", Issue.Fingerprints[0].Type);
				Assert.AreEqual("Compile errors in fog.cpp", Issue.Summary);

				IIssueSpan Stream = Spans[0];
				Assert.AreEqual(105, Stream.LastSuccess?.Change);
				Assert.AreEqual(null, Stream.NextSuccess?.Change);

				IReadOnlyList<IIssueSuspect> Suspects = await IssueService.GetIssueSuspectsAsync(Issue);
				Suspects = Suspects.OrderBy(x => x.Id).ToList();
				Assert.AreEqual(3, Suspects.Count);

				Assert.AreEqual(75 /*115*/, Suspects[1].Change);
				Assert.AreEqual(JerryId, Suspects[1].AuthorId);
				//				Assert.AreEqual(75, Suspects[1].OriginatingChange);

				Assert.AreEqual(110, Suspects[2].Change);
				Assert.AreEqual(BobId, Suspects[2].AuthorId);
				//			Assert.AreEqual(null, Suspects[2].OriginatingChange);

				Assert.AreEqual(120, Suspects[0].Change);
				Assert.AreEqual(TimId, Suspects[0].AuthorId);
				//				Assert.AreEqual(null, Suspects[0].OriginatingChange);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync(Resolved: false);
				Assert.AreEqual(1, OpenIssues.Count);
			}

			// #3
			// Scenario: Job step succeeds at CL 110
			// Expected: Issue is updated to vindicate change at CL 110
			{
				IJob Job = CreateJob(MainStreamId, 110, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 1);

				IIssueSpan Stream = Spans[0];
				Assert.AreEqual(110, Stream.LastSuccess?.Change);
				Assert.AreEqual(null, Stream.NextSuccess?.Change);

				IReadOnlyList<IIssueSuspect> Suspects = (await IssueService.GetIssueSuspectsAsync(Issue)).OrderByDescending(x => x.Change).ToList();
				Assert.AreEqual(2, Suspects.Count);

				Assert.AreEqual(120, Suspects[0].Change);
				Assert.AreEqual(TimId, Suspects[0].AuthorId);

				Assert.AreEqual(75, Suspects[1].Change);
				Assert.AreEqual(JerryId, Suspects[1].AuthorId);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync(Resolved: false);
				Assert.AreEqual(1, OpenIssues.Count);
			}

			// #4
			// Scenario: Job step succeeds at CL 125
			// Expected: Issue is updated to narrow range to 115, 120
			{
				IJob Job = CreateJob(MainStreamId, 125, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync(Resolved: true);
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 1);

				IIssueSpan Stream = Spans[0];
				Assert.AreEqual(110, Stream.LastSuccess?.Change);
				Assert.AreEqual(125, Stream.NextSuccess?.Change);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync(Resolved: false);
				Assert.AreEqual(0, OpenIssues.Count);
			}

			// #5
			// Scenario: Additional error in same node at 115
			// Expected: Event is merged into existing issue
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "fog.cpp").FullName + @"(114): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				IJob Job = CreateJob(MainStreamId, 115, "Test Build", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync(Resolved: true);
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(1, Spans.Count);

				IIssueSpan Span = Spans[0];
				Assert.AreEqual(110, Span.LastSuccess?.Change);
				Assert.AreEqual(125, Span.NextSuccess?.Change);

				List<IIssueStep> Steps = await IssueService.GetIssueStepsAsync(Span);
				Assert.AreEqual(2, Steps.Count);
			}

			// #5
			// Scenario: Additional error in different node at 115
			// Expected: New issue is created
			{
				IJob Job = CreateJob(MainStreamId, 115, "Test Build", Graph);
				await AddEvent(Job, 0, 1, new { });
				await UpdateCompleteStep(Job, 0, 1, JobStepOutcome.Failure);

				List<IIssue> ResolvedIssues = await IssueService.FindIssuesAsync(Resolved: true);
				Assert.AreEqual(1, ResolvedIssues.Count);

				List<IIssue> UnresolvedIssues = await IssueService.FindIssuesAsync(Resolved: false);
				Assert.AreEqual(1, UnresolvedIssues.Count);
			}
		}

		[TestMethod]
		public async Task DefaultOwnerTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Compile Test", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				Perforce.Changes[MainStreamName][110].Files.Add("/Engine/Source/Boo.cpp");
				Perforce.Changes[MainStreamName][115].Files.Add("/Engine/Source/Foo.cpp");
				Perforce.Changes[MainStreamName][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob Job = CreateJob(MainStreamId, 120, "Compile Test", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				List<IIssueSuspect> Suspects = await IssueService.GetIssueSuspectsAsync(Issues[0]);

				List<UserId> PrimarySuspects = Suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(2, PrimarySuspects.Count);
				Assert.IsTrue(PrimarySuspects.Contains(JerryId)); // 115
				Assert.IsTrue(PrimarySuspects.Contains(TimId)); // 120
			}

			// #3
			// Scenario: Job step succeeds at CL 115
			// Expected: Creates issue, blames submitter at CL 120
			{
				IJob Job = CreateJob(MainStreamId, 115, "Compile Test", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				Assert.AreEqual(TimId, Issue.OwnerId);

				// Also check updating an issue doesn't clear the owner
				Assert.IsTrue(await IssueService.UpdateIssueAsync(Issue.Id));
				Assert.AreEqual(TimId, Issue!.OwnerId);
			}
		}

		[TestMethod]
		public async Task CompileIssueTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Compile Test", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "FOO.CPP").FullName + @"(170) : warning C6011: Dereferencing NULL pointer 'CurrentProperty'. : Lines: 159, 162, 163, 169, 170, 174, 176, 159, 162, 163, 169, 170",
					FileReference.Combine(WorkspaceDir, "foo.cpp").FullName + @"(170) : warning C6011: Dereferencing NULL pointer 'CurrentProperty'. : Lines: 159, 162, 163, 169, 170, 174, 176, 159, 162, 163, 169, 170"
				};

				IJob Job = CreateJob(MainStreamId, 120, "Compile Test", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				Assert.AreEqual(1, Issue.Fingerprints.Count);

				CompileIssueHandler Handler = new CompileIssueHandler();

				IIssueFingerprint Fingerprint = Issue.Fingerprints[0];
				Assert.AreEqual(Handler.Type, Fingerprint.Type);
				Assert.AreEqual(1, Fingerprint.Keys.Count);
				Assert.AreEqual("FOO.CPP", Fingerprint.Keys.First());
			}
		}

		[TestMethod]
		public async Task DeprecationTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Compile Test", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitter at CL 115 that introduced deprecation message
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "Consumer.h").FullName + @"(22): warning C4996: 'USimpleWheeledVehicleMovementComponent': PhysX is deprecated.Use the UChaosWheeledVehicleMovementComponent from the ChaosVehiclePhysics Plugin.Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.",
					FileReference.Combine(WorkspaceDir, "Deprecater.h").FullName + @"(16): note: see declaration of 'USimpleWheeledVehicleMovementComponent'"
				};

				Perforce.Changes[MainStreamName][110].Files.Add("/Engine/Source/Boo.cpp");
				Perforce.Changes[MainStreamName][115].Files.Add("/Engine/Source/Deprecater.h");
				Perforce.Changes[MainStreamName][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob Job = CreateJob(MainStreamId, 120, "Compile Test", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				List<IIssueSuspect> Suspects = await IssueService.GetIssueSuspectsAsync(Issues[0]);

				List<UserId> PrimarySuspects = Suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(1, PrimarySuspects.Count);
				Assert.AreEqual(JerryId, PrimarySuspects[0]); // 115
			}
		}

		[TestMethod]
		public async Task DeclineIssueTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Compile Test", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				Perforce.Changes[MainStreamName][110].Files.Add("/Engine/Source/Boo.cpp");
				Perforce.Changes[MainStreamName][115].Files.Add("/Engine/Source/Foo.cpp");
				Perforce.Changes[MainStreamName][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob Job = CreateJob(MainStreamId, 120, "Compile Test", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				List<IIssueSuspect> Suspects = await IssueService.GetIssueSuspectsAsync(Issues[0]);

				List<UserId> PrimarySuspects = Suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(2, PrimarySuspects.Count);
				Assert.IsTrue(PrimarySuspects.Contains(JerryId)); // 115
				Assert.IsTrue(PrimarySuspects.Contains(TimId)); // 120
			}

			// #3
			// Scenario: Tim declines the issue
			// Expected: Only suspect is Jerry, but owner is still unassigned
			{
				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);
				await IssueService.UpdateIssueAsync(Issues[0].Id, DeclinedById: TimId);

				Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				List<IIssueSuspect> Suspects = await IssueService.GetIssueSuspectsAsync(Issues[0]);

				List<UserId> PrimarySuspects = Suspects.Where(x => x.DeclinedAt == null).Select(x => x.AuthorId).ToList();
				Assert.AreEqual(1, PrimarySuspects.Count);
				Assert.AreEqual(JerryId, PrimarySuspects[0]); // 115
			}
		}

		[TestMethod]
		public async Task SymbolIssueTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitter at CL 115 due to file matching symbol name
			{
				string[] Lines =
				{
					@"  Foo.cpp.obj : error LNK2019: unresolved external symbol ""__declspec(dllimport) private: static class UClass * __cdecl UE::FOO::BAR"" (__UE__FOO_BAR) referenced in function ""class UPhysicalMaterial * __cdecl ConstructorHelpersInternal::FindOrLoadObject<class UPhysicalMaterial>(class FString &,unsigned int)"" (??$FindOrLoadObject@VUPhysicalMaterial@@@ConstructorHelpersInternal@@YAPEAVUPhysicalMaterial@@AEAVFString@@I@Z)"
				};

				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 1);
				Assert.AreEqual(Issue.Fingerprints.Count, 1);
				Assert.AreEqual(Issue.Fingerprints[0].Type, "Symbol");

				IIssueSpan Stream = Spans[0];
				Assert.AreEqual(105, Stream.LastSuccess?.Change);
				Assert.AreEqual(null, Stream.NextSuccess?.Change);

				List<IIssueSuspect> Suspects = await IssueService.GetIssueSuspectsAsync(Issues[0]);

				List<UserId> PrimarySuspects = Suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(1, PrimarySuspects.Count);
				Assert.AreEqual(JerryId, PrimarySuspects[0]); // 115 = foo.cpp
			}
		}

		[TestMethod]
		public async Task SymbolIssueTest2()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 on different platforms
			// Expected: Creates single issue
			{
				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);

				string[] Lines1 =
				{
					@"Undefined symbols for architecture x86_64:",
					@"  ""Metasound::FTriggerOnThresholdOperator::DefaultThreshold"", referenced from:",
					@"      Metasound::FTriggerOnThresholdOperator::DeclareVertexInterface() in Module.MetasoundStandardNodes.cpp.o",
					@"ld: symbol(s) not found for architecture x86_64",
					@"clang: error: linker command failed with exit code 1 (use -v to see invocation)"
				};
				await ParseEventsAsync(Job, 0, 0, Lines1);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				// NB. This is a new step and has not succeeded before, but can still be merged with the issue above.
				string[] Lines2 =
				{
					@"ld.lld: error: undefined symbol: Metasound::FTriggerOnThresholdOperator::DefaultThreshold",
					@">>> referenced by Module.MetasoundStandardNodes.cpp",
					@">>>               D:/build/++UE5/Sync/Engine/Plugins/Runtime/Metasound/Intermediate/Build/Linux/B4D820EA/UnrealEditor/Debug/MetasoundStandardNodes/Module.MetasoundStandardNodes.cpp.o:(Metasound::FTriggerOnThresholdOperator::DeclareVertexInterface())",
					@"clang++: error: linker command failed with exit code 1 (use -v to see invocation)",
				};
				await ParseEventsAsync(Job, 0, 1, Lines2);
				await UpdateCompleteStep(Job, 0, 1, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 2);
				Assert.AreEqual(Issue.Fingerprints.Count, 1);
				Assert.AreEqual(Issue.Fingerprints[0].Type, "Symbol");

				IIssueSpan Span1 = Spans[0];
				Assert.AreEqual(105, Span1.LastSuccess?.Change);
				Assert.AreEqual(null, Span1.NextSuccess?.Change);

				IIssueSpan Span2 = Spans[1];
				Assert.AreEqual(null, Span2.LastSuccess?.Change);
				Assert.AreEqual(null, Span2.NextSuccess?.Change);
			}
		}

		[TestMethod]
		public async Task LinkerIssueTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 on different platforms
			// Expected: Creates single issue
			{
				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);

				string[] Lines =
				{
					@"  DatasmithDirectLink.cpp.obj : error LNK2019: unresolved external symbol ""enum DirectLink::ECommunicationStatus __cdecl DirectLink::ValidateCommunicationStatus(void)"" (?ValidateCommunicationStatus@DirectLink@@YA?AW4ECommunicationStatus@1@XZ) referenced in function ""public: static int __cdecl FDatasmithDirectLink::ValidateCommunicationSetup(void)"" (?ValidateCommunicationSetup@FDatasmithDirectLink@@SAHXZ)",
					@"  Engine\Binaries\Win64\UE4Editor-DatasmithExporter.dll: fatal error LNK1120: 1 unresolved externals",
				};
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 1);
				Assert.AreEqual(Issue.Fingerprints.Count, 1);
				Assert.AreEqual(Issue.Fingerprints[0].Type, "Symbol");

				IIssueSpan Span = Spans[0];
				Assert.AreEqual(105, Span.LastSuccess?.Change);
				Assert.AreEqual(null, Span.NextSuccess?.Change);
			}
		}

		[TestMethod]
		public async Task MaskIssueTest()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, Issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 with compile & link error
			// Expected: Creates one issue for compile error
			{
				string[] Lines =
				{
					FileReference.Combine(WorkspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
					@"  Foo.cpp.obj : error LNK2019: unresolved external symbol ""__declspec(dllimport) private: static class UClass * __cdecl UE::FOO::BAR"" (__UE__FOO_BAR) referenced in function ""class UPhysicalMaterial * __cdecl ConstructorHelpersInternal::FindOrLoadObject<class UPhysicalMaterial>(class FString &,unsigned int)"" (??$FindOrLoadObject@VUPhysicalMaterial@@@ConstructorHelpersInternal@@YAPEAVUPhysicalMaterial@@AEAVFString@@I@Z)"
				};

				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(1, Spans.Count);
				Assert.AreEqual(1, Issue.Fingerprints.Count);
				Assert.AreEqual("Compile", Issue.Fingerprints[0].Type);
			}
		}

		[TestMethod]
		public async Task MissingCopyrightTest()
		{
			string[] Lines =
			{
				@"WARNING: Engine\Source\Programs\UnrealBuildTool\ProjectFiles\Rider\ToolchainInfo.cs: Missing copyright boilerplate"
			};

			IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
			await ParseEventsAsync(Job, 0, 0, Lines);
			await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

			List<IIssue> Issues = await IssueService.FindIssuesAsync();
			Assert.AreEqual(1, Issues.Count);

			IIssue Issue = Issues[0];
			List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
			Assert.AreEqual(1, Spans.Count);
			Assert.AreEqual(1, Issue.Fingerprints.Count);
			Assert.AreEqual("Copyright", Issue.Fingerprints[0].Type);
			Assert.AreEqual("Missing copyright notice in ToolchainInfo.cs", Issue.Summary);
		}


		[TestMethod]
		public async Task AddSpanToIssueTest()
		{
			// Create the first issue
			IIssue IssueA;
			IIssueSpan SpanA;
			{
				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);

				string[] Lines =
				{
					@"  DatasmithDirectLink.cpp.obj : error LNK2019: unresolved external symbol ""enum DirectLink::ECommunicationStatus __cdecl DirectLink::ValidateCommunicationStatus(void)"" (?ValidateCommunicationStatus@DirectLink@@YA?AW4ECommunicationStatus@1@XZ) referenced in function ""public: static int __cdecl FDatasmithDirectLink::ValidateCommunicationSetup(void)"" (?ValidateCommunicationSetup@FDatasmithDirectLink@@SAHXZ)",
				};
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IssueA = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(IssueA);
				Assert.AreEqual(Spans.Count, 1);
				SpanA = Spans[0];
			}

			// Create the second issue
			IIssue IssueB;
			IIssueSpan SpanB;
			{
				string[] Lines =
				{
					@"WARNING: Engine\Source\Programs\UnrealBuildTool\ProjectFiles\Rider\ToolchainInfo.cs: Missing copyright boilerplate"
				};

				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
				await ParseEventsAsync(Job, 0, 0, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Issues.RemoveAll(x => x.Id == IssueA.Id);
				Assert.AreEqual(1, Issues.Count);

				IssueB = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(IssueB);
				Assert.AreEqual(1, Spans.Count);
				SpanB = Spans[0];
			}

			// Add SpanB to IssueA
			{
				await IssueService.UpdateIssueAsync(IssueA.Id, AddSpanIds: new List<ObjectId> { SpanB.Id });

				IIssue NewIssueA = (await IssueService.GetIssueAsync(IssueA.Id))!;
				Assert.IsNull(NewIssueA.VerifiedAt);
				Assert.IsNull(NewIssueA.ResolvedAt);
				Assert.AreEqual(2, NewIssueA.Fingerprints.Count);
				List<IIssueSpan> NewSpansA = await IssueService.GetIssueSpansAsync(NewIssueA!);
				Assert.AreEqual(2, NewSpansA.Count);
				Assert.AreEqual(NewIssueA.Id, NewSpansA[0].IssueId);
				Assert.AreEqual(NewIssueA.Id, NewSpansA[1].IssueId);

				IIssue NewIssueB = (await IssueService.GetIssueAsync(IssueB.Id))!;
				Assert.IsNotNull(NewIssueB.VerifiedAt);
				Assert.IsNotNull(NewIssueB.ResolvedAt);
				Assert.AreEqual(0, NewIssueB.Fingerprints.Count);
				List<IIssueSpan> NewSpansB = await IssueService.GetIssueSpansAsync(NewIssueB);
				Assert.AreEqual(0, NewSpansB.Count);
			}

			// Add SpanA and SpanB to IssueB
			{
				await IssueService.UpdateIssueAsync(IssueB.Id, AddSpanIds: new List<ObjectId> { SpanA.Id, SpanB.Id });

				IIssue NewIssueA = (await IssueService.GetIssueAsync(IssueA.Id))!;
				Assert.IsNotNull(NewIssueA.VerifiedAt);
				Assert.IsNotNull(NewIssueA.ResolvedAt);
				Assert.AreEqual(0, NewIssueA.Fingerprints.Count);
				List<IIssueSpan> NewSpansA = await IssueService.GetIssueSpansAsync(NewIssueA);
				Assert.AreEqual(0, NewSpansA.Count);

				IIssue NewIssueB = (await IssueService.GetIssueAsync(IssueB.Id))!;
				Assert.IsNull(NewIssueB.VerifiedAt);
				Assert.IsNull(NewIssueB.ResolvedAt);
				Assert.AreEqual(2, NewIssueB.Fingerprints.Count);
				List<IIssueSpan> NewSpansB = await IssueService.GetIssueSpansAsync(NewIssueB!);
				Assert.AreEqual(2, NewSpansB.Count);
				Assert.AreEqual(NewIssueB.Id, NewSpansB[0].IssueId);
				Assert.AreEqual(NewIssueB.Id, NewSpansB[1].IssueId);
			}
		}

		[TestMethod]
		public async Task GauntletIssueTest()
		{
			string[] Lines =
			{
				@"  Error: EngineTest.RunTests Group:HLOD (Win64 Development EditorGame) result=Failed",
				@"    # EngineTest.RunTests Group:HLOD Report",
				@"    ----------------------------------------",
				@"    ### Process Role: Editor (Win64 Development)",
				@"    ----------------------------------------",
				@"    ##### Result: Abnormal Exit: Reason=3/24 tests failed, Code=-1",
				@"    FatalErrors: 0, Ensures: 0, Errors: 8, Warnings: 20, Hash: 0",
				@"    ##### Artifacts",
				@"    Log: P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor\Saved_1\Editor\EditorOutput.log",
				@"    Commandline: d:\Build\++UE5\Sync\EngineTest\EngineTest.uproject   -gauntlet  -unattended  -stdout  -AllowStdOutLogVerbosity  -gauntlet.heartbeatperiod=30  -NoWatchdog  -FORCELOGFLUSH  -CrashForUAT  -buildmachine  -ReportExportPath=""P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor""  -ExecCmds=""Automation RunTests Group:HLOD; Quit;""  -ddc=default  -userdir=""d:\Build\++UE5\Sync/Tests\DeviceCache\Win64\LocalDevice0_UserDir""",
				@"    P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor\Saved_1\Editor",
				@"    ----------------------------------------",
				@"    ## Summary",
				@"    ### EngineTest.RunTests Group:HLOD Failed",
				@"    ### Editor: 3/24 tests failed",
				@"    See below for logs and any callstacks",
				@"    Context: Win64 Development EditorGame",
				@"    FatalErrors: 0, Ensures: 0, Errors: 8, Warnings: 20",
				@"    Result: Failed, ResultHash: 0",
				@"    21 of 24 tests passed",
				@"    ### The following tests failed:",
				@"    ##### SectionFlags: SectionFlags",
				@"    * LogAutomationController: Building static mesh SectionFlags... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Building static mesh SectionFlags... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SectionFlags_LOD_0_None' test failed, Screenshots were different!  Global Difference = 0.058361, Max Local Difference = 0.821376 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    ##### SimpleMerge: SimpleMerge",
				@"    * LogAutomationController: Building static mesh SM_TeapotHLOD... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Building static mesh SM_TeapotHLOD... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_0_None' was similar!  Global Difference = 0.000298, Max Local Difference = 0.010725 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_1_None' test failed, Screenshots were different!  Global Difference = 0.006954, Max Local Difference = 0.129438 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_2_None' test failed, Screenshots were different!  Global Difference = 0.007732, Max Local Difference = 0.127959 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_3_None' test failed, Screenshots were different!  Global Difference = 0.009140, Max Local Difference = 0.172337 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_0_BaseColor' was similar!  Global Difference = 0.000000, Max Local Difference = 0.000000 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_1_BaseColor' was similar!  Global Difference = 0.002068, Max Local Difference = 0.045858 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_2_BaseColor' was similar!  Global Difference = 0.002377, Max Local Difference = 0.045858 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_3_BaseColor' was similar!  Global Difference = 0.002647, Max Local Difference = 0.057322 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    ##### SingleLODMerge: SingleLODMerge",
				@"    * LogAutomationController: Building static mesh Pencil2... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Building static mesh Pencil2... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SingleLODMerge_LOD_0_BaseColor' test failed, Screenshots were different!  Global Difference = 0.013100, Max Local Difference = 0.131657 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]"
			};

			// #1
			// Scenario: Job with multiple GJob step fails at CL 120 with compile & link error
			// Expected: Creates one issue for compile error
			{
				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph);
				await ParseAsync(Job.Batches[0].Steps[0].LogId!.Value, Lines);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Failure);

				ILogFile? Log = await LogFileService.GetLogFileAsync(Job.Batches[0].Steps[0].LogId!.Value);
				List<ILogEvent> Events = await LogFileService.FindLogEventsAsync(Log!);
				Assert.AreEqual(1, Events.Count);
				ILogEventData EventData = await LogFileService.GetEventDataAsync(Log!, Events[0].LineIndex, Events[0].LineCount);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);

				IIssue Issue = Issues[0];
				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 1);
				Assert.AreEqual(Issue.Fingerprints.Count, 1);
				Assert.AreEqual(Issue.Fingerprints[0].Type, "Gauntlet");
				Assert.AreEqual(Issue.Summary, "HLOD test failures: SectionFlags, SimpleMerge and SingleLODMerge");
			}
		}

		[TestMethod]
		public async Task FixFailedTest()
		{
			int IssueId;

			// #1
			// Scenario: Warning in first step
			// Expected: Default issue is created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, Issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", Issues[0].Summary);

				IssueId = Issues[0].Id;
			}

			// #2
			// Scenario: Issue is marked fixed
			// Expected: Resolved time, owner is set
			{
				await IssueService.UpdateIssueAsync(IssueId, ResolvedById: BobId);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, OpenIssues.Count);

				IIssue Issue = (await IssueService.GetIssueAsync(IssueId))!;
				Assert.IsNotNull(Issue.ResolvedAt);
				Assert.AreEqual(BobId, Issue.OwnerId);
				Assert.AreEqual(BobId, Issue.ResolvedById);
			}

			// #3
			// Scenario: Issue recurs an hour later
			// Expected: Issue is still marked as resolved
			{
				IJob Job = CreateJob(MainStreamId, 110, "Test Build", Graph, TimeSpan.FromHours(1.0));
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, OpenIssues.Count);

				IIssue Issue = (await IssueService.GetIssueAsync(IssueId))!;
				Assert.IsNotNull(Issue.ResolvedAt);
				Assert.AreEqual(BobId, Issue.OwnerId);
				Assert.AreEqual(BobId, Issue.ResolvedById);
			}

			// #4
			// Scenario: Issue recurs a day later at the same change
			// Expected: Issue is reopened
			{
				IJob Job = CreateJob(MainStreamId, 110, "Test Build", Graph, TimeSpan.FromHours(25.0));
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, OpenIssues.Count);

				IIssue Issue = OpenIssues[0];
				Assert.AreEqual(IssueId, Issue.Id);
				Assert.IsNull(Issue.ResolvedAt);
				Assert.AreEqual(BobId, Issue.OwnerId);
				Assert.IsNull(Issue.ResolvedById);
			}

			// #5
			// Scenario: Issue is marked fixed again, at a particular changelist
			// Expected: Resolved time, owner is set
			{
				await IssueService.UpdateIssueAsync(IssueId, ResolvedById: BobId, FixChange: 115);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, OpenIssues.Count);

				IIssue Issue = (await IssueService.GetIssueAsync(IssueId))!;
				Assert.IsNotNull(Issue.ResolvedAt);
				Assert.AreEqual(BobId, Issue.OwnerId);
				Assert.AreEqual(BobId, Issue.ResolvedById);
			}

			// #6
			// Scenario: Issue fails again at a later changelist
			// Expected: Issue is reopened
			{
				IJob Job = CreateJob(MainStreamId, 120, "Test Build", Graph, TimeSpan.FromHours(25.0));
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, OpenIssues.Count);

				IIssue Issue = OpenIssues[0];
				Assert.AreEqual(IssueId, Issue.Id);
				Assert.IsNull(Issue.ResolvedAt);
				Assert.AreEqual(BobId, Issue.OwnerId);
				Assert.IsNull(Issue.ResolvedById);
			}

			// #7
			// Scenario: Issue is marked fixed again, at a particular changelist
			// Expected: Resolved time, owner is set
			{
				await IssueService.UpdateIssueAsync(IssueId, ResolvedById: BobId, FixChange: 125);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, OpenIssues.Count);

				IIssue Issue = (await IssueService.GetIssueAsync(IssueId))!;
				Assert.IsNotNull(Issue.ResolvedAt);
				Assert.AreEqual(BobId, Issue.OwnerId);
				Assert.AreEqual(BobId, Issue.ResolvedById);
			}

			// #8
			// Scenario: Issue succeeds at a later changelist
			// Expected: Issue remains closed
			{
				IJob Job = CreateJob(MainStreamId, 125, "Test Build", Graph, TimeSpan.FromHours(25.0));
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(0, OpenIssues.Count);
			}

			// #9
			// Scenario: Issue fails at a later changelist
			// Expected: New issue is opened
			{
				IJob Job = CreateJob(MainStreamId, 130, "Test Build", Graph, TimeSpan.FromHours(25.0));
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> OpenIssues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, OpenIssues.Count);

				IIssue Issue = OpenIssues[0];
				Assert.AreNotEqual(IssueId, Issue.Id);
			}
		}

		[TestMethod]
		public async Task AutoResolveTest()
		{
			int IssueId;

			// #1
			// Scenario: Warning in first step
			// Expected: Default issue is created
			{
				IJob Job = CreateJob(MainStreamId, 105, "Test Build", Graph);
				await AddEvent(Job, 0, 0, new { level = nameof(LogLevel.Warning) }, EventSeverity.Warning);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Warnings);

				List<IIssue> Issues = await IssueService.FindIssuesAsync();
				Assert.AreEqual(1, Issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, Issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", Issues[0].Summary);

				IssueId = Issues[0].Id;
			}

			// #2
			// Scenario: Job succeeds
			// Expected: Issue is marked as resolved
			{
				IJob Job = CreateJob(MainStreamId, 115, "Test Build", Graph);
				await UpdateCompleteStep(Job, 0, 0, JobStepOutcome.Success);

				IIssue? Issue = await IssueService.GetIssueAsync(IssueId);
				Assert.IsNotNull(Issue!.ResolvedAt);

				List<IIssueSpan> Spans = await IssueService.GetIssueSpansAsync(Issue);
				Assert.AreEqual(Spans.Count, 1);
			}
		}

		private async Task ParseAsync(LogId LogId, string[] Lines)
		{
			LogParserContext Context = new LogParserContext();
			Context.WorkspaceDir = DirectoryReference.GetCurrentDirectory();
			Context.PerforceStream = "//UE4/Main";
			Context.PerforceChange = 12345;

			await using (TestJsonLogger Logger = new TestJsonLogger(LogFileService, LogId))
			{
				using (LogParser Parser = new LogParser(Logger, Context, new List<string>()))
				{
					for (int Idx = 0; Idx < Lines.Length; Idx++)
					{
						Parser.WriteLine(Lines[Idx]);
					}
				}
			}
		}
	}
}
