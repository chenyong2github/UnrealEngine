// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Threading.Tasks;
using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Configuration object for agents
	/// </summary>
	public class BgAgentConfig
	{
		/// <summary>
		/// List of agent types to select from, in order of preference. The first agent type supported by a stream will be used.
		/// </summary>
		public BgList<BgString> Types { get; set; } = BgList<BgString>.Empty;

		/// <summary>
		/// List of diagnostics for this agent to run
		/// </summary>
		public BgList<BgDiagnosticSpec> Diagnostics { get; set; } = BgList<BgDiagnosticSpec>.Empty;

		/// <summary>
		/// Add agent types to the agent definition
		/// </summary>
		public BgAgentConfig AddTypes(params BgString[] typeNames)
		{
			Types = Types.Add(typeNames);
			return this;
		}

		/// <summary>
		/// Check that a certain condition is true before executing the node
		/// </summary>
		public BgAgentConfig Check(BgBool condition, LogLevel level, BgString message)
		{
			BgDiagnosticSpec diagnostic = new BgDiagnosticSpec(level, message);
			Diagnostics = Diagnostics.AddIf(condition, diagnostic);
			return this;
		}
	}

	/// <summary>
	/// Describes an agent that can execute execute build steps
	/// </summary>
	public class BgAgentSpec
	{
		/// <summary>
		/// Name of the agent
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// List of agent types to select from, in order of preference. The first agent type supported by a stream will be used.
		/// </summary>
		public BgList<BgString> TypeNames { get; set; } = BgList<BgString>.Empty;

		/// <summary>
		/// List of diagnostics for this agent to run
		/// </summary>
		public BgList<BgDiagnosticSpec> Diagnostics { get; set; } = BgList<BgDiagnosticSpec>.Empty;

		/// <summary>
		/// Nodes that have been added to the agent
		/// </summary>
		List<BgNodeSpec> NodeSpecs { get; set; } = new List<BgNodeSpec>();

		/// <summary>
		/// Private constructor. Use <see cref="BgGraphSpec.AddAgent(BgString)"/> to construct an agent
		/// </summary>
		/// <param name="name"></param>
		internal BgAgentSpec(BgString name)
		{
			Name = name;
		}

		/// <summary>
		/// Adds a node to be executed on this agent
		/// </summary>
		/// <param name="function">Lambda expression which calls a static method to be used for this node</param>
		/// <returns>New node spec instance</returns>
		public BgNodeSpec AddNode(Expression<Func<BgContext, Task>> function)
		{
			BgNodeSpec nodeSpec = BgNodeSpec.Create(function);
			NodeSpecs.Add(nodeSpec);
			return nodeSpec;
		}

		/// <summary>
		/// Adds a node to be executed on this agent
		/// </summary>
		/// <param name="function">Lambda expression which calls a static method to be used for this node</param>
		/// <returns>New node spec instance</returns>
		public BgNodeSpec<T> AddNode<T>(Expression<Func<BgContext, Task<T>>> function)
		{
			BgNodeSpec<T> nodeSpec = BgNodeSpec.Create<T>(function);
			NodeSpecs.Add(nodeSpec);
			return nodeSpec;
		}

		/// <summary>
		/// Add agent types to the agent definition
		/// </summary>
		public BgAgentSpec Type(params BgString[] newTypeNames)
		{
			TypeNames = TypeNames.Add(newTypeNames);
			return this;
		}

		/// <summary>
		/// Check that a certain condition is true before executing the node
		/// </summary>
		public BgAgentSpec WarnIf(BgBool condition, BgString message)
		{
			BgDiagnosticSpec diagnostic = new BgDiagnosticSpec(LogLevel.Warning, message);
			Diagnostics = Diagnostics.AddIf(condition, diagnostic);
			return this;
		}

		/// <summary>
		/// Check that a certain condition is true before executing the node
		/// </summary>
		public BgAgentSpec ErrorIf(BgBool condition, BgString message)
		{
			BgDiagnosticSpec diagnostic = new BgDiagnosticSpec(LogLevel.Error, message);
			Diagnostics = Diagnostics.AddIf(condition, diagnostic);
			return this;
		}

		/// <summary>
		/// Creates a concrete <see cref="BgAgent"/> object from the specification
		/// </summary>
		public void AddToGraph(BgExprContext context, BgGraph graph)
		{
			string[] types = TypeNames.Compute(context).ToArray();

			BgAgent agent = new BgAgent(Name.Compute(context), types);
			graph.NameToAgent.Add(agent.Name, agent);
			graph.Agents.Add(agent);

			foreach (BgDiagnosticSpec precondition in Diagnostics.GetEnumerable(context))
			{
				precondition.AddToGraph(context, graph, agent, null);
			}

			foreach (BgNodeSpec nodeSpec in NodeSpecs)
			{
				nodeSpec.AddToGraph(context, graph, agent);
			}
		}
	}
}
