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
using System.Text.RegularExpressions;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Abstract base class for expressions returning an enum. Use <see cref="BgEnum{TEnum}"/> for a strongly typed version.
	/// </summary>
	public interface IBgEnum : IBgExpr
	{
		/// <summary>
		/// The enum type
		/// </summary>
		Type EnumType { get; }
	}

	/// <summary>
	/// Abstract base class for expressions returning a string value 
	/// </summary>
	[BgType(typeof(BgEnumType<>))]
	public abstract class BgEnum<TEnum> : IBgEnum, IBgExpr<BgEnum<TEnum>> where TEnum : struct
	{
		/// <inheritdoc/>
		public Type EnumType => typeof(TEnum);

		/// <summary>
		/// Implicit conversion from a regular enum type
		/// </summary>
		public static implicit operator BgEnum<TEnum>(TEnum Value)
		{
			return new BgEnumConstantExpr<TEnum>(Value);
		}

		/// <summary>
		/// Explicit conversion from a string value
		/// </summary>
		public static explicit operator BgEnum<TEnum>(BgString Value)
		{
			return new BgEnumParseExpr<TEnum>(Value);
		}

		/// <inheritdoc/>
		public BgEnum<TEnum> IfThen(BgBool Condition, BgEnum<TEnum> ValueIfTrue) => new BgEnumChooseExpr<TEnum>(Condition, ValueIfTrue, this);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => Compute(Context);

		/// <inheritdoc/>
		public abstract TEnum Compute(BgExprContext Context);

		/// <inheritdoc/>
		public BgString ToBgString() => new BgEnumFormatExpr<TEnum>(this);
	}

	/// <summary>
	/// Type traits for a <see cref="BgEnum{TEnum}"/>
	/// </summary>
	class BgEnumType<TEnum> : BgTypeBase<BgEnum<TEnum>> where TEnum : struct
	{
		/// <inheritdoc/>
		public override BgEnum<TEnum> DeserializeArgument(string Text) => Enum.Parse<TEnum>(Text);

		/// <inheritdoc/>
		public override string SerializeArgument(BgEnum<TEnum> Value, BgExprContext Context) => Value.Compute(Context).ToString() ?? String.Empty;

		/// <inheritdoc/>
		public override BgEnum<TEnum> CreateConstant(object Value) => new BgEnumConstantExpr<TEnum>((TEnum)Value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgEnum<TEnum>> CreateVariable() => throw new NotImplementedException();
	}

	#region Expression classes

	class BgEnumParseExpr<TEnum> : BgEnum<TEnum> where TEnum : struct
	{
		public BgString Value { get; }

		public BgEnumParseExpr(BgString Value)
		{
			this.Value = Value;
		}

		public override TEnum Compute(BgExprContext Context) => Enum.Parse<TEnum>(Value.Compute(Context));

		public override string ToString()
		{
			return $"Parse<{typeof(TEnum).Name}>({Value})";
		}
	}

	class BgEnumConstantExpr<TEnum> : BgEnum<TEnum> where TEnum : struct
	{
		public TEnum Value { get; }

		public BgEnumConstantExpr(TEnum Value)
		{
			this.Value = Value;
		}

		public override TEnum Compute(BgExprContext Context) => Value;

		public override string ToString()
		{
			return $"\"{Value}\"";
		}
	}

	class BgEnumChooseExpr<TEnum> : BgEnum<TEnum> where TEnum : struct
	{
		public BgBool Condition { get; }
		public BgEnum<TEnum> ValueIfTrue { get; }
		public BgEnum<TEnum> ValueIfFalse { get; }

		public BgEnumChooseExpr(BgBool Condition, BgEnum<TEnum> ValueIfTrue, BgEnum<TEnum> ValueIfFalse)
		{
			this.Condition = Condition;
			this.ValueIfTrue = ValueIfTrue;
			this.ValueIfFalse = ValueIfFalse;
		}

		public override TEnum Compute(BgExprContext Context) => Condition.Compute(Context) ? ValueIfTrue.Compute(Context) : ValueIfFalse.Compute(Context);

		public override string ToString()
		{
			return $"Choose({Condition}, {ValueIfTrue}, {ValueIfFalse});";
		}
	}

	class BgEnumFormatExpr<TEnum> : BgString where TEnum : struct
	{
		public BgEnum<TEnum> Value { get; }

		public BgEnumFormatExpr(BgEnum<TEnum> Value)
		{
			this.Value = Value;
		}

		public override string Compute(BgExprContext Context) => Value.Compute(Context).ToString() ?? String.Empty;

		public override string ToString()
		{
			return $"Format({Value})";
		}
	}

	class BgEnumVariableExpr<TEnum> : BgEnum<TEnum>, IBgExprVariable<BgEnum<TEnum>> where TEnum : struct
	{
		public BgEnum<TEnum> Value { get; set; } = null!;

		public override TEnum Compute(BgExprContext Context) => Value.Compute(Context);
	}

	#endregion
}
