// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Abstract base class for expressions returning a string value 
	/// </summary>
	[BgType(typeof(BgStringTraits))]
	public abstract class BgString : IBgExpr<BgString>
	{
		/// <summary>
		/// Constant value for an empty string
		/// </summary>
		public static BgString Empty { get; } = new BgStringConstantExpr(String.Empty);

		/// <summary>
		/// Implicit conversion from a regular string type
		/// </summary>
		public static implicit operator BgString(string value)
		{
			return new BgStringConstantExpr(value);
		}

		/// <inheritdoc cref="String.Equals(String?, String?, StringComparison)"/>
		public static BgBool Equals(BgString lhs, BgString rhs, StringComparison comparison = StringComparison.CurrentCulture) => Compare(lhs, rhs, comparison) == 0;

		/// <inheritdoc cref="String.Compare(String?, String?, StringComparison)"/>
		public static BgInt Compare(BgString lhs, BgString rhs, StringComparison comparison = StringComparison.CurrentCulture) => new BgStringCompareExpr(lhs, rhs, comparison);

		/// <inheritdoc cref="String.Join{T}(String?, IEnumerable{T})"/>
		public static BgString Join(BgString separator, BgList<BgString> values) => new BgStringJoinExpr(separator, values);

		/// <inheritdoc cref="String.Format(String, Object?[])"/>
		public static BgString Format(string format, params object?[] args) => new BgStringFormatExpr(format, args);

		/// <inheritdoc/>
		public static BgString operator +(BgString lhs, BgString rhs) => new BgStringConcatExpr(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator ==(BgString lhs, BgString rhs) => Equals(lhs, rhs);

		/// <inheritdoc/>
		public static BgBool operator !=(BgString lhs, BgString rhs) => !Equals(lhs, rhs);

		/// <summary>
		/// Appens another string to this one
		/// </summary>
		public BgString Append(BgString other) => this + other;

		/// <summary>
		/// Appens another string to this one if a condition is true
		/// </summary>
		public BgString AppendIf(BgBool condition, BgString other) => IfThen(condition, this + other);

		/// <inheritdoc/>
		public BgString IfThen(BgBool condition, BgString valueIfTrue) => new BgStringChooseExpr(condition, valueIfTrue, this);

		/// <inheritdoc/>
		public BgBool IsMatch(BgString input, string pattern) => new BgStringIsMatchExpr(input, pattern);

		/// <inheritdoc/>
		public BgString Replace(BgString input, string pattern, string replace) => new BgStringReplaceExpr(input, pattern, replace);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => Compute(context);

		/// <inheritdoc/>
		public abstract string Compute(BgExprContext context);

		/// <inheritdoc/>
		public override bool Equals(object? obj) => throw new InvalidOperationException();

		/// <inheritdoc/>
		public override int GetHashCode() => throw new InvalidOperationException();

		/// <inheritdoc/>
		public BgString ToBgString() => this;
	}

	/// <summary>
	/// Traits implementation for <see cref="BgString"/>
	/// </summary>
	class BgStringTraits : BgTypeBase<BgString>
	{
		/// <inheritdoc/>
		public override BgString DeserializeArgument(string text) => text;

		/// <inheritdoc/>
		public override string SerializeArgument(BgString value, BgExprContext context) => value.Compute(context);

		/// <inheritdoc/>
		public override BgString CreateConstant(object value) => new BgStringConstantExpr((string)value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgString> CreateVariable() => new BgStringVariableExpr();
	}

	#region Expression classes

	class BgStringCompareExpr : BgInt
	{
		public BgString Lhs { get; }
		public BgString Rhs { get; }
		public StringComparison Comparison { get; }

		public BgStringCompareExpr(BgString lhs, BgString rhs, StringComparison comparison)
		{
			Lhs = lhs;
			Rhs = rhs;
			Comparison = comparison;
		}

		public override int Compute(BgExprContext context) => String.Compare(Lhs.Compute(context), Rhs.Compute(context), Comparison);

		public override string ToString() => $"Compare({Lhs}, {Rhs})";
	}

	class BgStringConcatExpr : BgString
	{
		public BgString Lhs { get; }
		public BgString Rhs { get; }

		public BgStringConcatExpr(BgString lhs, BgString rhs)
		{
			Lhs = lhs;
			Rhs = rhs;
		}

		public override string Compute(BgExprContext context) => Lhs.Compute(context) + Rhs.Compute(context);

		public override string ToString() => $"Concat({Lhs}, {Rhs})";
	}

	class BgStringJoinExpr : BgString
	{
		public BgString Separator { get; }
		public BgList<BgString> Values { get; }

		public BgStringJoinExpr(BgString separator, BgList<BgString> values)
		{
			Separator = separator;
			Values = values;
		}

		public override string Compute(BgExprContext context) => String.Join(Separator.Compute(context), Values.Compute(context));

		public override string ToString() => $"Join({Separator}, {Values})";
	}

	class BgStringIsMatchExpr : BgBool
	{
		public BgString Input { get; }
		public string Pattern { get; }

		public BgStringIsMatchExpr(BgString input, string pattern)
		{
			Input = input;
			Pattern = pattern;
		}

		public override bool Compute(BgExprContext context) => Regex.IsMatch(Input.Compute(context), Pattern);
	}

	class BgStringReplaceExpr : BgString
	{
		public BgString Input { get; }
		public string Pattern { get; }
		public string Replacement { get; }

		public BgStringReplaceExpr(BgString input, string pattern, string replacement)
		{
			Input = input;
			Pattern = pattern;
			Replacement = replacement;
		}

		public override string Compute(BgExprContext context) => Regex.Replace(Input.Compute(context), Pattern, Replacement);
	}

	class BgStringFormatExpr : BgString
	{
		new string Format { get; }
		BgString[] Arguments { get; }

		public BgStringFormatExpr(string format, object?[] args)
		{
			Format = format;

			Arguments = new BgString[args.Length];
			for (int idx = 0; idx < args.Length; idx++)
			{
				if (args[idx] is IBgExpr expr)
				{
					Arguments[idx] = expr.ToBgString();
				}
				else if (args[idx] != null)
				{
					Arguments[idx] = args[idx]?.ToString() ?? String.Empty;
				}
				else
				{
					Arguments[idx] = String.Empty;
				}
			}
		}

		public override string Compute(BgExprContext context) => String.Format(Format, Arguments.Select(x => x.Compute(context)).ToArray());

		public override string ToString() => $"Format({String.Join(", ", new[] { Format }.Concat(Arguments.Select(x => x.ToString())))})";
	}

	class BgStringChooseExpr : BgString
	{
		public BgBool Condition { get; }
		public BgString ValueIfTrue { get; }
		public BgString ValueIfFalse { get; }

		public BgStringChooseExpr(BgBool condition, BgString valueIfTrue, BgString valueIfFalse)
		{
			Condition = condition;
			ValueIfTrue = valueIfTrue;
			ValueIfFalse = valueIfFalse;
		}

		public override string Compute(BgExprContext context) => Condition.Compute(context) ? ValueIfTrue.Compute(context) : ValueIfFalse.Compute(context);
	}

	class BgStringVariableExpr : BgString, IBgExprVariable<BgString>
	{
		public BgString Value { get; set; } = BgString.Empty;

		public BgString Variable => this;

		public override string Compute(BgExprContext context) => Value.Compute(context);
	}

	class BgStringConstantExpr : BgString
	{
		public string Value { get; }

		public BgStringConstantExpr(string value)
		{
			Value = value;
		}

		public override string Compute(BgExprContext context) => Value;

		public override bool Equals(object? obj)
		{
			return obj is string objStr && String.Equals(Value, objStr, StringComparison.OrdinalIgnoreCase);
		}

		public override int GetHashCode()
		{
			return String.GetHashCode(Value, StringComparison.OrdinalIgnoreCase);
		}

		public override string ToString()
		{
			return $"\"Value\"";
		}
	}

	#endregion
}
