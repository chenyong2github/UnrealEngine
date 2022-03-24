// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Represents a placeholder for the output from a node, which can be exchanged for the artifacts produced by a node at runtime
	/// </summary>
	[BgType(typeof(BgFileSetType))]
	public abstract class BgFileSet : IBgExpr<BgFileSet>
	{
		/// <inheritdoc/>
		public BgFileSet IfThen(BgBool condition, BgFileSet valueIfTrue) => new BgFileSetIfThenExpr(condition, valueIfTrue, this);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => Compute(context);

		/// <inheritdoc/>
		public abstract BgFileSet Compute(BgExprContext context);

		/// <summary>
		/// Gets the tag name for this fileset
		/// </summary>
		/// <returns></returns>
		public string ComputeTag(BgExprContext context) => ((BgFileSetTagExpr)Compute(context)).Name;

		/// <summary>
		/// Gets the tag name for this fileset
		/// </summary>
		/// <returns></returns>
		public FileSet ComputeValue(BgExprContext context) => ((BgFileSetValueExpr)Compute(context)).FileSet;

		/// <summary>
		/// Implicit conversion from a file set to a functional file set
		/// </summary>
		/// <param name="fileSet"></param>
		public static implicit operator BgFileSet(FileSet fileSet) => new BgFileSetValueExpr(fileSet);

		/// <inheritdoc/>
		public BgString ToBgString() => new BgFileSetToStringExpr(this);
	}

	/// <summary>
	/// Traits for a <see cref="BgFileSet"/>
	/// </summary>
	class BgFileSetType : BgTypeBase<BgFileSet>
	{
		/// <inheritdoc/>
		public override BgFileSet DeserializeArgument(string text) => new BgFileSetTagExpr(text);

		/// <inheritdoc/>
		public override string SerializeArgument(BgFileSet value, BgExprContext context) => ((BgFileSetTagExpr)value.Compute(context)).Name;

		/// <inheritdoc/>
		public override BgFileSet CreateConstant(object value) => new BgFileSetTagExpr(((BgFileSetTagExpr)value).Name);

		/// <inheritdoc/>
		public override IBgExprVariable<BgFileSet> CreateVariable() => throw new NotImplementedException();
	}

	#region Expression classes

	class BgFileSetIfThenExpr : BgFileSet
	{
		public BgBool Condition { get; }
		public BgFileSet ValueIfTrue { get; }
		public BgFileSet ValueIfFalse { get; }

		public BgFileSetIfThenExpr(BgBool condition, BgFileSet valueIfTrue, BgFileSet valueIfFalse)
		{
			Condition = condition;
			ValueIfTrue = valueIfTrue;
			ValueIfFalse = valueIfFalse;
		}

		public override BgFileSet Compute(BgExprContext context) => Condition.Compute(context) ? ValueIfTrue.Compute(context) : ValueIfFalse.Compute(context);
	}

	class BgFileSetToStringExpr : BgString
	{
		public BgFileSet FileSet { get; }

		internal BgFileSetToStringExpr(BgFileSet token)
		{
			FileSet = token;
		}

		public override string Compute(BgExprContext context)
		{
			BgFileSet fileSetValue = FileSet.Compute(context);
			if (fileSetValue is BgFileSetTagExpr tagExpr)
			{
				return tagExpr.Name;
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

		public BgFileSetTagExpr(string name)
		{
			Name = name;
		}

		public override BgFileSet Compute(BgExprContext context) => this;
	}

	class BgFileSetTagFromStringExpr : BgFileSet
	{
		public BgString Name { get; }

		public BgFileSetTagFromStringExpr(BgString name)
		{
			Name = name;
		}

		public override BgFileSet Compute(BgExprContext context) => new BgFileSetTagExpr(Name.Compute(context));
	}

	class BgFileSetVariableExpr : BgFileSet, IBgExprVariable<BgFileSet>
	{
		public BgFileSet Value { get; set; } = new BgFileSetValueExpr(FileSet.Empty);

		public override BgFileSet Compute(BgExprContext context) => Value.Compute(context);
	}

	class BgFileSetValueExpr : BgFileSet
	{
		public FileSet FileSet { get; }

		public BgFileSetValueExpr(FileSet fileSet)
		{
			FileSet = fileSet;
		}

		public override BgFileSet Compute(BgExprContext context) => this;
	}

	#endregion
}

