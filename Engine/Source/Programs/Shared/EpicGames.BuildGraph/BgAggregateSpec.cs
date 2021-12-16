// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

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
		internal BgAggregateSpec(BgString Name, BgList<BgFileSet> RequiredOutputs)
		{
			this.Name = Name;
			this.RequiredOutputs = RequiredOutputs;
		}

		/// <summary>
		/// Creates a concrete aggregate object from this specification.
		/// </summary>
		internal void AddToGraph(BgExprContext Context, BgGraph Graph)
		{
			BgAggregate Aggregate = new BgAggregate(Name.Compute(Context));
			Aggregate.RequiredNodes.UnionWith(RequiredOutputs.ComputeTags(Context).Select(x => Graph.TagNameToNodeOutput[x].ProducingNode));
			Graph.NameToAggregate.Add(Aggregate.Name, Aggregate);
		}

		/// <summary>
		/// Adds a set of dependencies to this aggregate
		/// </summary>
		/// <param name="Tokens">List of token dependencies</param>
		public void Requires(params BgFileSet[] Tokens)
		{
			RequiredOutputs.Add(Tokens);
		}
	}
}
