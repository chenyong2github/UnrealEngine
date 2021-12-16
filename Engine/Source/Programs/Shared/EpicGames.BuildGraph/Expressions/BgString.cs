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
		public static implicit operator BgString(string Value)
		{
			return new BgStringConstantExpr(Value);
		}

		/// <inheritdoc cref="String.Equals(string?, string?, StringComparison)"/>
		public static BgBool Equals(BgString Lhs, BgString Rhs, StringComparison Comparison = StringComparison.CurrentCulture) => Compare(Lhs, Rhs, Comparison) == 0;

		/// <inheritdoc cref="String.Compare(string?, string?, StringComparison)"/>
		public static BgInt Compare(BgString Lhs, BgString Rhs, StringComparison Comparison = StringComparison.CurrentCulture) => new BgStringCompareExpr(Lhs, Rhs, Comparison);

		/// <inheritdoc cref="String.Join{T}(string?, IEnumerable{T})"/>
		public static BgString Join(BgString Separator, BgList<BgString> Values) => new BgStringJoinExpr(Separator, Values);

		/// <inheritdoc cref="String.Format(string, object?[])"/>
		public static BgString Format(string Format, params object?[] Args) => new BgStringFormatExpr(Format, Args);

		/// <inheritdoc/>
		public static BgString operator +(BgString Lhs, BgString Rhs) => new BgStringConcatExpr(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator ==(BgString Lhs, BgString Rhs) => Equals(Lhs, Rhs);

		/// <inheritdoc/>
		public static BgBool operator !=(BgString Lhs, BgString Rhs) => !Equals(Lhs, Rhs);

		/// <summary>
		/// Appens another string to this one
		/// </summary>
		public BgString Append(BgString Other) => this + Other;

		/// <summary>
		/// Appens another string to this one if a condition is true
		/// </summary>
		public BgString AppendIf(BgBool Condition, BgString Other) => IfThen(Condition, this + Other);

		/// <inheritdoc/>
		public BgString IfThen(BgBool Condition, BgString ValueIfTrue) => new BgStringChooseExpr(Condition, ValueIfTrue, this);

		/// <inheritdoc/>
		public BgBool IsMatch(BgString Input, string Pattern) => new BgStringIsMatchExpr(Input, Pattern);

		/// <inheritdoc/>
		public BgString Replace(BgString Input, string Pattern, string Replace) => new BgStringReplaceExpr(Input, Pattern, Replace);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => Compute(Context);

		/// <inheritdoc/>
		public abstract string Compute(BgExprContext Context);

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
		public override BgString DeserializeArgument(string Text) => Text;

		/// <inheritdoc/>
		public override string SerializeArgument(BgString Value, BgExprContext Context) => Value.Compute(Context);

		/// <inheritdoc/>
		public override BgString CreateConstant(object Value) => new BgStringConstantExpr((string)Value);

		/// <inheritdoc/>
		public override IBgExprVariable<BgString> CreateVariable() => new BgStringVariableExpr();
	}

	#region Expression classes

	class BgStringCompareExpr : BgInt
	{
		public BgString Lhs { get; }
		public BgString Rhs { get; }
		public StringComparison Comparison { get; }

		public BgStringCompareExpr(BgString Lhs, BgString Rhs, StringComparison Comparison)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
			this.Comparison = Comparison;
		}

		public override int Compute(BgExprContext Context) => String.Compare(Lhs.Compute(Context), Rhs.Compute(Context), Comparison);

		public override string ToString() => $"Compare({Lhs}, {Rhs})";
	}

	class BgStringConcatExpr : BgString
	{
		public BgString Lhs { get; }
		public BgString Rhs { get; }

		public BgStringConcatExpr(BgString Lhs, BgString Rhs)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		public override string Compute(BgExprContext Context) => Lhs.Compute(Context) + Rhs.Compute(Context);

		public override string ToString() => $"Concat({Lhs}, {Rhs})";
	}

	class BgStringJoinExpr : BgString
	{
		public BgString Separator { get; }
		public BgList<BgString> Values { get; }

		public BgStringJoinExpr(BgString Separator, BgList<BgString> Values)
		{
			this.Separator = Separator;
			this.Values = Values;
		}

		public override string Compute(BgExprContext Context) => String.Join(Separator.Compute(Context), Values.Compute(Context));

		public override string ToString() => $"Join({Separator}, {Values})";
	}

	class BgStringIsMatchExpr : BgBool
	{
		public BgString Input { get; }
		public string Pattern { get; }

		public BgStringIsMatchExpr(BgString Input, string Pattern)
		{
			this.Input = Input;
			this.Pattern = Pattern;
		}

		public override bool Compute(BgExprContext Context) => Regex.IsMatch(Input.Compute(Context), Pattern);
	}

	class BgStringReplaceExpr : BgString
	{
		public BgString Input { get; }
		public string Pattern { get; }
		public string Replacement { get; }

		public BgStringReplaceExpr(BgString Input, string Pattern, string Replacement)
		{
			this.Input = Input;
			this.Pattern = Pattern;
			this.Replacement = Replacement;
		}

		public override string Compute(BgExprContext Context) => Regex.Replace(Input.Compute(Context), Pattern, Replacement);
	}

	class BgStringFormatExpr : BgString
	{
		new string Format { get; }
		BgString[] Arguments { get; }

		public BgStringFormatExpr(string Format, object?[] Args)
		{
			this.Format = Format;

			Arguments = new BgString[Args.Length];
			for (int Idx = 0; Idx < Args.Length; Idx++)
			{
				if (Args[Idx] is IBgExpr Expr)
				{
					Arguments[Idx] = Expr.ToBgString();
				}
				else if (Args[Idx] != null)
				{
					Arguments[Idx] = Args[Idx]?.ToString() ?? String.Empty;
				}
				else
				{
					Arguments[Idx] = String.Empty;
				}
			}
		}

		public override string Compute(BgExprContext Context) => String.Format(Format, Arguments.Select(x => x.Compute(Context)).ToArray());

		public override string ToString() => $"Format({String.Join(", ", new[] { Format }.Concat(Arguments.Select(x => x.ToString())))})";
	}

	class BgStringChooseExpr : BgString
	{
		public BgBool Condition;
		public BgString ValueIfTrue;
		public BgString ValueIfFalse;

		public BgStringChooseExpr(BgBool Condition, BgString ValueIfTrue, BgString ValueIfFalse)
		{
			this.Condition = Condition;
			this.ValueIfTrue = ValueIfTrue;
			this.ValueIfFalse = ValueIfFalse;
		}

		public override string Compute(BgExprContext Context) => Condition.Compute(Context) ? ValueIfTrue.Compute(Context) : ValueIfFalse.Compute(Context);
	}

	class BgStringVariableExpr : BgString, IBgExprVariable<BgString>
	{
		public BgString Value { get; set; } = BgString.Empty;

		public BgString Variable => this;

		public override string Compute(BgExprContext Context) => Value.Compute(Context);
	}

	class BgStringConstantExpr : BgString
	{
		public string Value { get; }

		public BgStringConstantExpr(string Value)
		{
			this.Value = Value;
		}

		public override string Compute(BgExprContext Context) => Value;

		public override bool Equals(object? Obj)
		{
			return Obj is string ObjStr && String.Equals(Value, ObjStr, StringComparison.OrdinalIgnoreCase);
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
