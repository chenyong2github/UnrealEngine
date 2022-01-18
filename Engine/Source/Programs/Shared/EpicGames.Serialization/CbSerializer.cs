// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization.Converters;
using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;

namespace EpicGames.Serialization
{
	/// <summary>
	/// Attribute used to mark a property that should be serialized to compact binary
	/// </summary>
	[AttributeUsage(AttributeTargets.Property)]
	public class CbFieldAttribute : Attribute
	{
		/// <summary>
		/// Name of the serialized field
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public CbFieldAttribute()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		public CbFieldAttribute(string Name)
		{
			this.Name = Name;
		}
	}

	/// <summary>
	/// Attribute used to indicate that this object is the base for a class hierarchy. Each derived class must have a [CbDiscriminator] attribute.
	/// </summary>
	public class CbPolymorphicAttribute : Attribute
	{
	}

	/// <summary>
	/// Sets the name used for discriminating between derived classes during serialization
	/// </summary>
	public class CbDiscriminatorAttribute : Attribute
	{
		/// <summary>
		/// Name used to identify this class
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name used to identify this class</param>
		public CbDiscriminatorAttribute(string Name) => this.Name = Name;
	}

	/// <summary>
	/// Exception thrown when serializing cb objects
	/// </summary>
	public class CbException : Exception
	{
		/// <inheritdoc cref="Exception(string?)"/>
		public CbException(string Message) : base(Message)
		{
		}

		/// <inheritdoc cref="Exception(string?, Exception)"/>
		public CbException(string Message, Exception Inner) : base(Message, Inner)
		{
		}
	}

	/// <summary>
	/// Attribute-driven compact binary serializer
	/// </summary>
	public static class CbSerializer
	{
		/// <summary>
		/// Serialize an object
		/// </summary>
		/// <param name="Type">Type of the object to serialize</param>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static CbObject Serialize(Type Type, object Value)
		{
			CbWriter Writer = new CbWriter();
			CbConverter.GetConverter(Type).WriteObject(Writer, Value);
			return Writer.ToObject();
		}

		/// <summary>
		/// Serialize an object
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static CbObject Serialize<T>(T Value)
		{
			CbWriter Writer = new CbWriter();
			CbConverter.GetConverter<T>().Write(Writer, Value);
			return Writer.ToObject();
		}

		/// <summary>
		/// Serialize a property to a given writer
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Writer"></param>
		/// <param name="Value"></param>
		public static void Serialize<T>(CbWriter Writer, T Value)
		{
			CbConverter.GetConverter<T>().Write(Writer, Value);
		}

		/// <summary>
		/// Serialize a named property to the given writer
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Writer"></param>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		public static void Serialize<T>(CbWriter Writer, Utf8String Name, T Value)
		{
			CbConverter.GetConverter<T>().WriteNamed(Writer, Name, Value);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbObject"/>
		/// </summary>
		/// <param name="Field"></param>
		/// <param name="Type">Type of the object to read</param>
		/// <returns></returns>
		public static object? Deserialize(CbField Field, Type Type)
		{
			return CbConverter.GetConverter(Type).ReadObject(Field);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbField"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Field"></param>
		/// <returns></returns>
		public static T Deserialize<T>(CbField Field)
		{
			return CbConverter.GetConverter<T>().Read(Field);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbObject"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Object"></param>
		/// <returns></returns>
		public static T Deserialize<T>(CbObject Object) => Deserialize<T>(Object.AsField());

		/// <summary>
		/// Deserialize an object from a block of memory
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Data"></param>
		/// <returns></returns>
		public static T Deserialize<T>(ReadOnlyMemory<byte> Data) => Deserialize<T>(new CbField(Data));
	}
}
