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
using System.Threading.Tasks;

namespace EpicGames.BuildGraph.Tests
{
	[TestClass]
	public class GraphTests
	{
		static Task UpdateVersionFiles() => Task.CompletedTask;
		static Task CompileShooterGameWin64() => Task.CompletedTask;
		static Task CookShooterGameWin64() => Task.CompletedTask;

		object Evaluate(BgExpr expr)
		{
			(byte[] data, BgThunkDef[] methods) = BgCompiler.Compile(expr);
			BgInterpreter interpreter = new BgInterpreter(data, methods, new Dictionary<string, string>());
			return interpreter.Evaluate();
		}

		[TestMethod]
		public void NodeTest()
		{
			BgAgent agent = new BgAgent("name", "type");
			BgNode nodeSpec1 = agent.AddNode(x => UpdateVersionFiles()).Construct();

			BgNode nodeSpec2 = agent.AddNode(x => CompileShooterGameWin64()).Requires(BgList<BgFileSet>.Create(nodeSpec1)).Construct();

			BgNode nodeSpec3 = agent.AddNode(x => CookShooterGameWin64()).Requires(BgList<BgFileSet>.Create(nodeSpec2.DefaultOutput)).Construct();

			BgNodeDef node3 = ((BgNodeDef)Evaluate(nodeSpec3));
			Assert.AreEqual(node3.Name, "Cook Shooter Game Win64");
			Assert.AreEqual(2, node3.InputDependencies.Count);
			Assert.AreEqual(1, node3.Inputs.Count);

			BgNodeDef node2 = node3.Inputs[0].ProducingNode;
			Assert.AreEqual(node2.Name, "Compile Shooter Game Win64");
			Assert.AreEqual(1, node2.InputDependencies.Count);
			Assert.AreEqual(1, node2.Inputs.Count);

			BgNodeDef node1 = node2.Inputs[0].ProducingNode;
			Assert.AreEqual(node1.Name, "Update Version Files");
			Assert.AreEqual(0, node1.InputDependencies.Count);
			Assert.AreEqual(0, node1.Inputs.Count);
		}
		
		[TestMethod]
		public void AgentTest()
		{
			BgAgent expr = new BgAgent("TestAgent", BgList<BgString>.Create("win64", "incremental"));

			BgAgentDef agent = ((BgObjectDef)Evaluate(expr)).Deserialize<BgAgentDef>();
			Assert.AreEqual("TestAgent", agent.Name);

			Assert.AreEqual(2, agent.PossibleTypes.Count);
			Assert.AreEqual("win64", agent.PossibleTypes[0]);
			Assert.AreEqual("incremental", agent.PossibleTypes[1]);
/*
			Assert.AreEqual(1, agent.Nodes.Count);

			BgNode node = agent.Nodes[0];
			Assert.AreEqual("Update Version Files", node.Name);
*/		}

		[TestMethod]
		public void AggregateTest()
		{
			BgAgent agent = new BgAgent("test", "test");

			BgNode nodeSpec1 = agent.AddNode(x => UpdateVersionFiles()).Construct();
			BgNode nodeSpec2 = agent.AddNode(x => CompileShooterGameWin64()).Construct();

			BgAggregate aggregateSpec = new BgAggregate("All nodes", BgList<BgNode>.Create(nodeSpec1, nodeSpec2));

			BgAggregateDef aggregate = ((BgObjectDef)Evaluate(aggregateSpec)).Deserialize<BgAggregateDef>();
			Assert.AreEqual("All nodes", aggregate.Name);

			BgNodeDef node1 = aggregate.RequiredNodes.OrderBy(x => x.Name).ElementAt(0);
			Assert.AreEqual("Compile Shooter Game Win64", node1.Name);

			BgNodeDef node2 = aggregate.RequiredNodes.OrderBy(x => x.Name).ElementAt(1);
			Assert.AreEqual("Update Version Files", node2.Name);
		}

		[TestMethod]
		public void LabelTest()
		{
			BgLabel labelSpec = new BgLabel("name", "category", "ugsBadge", "ugsProject", BgLabelChange.Code);

			BgLabelDef label = ((BgObjectDef)Evaluate(labelSpec)).Deserialize<BgLabelDef>();
			Assert.AreEqual("name", label.DashboardName);
			Assert.AreEqual("category", label.DashboardCategory);
			Assert.AreEqual("ugsBadge", label.UgsBadge);
			Assert.AreEqual("ugsProject", label.UgsProject);
			Assert.AreEqual(BgLabelChange.Code, label.Change);
		}

		[TestMethod]
		public void GraphTest()
		{
			BgAgent agent = new BgAgent("test", "test");

			BgNode nodeSpec1 = agent.AddNode(x => UpdateVersionFiles()).Construct();
			BgNode nodeSpec2 = agent.AddNode(x => CompileShooterGameWin64()).Construct();

			BgAggregate aggregateSpec = new BgAggregate("All nodes", BgList<BgNode>.Create(nodeSpec1, nodeSpec2));
			BgGraph graphSpec = new BgGraph(BgList.Create(nodeSpec1, nodeSpec2), BgList.Create(aggregateSpec));

			BgGraphExpressionDef graph = ((BgObjectDef)Evaluate(graphSpec)).Deserialize<BgGraphExpressionDef>();
			Assert.AreEqual(2, graph.Nodes.Count);
			Assert.AreEqual(1, graph.Aggregates.Count);
		}
	}
}
