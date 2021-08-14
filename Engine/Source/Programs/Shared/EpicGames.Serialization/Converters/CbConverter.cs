// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	/// <summary>
	/// Attribute declaring the converter to use for a type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public class CbConverterAttribute : Attribute
	{
		/// <summary>
		/// The converter type
		/// </summary>
		public Type ConverterType { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ConverterType"></param>
		public CbConverterAttribute(Type ConverterType)
		{
			this.ConverterType = ConverterType;
		}
	}

	/// <summary>
	/// Base class for all converters. Deriving from <see cref="CbConverter{T}"/> is more efficient
	/// </summary>
	public abstract class CbConverter
	{
		/// <summary>
		/// Reads an object from a field
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		public abstract object? ReadObject(CbField Field);

		/// <summary>
		/// Writes an object to a field
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Value"></param>
		public abstract void WriteObject(CbWriter Writer, object? Value);

		/// <summary>
		/// Writes an object to a named field, if not equal to the default value
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		public abstract void WriteNamedObject(CbWriter Writer, Utf8String Name, object? Value);
	}

	/// <summary>
	/// Converter for a particular type
	/// </summary>
	public abstract class CbConverter<T> : CbConverter
	{
		/// <summary>
		/// Reads an object from a field
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		public abstract T Read(CbField Field);

		/// <summary>
		/// Writes an object to a field
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Value"></param>
		public abstract void Write(CbWriter Writer, T Value);

		/// <summary>
		/// Writes an object to a named field, if not equal to the default value
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		public abstract void WriteNamed(CbWriter Writer, Utf8String Name, T Value);

		/// <inheritdoc/>
		public sealed override object? ReadObject(CbField Field) => Read(Field);

		/// <inheritdoc/>
		public sealed override void WriteObject(CbWriter Writer, object? Value) => Write(Writer, (T)Value!);

		/// <inheritdoc/>
		public sealed override void WriteNamedObject(CbWriter Writer, Utf8String Name, object? Value) => WriteNamed(Writer, Name, (T)Value!);
	}

	/// <summary>
	/// Base class for converter factories
	/// </summary>
	public abstract class CbConverterFactory
	{
		/// <summary>
		/// Create a converter for the given type
		/// </summary>
		/// <param name="Type">The type to create a converter for</param>
		/// <returns>The converter instance, or null if this factory does not support the given type</returns>
		public abstract CbConverter? CreateConverter(Type Type);
	}
}
