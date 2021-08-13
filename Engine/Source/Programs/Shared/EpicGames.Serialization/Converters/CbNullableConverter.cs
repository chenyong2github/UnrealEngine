// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	/// <summary>
	/// Converter for list types
	/// </summary>
	/// <typeparam name="T"></typeparam>
	class CbNullableConverter<T> : CbConverter<Nullable<T>> where T : struct
	{
		/// <inheritdoc/>
		public override T? Read(CbField Field)
		{
			return CbSerializer.Deserialize<T>(Field);
		}

		/// <inheritdoc/>
		public override void Write(CbWriter Writer, T? Nullable)
		{
			if (Nullable.HasValue)
			{
				CbSerializer.Serialize<T>(Writer, Nullable.Value);
			}
		}

		/// <inheritdoc/>
		public override void WriteNamed(CbWriter Writer, Utf8String Name, T? Nullable)
		{
			if (Nullable.HasValue)
			{
				CbSerializer.Serialize<T>(Writer, Name, Nullable.Value);
			}
		}
	}

	/// <summary>
	/// Factory for CbNullableConverter
	/// </summary>
	class CbNullableConverterFactory : CbConverterFactory
	{
		/// <inheritdoc/>
		public override CbConverter? CreateConverter(Type Type)
		{
			if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				Type ConverterType = typeof(CbNullableConverter<>).MakeGenericType(Type.GenericTypeArguments);
				return (CbConverter)Activator.CreateInstance(ConverterType)!;
			}
			return null;
		}
	}
}
