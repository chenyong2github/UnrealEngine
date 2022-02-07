// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Notifications;
using HordeServer.Notifications.Impl;
using HordeServer.Utilities;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using StackExchange.Redis;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;

	public class FakeNotificationSink : INotificationSink
	{
		public List<JobScheduledNotification> JobScheduledNotifications = new();
		public int JobScheduledCallCount;
		
		public Task NotifyJobScheduledAsync(List<JobScheduledNotification> Notifications)
		{
			JobScheduledNotifications.AddRange(Notifications);
			JobScheduledCallCount++;
			return Task.CompletedTask;
		}

		public Task NotifyJobCompleteAsync(IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome) { throw new NotImplementedException(); }
		public Task NotifyJobCompleteAsync(IUser User, IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome) { throw new NotImplementedException(); }
		public Task NotifyJobStepCompleteAsync(IUser User, IStream JobStream, IJob Job, IJobStepBatch Batch, IJobStep Step, INode Node, List<ILogEventData> JobStepEventData) { throw new NotImplementedException(); }
		public Task NotifyLabelCompleteAsync(IUser User, IJob Job, IStream Stream, ILabel Label, int LabelIdx, LabelOutcome Outcome, List<(string, JobStepOutcome, Uri)> StepData) { throw new NotImplementedException(); }
		public Task NotifyIssueUpdatedAsync(IIssue Issue) { throw new NotImplementedException(); }
		public Task NotifyConfigUpdateFailureAsync(string ErrorMessage, string FileName, int? Change = null, IUser? Author = null, string? Description = null) { throw new NotImplementedException(); }
		public Task NotifyDeviceServiceAsync(string Message, IDevice? Device = null, IDevicePool? Pool = null, IStream? Stream = null, IJob? Job = null, IJobStep? Step = null, INode? Node = null, IUser? User = null) { throw new NotImplementedException(); }
	}
	

	[TestClass]
	public class NotificationServiceTests : TestSetup
	{
		public IJob CreateJob(StreamId StreamId, int Change, string Name, IGraph Graph)
		{
			JobId JobId = new JobId("5ec16da1774cb4000107c2c1");

			List<IJobStepBatch> Batches = new List<IJobStepBatch>();
			for (int GroupIdx = 0; GroupIdx < Graph.Groups.Count; GroupIdx++)
			{
				INodeGroup Group = Graph.Groups[GroupIdx];

				List<IJobStep> Steps = new List<IJobStep>();
				for (int NodeIdx = 0; NodeIdx < Group.Nodes.Count; NodeIdx++)
				{
					SubResourceId StepId = new SubResourceId((ushort)((GroupIdx * 100) + NodeIdx));

					Mock<IJobStep> Step = new Mock<IJobStep>(MockBehavior.Strict);
					Step.SetupGet(x => x.Id).Returns(StepId);
					Step.SetupGet(x => x.NodeIdx).Returns(NodeIdx);

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
			Job.SetupGet(x => x.Change).Returns(Change);
			Job.SetupGet(x => x.Batches).Returns(Batches);
			return Job.Object;
		}
		
		[TestMethod]
		public async Task NotifyJobScheduled()
		{
			FakeNotificationSink FakeSink = new();
			NotificationService Service = GetNotificationService(FakeSink);
			Fixture Fixture = await CreateFixtureAsync();
			IPool Pool = await PoolService.CreatePoolAsync("BogusPool", Properties: new Dictionary<string, string>());

			Assert.AreEqual(0, FakeSink.JobScheduledNotifications.Count);
			Service.NotifyJobScheduled(Pool, false, Fixture.Job1, Fixture.Graph, SubResourceId.Random());
			Service.NotifyJobScheduled(Pool, false, Fixture.Job2, Fixture.Graph, SubResourceId.Random());
			
			// Currently no good way to wait for NotifyJobScheduled() to complete as the execution is completely async in background task (see ExecuteAsync)
			await Task.Delay(1000);
			await Clock.AdvanceAsync(Service.NotificationBatchInterval + TimeSpan.FromMinutes(5));
			Assert.AreEqual(2, FakeSink.JobScheduledNotifications.Count);
			Assert.AreEqual(1, FakeSink.JobScheduledCallCount);
		}

		private NotificationService GetNotificationService(INotificationSink? Sink)
		{
			ILogger<NotificationService> Logger = ServiceProvider.GetRequiredService<ILogger<NotificationService>>();
			IDatabase Database = GetRedisConnectionPool().GetDatabase();
			List<INotificationSink> Sinks = new ();
			if (Sink != null) Sinks.Add(Sink);
			
			NotificationService Service = new NotificationService(
				Sinks, ServerSettingsMon, Logger, GraphCollection, SubscriptionCollection,
				NotificationTriggerCollection, UserCollection, JobService, StreamService, IssueService, LogFileService,
				new NoOpDogStatsd(), Database, Clock);

			Service.Ticker.StartAsync().Wait(5000);
			return Service;
		}

		//public void NotifyLabelUpdate(IJob Job, IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates)
		//{
		//	// If job has any label trigger IDs, send label complete notifications if needed
		//	if (Job.LabelIdxToTriggerId.Any())
		//	{
		//		EnqueueTask(() => SendAllLabelNotificationsAsync(Job, OldLabelStates, NewLabelStates));
		//	}
		//}

		//private async Task SendAllLabelNotificationsAsync(IJob Job, IReadOnlyList<(LabelState State, LabelOutcome Outcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates)
		//{
		//	IStream? Stream = await StreamService.GetStreamAsync(Job.StreamId);
		//	if (Stream == null)
		//	{
		//		return;
		//	}

		//	IGraph? Graph = await GraphCollection.GetAsync(Job.GraphHash);
		//	if (Graph == null)
		//	{
		//		return;
		//	}

		//	IReadOnlyDictionary<NodeRef, IJobStep> StepForNode = Job.GetStepForNodeMap();
		//	for (int LabelIdx = 0; LabelIdx < Graph.Labels.Count; ++LabelIdx)
		//	{
		//		(LabelState State, LabelOutcome Outcome) OldLabel = OldLabelStates[LabelIdx];
		//		(LabelState State, LabelOutcome Outcome) NewLabel = NewLabelStates[LabelIdx];
		//		if (OldLabel != NewLabel)
		//		{
		//			// If the state transitioned from Unspecified to Running, don't update unless the outcome also changed.
		//			if (OldLabel.State == LabelState.Unspecified && NewLabel.State == LabelState.Running && OldLabel.Outcome == NewLabel.Outcome)
		//			{
		//				continue;
		//			}

		//			// If the label isn't complete, don't report on outcome changing to success, this will be reported when the label state becomes complete.
		//			if (NewLabel.State != LabelState.Complete && NewLabel.Outcome == LabelOutcome.Success)
		//			{
		//				return;
		//			}

		//			bool bFireTrigger = NewLabel.State == LabelState.Complete;
		//			INotificationTrigger? Trigger = await GetNotificationTrigger(Job.LabelIdxToTriggerId[LabelIdx], bFireTrigger);
		//			if (Trigger == null)
		//			{
		//				continue;
		//			}

		//			await SendLabelUpdateNotificationsAsync(Job, Stream, Graph, StepForNode, Graph.Labels[LabelIdx], NewLabel.State, NewLabel.Outcome, Trigger);
		//		}
		//	}
		//}
	}
}
