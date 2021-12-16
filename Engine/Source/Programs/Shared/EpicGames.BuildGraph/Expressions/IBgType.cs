// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Text;

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
		/// <param name="Value">The value to serialize</param>
		/// <param name="Context">Context for evaluating the expression</param>
		/// <returns></returns>
		string SerializeArgument(object Value, BgExprContext Context);

		/// <summary>
		/// Constructs an expression from a string value
		/// </summary>
		/// <param name="Text">The serialized value</param>
		/// <returns>Instance of the expression</returns>
		object DeserializeArgument(string Text);
	}

	/// <summary>
	/// Converts expressions to and from strings
	/// </summary>
	public interface IBgType<T> : IBgType where T : IBgExpr<T>
	{
		/// <summary>
		/// Serializes the value of an expression to an argument string
		/// </summary>
		/// <param name="Value">The value to serialize</param>
		/// <param name="Context">Context for evaluating the expression</param>
		/// <returns></returns>
		string SerializeArgument(T Value, BgExprContext Context);

		/// <summary>
		/// Constructs an expression from an argument value
		/// </summary>
		/// <param name="Text">The serialized value</param>
		/// <returns>Instance of the expression</returns>
		new T DeserializeArgument(string Text);

		/// <summary>
		/// Creates a value of the expression type
		/// </summary>
		/// <param name="Value">Value to wrap</param>
		/// <returns></returns>
		T CreateConstant(object Value);

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
		object IBgType.DeserializeArgument(string Text) => DeserializeArgument(Text);

		/// <inheritdoc/>
		public abstract T DeserializeArgument(string Text);

		/// <inheritdoc/>
		string IBgType.SerializeArgument(object Value, BgExprContext Context) => SerializeArgument((T)Value, Context);

		/// <inheritdoc/>
		public abstract string SerializeArgument(T Value, BgExprContext Context);

		/// <inheritdoc/>
		public abstract T CreateConstant(object Value);

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
		/// <param name="Type"></param>
		public BgTypeAttribute(Type Type)
		{
			this.Type = Type;
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
		static ConcurrentDictionary<Type, IBgType> TypeToTraits = new ConcurrentDictionary<Type, IBgType>();

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
		/// <param name="Type"></param>
		/// <returns></returns>
		public static IBgType Get(Type Type)
		{
			IBgType? Traits = GetInner(Type);
			if(Traits == null)
			{
				throw new ArgumentException($"Missing converter attribute on type {Type.Name}");
			}
			return Traits;
		}

		/// <summary>
		/// Helper method for getting the type traits from a hierarchy of types
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		static IBgType? GetInner(Type Type)
		{
			IBgType? Traits;
			if (!TypeToTraits.TryGetValue(Type, out Traits))
			{
				BgTypeAttribute? ConverterAttr = Type.GetCustomAttribute<BgTypeAttribute>(false);
				if (ConverterAttr == null)
				{
					if (Type.BaseType == null)
					{
						Traits = null;
					}
					else
					{
						Traits = GetInner(Type.BaseType);
					}
				}
				else
				{
					Type ConverterType = ConverterAttr.Type;
					if (ConverterType.IsGenericType)
					{
						ConverterType = ConverterType.MakeGenericType(Type.GetGenericArguments());
					}

					IBgType NewTraits = (IBgType)Activator.CreateInstance(ConverterType)!;
					if (TypeToTraits.TryAdd(Type, NewTraits))
					{
						Traits = NewTraits;
					}
					else
					{
						Traits = TypeToTraits[Type];
					}
				}
			}
			return Traits;
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
