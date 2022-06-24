// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.BuildGraph.Expressions;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Specification for a graph in fluent syntax
	/// </summary>
	public class BgGraphSpec : BgExpr
	{
		/// <summary>
		/// Nodes for the graph
		/// </summary>
		public BgList<BgNodeSpec> Nodes { get; }

		/// <summary>
		/// Aggregates for the graph
		/// </summary>
		public BgList<BgAggregateSpec> Aggregates { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgGraphSpec(BgList<BgNodeSpec> nodes, BgList<BgAggregateSpec> aggregates)
			: base(BgExprFlags.ForceFragment)
		{
			Nodes = nodes;
			Aggregates = aggregates;
		}

		/// <summary>
		/// Implicit conversion from a node spec
		/// </summary>
		public static implicit operator BgGraphSpec(BgNodeSpec node)
		{
			return new BgGraphSpec(node, BgList<BgAggregateSpec>.Empty);
		}

		/// <summary>
		/// Implicit conversion from a list of node specs
		/// </summary>
		public static implicit operator BgGraphSpec(BgList<BgNodeSpec> nodes)
		{
			return new BgGraphSpec(nodes, BgList<BgAggregateSpec>.Empty);
		}

		/// <summary>
		/// Implicit conversion from an aggregate spec
		/// </summary>
		public static implicit operator BgGraphSpec(BgAggregateSpec aggregate)
		{
			return new BgGraphSpec(BgList<BgNodeSpec>.Empty, aggregate);
		}

		/// <summary>
		/// Implicit conversion from a list of node specs
		/// </summary>
		public static implicit operator BgGraphSpec(BgList<BgAggregateSpec> aggregates)
		{
			return new BgGraphSpec(BgList<BgNodeSpec>.Empty, aggregates);
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.Graph);
			writer.WriteExpr(Nodes);
			writer.WriteExpr(Aggregates);
		}

		/// <inheritdoc/>
		public override BgString ToBgString() => "{Graph}";
	}
}
