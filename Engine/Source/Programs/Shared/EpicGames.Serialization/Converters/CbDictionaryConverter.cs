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
	/// Converter for dictionary types
	/// </summary>
	class CbDictionaryConverter<TKey, TValue> : CbConverter<Dictionary<TKey, TValue>> where TKey : notnull
	{
		/// <inheritdoc/>
		public override Dictionary<TKey, TValue> Read(CbField Field)
		{
			Dictionary<TKey, TValue> Dictionary = new Dictionary<TKey, TValue>();
			foreach (CbField Element in Field)
			{
				IEnumerator<CbField> Enumerator = Element.AsArray().GetEnumerator();

				if (!Enumerator.MoveNext())
				{
					throw new CbException("Missing key for dictionary entry");
				}
				TKey Key = CbSerializer.Deserialize<TKey>(Enumerator.Current);

				if (!Enumerator.MoveNext())
				{
					throw new CbException("Missing value for dictionary entry");
				}
				TValue Value = CbSerializer.Deserialize<TValue>(Enumerator.Current);

				Dictionary.Add(Key, Value);
			}
			return Dictionary;
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, Dictionary<TKey, TValue> Value)
		{
			if (Value.Count > 0)
			{
				Writer.BeginUniformArray(CbFieldType.Array);
				foreach (KeyValuePair<TKey, TValue> Pair in Value)
				{
					Writer.BeginArray();
					CbSerializer.Serialize(Writer, Pair.Key);
					CbSerializer.Serialize(Writer, Pair.Value);
					Writer.EndArray();
				}
				Writer.EndUniformArray();
			}
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, Dictionary<TKey, TValue> Value)
		{
			if (Value.Count > 0)
			{
				Writer.BeginUniformArray(Name, CbFieldType.Array);
				foreach (KeyValuePair<TKey, TValue> Pair in Value)
				{
					Writer.BeginArray();
					CbSerializer.Serialize(Writer, Pair.Key);
					CbSerializer.Serialize(Writer, Pair.Value);
					Writer.EndArray();
				}
				Writer.EndUniformArray();
			}
		}
	}

	/// <summary>
	/// Factory for CbDictionaryConverter
	/// </summary>
	class CbDictionaryConverterFactory : CbConverterFactory
	{
		/// <inheritdoc/>
		public override CbConverter? CreateConverter(Type Type)
		{
			if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(Dictionary<,>))
			{
				Type ConverterType = typeof(CbDictionaryConverter<,>).MakeGenericType(Type.GenericTypeArguments);
				return (CbConverter)Activator.CreateInstance(ConverterType)!;
			}
			return null;
		}
	}
}
