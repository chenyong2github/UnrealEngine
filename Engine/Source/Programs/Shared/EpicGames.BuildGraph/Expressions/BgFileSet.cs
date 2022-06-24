// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Core;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Represents a placeholder for the output from a node, which can be exchanged for the artifacts produced by a node at runtime
	/// </summary>
	[BgType(typeof(BgFileSetType))]
	public abstract class BgFileSet : BgExpr
	{
		/// <summary>
		/// Constant empty fileset
		/// </summary>
		public static BgFileSet Empty { get; } = new BgFileSetOutputExpr(FileSet.Empty);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="flags"></param>
		public BgFileSet(BgExprFlags flags)
			: base(flags)
		{
		}

		/// <summary>
		/// Implicit conversion from a regular fileset
		/// </summary>
		/// <param name="fileSet"></param>
		public static implicit operator BgFileSet(FileSet fileSet)
		{
			return new BgFileSetOutputExpr(fileSet);
		}

		/// <inheritdoc/>
		public override BgString ToBgString() => "{FileSet}";
	}

	/// <summary>
	/// Traits for a <see cref="BgFileSet"/>
	/// </summary>
	class BgFileSetType : BgType<BgFileSet>
	{
		/// <inheritdoc/>
		public override BgFileSet Constant(object value) => new BgFileSetInputExpr((BgNodeOutput[])value);

		/// <inheritdoc/>
		public override BgFileSet Wrap(BgExpr expr) => new BgFileSetWrappedExpr(expr);
	}

	#region Expression classes

	class BgFileSetInputExpr : BgFileSet
	{
		public IReadOnlyList<BgNodeOutput> Outputs { get; }

		public BgFileSetInputExpr(BgNodeOutput[] outputs)
			: base(BgExprFlags.None)
		{
			Outputs = outputs;
		}

		public override void Write(BgBytecodeWriter writer)
		{
			throw new NotImplementedException();
		}
	}

	/// <summary>
	/// /
	/// </summary>
	public class BgFileSetOutputExpr : BgFileSet
	{
		/// <summary>
		/// 
		/// </summary>
		public FileSet Value { get; }

		/// <summary>
		/// 
		/// </summary>
		/// <param name="value"></param>
		public BgFileSetOutputExpr(FileSet value)
			: base(BgExprFlags.NotInterned)
		{
			Value = value;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			throw new NotImplementedException();
		}
	}

	class BgFileSetFromNodeExpr : BgFileSet
	{
		public BgNodeSpec Node { get; }

		public BgFileSetFromNodeExpr(BgNodeSpec node)
			: base(BgExprFlags.NotInterned)
		{
			Node = node;
		}

		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.FileSetFromNode);
			writer.WriteExpr(Node);
		}
	}

	class BgFileSetFromNodeOutputExpr : BgFileSet
	{
		public BgNodeSpec Node { get; }
		public int OutputIndex { get; }

		public BgFileSetFromNodeOutputExpr(BgNodeSpec node, int outputIndex)
			: base(BgExprFlags.NotInterned)
		{
			Node = node;
			OutputIndex = outputIndex;
		}

		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.FileSetFromNodeOutput);
			writer.WriteExpr(Node);
			writer.WriteUnsignedInteger(OutputIndex);
		}
	}

	class BgFileSetWrappedExpr : BgFileSet
	{
		public BgExpr Expr { get; }

		public BgFileSetWrappedExpr(BgExpr expr)
			: base(expr.Flags)
		{
			Expr = expr;
		}

		public override void Write(BgBytecodeWriter writer) => Expr.Write(writer);
	}

	#endregion
}

