// Copyright Epic Games, Inc. All Rights Reserved.

using System.Linq;
using EpicGames.BuildGraph.Expressions;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Specification for an aggregate target in the graph
	/// </summary>
	public class BgAggregateSpec
	{
		/// <summary>
		/// Name of the aggregate
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Outputs required for the aggregate
		/// </summary>
		public BgList<BgFileSet> RequiredOutputs { get; set; }

		/// <summary>
		/// Internal constructor. Use <see cref="BgGraphSpec.AddAggregate(BgString, BgList{BgFileSet})"/> to create an aggregate.
		/// </summary>
		internal BgAggregateSpec(BgString name, BgList<BgFileSet> requiredOutputs)
		{
			Name = name;
			RequiredOutputs = requiredOutputs;
		}

		/// <summary>
		/// Creates a concrete aggregate object from this specification.
		/// </summary>
		internal void AddToGraph(BgExprContext context, BgGraph graph)
		{
			BgAggregate aggregate = new BgAggregate(Name.Compute(context));
			aggregate.RequiredNodes.UnionWith(RequiredOutputs.ComputeTags(context).Select(x => graph.TagNameToNodeOutput[x].ProducingNode));
			graph.NameToAggregate.Add(aggregate.Name, aggregate);
		}

		/// <summary>
		/// Adds a set of dependencies to this aggregate
		/// </summary>
		/// <param name="tokens">List of token dependencies</param>
		public void Requires(params BgFileSet[] tokens)
		{
			RequiredOutputs.Add(tokens);
		}
	}
}
