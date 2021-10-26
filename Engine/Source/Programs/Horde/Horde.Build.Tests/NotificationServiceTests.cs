// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using Moq;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;

	[TestClass]
	public class NotificationServiceTests
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
		[Ignore]
		public Task NotifyLabelUpdateTests()
		{
			// #1 
			return Task.CompletedTask;
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
