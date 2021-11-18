// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Reflection;

namespace EpicGames.Core
{
	/// <summary>
	/// Base interface for a binary converter. Implementations must derive from <see cref="IBinaryConverter{TValue}"/> instead
	/// </summary>
	public interface IBinaryConverter
	{
		/// <summary>
		/// Converter version number
		/// </summary>
		int Version { get; }
	}

	/// <summary>
	/// Interface for converting serializing object to and from a binary format
	/// </summary>
	/// <typeparam name="TValue">The value to serialize</typeparam>
	public interface IBinaryConverter<TValue> : IBinaryConverter
	{
		/// <summary>
		/// Reads a value from the archive
		/// </summary>
		/// <param name="Reader">The archive reader</param>
		/// <returns>New instance of the value</returns>
		TValue Read(BinaryArchiveReader Reader);

		/// <summary>
		/// Writes a value to the archive
		/// </summary>
		/// <param name="Writer">The archive writer</param>
		/// <param name="Value">The value to write</param>
		void Write(BinaryArchiveWriter Writer, TValue Value);
	}

	/// <summary>
	/// Registration of IBinaryConverter instances
	/// </summary>
	public static class BinaryConverter
	{
		/// <summary>
		/// Map from type to the converter type
		/// </summary>
		static readonly ConcurrentDictionary<Type, Type> TypeToConverterType = new ConcurrentDictionary<Type, Type>();

		/// <summary>
		/// Explicitly register the converter for a type. If Type is a generic type, the converter should also be a generic type with the same type arguments.
		/// </summary>
		/// <param name="Type">Type to register a converter for</param>
		/// <param name="ConverterType">The converter type</param>
		public static void RegisterConverter(Type Type, Type ConverterType)
		{
			if (!TypeToConverterType.TryAdd(Type, ConverterType))
			{
				throw new Exception($"Type '{Type.Name}' already has a registered converter ({TypeToConverterType[Type].Name})");
			}
		}

		/// <summary>
		/// Attempts to get the converter for a particular type
		/// </summary>
		/// <param name="Type">The type to use</param>
		/// <param name="ConverterType">The converter type</param>
		/// <returns>True if a converter was found</returns>
		public static bool TryGetConverterType(Type Type, out Type? ConverterType)
		{
			if (TypeToConverterType.TryGetValue(Type, out Type? CustomConverterType))
			{
				ConverterType = CustomConverterType;
				return true;
			}

			BinaryConverterAttribute? ConverterAttribute = Type.GetCustomAttribute<BinaryConverterAttribute>();
			if (ConverterAttribute != null)
			{
				ConverterType = ConverterAttribute.Type;
				return true;
			}

			if (Type.IsGenericType)
			{
				BinaryConverterAttribute? GenericConverterAttribute = Type.GetGenericTypeDefinition().GetCustomAttribute<BinaryConverterAttribute>();
				if (GenericConverterAttribute != null)
				{
					ConverterType = GenericConverterAttribute.Type.MakeGenericType(Type.GetGenericArguments());
					return true;
				}
			}

			ConverterType = null;
			return false;
		}
	}
}
