// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;

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

		readonly List<IBgOption> _options = new List<IBgOption>();
		readonly List<BgAgentSpec> _agentSpecs = new List<BgAgentSpec>();
		readonly List<BgAggregateSpec> _aggregateSpecs = new List<BgAggregateSpec>();
		readonly List<BgLabelSpec> _labelSpecs = new List<BgLabelSpec>();
		BgList<BgDiagnosticSpec> _diagnosticSpecs = new List<BgDiagnosticSpec>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="constants">Constant values for use during graph evaluation</param>
		public BgGraphSpec(IBgGraphConstants constants)
		{
			Constants = constants;
		}

		/// <summary>
		/// Adds a boolean option
		/// </summary>
		/// <param name="name">Name of the option. Must be unique.</param>
		/// <param name="description">Description for the option</param>
		/// <param name="defaultValue">Default value for the option.</param>
		/// <returns>Boolean expression</returns>
		public BgBoolOption AddOption(string name, BgString description, BgBool defaultValue)
		{
			BgBoolOption option = new BgBoolOption(name, description, defaultValue);
			_options.Add(option);
			return option;
		}

		/// <summary>
		/// Adds an integer option
		/// </summary>
		/// <param name="name">Name of the option. Must be unique.</param>
		/// <param name="description">Description for the option</param>
		/// <param name="defaultValue">Default value for the option.</param>
		/// <returns>Integer expression</returns>
		public BgIntOption AddIntOption(string name, BgString description, BgInt defaultValue)
		{
			BgIntOption option = new BgIntOption(name, description, defaultValue);
			_options.Add(option);
			return option;
		}

		/// <summary>
		/// Adds a string option
		/// </summary>
		/// <param name="name">Name of the option. Must be unique.</param>
		/// <param name="description">Description for the option</param>
		/// <param name="defaultValue">Default value for the option.</param>
		/// <returns>String expression</returns>
		public BgStringOption AddOption(string name, BgString description, BgString defaultValue)
		{
			BgStringOption option = new BgStringOption(name, description, defaultValue);
			_options.Add(option);
			return option;
		}

		/// <summary>
		/// Adds an enum option
		/// </summary>
		/// <typeparam name="TEnum">Type of enum value</typeparam>
		/// <param name="name">Name of the option. Must be unique.</param>
		/// <param name="description">Description for the option</param>
		/// <param name="defaultValue">Default value for the option.</param>
		/// <returns>Enum option</returns>
		public BgEnumOption<TEnum> AddOption<TEnum>(string name, BgString description, BgEnum<TEnum> defaultValue) where TEnum : struct
		{
			BgEnumOption<TEnum> option = new BgEnumOption<TEnum>(name, description, defaultValue);
			_options.Add(option);
			return option;
		}

		/// <summary>
		/// Adds an option which may be a list of enum values
		/// </summary>
		/// <typeparam name="TEnum">Type of enum value</typeparam>
		/// <param name="name">Name of the option. Must be unique.</param>
		/// <param name="description">Description for the option</param>
		/// <param name="defaultValue">Default value for the option.</param>
		/// <returns></returns>
		public BgEnumListOption<TEnum> AddOption<TEnum>(string name, BgString description, BgList<BgEnum<TEnum>> defaultValue) where TEnum : struct
		{
			BgEnumListOption<TEnum> option = new BgEnumListOption<TEnum>(name, description, defaultValue);
			_options.Add(option);
			return option;
		}

		/// <summary>
		/// Adds a new agent to the graph
		/// </summary>
		/// <param name="name">Name of the agent</param>
		/// <returns>Agent specification object</returns>
		public BgAgentSpec AddAgent(BgString name)
		{
			BgAgentSpec agent = new BgAgentSpec(name);
			_agentSpecs.Add(agent);
			return agent;
		}

		/// <summary>
		/// Adds a new aggregate to the graph
		/// </summary>
		/// <param name="name">Name of the agent</param>
		/// <param name="outputs">Initial set of tokens to include in the aggregate</param>
		/// <returns>Agent specification object</returns>
		public BgAggregateSpec AddAggregate(BgString name, BgList<BgFileSet> outputs)
		{
			BgAggregateSpec aggregate = new BgAggregateSpec(name, outputs);
			_aggregateSpecs.Add(aggregate);
			return aggregate;
		}

		/// <summary>
		/// Adds a new aggregate to the graph
		/// </summary>
		/// <param name="name">Name of the agent</param>
		/// <param name="outputs">Initial set of tokens to include in the aggregate</param>
		/// <param name="label"></param>
		/// <returns>Agent specification object</returns>
		public BgAggregateSpec AddAggregate(BgString name, BgList<BgFileSet> outputs, BgString label)
		{
			AddLabel(config =>
			{
				config.DashboardName = label;
				config.RequiredNodes = config.RequiredNodes.Add(outputs).Distinct();
			});
			return AddAggregate(name, outputs);
		}

		/// <summary>
		/// Adds a new label to the graph
		/// </summary>
		/// <param name="configure">Callback for configuring the label</param>
		/// <returns>Label specification object</returns>
		public BgLabelSpec AddLabel(Action<BgLabelConfig> configure)
		{
			BgLabelConfig config = new BgLabelConfig();
			configure(config);

			BgLabelSpec labelSpec = new BgLabelSpec(config);
			_labelSpecs.Add(labelSpec);

			return labelSpec;
		}

		/// <summary>
		/// Adds a diagnostic to the graph
		/// </summary>
		/// <param name="condition">Condition for adding the diagnostic to the graph</param>
		/// <param name="level"></param>
		/// <param name="message"></param>
		public void AddDiagnostic(BgBool condition, LogLevel level, BgString message)
		{
			BgDiagnosticSpec diagnostic = new BgDiagnosticSpec(level, message);
			_diagnosticSpecs = _diagnosticSpecs.AddIf(condition, diagnostic);
		}

		/// <summary>
		/// Creates a concrete graph from the specifications
		/// </summary>
		/// <param name="options"></param>
		/// <returns>New graph instance</returns>
		public BgGraph CreateGraph(Dictionary<string, string> options)
		{
			BgExprContext context = new BgExprContext();
			context.Options = options;

			BgGraph graph = new BgGraph();
			foreach (BgAgentSpec agentSpec in _agentSpecs)
			{
				agentSpec.AddToGraph(context, graph);
			}
			foreach (BgAggregateSpec aggregateSpec in _aggregateSpecs)
			{
				aggregateSpec.AddToGraph(context, graph);
			}
			foreach (BgDiagnosticSpec diagnosticSpec in _diagnosticSpecs.GetEnumerable(context))
			{
				diagnosticSpec.AddToGraph(context, graph, null, null);
			}
			foreach (BgLabelSpec labelSpec in _labelSpecs)
			{
				labelSpec.AddToGraph(context, graph);
			}

			return graph;
		}
	}
}
