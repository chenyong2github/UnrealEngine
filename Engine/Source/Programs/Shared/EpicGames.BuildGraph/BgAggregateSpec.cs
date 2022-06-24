// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.BuildGraph.Expressions;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Specification for an aggregate target in the graph
	/// </summary>
	public class BgAggregateSpec : BgExpr
	{
		/// <summary>
		/// Name of the aggregate
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Outputs required for the aggregate
		/// </summary>
		public BgList<BgFileSet> Requires { get; }

		/// <summary>
		/// Label to apply to this aggregate
		/// </summary>
		public BgLabelSpec? Label { get; }

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregateSpec(BgString name, params BgFileSet[] requires)
			: this(name, BgList<BgFileSet>.Create(requires))
		{
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregateSpec(BgString name, params BgList<BgFileSet>[] requires)
			: this(name, BgList<BgFileSet>.Concat(requires))
		{
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregateSpec(BgString name, BgList<BgFileSet> requires, string label)
			: this(name, requires, new BgLabelSpec(label))
		{
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregateSpec(BgString name, BgList<BgFileSet> requires, BgLabelSpec? label = null)
			: base(BgExprFlags.None)
		{
			Name = name;
			Requires = requires;
			Label = label;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.Aggregate);
			writer.WriteExpr(Name);
			writer.WriteExpr(Requires);
			writer.WriteExpr(Label ?? BgExpr.Null);
		}

		/// <inheritdoc/>
		public override BgString ToBgString() => Name;
	}
}
