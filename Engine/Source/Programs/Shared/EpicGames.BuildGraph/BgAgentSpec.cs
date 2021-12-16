// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

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
		public BgAgentConfig AddTypes(params BgString[] TypeNames)
		{
			Types = Types.Add(TypeNames);
			return this;
		}

		/// <summary>
		/// Check that a certain condition is true before executing the node
		/// </summary>
		public BgAgentConfig Check(BgBool Condition, LogLevel Level, BgString Message)
		{
			BgDiagnosticSpec Diagnostic = new BgDiagnosticSpec(Level, Message);
			Diagnostics = Diagnostics.AddIf(Condition, Diagnostic);
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
		/// <param name="Name"></param>
		internal BgAgentSpec(BgString Name)
		{
			this.Name = Name;
		}

		/// <summary>
		/// Adds a node to be executed on this agent
		/// </summary>
		/// <param name="Function">Lambda expression which calls a static method to be used for this node</param>
		/// <returns>New node spec instance</returns>
		public BgNodeSpec AddNode(Expression<Func<BgContext, Task>> Function)
		{
			BgNodeSpec NodeSpec = BgNodeSpec.Create(Function);
			NodeSpecs.Add(NodeSpec);
			return NodeSpec;
		}

		/// <summary>
		/// Adds a node to be executed on this agent
		/// </summary>
		/// <param name="Function">Lambda expression which calls a static method to be used for this node</param>
		/// <returns>New node spec instance</returns>
		public BgNodeSpec<T> AddNode<T>(Expression<Func<BgContext, Task<T>>> Function)
		{
			BgNodeSpec<T> NodeSpec = BgNodeSpec.Create<T>(Function);
			NodeSpecs.Add(NodeSpec);
			return NodeSpec;
		}

		/// <summary>
		/// Add agent types to the agent definition
		/// </summary>
		public BgAgentSpec Type(params BgString[] NewTypeNames)
		{
			TypeNames = TypeNames.Add(NewTypeNames);
			return this;
		}

		/// <summary>
		/// Check that a certain condition is true before executing the node
		/// </summary>
		public BgAgentSpec WarnIf(BgBool Condition, BgString Message)
		{
			BgDiagnosticSpec Diagnostic = new BgDiagnosticSpec(LogLevel.Warning, Message);
			Diagnostics = Diagnostics.AddIf(Condition, Diagnostic);
			return this;
		}

		/// <summary>
		/// Check that a certain condition is true before executing the node
		/// </summary>
		public BgAgentSpec ErrorIf(BgBool Condition, BgString Message)
		{
			BgDiagnosticSpec Diagnostic = new BgDiagnosticSpec(LogLevel.Error, Message);
			Diagnostics = Diagnostics.AddIf(Condition, Diagnostic);
			return this;
		}

		/// <summary>
		/// Creates a concrete <see cref="BgAgent"/> object from the specification
		/// </summary>
		public void AddToGraph(BgExprContext Context, BgGraph Graph)
		{
			string[] Types = TypeNames.Compute(Context).ToArray();

			BgAgent Agent = new BgAgent(Name.Compute(Context), Types);
			Graph.NameToAgent.Add(Agent.Name, Agent);
			Graph.Agents.Add(Agent);

			foreach (BgDiagnosticSpec Precondition in Diagnostics.GetEnumerable(Context))
			{
				Precondition.AddToGraph(Context, Graph, Agent, null);
			}

			foreach (BgNodeSpec NodeSpec in NodeSpecs)
			{
				NodeSpec.AddToGraph(Context, Graph, Agent);
			}
		}
	}
}
