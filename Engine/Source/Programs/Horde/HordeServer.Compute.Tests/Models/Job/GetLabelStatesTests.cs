// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using Moq;
using HordeServer.Utilities;

namespace HordeServerTests
{
	using JobId = ObjectId<IJob>;

	[TestClass]
	public class GetLabelStatesTests
	{
		public static INode MockNode(string Name)
		{
			Mock<INode> Node = new Mock<INode>();
			Node.SetupGet(x => x.Name).Returns(Name);
			return Node.Object;
		}

		public static ILabel MockLabel(string LabelName, List<NodeRef> Nodes)
		{
			Mock<ILabel> Label = new Mock<ILabel>();
			Label.SetupGet(x => x.DashboardName).Returns(LabelName);
			Label.SetupGet(x => x.IncludedNodes).Returns(Nodes);
			Label.SetupGet(x => x.RequiredNodes).Returns(Nodes);
			return Label.Object;
		}

		public static IGraph CreateGraph()
		{
			List<INode> Nodes1 = new List<INode>();
			Nodes1.Add(MockNode("Update Version Files"));
			Nodes1.Add(MockNode("Compile UnrealHeaderTool Win64"));
			Nodes1.Add(MockNode("Compile ShooterGameEditor Win64"));
			Nodes1.Add(MockNode("Cook ShooterGame Win64"));

			List<INode> Nodes2 = new List<INode>();
			Nodes2.Add(MockNode("Compile UnrealHeaderTool Mac"));
			Nodes2.Add(MockNode("Compile ShooterGameEditor Mac"));
			Nodes2.Add(MockNode("Cook ShooterGame Mac"));

			List<ILabel> Labels = new List<ILabel>();
			Labels.Add(MockLabel("Label_AllGroup1Nodes", new List<NodeRef>() { new NodeRef(0, 0), new NodeRef(0, 1), new NodeRef(0, 2), new NodeRef(0, 3) }));
			Labels.Add(MockLabel("Label_Win64UHTOnly", new List<NodeRef>() { new NodeRef(0, 1) }));
			Labels.Add(MockLabel("Label_Group1TwoNodes", new List<NodeRef>() { new NodeRef(0, 0), new NodeRef(0, 3) }));
			Labels.Add(MockLabel("Label_BothGroups", new List<NodeRef>() { new NodeRef(0, 0), new NodeRef(1, 0) }));

			Mock<INodeGroup> Group1 = new Mock<INodeGroup>(MockBehavior.Strict);
			Group1.SetupGet(x => x.Nodes).Returns(Nodes1);

			Mock<INodeGroup> Group2 = new Mock<INodeGroup>(MockBehavior.Strict);
			Group2.SetupGet(x => x.Nodes).Returns(Nodes2);

			Mock<IGraph> GraphMock = new Mock<IGraph>(MockBehavior.Strict);
			GraphMock.SetupGet(x => x.Groups).Returns(new List<INodeGroup> { Group1.Object, Group2.Object });
			GraphMock.SetupGet(x => x.Labels).Returns(Labels);
			return GraphMock.Object;
		}

		public static IJob CreateGetLabelStatesJob(string Name, IGraph Graph, List<(JobStepState, JobStepOutcome)> JobStepData)
		{
			JobId JobId = JobId.Parse("5ec16da1774cb4000107c2c1");
			List<IJobStepBatch> Batches = new List<IJobStepBatch>();
			int DataIdx = 0;
			for (int GroupIdx = 0; GroupIdx < Graph.Groups.Count; GroupIdx++)
			{
				INodeGroup Group = Graph.Groups[GroupIdx];

				List<IJobStep> Steps = new List<IJobStep>();
				for (int NodeIdx = 0; NodeIdx < Group.Nodes.Count; NodeIdx++, DataIdx++)
				{
					SubResourceId StepId = new SubResourceId((ushort)((GroupIdx * 100) + NodeIdx));

					Mock<IJobStep> Step = new Mock<IJobStep>(MockBehavior.Strict);
					Step.SetupGet(x => x.Id).Returns(StepId);
					Step.SetupGet(x => x.NodeIdx).Returns(NodeIdx);
					Step.SetupGet(x => x.State).Returns(JobStepData[DataIdx].Item1);
					Step.SetupGet(x => x.Outcome).Returns(JobStepData[DataIdx].Item2);
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
			Job.SetupGet(x => x.Batches).Returns(Batches);
			return Job.Object;
		}

		public static List<(JobStepState, JobStepOutcome)> GetJobStepDataList(JobStepState State, JobStepOutcome Outcome)
		{
			List<(JobStepState, JobStepOutcome)> Data = new List<(JobStepState, JobStepOutcome)>();
			for (int i = 0; i < 7; ++i)
			{
				Data.Add((State, Outcome));
			}
			return Data;
		}

		[TestMethod]
		public void GetLabelStates_InitialStateTest()
		{
			// Verify initial state. All job steps are unspecified, so all labels unspecified.
			IGraph Graph = CreateGraph();
			IJob Job = CreateGetLabelStatesJob("Job", Graph, GetJobStepDataList(JobStepState.Unspecified, JobStepOutcome.Unspecified));
			IReadOnlyList<(LabelState, LabelOutcome)> LabelStates = Job.GetLabelStates(Graph);
			Assert.AreEqual(Graph.Labels.Count, LabelStates.Count);
			foreach ((LabelState State, LabelOutcome Outcome) Result in LabelStates)
			{
				Assert.IsTrue(Result.State == LabelState.Running);
				Assert.IsTrue(Result.Outcome == LabelOutcome.Success);
			}
		}

		[TestMethod]
		public void GetLabelStates_RunningStateTest()
		{
			// Verify that if any nodes are in the ready or running state that the approrpirate labels switch to running.
			List<(JobStepState, JobStepOutcome)> JobStepData = GetJobStepDataList(JobStepState.Unspecified, JobStepOutcome.Unspecified);
			JobStepData[3] = (JobStepState.Ready, JobStepOutcome.Unspecified);
			JobStepData[4] = (JobStepState.Running, JobStepOutcome.Unspecified);
			IGraph Graph = CreateGraph();
			IJob Job = CreateGetLabelStatesJob("Job", Graph, JobStepData);
			IReadOnlyList<(LabelState State, LabelOutcome Outcome)> LabelStates = Job.GetLabelStates(Graph);
			Assert.AreEqual(Graph.Labels.Count, LabelStates.Count);
			Assert.IsTrue(LabelStates[0].State == LabelState.Running);
			Assert.IsTrue(LabelStates[0].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[1].State == LabelState.Running);
			Assert.IsTrue(LabelStates[1].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[2].State == LabelState.Running);
			Assert.IsTrue(LabelStates[2].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[3].State == LabelState.Running);
			Assert.IsTrue(LabelStates[3].Outcome == LabelOutcome.Success);
		}

		[TestMethod]
		public void GetLabelStates_WarningOutcomeTest()
		{
			// Verify that if a step has a warning, any applicable labels are set to warning.
			List<(JobStepState, JobStepOutcome)> JobStepData = GetJobStepDataList(JobStepState.Ready, JobStepOutcome.Unspecified);
			JobStepData[3] = (JobStepState.Running, JobStepOutcome.Warnings);
			IGraph Graph = CreateGraph();
			IJob Job = CreateGetLabelStatesJob("Job", Graph, JobStepData);
			IReadOnlyList<(LabelState State, LabelOutcome Outcome)> LabelStates = Job.GetLabelStates(Graph);
			Assert.AreEqual(Graph.Labels.Count, LabelStates.Count);
			Assert.IsTrue(LabelStates[0].State == LabelState.Running);
			Assert.IsTrue(LabelStates[0].Outcome == LabelOutcome.Warnings);
			Assert.IsTrue(LabelStates[1].State == LabelState.Running);
			Assert.IsTrue(LabelStates[1].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[2].State == LabelState.Running);
			Assert.IsTrue(LabelStates[2].Outcome == LabelOutcome.Warnings);
			Assert.IsTrue(LabelStates[3].State == LabelState.Running);
			Assert.IsTrue(LabelStates[3].Outcome == LabelOutcome.Success);
		}

		[TestMethod]
		public void GetLabelStates_ErrorOutcomeTest()
		{
			// Verify that if a step has an error, any applicable labels are set to failure.
			List<(JobStepState, JobStepOutcome)> JobStepData = GetJobStepDataList(JobStepState.Ready, JobStepOutcome.Unspecified);
			JobStepData[2] = (JobStepState.Running, JobStepOutcome.Warnings);
			JobStepData[4] = (JobStepState.Running, JobStepOutcome.Failure);
			IGraph Graph = CreateGraph();
			IJob Job = CreateGetLabelStatesJob("Job", Graph, JobStepData);
			IReadOnlyList<(LabelState State, LabelOutcome Outcome)> LabelStates = Job.GetLabelStates(Graph);
			Assert.AreEqual(Graph.Labels.Count, LabelStates.Count);
			Assert.IsTrue(LabelStates[0].State == LabelState.Running);
			Assert.IsTrue(LabelStates[0].Outcome == LabelOutcome.Warnings);
			Assert.IsTrue(LabelStates[1].State == LabelState.Running);
			Assert.IsTrue(LabelStates[1].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[2].State == LabelState.Running);
			Assert.IsTrue(LabelStates[2].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[3].State == LabelState.Running);
			Assert.IsTrue(LabelStates[3].Outcome == LabelOutcome.Failure);
		}

		[TestMethod]
		public void GetLabelStates_CompleteStateTest()
		{
			// Verify that if a label's steps are all complete the state is set to complete.
			List<(JobStepState, JobStepOutcome)> JobStepData = GetJobStepDataList(JobStepState.Completed, JobStepOutcome.Success);
			JobStepData[2] = (JobStepState.Running, JobStepOutcome.Failure);
			JobStepData[4] = (JobStepState.Ready, JobStepOutcome.Unspecified);
			IGraph Graph = CreateGraph();
			IJob Job = CreateGetLabelStatesJob("Job", Graph, JobStepData);
			IReadOnlyList<(LabelState State, LabelOutcome Outcome)> LabelStates = Job.GetLabelStates(Graph);
			Assert.AreEqual(Graph.Labels.Count, LabelStates.Count);
			Assert.IsTrue(LabelStates[0].State == LabelState.Running);
			Assert.IsTrue(LabelStates[0].Outcome == LabelOutcome.Failure);
			Assert.IsTrue(LabelStates[1].State == LabelState.Complete);
			Assert.IsTrue(LabelStates[1].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[2].State == LabelState.Complete);
			Assert.IsTrue(LabelStates[2].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[3].State == LabelState.Running);
			Assert.IsTrue(LabelStates[3].Outcome == LabelOutcome.Success);
		}

		[TestMethod]
		public void GetLabelStates_SkippedAbortedStepTest()
		{
			// Verify that if a label's steps are complete and successful but any were skipped or aborted the result is unspecified.
			List<(JobStepState, JobStepOutcome)> JobStepData = GetJobStepDataList(JobStepState.Completed, JobStepOutcome.Success);
			JobStepData[2] = (JobStepState.Aborted, JobStepOutcome.Unspecified);
			JobStepData[4] = (JobStepState.Skipped, JobStepOutcome.Unspecified);
			IGraph Graph = CreateGraph();
			IJob Job = CreateGetLabelStatesJob("Job", Graph, JobStepData);
			IReadOnlyList<(LabelState State, LabelOutcome Outcome)> LabelStates = Job.GetLabelStates(Graph);
			Assert.AreEqual(Graph.Labels.Count, LabelStates.Count);
			Assert.IsTrue(LabelStates[0].State == LabelState.Complete);
			Assert.IsTrue(LabelStates[0].Outcome == LabelOutcome.Unspecified);
			Assert.IsTrue(LabelStates[1].State == LabelState.Complete);
			Assert.IsTrue(LabelStates[1].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[2].State == LabelState.Complete);
			Assert.IsTrue(LabelStates[2].Outcome == LabelOutcome.Success);
			Assert.IsTrue(LabelStates[3].State == LabelState.Complete);
			Assert.IsTrue(LabelStates[3].Outcome == LabelOutcome.Unspecified);
		}
	}
}
