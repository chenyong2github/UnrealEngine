// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Abstract base class for expressions returning a boolean value 
	/// </summary>
	[BgType(typeof(BgBoolType))]
	public abstract class BgBool : IBgExpr<BgBool>
	{
		/// <summary>
		/// Constant value for false
		/// </summary>
		public static BgBool False { get; } = false;

		/// <summary>
		/// Constant value for true
		/// </summary>
		public static BgBool True { get; } = true;

		/// <inheritdoc/>
		public BgBool IfThen(BgBool condition, BgBool valueIfTrue) => new BgBoolChooseExpr(condition, valueIfTrue, this);

		/// <summary>
		/// Implict conversion operator from a boolean literal
		/// </summary>
		public static implicit operator BgBool(bool value)
		{
			return new BgBoolConstantExpr(value);
		}

		/// <inheritdoc/>
		public static BgBool operator !(BgBool inner) => new BgBoolNotExpr(inner);

		/// <inheritdoc/>
		public static BgBool operator &(BgBool lhs, BgBool rhs) => new BgBoolAndExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator |(BgBool lhs, BgBool rhs) => new BgBoolOrExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator ==(BgBool lhs, BgBool rhs) => new BgBoolEqExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator !=(BgBool lhs, BgBool rhs) => new BgBoolNotExpr(new BgBoolEqExpr(lhs, rhs));

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => Compute(context);

		/// <inheritdoc/>
		public abstract bool Compute(BgExprContext context);

		/// <inheritdoc/>
		public sealed override bool Equals(object? obj) => throw new InvalidOperationException();

		/// <inheritdoc/>
		public sealed override int GetHashCode() => throw new InvalidOperationException();

		/// <inheritdoc/>
		public BgString ToBgString() => new BgBoolFormatExpr(this);
	}

	/// <summary>
	/// Type traits for a <see cref="BgBool"/>
	/// </summary>
	class BgBoolType : BgTypeBase<BgBool>
	{
		/// <inheritdoc/>
		public override BgBool DeserializeArgument(string text) => Boolean.Parse(text);

		/// <inheritdoc/>
		public override string SerializeArgument(BgBool value, BgExprContext context) => value.Compute(context).ToString();

		/// <inheritdoc/>
		public override BgBool CreateConstant(object value) => new BgBoolConstantExpr((bool)value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgBool> CreateVariable() => new BgBoolVariableExpr();
	}

	#region Expression classes

	class BgBoolNotExpr : BgBool
	{
		public BgBool Inner { get; }

		public BgBoolNotExpr(BgBool inner)
		{
			Inner = inner;
		}

		public override bool Compute(BgExprContext context) => !Inner.Compute(context);
	}

	class BgBoolEqExpr : BgBool
	{
		public BgBool Lhs { get; }
		public BgBool Rhs { get; }

		public BgBoolEqExpr(BgBool lhs, BgBool rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override bool Compute(BgExprContext context) => Lhs.Compute(context) == Rhs.Compute(context);
	}

	class BgBoolAndExpr : BgBool
	{
		public BgBool Lhs { get; }
		public BgBool Rhs { get; }

		public BgBoolAndExpr(BgBool lhs, BgBool rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override bool Compute(BgExprContext context) => Lhs.Compute(context) && Rhs.Compute(context);
	}

	class BgBoolOrExpr : BgBool
	{
		public BgBool Lhs { get; }
		public BgBool Rhs { get; }

		public BgBoolOrExpr(BgBool lhs, BgBool rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override bool Compute(BgExprContext context) => Lhs.Compute(context) || Rhs.Compute(context);
	}

	class BgBoolChooseExpr : BgBool
	{
		public BgBool Condition { get; }
		public BgBool ValueIfTrue { get; }
		public BgBool ValueIfFalse { get; }

		public BgBoolChooseExpr(BgBool condition, BgBool valueIfTrue, BgBool valueIfFalse)
		{
			Condition = condition;
			ValueIfTrue = valueIfTrue;
			ValueIfFalse = valueIfFalse;
		}

		public override bool Compute(BgExprContext context) => Condition.Compute(context) ? ValueIfTrue.Compute(context) : ValueIfFalse.Compute(context);
	}

	class BgBoolVariableExpr : BgBool, IBgExprVariable<BgBool>
	{
		public BgBool Value { get; set; } = BgBool.False;

		public override bool Compute(BgExprContext context) => Value.Compute(context);
	}

	class BgBoolConstantExpr : BgBool
	{
		public bool Value { get; }

		public BgBoolConstantExpr(bool value)
		{
			Value = value;
		}

		public override bool Compute(BgExprContext context) => Value;
	}

	class BgBoolFormatExpr : BgString
	{
		public BgBool Value { get; }

		public BgBoolFormatExpr(BgBool value)
		{
			Value = value;
		}

		public override string Compute(BgExprContext context) => Value.Compute(context).ToString();
	}

	#endregion
}
