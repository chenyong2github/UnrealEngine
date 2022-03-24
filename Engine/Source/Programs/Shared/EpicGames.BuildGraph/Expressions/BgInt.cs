// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Abstract base class for expressions returning a 32-bit integer value 
	/// </summary>
	[BgType(typeof(BgIntTraits))]
	public abstract class BgInt : IBgExpr<BgInt>
	{
		/// <summary>
		/// Implicit conversion from an integer value
		/// </summary>
		public static implicit operator BgInt(int value)
		{
			return new BgIntConstantExpr(value);
		}

		/// <inheritdoc/>
		public static BgInt operator +(BgInt lhs, BgInt rhs) => new BgIntAddExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgInt operator -(BgInt lhs, BgInt rhs) => new BgIntAddExpr(lhs, new BgIntNegateExpr(rhs));

		/// <inheritdoc/>
		public static BgBool operator <(BgInt lhs, BgInt rhs) => new BgIntLtExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator >(BgInt lhs, BgInt rhs) => new BgIntGtExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator ==(BgInt lhs, BgInt rhs) => new BgIntEqExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator !=(BgInt lhs, BgInt rhs) => new BgBoolNotExpr(new BgIntEqExpr(lhs, rhs));

		/// <inheritdoc/>
		public static BgBool operator <=(BgInt lhs, BgInt rhs) => new BgBoolNotExpr(new BgIntGtExpr(lhs, rhs));

		/// <inheritdoc/>
		public static BgBool operator >=(BgInt lhs, BgInt rhs) => new BgBoolNotExpr(new BgIntLtExpr(lhs, rhs));

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => Compute(context);

		/// <inheritdoc/>
		public abstract int Compute(BgExprContext context);

		/// <inheritdoc/>
		public BgInt IfThen(BgBool condition, BgInt valueIfTrue) => new BgIntChooseExpr(condition, valueIfTrue, this);

		/// <inheritdoc/>
		public override bool Equals(object? obj) => throw new InvalidOperationException();

		/// <inheritdoc/>
		public override int GetHashCode() => throw new InvalidOperationException();

		/// <inheritdoc/>
		public BgString ToBgString() => new BgIntFormatExpr(this);
	}

	/// <summary>
	/// Traits for a <see cref="BgInt"/>
	/// </summary>
	class BgIntTraits : BgTypeBase<BgInt>
	{
		/// <inheritdoc/>
		public override BgInt DeserializeArgument(string value) => (BgInt)int.Parse(value);

		/// <inheritdoc/>
		public override string SerializeArgument(BgInt value, BgExprContext context) => value.Compute(context).ToString();

		/// <inheritdoc/>
		public override BgInt CreateConstant(object value) => new BgIntConstantExpr((int)value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgInt> CreateVariable() => new BgIntVariableExpr();
	}

	#region Expression classes

	class BgIntEqExpr : BgBool
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntEqExpr(BgInt lhs, BgInt rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override bool Compute(BgExprContext context) => Lhs.Compute(context) == Rhs.Compute(context);
	}

	class BgIntLtExpr : BgBool
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntLtExpr(BgInt lhs, BgInt rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override bool Compute(BgExprContext context) => Lhs.Compute(context) < Rhs.Compute(context);
	}

	class BgIntGtExpr : BgBool
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntGtExpr(BgInt lhs, BgInt rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override bool Compute(BgExprContext context) => Lhs.Compute(context) > Rhs.Compute(context);
	}

	class BgIntAddExpr : BgInt
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntAddExpr(BgInt lhs, BgInt rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override int Compute(BgExprContext context) => Lhs.Compute(context) + Rhs.Compute(context);
	}

	class BgIntNegateExpr : BgInt
	{
		public BgInt Inner { get; }

		public BgIntNegateExpr(BgInt inner)
		{
			Inner = inner;
		}

		public override int Compute(BgExprContext context) => -Inner.Compute(context);
	}

	class BgIntChooseExpr : BgInt
	{
		public BgBool Condition { get; }
		public BgInt ValueIfTrue { get; }
		public BgInt ValueIfFalse { get; }

		public BgIntChooseExpr(BgBool condition, BgInt valueIfTrue, BgInt valueIfFalse)
		{
			Condition = condition;
			ValueIfTrue = valueIfTrue;
			ValueIfFalse = valueIfFalse;
		}

		public override int Compute(BgExprContext context) => Condition.Compute(context) ? ValueIfTrue.Compute(context) : ValueIfFalse.Compute(context);
	}

	class BgIntConstantExpr : BgInt
	{
		public int Value { get; }

		public BgIntConstantExpr(int value)
		{
			Value = value;
		}

		public override int Compute(BgExprContext context) => Value;
	}

	class BgIntVariableExpr : BgInt, IBgExprVariable<BgInt>
	{
		public BgInt Value { get; set; } = 0;

		public override int Compute(BgExprContext context) => Value.Compute(context);
	}

	class BgIntFormatExpr : BgString
	{
		public BgInt Value { get; }

		public BgIntFormatExpr(BgInt value)
		{
			Value = value;
		}

		public override string Compute(BgExprContext context) => Value.Compute(context).ToString();
	}

	#endregion
}
