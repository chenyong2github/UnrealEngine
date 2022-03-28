// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;

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
		public static implicit operator BgList<T>(T value) => new BgListConstantExpr<T>(new[] { value });

		/// <summary>
		/// Implicit conversion operator from an array of values
		/// </summary>
		public static implicit operator BgList<T>(T[] value) => new BgListConstantExpr<T>(value);

		/// <summary>
		/// Implicit conversion operator from a list of values
		/// </summary>
		public static implicit operator BgList<T>(List<T> value) => new BgListConstantExpr<T>(value);

		/// <summary>
		/// Crates a list from an array of values
		/// </summary>
		/// <param name="items">Sequence to construct from</param>
		/// <returns></returns>
		public static BgList<T> Create(IEnumerable<T> items) => new BgListConstantExpr<T>(items);

		/// <summary>
		/// Crates a list from an array of values
		/// </summary>
		/// <param name="items">Sequence to construct from</param>
		/// <returns></returns>
		public static BgList<T> Create(params T[] items) => new BgListConstantExpr<T>(items);

		/// <summary>
		/// Concatenates mutiple lists together
		/// </summary>
		/// <param name="sources"></param>
		/// <returns></returns>
		public static BgList<T> Concat(params BgList<T>[] sources) => new BgListConcatExpr<T>(sources);

		/// <summary>
		/// Adds items to the end of the list, returning the new list
		/// </summary>
		/// <param name="items">Items to add</param>
		/// <returns>New list containing the given items</returns>
		public BgList<T> Add(params T[] items) => new BgListConcatExpr<T>(this, items);

		/// <inheritdoc cref="Add(T[])"/>
		public BgList<T> Add(params BgList<T>[] items) => new BgListConcatExpr<T>(this, items);

		/// <inheritdoc cref="Add(T[])"/>
		public BgList<T> AddIf(BgBool condition, params T[] items) => IfThen(condition, Add(items));

		/// <inheritdoc cref="Add(T[])"/>
		public BgList<T> AddIf(BgBool condition, params BgList<T>[] items) => IfThen(condition, Add(items));

		/// <summary>
		/// Removes the given items from this list
		/// </summary>
		/// <param name="items">Items to remove</param>
		/// <returns>New list without the given items</returns>
		public BgList<T> Except(BgList<T> items) => new BgListExceptExpr<T>(this, items);

		/// <summary>
		/// Removes the given items from this list
		/// </summary>
		/// <param name="condition">Condition to remove the items</param>
		/// <param name="items">Items to remove</param>
		/// <returns>New list without the given items</returns>
		public BgList<T> ExceptIf(BgBool condition, BgList<T> items) => IfThen(condition, Except(items));

		/// <summary>
		/// Removes any duplicate items from the list. The first item in the list is retained in its original order.
		/// </summary>
		/// <returns>New list containing the distinct items.</returns>
		public BgList<T> Distinct() => new BgListDistinctExpr<T>(this);

		/// <inheritdoc cref="Enumerable.Select{TSource, TResult}(IEnumerable{TSource}, Func{TSource, TResult})"/>
		public BgList<TResult> Select<TResult>(Func<T, TResult> function) where TResult : IBgExpr<TResult> => new BgListSelectExpr<T, TResult>(this, function);

		/// <inheritdoc cref="Enumerable.Where{TSource}(IEnumerable{TSource}, Func{TSource, Boolean})"/>
		public BgList<T> Where(Func<T, BgBool> predicate) => new BgListWhereExpr<T>(this, predicate);

		/// <inheritdoc cref="Enumerable.Contains{TSource}(IEnumerable{TSource}, TSource)"/>
		public BgBool Contains(T item) => new BgListContainsExpr<T>(this, item);

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => GetEnumerable(context);

		/// <inheritdoc/>
		public abstract IEnumerable<T> GetEnumerable(BgExprContext context);

		/// <inheritdoc/>
		public BgList<T> IfThen(BgBool condition, BgList<T> valueIfTrue) => new BgListChooseExpr<T>(condition, valueIfTrue, this);

		/// <inheritdoc/>
		public BgString ToBgString() => new BgListFormatExpr<T>(this);
	}

	/// <summary>
	/// Traits for a <see cref="BgList{T}"/>
	/// </summary>
	class BgListType<T> : BgTypeBase<BgList<T>> where T : IBgExpr<T>
	{
		/// <inheritdoc/>
		public override BgList<T> DeserializeArgument(string text)
		{
			IBgType<T> converter = BgType.Get<T>();
			return BgList<T>.Create(text.Split(';').Select(x => converter.DeserializeArgument(x)));
		}

		/// <inheritdoc/>
		public override string SerializeArgument(BgList<T> value, BgExprContext context)
		{
			IBgType<T> converter = BgType.Get<T>();
			return String.Join(";", value.GetEnumerable(context).Select(x => converter.SerializeArgument(x, context)));
		}

		/// <inheritdoc/>
		public override BgList<T> CreateConstant(object value)
		{
			return new BgListConstantExpr<T>((IEnumerable<T>)value);
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
		internal static List<string> Compute(this BgList<BgString> list, BgExprContext context)
		{
			return list.GetEnumerable(context).Select(x => x.Compute(context)).ToList();
		}

		internal static List<BgFileSet> Compute(this BgList<BgFileSet> list, BgExprContext context)
		{
			return list.GetEnumerable(context).Select(x => x.Compute(context)).ToList();
		}

		internal static List<string> ComputeTags(this BgList<BgFileSet> list, BgExprContext context)
		{
			return list.GetEnumerable(context).Select(x => x.ComputeTag(context)).ToList();
		}
	}

	#region Expression classes

	class BgListConcatExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgList<T>[] Sources { get; }

		public BgListConcatExpr(params BgList<T>[] sources)
		{
			Sources = sources;
		}

		public BgListConcatExpr(BgList<T> firstSource, BgList<T>[] otherSources)
		{
			Sources = new BgList<T>[otherSources.Length + 1];
			Sources[0] = firstSource;
			Array.Copy(otherSources, 0, Sources, 1, otherSources.Length);
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext context)
		{
			return Sources.SelectMany(x => x.GetEnumerable(context));
		}
	}

	class BgListChooseExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgBool Condition { get; }
		public BgList<T> ValueIfTrue { get; }
		public BgList<T> ValueIfFalse { get; }

		public BgListChooseExpr(BgBool condition, BgList<T> valueIfTrue, BgList<T> valueIfFalse)
		{
			Condition = condition;
			ValueIfTrue = valueIfTrue;
			ValueIfFalse = valueIfFalse;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext context) => Condition.Compute(context) ? ValueIfTrue.GetEnumerable(context) : ValueIfFalse.GetEnumerable(context);
	}

	class BgListConstantExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public IEnumerable<T> Value { get; }

		public BgListConstantExpr(IEnumerable<T> value)
		{
			Value = value;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext context) => Value;
	}

	class BgListFormatExpr<T> : BgString where T : IBgExpr<T>
	{
		public BgList<T> Inner { get; }

		public BgListFormatExpr(BgList<T> inner)
		{
			Inner = inner;
		}

		public override string Compute(BgExprContext context) => String.Join(";", Inner.GetEnumerable(context).Select(x => x.ToBgString().Compute(context)));
	}

	class BgListVariableExpr<T> : BgList<T>, IBgExprVariable<BgList<T>> where T : IBgExpr<T>
	{
		public BgList<T> Value { get; set; } = BgList<T>.Empty;

		public override IEnumerable<T> GetEnumerable(BgExprContext context) => Value.GetEnumerable(context);
	}

	class BgListDistinctExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgList<T> Source { get; }

		public BgListDistinctExpr(BgList<T> source)
		{
			Source = source;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext context)
		{
			HashSet<object> values = new HashSet<object>();
			foreach (T item in Source.GetEnumerable(context))
			{
				object itemValue = item.Compute(context);
				if (values.Add(itemValue))
				{
					yield return item;
				}
			}
		}
	}

	class BgFunc<TIn, TOut> where TIn : IBgExpr<TIn> where TOut : IBgExpr<TOut>
	{
		public IBgExprVariable<TIn> ArgVar { get; }
		public IBgExpr<TOut> FuncExpr { get; }

		public BgFunc(IBgExprVariable<TIn> argVar, IBgExpr<TOut> funcExpr)
		{
			ArgVar = argVar;
			FuncExpr = funcExpr;
		}

		public static implicit operator BgFunc<TIn, TOut>(Func<TIn, TOut> function)
		{
			IBgExprVariable<TIn> argVar = BgType.Get<TIn>().CreateVariable();
			return new BgFunc<TIn, TOut>(argVar, function((TIn)argVar));
		}

		public object Compute(BgExprContext context, TIn argument)
		{
			ArgVar.Value = argument;
			return FuncExpr.Compute(context);
		}

		public BgString ToBgString()
		{
			throw new NotImplementedException();
		}
	}

	class BgListSelectExpr<TIn, TOut> : BgList<TOut> where TIn : IBgExpr<TIn> where TOut : IBgExpr<TOut>
	{
		public BgList<TIn> Source { get; }
		public BgFunc<TIn, TOut> Function { get; }

		public BgListSelectExpr(BgList<TIn> source, BgFunc<TIn, TOut> function)
		{
			Source = source;
			Function = function;
		}

		public override IEnumerable<TOut> GetEnumerable(BgExprContext context)
		{
			return Source.GetEnumerable(context).Select(x => (TOut)Function.Compute(context, x));
		}
	}

	class BgListWhereExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgList<T> Source { get; }
		public BgFunc<T, BgBool> Predicate { get; }

		public BgListWhereExpr(BgList<T> source, BgFunc<T, BgBool> predicate)
		{
			Source = source;
			Predicate = predicate;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext context)
		{
			return Source.GetEnumerable(context).Where(x => (bool)Predicate.Compute(context, x));
		}
	}

	class BgListExceptExpr<T> : BgList<T> where T : IBgExpr<T>
	{
		public BgList<T> SourceList { get; }
		public BgList<T> ExceptList { get; }

		public BgListExceptExpr(BgList<T> source, BgList<T> except)
		{
			SourceList = source;
			ExceptList = except;
		}

		public override IEnumerable<T> GetEnumerable(BgExprContext context)
		{
			HashSet<object> exceptValues = new HashSet<object>();
			exceptValues.UnionWith(ExceptList.GetEnumerable(context).Select(x => x.Compute(context)));

			IBgType<T> type = BgType.Get<T>();
			foreach (T sourceItem in SourceList.GetEnumerable(context))
			{
				object sourceValue = sourceItem.Compute(context);
				if (!exceptValues.Contains(sourceValue))
				{
					yield return type.CreateConstant(sourceValue);
				}
			}
		}
	}

	class BgListContainsExpr<T> : BgBool where T : IBgExpr<T>
	{
		public BgList<T> Source { get; }
		public T Item { get; }

		public BgListContainsExpr(BgList<T> source, T item)
		{
			Source = source;
			Item = item;
		}

		public override bool Compute(BgExprContext context)
		{
			object value = Item.Compute(context);
			return Source.GetEnumerable(context).Any(x => x.Compute(context).Equals(value));
		}
	}

	#endregion
}
