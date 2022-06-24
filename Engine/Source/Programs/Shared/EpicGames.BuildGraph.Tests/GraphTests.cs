// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using OpenTracing;
using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph.Tests
{
	/*
	[TestClass]
	public class GraphTests
	{
		object Evaluate(BgExpr expr)
		{
			byte[] data = BgBytecode.Compile(expr);
			BgBytecodeInterpreter interpreter = new BgBytecodeInterpreter(data);
			return interpreter.Evaluate();
		}

		[TestMethod]
		public void NodeTest()
		{
			BgNodeSpec nodeSpec1 = new BgNodeSpec("Update Version Files");

			BgNodeSpec nodeSpec2 = new BgNodeSpec("Compile ShooterGameEditor Win64", requires: BgList<BgFileSet>.Create(nodeSpec1));

			BgNodeSpec nodeSpec3 = new BgNodeSpec("Cook ShooterGame Win64", requires: BgList<BgFileSet>.Create(nodeSpec2.DefaultOutput));

			BgNode node3 = (BgNode)Evaluate(nodeSpec3);
			Assert.AreEqual(node3.Name, "Cook ShooterGame Win64");
			Assert.AreEqual(2, node3.InputDependencies.Count);
			Assert.AreEqual(1, node3.Inputs.Count);

			BgNode node2 = node3.Inputs[0].ProducingNode;
			Assert.AreEqual(node2.Name, "Compile ShooterGameEditor Win64");
			Assert.AreEqual(1, node2.InputDependencies.Count);
			Assert.AreEqual(1, node2.Inputs.Count);

			BgNode node1 = node2.Inputs[0].ProducingNode;
			Assert.AreEqual(node1.Name, "Update Version Files");
			Assert.AreEqual(0, node1.InputDependencies.Count);
			Assert.AreEqual(0, node1.Inputs.Count);
		}

		[TestMethod]
		public void AgentTest()
		{
			BgNodeSpec nodeSpec = new BgNodeSpec("Update Version Files");

			BgAgentSpec agentSpec = new BgAgentSpec(BgList<BgString>.Create("win64", "incremental"));

			BgAgent agent = (BgAgent)Evaluate(agentSpec);
			Assert.AreEqual("TestAgent", agent.Name);

			Assert.AreEqual(2, agent.PossibleTypes.Length);
			Assert.AreEqual("win64", agent.PossibleTypes[0]);
			Assert.AreEqual("incremental", agent.PossibleTypes[1]);

			Assert.AreEqual(1, agent.Nodes.Count);

			BgNode node = agent.Nodes[0];
			Assert.AreEqual("Update Version Files", node.Name);
		}

		[TestMethod]
		public void AggregateTest()
		{
			BgNodeSpec nodeSpec1 = new BgNodeSpec("Update Version Files");
			BgNodeSpec nodeSpec2 = new BgNodeSpec("Compile UnrealEditor Win64");

			BgAggregateSpec aggregateSpec = new BgAggregateSpec("All nodes", BgList<BgFileSet>.Create(nodeSpec1, nodeSpec2));

			BgAggregate aggregate = (BgAggregate)Evaluate(aggregateSpec);
			Assert.AreEqual("All nodes", aggregate.Name);

			BgNode node1 = aggregate.RequiredNodes.OrderBy(x => x.Name).ElementAt(0);
			Assert.AreEqual("Compile UnrealEditor Win64", node1.Name);

			BgNode node2 = aggregate.RequiredNodes.OrderBy(x => x.Name).ElementAt(1);
			Assert.AreEqual("Update Version Files", node2.Name);
		}

		[TestMethod]
		public void LabelTest()
		{
			BgNodeSpec nodeSpec1 = new BgNodeSpec("Update Version Files");
			BgNodeSpec nodeSpec2 = new BgNodeSpec("Compile UnrealEditor Win64");

			BgLabelSpec labelSpec = new BgLabelSpec("name", "category", "ugsBadge", "ugsProject", "Code", BgList<BgNodeSpec>.Create(nodeSpec2), BgList<BgNodeSpec>.Create(nodeSpec1));

			BgLabel label = (BgLabel)Evaluate(labelSpec);
			Assert.AreEqual("name", label.DashboardName);
			Assert.AreEqual("category", label.DashboardCategory);
			Assert.AreEqual("ugsBadge", label.UgsBadge);
			Assert.AreEqual("ugsProject", label.UgsProject);
			Assert.AreEqual(BgLabelChange.Code, label.Change);
			Assert.AreEqual(1, label.RequiredNodes.Count);
			Assert.AreEqual(1, label.IncludedNodes.Count);

			BgNode node1 = label.RequiredNodes.First();
			Assert.AreEqual("Compile UnrealEditor Win64", node1.Name);

			BgNode node2 = label.IncludedNodes.First();
			Assert.AreEqual("Update Version Files", node2.Name);
		}

		[TestMethod]
		public void DiagnosticTest()
		{
			BgDiagnosticSpec diagnosticSpec = new BgDiagnosticSpec(LogLevel.Error, "hello world", "file.cs", 123);

			BgDiagnostic diagnostic = (BgDiagnostic)Evaluate(diagnosticSpec);
			Assert.AreEqual(LogLevel.Error, diagnostic.Level);
			Assert.AreEqual("hello world", diagnostic.Message);
			Assert.AreEqual("file.cs", diagnostic.Location.File);
			Assert.AreEqual(123, diagnostic.Location.LineNumber);
		}
	}
	*/
}
