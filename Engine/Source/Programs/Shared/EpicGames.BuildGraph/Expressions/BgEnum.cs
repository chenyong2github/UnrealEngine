// Copyright Epic Games, Inc. All Rights Reserved.

using System;

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
		public static implicit operator BgEnum<TEnum>(TEnum value)
		{
			return new BgEnumConstantExpr<TEnum>(value);
		}

		/// <summary>
		/// Explicit conversion from a string value
		/// </summary>
		public static explicit operator BgEnum<TEnum>(BgString value)
		{
			return new BgEnumParseExpr<TEnum>(value);
		}

		/// <inheritdoc/>
		public BgEnum<TEnum> IfThen(BgBool condition, BgEnum<TEnum> valueIfTrue) => new BgEnumChooseExpr<TEnum>(condition, valueIfTrue, this);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => Compute(context);

		/// <inheritdoc/>
		public abstract TEnum Compute(BgExprContext context);

		/// <inheritdoc/>
		public BgString ToBgString() => new BgEnumFormatExpr<TEnum>(this);
	}

	/// <summary>
	/// Type traits for a <see cref="BgEnum{TEnum}"/>
	/// </summary>
	class BgEnumType<TEnum> : BgTypeBase<BgEnum<TEnum>> where TEnum : struct
	{
		/// <inheritdoc/>
		public override BgEnum<TEnum> DeserializeArgument(string text) => Enum.Parse<TEnum>(text);

		/// <inheritdoc/>
		public override string SerializeArgument(BgEnum<TEnum> value, BgExprContext context) => value.Compute(context).ToString() ?? String.Empty;

		/// <inheritdoc/>
		public override BgEnum<TEnum> CreateConstant(object value) => new BgEnumConstantExpr<TEnum>((TEnum)value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgEnum<TEnum>> CreateVariable() => throw new NotImplementedException();
	}

	#region Expression classes

	class BgEnumParseExpr<TEnum> : BgEnum<TEnum> where TEnum : struct
	{
		public BgString Value { get; }

		public BgEnumParseExpr(BgString value)
		{
			Value = value;
		}

		public override TEnum Compute(BgExprContext context) => Enum.Parse<TEnum>(Value.Compute(context));

		public override string ToString()
		{
			return $"Parse<{typeof(TEnum).Name}>({Value})";
		}
	}

	class BgEnumConstantExpr<TEnum> : BgEnum<TEnum> where TEnum : struct
	{
		public TEnum Value { get; }

		public BgEnumConstantExpr(TEnum value)
		{
			Value = value;
		}

		public override TEnum Compute(BgExprContext context) => Value;

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

		public BgEnumChooseExpr(BgBool condition, BgEnum<TEnum> valueIfTrue, BgEnum<TEnum> valueIfFalse)
		{
			Condition = condition;
			ValueIfTrue = valueIfTrue;
			ValueIfFalse = valueIfFalse;
		}

		public override TEnum Compute(BgExprContext context) => Condition.Compute(context) ? ValueIfTrue.Compute(context) : ValueIfFalse.Compute(context);

		public override string ToString()
		{
			return $"Choose({Condition}, {ValueIfTrue}, {ValueIfFalse});";
		}
	}

	class BgEnumFormatExpr<TEnum> : BgString where TEnum : struct
	{
		public BgEnum<TEnum> Value { get; }

		public BgEnumFormatExpr(BgEnum<TEnum> value)
		{
			Value = value;
		}

		public override string Compute(BgExprContext context) => Value.Compute(context).ToString() ?? String.Empty;

		public override string ToString()
		{
			return $"Format({Value})";
		}
	}

	class BgEnumVariableExpr<TEnum> : BgEnum<TEnum>, IBgExprVariable<BgEnum<TEnum>> where TEnum : struct
	{
		public BgEnum<TEnum> Value { get; set; } = null!;

		public override TEnum Compute(BgExprContext context) => Value.Compute(context);
	}

	#endregion
}
