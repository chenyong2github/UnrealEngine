// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Reflection;

namespace EpicGames.BuildGraph.Expressions
{
	/// <summary>
	/// Converts expressions to and from strings
	/// </summary>
	public interface IBgType
	{
		/// <summary>
		/// Serializes the value of an expression to a string
		/// </summary>
		/// <param name="value">The value to serialize</param>
		/// <param name="context">Context for evaluating the expression</param>
		/// <returns></returns>
		string SerializeArgument(object value, BgExprContext context);

		/// <summary>
		/// Constructs an expression from a string value
		/// </summary>
		/// <param name="text">The serialized value</param>
		/// <returns>Instance of the expression</returns>
		object DeserializeArgument(string text);

		/// <summary>
		/// Creates a value of the expression type
		/// </summary>
		/// <param name="value">Value to wrap</param>
		/// <returns></returns>
		object CreateConstant(object value);
	}

	/// <summary>
	/// Converts expressions to and from strings
	/// </summary>
	public interface IBgType<T> : IBgType where T : IBgExpr<T>
	{
		/// <summary>
		/// Serializes the value of an expression to an argument string
		/// </summary>
		/// <param name="value">The value to serialize</param>
		/// <param name="context">Context for evaluating the expression</param>
		/// <returns></returns>
		string SerializeArgument(T value, BgExprContext context);

		/// <summary>
		/// Constructs an expression from an argument value
		/// </summary>
		/// <param name="text">The serialized value</param>
		/// <returns>Instance of the expression</returns>
		new T DeserializeArgument(string text);

		/// <summary>
		/// Creates a value of the expression type
		/// </summary>
		/// <param name="value">Value to wrap</param>
		/// <returns></returns>
		new T CreateConstant(object value);

		/// <summary>
		/// Creates a variable of the given type
		/// </summary>
		/// <returns></returns>
		IBgExprVariable<T> CreateVariable();
	}

	/// <summary>
	/// Base class for implementations of <see cref="IBgType{T}"/>
	/// </summary>
	public abstract class BgTypeBase<T> : IBgType<T> where T : IBgExpr<T>
	{
		/// <inheritdoc/>
		object IBgType.DeserializeArgument(string text) => DeserializeArgument(text);

		/// <inheritdoc/>
		public abstract T DeserializeArgument(string text);

		/// <inheritdoc/>
		string IBgType.SerializeArgument(object value, BgExprContext context) => SerializeArgument((T)value, context);

		/// <inheritdoc/>
		public abstract string SerializeArgument(T value, BgExprContext context);

		/// <inheritdoc/>
		object IBgType.CreateConstant(object value) => CreateConstant(value);

		/// <inheritdoc/>
		public abstract T CreateConstant(object value);

		/// <inheritdoc/>
		public abstract IBgExprVariable<T> CreateVariable();
	}

	/// <summary>
	/// Attribute used to specify the converter class to use for a type
	/// </summary>
	class BgTypeAttribute : Attribute
	{
		/// <summary>
		/// The converter type
		/// </summary>
		public Type Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="type"></param>
		public BgTypeAttribute(Type type)
		{
			Type = type;
		}
	}

	/// <summary>
	/// Utility methods for type traits
	/// </summary>
	public static class BgType
	{
		/// <summary>
		/// Cache of traits implementations for each type
		/// </summary>
		static readonly ConcurrentDictionary<Type, IBgType> s_typeToTraits = new ConcurrentDictionary<Type, IBgType>();

		/// <summary>
		/// Cache of traits for a static type
		/// </summary>
		class BgTypeTraitsCache<T> where T : IBgExpr<T>
		{
			public static IBgType<T> Traits { get; } = (IBgType<T>)BgType.Get(typeof(T));
		}

		/// <summary>
		/// Create a converter instance for the given type
		/// </summary>
		/// <param name="type"></param>
		/// <returns></returns>
		public static IBgType Get(Type type)
		{
			IBgType? traits = GetInner(type);
			if (traits == null)
			{
				throw new ArgumentException($"Missing converter attribute on type {type.Name}");
			}
			return traits;
		}

		/// <summary>
		/// Helper method for getting the type traits from a hierarchy of types
		/// </summary>
		/// <param name="type"></param>
		/// <returns></returns>
		static IBgType? GetInner(Type type)
		{
			IBgType? traits;
			if (!s_typeToTraits.TryGetValue(type, out traits))
			{
				BgTypeAttribute? converterAttr = type.GetCustomAttribute<BgTypeAttribute>(false);
				if (converterAttr == null)
				{
					if (type.BaseType == null)
					{
						traits = null;
					}
					else
					{
						traits = GetInner(type.BaseType);
					}
				}
				else
				{
					Type converterType = converterAttr.Type;
					if (converterType.IsGenericType)
					{
						converterType = converterType.MakeGenericType(type.GetGenericArguments());
					}

					IBgType newTraits = (IBgType)Activator.CreateInstance(converterType)!;
					if (s_typeToTraits.TryAdd(type, newTraits))
					{
						traits = newTraits;
					}
					else
					{
						traits = s_typeToTraits[type];
					}
				}
			}
			return traits;
		}

		/// <summary>
		/// Create a traits instance for the given type
		/// </summary>
		/// <returns></returns>
		public static IBgType<T> Get<T>() where T : IBgExpr<T>
		{
			return BgTypeTraitsCache<T>.Traits;
		}
	}
}
