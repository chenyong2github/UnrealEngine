// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Represents a placeholder for the output from a node, which can be exchanged for the artifacts produced by a node at runtime
	/// </summary>
	[BgType(typeof(BgFileSetType))]
	public abstract class BgFileSet : IBgExpr<BgFileSet>
	{
		/// <inheritdoc/>
		public BgFileSet IfThen(BgBool Condition, BgFileSet ValueIfTrue) => new BgFileSetIfThenExpr(Condition, ValueIfTrue, this);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => Compute(Context);

		/// <inheritdoc/>
		public abstract BgFileSet Compute(BgExprContext Context);

		/// <summary>
		/// Gets the tag name for this fileset
		/// </summary>
		/// <returns></returns>
		public string ComputeTag(BgExprContext Context) => ((BgFileSetTagExpr)Compute(Context)).Name;

		/// <summary>
		/// Gets the tag name for this fileset
		/// </summary>
		/// <returns></returns>
		public FileSet ComputeValue(BgExprContext Context) => ((BgFileSetValueExpr)Compute(Context)).FileSet;

		/// <summary>
		/// Implicit conversion from a file set to a functional file set
		/// </summary>
		/// <param name="FileSet"></param>
		public static implicit operator BgFileSet(FileSet FileSet) => new BgFileSetValueExpr(FileSet);

		/// <inheritdoc/>
		public BgString ToBgString() => new BgFileSetToStringExpr(this);
	}

	/// <summary>
	/// Traits for a <see cref="BgFileSet"/>
	/// </summary>
	class BgFileSetType : BgTypeBase<BgFileSet>
	{
		/// <inheritdoc/>
		public override BgFileSet DeserializeArgument(string Text) => new BgFileSetTagExpr(Text);

		/// <inheritdoc/>
		public override string SerializeArgument(BgFileSet Value, BgExprContext Context) => ((BgFileSetTagExpr)Value.Compute(Context)).Name;

		/// <inheritdoc/>
		public override BgFileSet CreateConstant(object Value) => new BgFileSetTagExpr(((BgFileSetTagExpr)Value).Name);

		/// <inheritdoc/>
		public override IBgExprVariable<BgFileSet> CreateVariable() => throw new NotImplementedException();
	}

	#region Expression classes

	class BgFileSetIfThenExpr : BgFileSet
	{
		public BgBool Condition;
		public BgFileSet ValueIfTrue;
		public BgFileSet ValueIfFalse;

		public BgFileSetIfThenExpr(BgBool Condition, BgFileSet ValueIfTrue, BgFileSet ValueIfFalse)
		{
			this.Condition = Condition;
			this.ValueIfTrue = ValueIfTrue;
			this.ValueIfFalse = ValueIfFalse;
		}

		public override BgFileSet Compute(BgExprContext Context) => Condition.Compute(Context) ? ValueIfTrue.Compute(Context) : ValueIfFalse.Compute(Context);
	}

	class BgFileSetToStringExpr : BgString
	{
		BgFileSet FileSet;

		internal BgFileSetToStringExpr(BgFileSet Token)
		{
			this.FileSet = Token;
		}

		public override string Compute(BgExprContext Context)
		{
			BgFileSet FileSetValue = FileSet.Compute(Context);
			if (FileSetValue is BgFileSetTagExpr TagExpr)
			{
				return TagExpr.Name;
			}
			else
			{
				return "{...}";
			}
		}
	}

	class BgFileSetTagExpr : BgFileSet
	{
		public string Name { get; set; }

		public BgFileSetTagExpr(string Name)
		{
			this.Name = Name;
		}

		public override BgFileSet Compute(BgExprContext Context) => this;
	}

	class BgFileSetTagFromStringExpr : BgFileSet
	{
		public BgString Name { get; }

		public BgFileSetTagFromStringExpr(BgString Name)
		{
			this.Name = Name;
		}

		public override BgFileSet Compute(BgExprContext Context) => new BgFileSetTagExpr(Name.Compute(Context));
	}

	class BgFileSetVariableExpr : BgFileSet, IBgExprVariable<BgFileSet>
	{
		public BgFileSet Value { get; set; } = new BgFileSetValueExpr(FileSet.Empty);

		public override BgFileSet Compute(BgExprContext Context) => Value.Compute(Context);
	}

	class BgFileSetValueExpr : BgFileSet
	{
		public FileSet FileSet { get; }

		public BgFileSetValueExpr(FileSet FileSet)
		{
			this.FileSet = FileSet;
		}

		public override BgFileSet Compute(BgExprContext Context) => this;
	}

	#endregion
}

