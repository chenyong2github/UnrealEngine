// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Specification for an aggregate target in the graph
	/// </summary>
	public class BgAggregate : BgExpr
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
		public BgLabel? Label { get; }

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregate(BgString name, params BgFileSet[] requires)
			: this(name, BgList<BgFileSet>.Create(requires))
		{
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregate(BgString name, params BgList<BgFileSet>[] requires)
			: this(name, BgList<BgFileSet>.Concat(requires))
		{
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregate(BgString name, BgList<BgFileSet> requires, string label)
			: this(name, requires, new BgLabel(label))
		{
		}

		/// <summary>
		/// Constructor.
		/// </summary>
		public BgAggregate(BgString name, BgList<BgFileSet> requires, BgLabel? label = null)
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
