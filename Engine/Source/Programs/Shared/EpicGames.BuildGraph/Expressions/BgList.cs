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
using System.Diagnostics.Contracts;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Interface for all list types
	/// </summary>
	public interface IBgList : IBgExpr
	{
		/// <summary>
		/// Type of elements in this list
		/// </summary>
		Type ElementType { get; }
	}

	/// <summary>
	/// Abstract base class for expressions returning an immutable list of values
	/// </summary>
	[BgType(typeof(BgListType<>))]
	public abstract class BgList<T> : IBgExpr<BgList<T>>, IBgList where T : IBgExpr<T>
	{
		/// <inheritdoc/>
		public Type ElementType => typeof(T);

		/// <summary>
		/// Constant representation of an empty list
		/// </summary>
		public static BgList<T> Empty { get; } = new BgListConstantExpr<T>(Array.Empty<T>());

		/// <summary>
		/// Implicit conversion operator from a single value
		/// </summary>
		public static implicit operator BgList<T>(T Value) => new BgListConstantExpr<T>(new[] { Value });

		/// <summary>
		/// Implicit conversion operator from an array of values
		/// </summary>
		public static implicit operator BgList<T>(T[] Value) => new BgListConstantExpr<T>(Value);

		/// <summary>
		/// Implicit conversion operator from a list of values
		/// </summary>
		public static implicit operator BgList<T>(List<T> Value) => new BgListConstantExpr<T>(Value);

		/// <summary>
		/// Crates a list from an array of values
		/// </summary>
		/// <param name="Items">Sequence to construct from</param>
		/// <returns></returns>
		public static BgList<T> Create(IEnumerable<T> Items) => new BgListConstantExpr<T>(Items);

		/// <summary>
		/// Crates a list from an array of values
		/// </summary>
		/// <param name="Items">Sequence to construct from</param>
		/// <returns></returns>
		public static BgList<T> Create(params T[] Items) => new BgListConstantExpr<T>(Items);

		/// <summary>
		/// Concatenates mutiple lists together
		/// </summary>
		/// <param name="Sources"></param>
		/// <returns></returns>
		public static BgList<T> Concat(params BgList<T>[] Sources) => new BgListConcatExpr<T>(Sources);

		/// <summary>
		/// Adds items to the end of the list, returning the new list
		/// </summary>
		/// <param name="Items">Items to add</param>
		/// <returns>New list containing the given items</returns>
		public BgList<T> Add(params T[] Items) => new BgListConcatExpr<T>(this, Items);

		/// <inheritdoc cref="Add(T[])"/>
		public BgList<T> Add(params BgList<T>[] Items) => new BgListConcatExpr<T>(this, Items);

		/// <inheritdoc cref="Add(T[])"/>
		public BgList<T> AddIf(BgBool Condition, params T[] Items) => IfThen(Condition, Add(Items));

		/// <inheritdoc cref="Add(T[])"/>
		public BgList<T> AddIf(BgBool Condition, params BgList<T>[] Items) => IfThen(Condition, Add(Items));

		/// <summary>
		/// Removes the given items from this list
		/// </summary>
		/// <param name="Items">Items to remove</param>
		/// <returns>New list without the given items</returns>
		public BgList<T> Except(BgList<T> Items) => new BgListExceptExpr<T>(this, Items);

		/// <summary>
		/// Removes the given items from this list
		/// </summary>
		/// <param name="Condition">Condition to remove the items</param>
		/// <param name="Items">Items to remove</param>
		/// <returns>New list without the given items</returns>
		public BgList<T> ExceptIf(BgBool Condition, BgList<T> Items) => IfThen(Condition, Except(Items));

		/// <summary>
		/// Removes any duplicate items from the list. The first item in the list is retained in its original order.
		/// </summary>
		/// <returns>New list containing the distinct items.</returns>
		public BgList<T> Distinct() => new BgListDistinctExpr<T>(this);

		/// <inheritdoc cref="Enumerable.Select{TSource, TResult}(IEnumerable{TSource}, Func{TSource, TResult})"/>
		public BgList<TResult> Select<TResult>(Func<T, TResult> Function) where TResult : IBgExpr<TResult> => new BgListSelectExpr<T, TResult>(this, Function);

		/// <inheritdoc cref="Enumerable.Where{TSource}(IEnumerable{TSource}, Func{TSource, bool})"/>
		public BgList<T> Where(Func<T, BgBool> Predicate) => new BgListWhereExpr<T>(this, Predicate);

		/// <inheritdoc cref="Enumerable.Contains{TSource}(IEnumerable{TSource}, TSource)"/>
		public BgBool Contains(T Item) => new BgListContainsExpr<T>(this, Item);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => GetEnumerable(Context);

		/// <inheritdoc/>
		public abstract IEnumerable<T> GetEnumerable(BgExprContext Context);

		/// <inheritdoc/>
		public BgList<T> IfThen(BgBool Condition, BgList<T> ValueIfTrue) => new BgListChooseExpr<T>(Condition, ValueIfTrue, this);

		/// <inheritdoc/>
		public BgString ToBgString() => new BgListFormatExpr<T>(this);
	}

	/// <summary>
	/// Traits for a <see cref="BgList{T}"/>
	/// </summary>
	class BgListType<T> : BgTypeBase<BgList<T>> where T : IBgExpr<T>
	{
		/// <inheritdoc/>
		public override BgList<T> DeserializeArgument(string Text)
		{
			IBgType<T> Converter = BgType.Get<T>();
			return BgList<T>.Create(Text.Split(';').Select(x => Converter.DeserializeArgument(x)));
		}

		/// <inheritdoc/>
		public override string SerializeArgument(BgList<T> Value, BgExprContext Context)
		{
			IBgType<T> Converter = BgType.Get<T>();
			return String.Join(";", Value.GetEnumerable(Context).Select(x => Converter.SerializeArgument(x, Context)));
		}

		/// <inheritdoc/>
		public override BgList<T> CreateConstant(object Value)
		{
			return new BgListConstantExpr<T>((IEnumerable<T>)Value);
		}

		/// <inheritdoc/>
		public override IBgExprVariable<BgList<T>> CreateVariable()
		{
			throw new NotImplementedException();
		}
	}

	/// <summary>
	/// Extension methods for lists
	/// </summary>
	public static class BgListExtensions
	{
		internal static List<string> Compute(this BgList<BgString> List, BgExprContext Context)
		{
			return List.GetEnumerable(Context).Select(x => x.Compute(Context)).ToList();
		}

		internal static List<BgFileSet> Compute(this BgList<BgFileSet> List, BgExprContext Context)
		{
			return List.GetEnumerable(Context).Select(x => x.Compute(Context)).ToList();
		}

		internal static List<string> ComputeTags(this BgList<BgFileSet> List, BgExprContext Context)
		{
			return List.GetEnumerable(Context).Select(x => x.ComputeTag(Context)).ToList();
		}
	}

	#region Expression classes

	class BgListConcatExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgList<T>[] Sources;

		public BgListConcatExpr(params BgList<T>[] Sources)
		{
			this.Sources = Sources;
		}

		public BgListConcatExpr(BgList<T> FirstSource, BgList<T>[] OtherSources)
		{
			Sources = new BgList<T>[OtherSources.Length + 1];
			Sources[0] = FirstSource;
			Array.Copy(OtherSources, 0, Sources, 1, OtherSources.Length);
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext Context)
		{
			return Sources.SelectMany(x => x.GetEnumerable(Context));
		}
	}

	class BgListChooseExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgBool Condition;
		public BgList<T> ValueIfTrue;
		public BgList<T> ValueIfFalse;

		public BgListChooseExpr(BgBool Condition, BgList<T> ValueIfTrue, BgList<T> ValueIfFalse)
		{
			this.Condition = Condition;
			this.ValueIfTrue = ValueIfTrue;
			this.ValueIfFalse = ValueIfFalse;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext Context) => Condition.Compute(Context) ? ValueIfTrue.GetEnumerable(Context) : ValueIfFalse.GetEnumerable(Context);
	}

	class BgListConstantExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public IEnumerable<T> Value { get; }

		public BgListConstantExpr(IEnumerable<T> Value)
		{
			this.Value = Value;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext Context) => Value;
	}

	class BgListFormatExpr<T> : BgString where T : IBgExpr<T>
	{
		public BgList<T> Inner;

		public BgListFormatExpr(BgList<T> Inner)
		{
			this.Inner = Inner;
		}

		public override string Compute(BgExprContext Context) => String.Join(";", Inner.GetEnumerable(Context).Select(x => x.ToBgString().Compute(Context)));
	}

	class BgListVariableExpr<T> : BgList<T>, IBgExprVariable<BgList<T>> where T : IBgExpr<T>
	{
		public BgList<T> Value { get; set; } = BgList<T>.Empty;

		public override IEnumerable<T> GetEnumerable(BgExprContext Context) => Value.GetEnumerable(Context);
	}

	class BgListDistinctExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		BgList<T> Source;

		public BgListDistinctExpr(BgList<T> Source)
		{
			this.Source = Source;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext Context)
		{
			IBgType<T> Type = BgType.Get<T>();

			HashSet<object> Values = new HashSet<object>();
			foreach (T Item in Source.GetEnumerable(Context))
			{
				object ItemValue = Item.Compute(Context);
				if (Values.Add(ItemValue))
				{
					yield return Item;
				}
			}
		}
	}

	class BgFunc<TIn, TOut> where TIn : IBgExpr<TIn> where TOut : IBgExpr<TOut>
	{
		IBgExprVariable<TIn> ArgVar;
		IBgExpr<TOut> FuncExpr;

		public BgFunc(IBgExprVariable<TIn> ArgVar, IBgExpr<TOut> FuncExpr)
		{
			this.ArgVar = ArgVar;
			this.FuncExpr = FuncExpr;
		}

		public static implicit operator BgFunc<TIn, TOut>(Func<TIn, TOut> Function)
		{
			IBgExprVariable<TIn> ArgVar = BgType.Get<TIn>().CreateVariable();
			return new BgFunc<TIn, TOut>(ArgVar, Function((TIn)ArgVar));
		}

		public object Compute(BgExprContext Context, TIn Argument)
		{
			ArgVar.Value = Argument;
			return FuncExpr.Compute(Context);
		}

		public BgString ToBgString()
		{
			throw new NotImplementedException();
		}
	}

	class BgListSelectExpr<TIn, TOut> : BgList<TOut> where TIn : IBgExpr<TIn> where TOut : IBgExpr<TOut>
	{
		BgList<TIn> Source;
		BgFunc<TIn, TOut> Function;

		public BgListSelectExpr(BgList<TIn> Source, BgFunc<TIn, TOut> Function)
		{
			this.Source = Source;
			this.Function = Function;
		}

		public override IEnumerable<TOut> GetEnumerable(BgExprContext Context)
		{
			return Source.GetEnumerable(Context).Select(x => (TOut)Function.Compute(Context, x));
		}
	}

	class BgListWhereExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		BgList<T> Source;
		BgFunc<T, BgBool> Predicate;

		public BgListWhereExpr(BgList<T> Source, BgFunc<T, BgBool> Predicate)
		{
			this.Source = Source;
			this.Predicate = Predicate;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext Context)
		{
			return Source.GetEnumerable(Context).Where(x => (bool)Predicate.Compute(Context, x));
		}
	}

	class BgListExceptExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		BgList<T> SourceList;
		BgList<T> ExceptList;

		public BgListExceptExpr(BgList<T> Source, BgList<T> Except)
		{
			this.SourceList = Source;
			this.ExceptList = Except;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext Context)
		{
			HashSet<object> ExceptValues = new HashSet<object>();
			ExceptValues.UnionWith(ExceptList.GetEnumerable(Context).Select(x => x.Compute(Context)));

			IBgType<T> Type = BgType.Get<T>();
			foreach (T SourceItem in SourceList.GetEnumerable(Context))
			{
				object SourceValue = SourceItem.Compute(Context);
				if (!ExceptValues.Contains(SourceValue))
				{
					yield return Type.CreateConstant(SourceValue);
				}
			}
		}
	}

	class BgListContainsExpr<T> : BgBool where T : IBgExpr<T>
	{
		BgList<T> Source;
		T Item;

		public BgListContainsExpr(BgList<T> Source, T Item)
		{
			this.Source = Source;
			this.Item = Item;
		}

		public override bool Compute(BgExprContext Context)
		{
			object Value = Item.Compute(Context);
			return Source.GetEnumerable(Context).Any(x => x.Compute(Context).Equals(Value));
		}
	}

	#endregion
}
