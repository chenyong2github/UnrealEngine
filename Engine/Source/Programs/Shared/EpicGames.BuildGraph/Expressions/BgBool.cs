// Copyright Epic Games, Inc. All Rights Reserved.

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
using System.Threading.Tasks;

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
		public BgBool IfThen(BgBool Condition, BgBool ValueIfTrue) => new BgBoolChooseExpr(Condition, ValueIfTrue, this);

		/// <summary>
		/// Implict conversion operator from a boolean literal
		/// </summary>
		public static implicit operator BgBool(bool Value)
		{
			return new BgBoolConstantExpr(Value);
		}

		/// <inheritdoc/>
		public static BgBool operator !(BgBool Inner) => new BgBoolNotExpr(Inner);

		/// <inheritdoc/>
		public static BgBool operator &(BgBool Lhs, BgBool Rhs) => new BgBoolAndExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator |(BgBool Lhs, BgBool Rhs) => new BgBoolOrExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator ==(BgBool Lhs, BgBool Rhs) => new BgBoolEqExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator !=(BgBool Lhs, BgBool Rhs) => new BgBoolNotExpr(new BgBoolEqExpr(Lhs, Rhs));

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => Compute(Context);

		/// <inheritdoc/>
		public abstract bool Compute(BgExprContext Context);

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
		public override BgBool DeserializeArgument(string Text) => bool.Parse(Text);

		/// <inheritdoc/>
		public override string SerializeArgument(BgBool Value, BgExprContext Context) => Value.Compute(Context).ToString();

		/// <inheritdoc/>
		public override BgBool CreateConstant(object Value) => new BgBoolConstantExpr((bool)Value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgBool> CreateVariable() => new BgBoolVariableExpr();
	}

	#region Expression classes

	class BgBoolNotExpr : BgBool
	{
		public BgBool Inner { get; }

		public BgBoolNotExpr(BgBool Inner)
		{
			this.Inner = Inner;
		}

		public override bool Compute(BgExprContext Context) => !Inner.Compute(Context);
	}

	class BgBoolEqExpr : BgBool
	{
		public BgBool Lhs { get; }
		public BgBool Rhs { get; }

		public BgBoolEqExpr(BgBool Lhs, BgBool Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override bool Compute(BgExprContext Context) => Lhs.Compute(Context) == Rhs.Compute(Context);
	}

	class BgBoolAndExpr : BgBool
	{
		public BgBool Lhs { get; }
		public BgBool Rhs { get; }

		public BgBoolAndExpr(BgBool Lhs, BgBool Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override bool Compute(BgExprContext Context) => Lhs.Compute(Context) && Rhs.Compute(Context);
	}

	class BgBoolOrExpr : BgBool
	{
		public BgBool Lhs { get; }
		public BgBool Rhs { get; }

		public BgBoolOrExpr(BgBool Lhs, BgBool Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override bool Compute(BgExprContext Context) => Lhs.Compute(Context) || Rhs.Compute(Context);
	}

	class BgBoolChooseExpr : BgBool
	{
		public BgBool Condition;
		public BgBool ValueIfTrue;
		public BgBool ValueIfFalse;

		public BgBoolChooseExpr(BgBool Condition, BgBool ValueIfTrue, BgBool ValueIfFalse)
		{
			this.Condition = Condition;
			this.ValueIfTrue = ValueIfTrue;
			this.ValueIfFalse = ValueIfFalse;
		}

		public override bool Compute(BgExprContext Context) => Condition.Compute(Context) ? ValueIfTrue.Compute(Context) : ValueIfFalse.Compute(Context);
	}

	class BgBoolVariableExpr : BgBool, IBgExprVariable<BgBool>
	{
		public BgBool Value { get; set; } = BgBool.False;

		public override bool Compute(BgExprContext Context) => Value.Compute(Context);
	}

	class BgBoolConstantExpr : BgBool
	{
		public bool Value { get; }

		public BgBoolConstantExpr(bool Value)
		{
			this.Value = Value;
		}

		public override bool Compute(BgExprContext Context) => Value;
	}

	class BgBoolFormatExpr : BgString
	{
		BgBool Value;

		public BgBoolFormatExpr(BgBool Value)
		{
			this.Value = Value;
		}

		public override string Compute(BgExprContext Context) => Value.Compute(Context).ToString();
	}

	#endregion
}
