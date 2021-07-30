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
	/// Converter for list types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	class CbListConverter<T> : CbConverter<List<T>>
	{
		/// <inheritdoc/>
		public override List<T> Read(CbField Field)
		{
			List<T> List = new List<T>();
			foreach (CbField ElementField in Field)
			{
				List.Add(CbSerializer.Deserialize<T>(ElementField));
			}
			return List;
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, List<T> List)
		{
			Writer.BeginArray();
			foreach (T Element in List)
			{
				CbSerializer.Serialize<T>(Writer, Element);
			}
			Writer.EndArray();
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, List<T> List)
		{
			if (List.Count > 0)
			{
				Writer.BeginArray(Name);
				foreach (T Element in List)
				{
					CbSerializer.Serialize<T>(Writer, Element);
				}
				Writer.EndArray();
			}
		}
	}

	/// <summary>
	/// Specialization for serializing string lists
	/// </summary>
	sealed class CbStringListConverter : CbConverter<List<Utf8String>>
	{
		/// <inheritdoc/>
		public override List<Utf8String> Read(CbField Field)
		{
			List<Utf8String> List = new List<Utf8String>();
			foreach (CbField ElementField in Field)
			{
				List.Add(ElementField.AsString());
			}
			return List;
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, List<Utf8String> List)
		{
			Writer.BeginUniformArray(CbFieldType.String);
			foreach (Utf8String String in List)
			{
				Writer.WriteStringValue(String);
			}
			Writer.EndUniformArray();
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, List<Utf8String> List)
		{
			if (List.Count > 0)
			{
				Writer.BeginUniformArray(Name, CbFieldType.String);
				foreach (Utf8String String in List)
				{
					Writer.WriteStringValue(String);
				}
				Writer.EndUniformArray();
			}
		}
	}

	/// <summary>
	/// Factory for CbListConverter
	/// </summary>
	class CbListConverterFactory : CbConverterFactory
	{
		/// <inheritdoc/>
		public override CbConverter? CreateConverter(Type Type)
		{
			if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(List<>))
			{
				if (Type.GenericTypeArguments[0] == typeof(Utf8String))
				{
					return new CbStringListConverter();
				}
				else
				{
					Type ConverterType = typeof(CbListConverter<>).MakeGenericType(Type.GenericTypeArguments);
					return (CbConverter)Activator.CreateInstance(ConverterType)!;
				}
			}
			return null;
		}
	}
}
