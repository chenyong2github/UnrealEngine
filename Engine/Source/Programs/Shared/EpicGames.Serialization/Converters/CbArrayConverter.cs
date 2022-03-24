// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;

namespace EpicGames.Serialization.Converters
{
	/// <summary>
	/// Converter for array types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	class CbArrayConverter<T> : CbConverterBase<T[]>
	{
		/// <inheritdoc/>
		public override T[] Read(CbField field)
		{
			List<T> list = new List<T>();
			foreach (CbField elementField in field)
			{
				list.Add(CbSerializer.Deserialize<T>(elementField));
			}
			return list.ToArray();
		}

		/// <inheritdoc/>
		public override void Write(CbWriter writer, T[] list)
		{
			writer.BeginArray();
			foreach (T element in list)
			{
				CbSerializer.Serialize<T>(writer, element);
			}
			writer.EndArray();
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter writer, Utf8String name, T[] array)
		{
			writer.BeginArray(name);
			foreach (T element in array)
			{
				CbSerializer.Serialize<T>(writer, element);
			}
			writer.EndArray();
		}
	}
	
	/// <summary>
	/// Factory for CbListConverter
	/// </summary>
	class CbArrayConverterFactory : CbConverterFactory
	{
		/// <inheritdoc/>
		public override ICbConverter? CreateConverter(Type type)
		{
			if (type.IsArray)
			{
				Type elementType = type.GetElementType()!;
				Type converterType = typeof(CbArrayConverter<>).MakeGenericType(elementType);
				return (ICbConverter)Activator.CreateInstance(converterType)!;
			}
			return null;
		}
	}
}
