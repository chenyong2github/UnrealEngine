// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Xml;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using OpenTracing;
using OpenTracing.Util;
using System.Threading.Tasks;

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
		public static implicit operator BgInt(int Value)
		{
			return new BgIntConstantExpr(Value);
		}

		/// <inheritdoc/>
		public static BgInt operator +(BgInt Lhs, BgInt Rhs) => new BgIntAddExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgInt operator -(BgInt Lhs, BgInt Rhs) => new BgIntAddExpr(Lhs, new BgIntNegateExpr(Rhs));

		/// <inheritdoc/>
		public static BgBool operator <(BgInt Lhs, BgInt Rhs) => new BgIntLtExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator >(BgInt Lhs, BgInt Rhs) => new BgIntGtExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator ==(BgInt Lhs, BgInt Rhs) => new BgIntEqExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator !=(BgInt Lhs, BgInt Rhs) => new BgBoolNotExpr(new BgIntEqExpr(Lhs, Rhs));

		/// <inheritdoc/>
		public static BgBool operator <=(BgInt Lhs, BgInt Rhs) => new BgBoolNotExpr(new BgIntGtExpr(Lhs, Rhs));

		/// <inheritdoc/>
		public static BgBool operator >=(BgInt Lhs, BgInt Rhs) => new BgBoolNotExpr(new BgIntLtExpr(Lhs, Rhs));

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => Compute(Context);

		/// <inheritdoc/>
		public abstract int Compute(BgExprContext Context);

		/// <inheritdoc/>
		public BgInt IfThen(BgBool Condition, BgInt ValueIfTrue) => new BgIntChooseExpr(Condition, ValueIfTrue, this);

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
		public override BgInt DeserializeArgument(string Value) => (BgInt)int.Parse(Value);

		/// <inheritdoc/>
		public override string SerializeArgument(BgInt Value, BgExprContext Context) => Value.Compute(Context).ToString();

		/// <inheritdoc/>
		public override BgInt CreateConstant(object Value) => new BgIntConstantExpr((int)Value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgInt> CreateVariable() => new BgIntVariableExpr();
	}

	#region Expression classes

	class BgIntEqExpr : BgBool
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntEqExpr(BgInt Lhs, BgInt Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override bool Compute(BgExprContext Context) => Lhs.Compute(Context) == Rhs.Compute(Context);
	}

	class BgIntLtExpr : BgBool
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntLtExpr(BgInt Lhs, BgInt Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override bool Compute(BgExprContext Context) => Lhs.Compute(Context) < Rhs.Compute(Context);
	}

	class BgIntGtExpr : BgBool
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntGtExpr(BgInt Lhs, BgInt Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override bool Compute(BgExprContext Context) => Lhs.Compute(Context) > Rhs.Compute(Context);
	}

	class BgIntAddExpr : BgInt
	{
		public BgInt Lhs { get; }
		public BgInt Rhs { get; }

		public BgIntAddExpr(BgInt Lhs, BgInt Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override int Compute(BgExprContext Context) => Lhs.Compute(Context) + Rhs.Compute(Context);
	}

	class BgIntNegateExpr : BgInt
	{
		public BgInt Inner { get; }

		public BgIntNegateExpr(BgInt Inner)
		{
			this.Inner = Inner;
		}

		public override int Compute(BgExprContext Context) => -Inner.Compute(Context);
	}

	class BgIntChooseExpr : BgInt
	{
		public BgBool Condition;
		public BgInt ValueIfTrue;
		public BgInt ValueIfFalse;

		public BgIntChooseExpr(BgBool Condition, BgInt ValueIfTrue, BgInt ValueIfFalse)
		{
			this.Condition = Condition;
			this.ValueIfTrue = ValueIfTrue;
			this.ValueIfFalse = ValueIfFalse;
		}

		public override int Compute(BgExprContext Context) => Condition.Compute(Context) ? ValueIfTrue.Compute(Context) : ValueIfFalse.Compute(Context);
	}

	class BgIntConstantExpr : BgInt
	{
		public int Value { get; }

		public BgIntConstantExpr(int Value)
		{
			this.Value = Value;
		}

		public override int Compute(BgExprContext Context) => Value;
	}

	class BgIntVariableExpr : BgInt, IBgExprVariable<BgInt>
	{
		public BgInt Value { get; set; } = 0;

		public override int Compute(BgExprContext Context) => Value.Compute(Context);
	}

	class BgIntFormatExpr : BgString
	{
		BgInt Value;

		public BgIntFormatExpr(BgInt Value)
		{
			this.Value = Value;
		}

		public override string Compute(BgExprContext Context) => Value.Compute(Context).ToString();
	}

#endregion
}
