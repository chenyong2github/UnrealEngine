// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Linq;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Constant values for evaluating the graph
	/// </summary>
	public interface IBgGraphConstants
	{
		/// <summary>
		/// The current stream
		/// </summary>
		public BgString Stream { get; }

		/// <summary>
		/// The changelist being built
		/// </summary>
		public BgInt Change { get; }

		/// <summary>
		/// Current code changelist
		/// </summary>
		public BgInt CodeChange { get; }

		/// <summary>
		/// The current engnine version
		/// </summary>
		public (BgInt Major, BgInt Minor, BgInt Patch) EngineVersion { get; }
	}

	/// <summary>
	/// Specification for a graph in fluent syntax
	/// </summary>
	public class BgGraphSpec
	{
		/// <summary>
		/// Constant properties for evaluating the graph
		/// </summary>
		public IBgGraphConstants Constants { get; }

		List<IBgOption> Options = new List<IBgOption>();
		List<BgAgentSpec> AgentSpecs = new List<BgAgentSpec>();
		List<BgAggregateSpec> AggregateSpecs = new List<BgAggregateSpec>();
		List<BgLabelSpec> LabelSpecs = new List<BgLabelSpec>();
		BgList<BgDiagnosticSpec> DiagnosticSpecs = new List<BgDiagnosticSpec>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Constants">Constant values for use during graph evaluation</param>
		public BgGraphSpec(IBgGraphConstants Constants)
		{
			this.Constants = Constants;
		}

		/// <summary>
		/// Adds a boolean option
		/// </summary>
		/// <param name="Name">Name of the option. Must be unique.</param>
		/// <param name="Description">Description for the option</param>
		/// <param name="DefaultValue">Default value for the option.</param>
		/// <returns>Boolean expression</returns>
		public BgBoolOption AddOption(string Name, BgString Description, BgBool DefaultValue)
		{
			BgBoolOption Option = new BgBoolOption(Name, Description, DefaultValue);
			Options.Add(Option);
			return Option;
		}

		/// <summary>
		/// Adds an integer option
		/// </summary>
		/// <param name="Name">Name of the option. Must be unique.</param>
		/// <param name="Description">Description for the option</param>
		/// <param name="DefaultValue">Default value for the option.</param>
		/// <returns>Integer expression</returns>
		public BgIntOption AddIntOption(string Name, BgString Description, BgInt DefaultValue)
		{
			BgIntOption Option = new BgIntOption(Name, Description, DefaultValue);
			Options.Add(Option);
			return Option;
		}

		/// <summary>
		/// Adds a string option
		/// </summary>
		/// <param name="Name">Name of the option. Must be unique.</param>
		/// <param name="Description">Description for the option</param>
		/// <param name="DefaultValue">Default value for the option.</param>
		/// <returns>String expression</returns>
		public BgStringOption AddOption(string Name, BgString Description, BgString DefaultValue)
		{
			BgStringOption Option = new BgStringOption(Name, Description, DefaultValue);
			Options.Add(Option);
			return Option;
		}

		/// <summary>
		/// Adds an enum option
		/// </summary>
		/// <typeparam name="TEnum">Type of enum value</typeparam>
		/// <param name="Name">Name of the option. Must be unique.</param>
		/// <param name="Description">Description for the option</param>
		/// <param name="DefaultValue">Default value for the option.</param>
		/// <returns>Enum option</returns>
		public BgEnumOption<TEnum> AddOption<TEnum>(string Name, BgString Description, BgEnum<TEnum> DefaultValue) where TEnum : struct
		{
			BgEnumOption<TEnum> Option = new BgEnumOption<TEnum>(Name, Description, DefaultValue);
			Options.Add(Option);
			return Option;
		}

		/// <summary>
		/// Adds an option which may be a list of enum values
		/// </summary>
		/// <typeparam name="TEnum">Type of enum value</typeparam>
		/// <param name="Name">Name of the option. Must be unique.</param>
		/// <param name="Description">Description for the option</param>
		/// <param name="DefaultValue">Default value for the option.</param>
		/// <returns></returns>
		public BgEnumListOption<TEnum> AddOption<TEnum>(string Name, BgString Description, BgList<BgEnum<TEnum>> DefaultValue) where TEnum : struct
		{
			BgEnumListOption<TEnum> Option = new BgEnumListOption<TEnum>(Name, Description, DefaultValue);
			Options.Add(Option);
			return Option;
		}

		/// <summary>
		/// Adds a new agent to the graph
		/// </summary>
		/// <param name="Name">Name of the agent</param>
		/// <returns>Agent specification object</returns>
		public BgAgentSpec AddAgent(BgString Name)
		{
			BgAgentSpec Agent = new BgAgentSpec(Name);
			AgentSpecs.Add(Agent);
			return Agent;
		}

		/// <summary>
		/// Adds a new aggregate to the graph
		/// </summary>
		/// <param name="Name">Name of the agent</param>
		/// <param name="Outputs">Initial set of tokens to include in the aggregate</param>
		/// <returns>Agent specification object</returns>
		public BgAggregateSpec AddAggregate(BgString Name, BgList<BgFileSet> Outputs)
		{
			BgAggregateSpec Aggregate = new BgAggregateSpec(Name, Outputs);
			AggregateSpecs.Add(Aggregate);
			return Aggregate;
		}

		/// <summary>
		/// Adds a new aggregate to the graph
		/// </summary>
		/// <param name="Name">Name of the agent</param>
		/// <param name="Outputs">Initial set of tokens to include in the aggregate</param>
		/// <param name="Label"></param>
		/// <returns>Agent specification object</returns>
		public BgAggregateSpec AddAggregate(BgString Name, BgList<BgFileSet> Outputs, BgString Label)
		{
			AddLabel(Config => Config.RequiredNodes = Config.RequiredNodes.Add(Outputs).Distinct());
			return AddAggregate(Name, Outputs);
		}

		/// <summary>
		/// Adds a new label to the graph
		/// </summary>
		/// <param name="Configure">Callback for configuring the label</param>
		/// <returns>Label specification object</returns>
		public BgLabelSpec AddLabel(Action<BgLabelConfig> Configure)
		{
			BgLabelConfig Config = new BgLabelConfig();
			Configure(Config);

			BgLabelSpec LabelSpec = new BgLabelSpec(Config);
			LabelSpecs.Add(LabelSpec);

			return LabelSpec;
		}

		/// <summary>
		/// Adds a diagnostic to the graph
		/// </summary>
		/// <param name="Condition">Condition for adding the diagnostic to the graph</param>
		/// <param name="Level"></param>
		/// <param name="Message"></param>
		public void AddDiagnostic(BgBool Condition, LogLevel Level, BgString Message)
		{
			BgDiagnosticSpec Diagnostic = new BgDiagnosticSpec(Level, Message);
			DiagnosticSpecs = DiagnosticSpecs.AddIf(Condition, Diagnostic);
		}

		/// <summary>
		/// Creates a concrete graph from the specifications
		/// </summary>
		/// <param name="Targets"></param>
		/// <param name="Options"></param>
		/// <returns>New graph instance</returns>
		public BgGraph CreateGraph(HashSet<string> Targets, Dictionary<string, string> Options)
		{
			BgExprContext Context = new BgExprContext();
			Context.Options = Options;

			BgGraph Graph = new BgGraph();
			foreach (BgAgentSpec AgentSpec in AgentSpecs)
			{
				AgentSpec.AddToGraph(Context, Graph);
			}
			foreach (BgAggregateSpec AggregateSpec in AggregateSpecs)
			{
				AggregateSpec.AddToGraph(Context, Graph);
			}
			foreach (BgDiagnosticSpec DiagnosticSpec in DiagnosticSpecs.GetEnumerable(Context))
			{
				DiagnosticSpec.AddToGraph(Context, Graph, null, null);
			}
			foreach (BgLabelSpec LabelSpec in LabelSpecs)
			{
				LabelSpec.AddToGraph(Context, Graph);
			}

			return Graph;
		}
	}
}
