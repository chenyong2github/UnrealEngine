// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;

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
		/// <param name="name"></param>
		public CbFieldAttribute(string name)
		{
			Name = name;
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
		/// <param name="name">Name used to identify this class</param>
		public CbDiscriminatorAttribute(string name) => Name = name;
	}

	/// <summary>
	/// Exception thrown when serializing cb objects
	/// </summary>
	public class CbException : Exception
	{
		/// <inheritdoc cref="Exception(String?)"/>
		public CbException(string message) : base(message)
		{
		}

		/// <inheritdoc cref="Exception(String?, Exception)"/>
		public CbException(string message, Exception inner) : base(message, inner)
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
		/// <param name="type">Type of the object to serialize</param>
		/// <param name="value"></param>
		/// <returns></returns>
		public static CbObject Serialize(Type type, object value)
		{
			CbWriter writer = new CbWriter();
			CbConverter.GetConverter(type).WriteObject(writer, value);
			return writer.ToObject();
		}

		/// <summary>
		/// Serialize an object
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="value"></param>
		/// <returns></returns>
		public static CbObject Serialize<T>(T value)
		{
			CbWriter writer = new CbWriter();
			CbConverter.GetConverter<T>().Write(writer, value);
			return writer.ToObject();
		}

		/// <summary>
		/// Serialize a property to a given writer
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="writer"></param>
		/// <param name="value"></param>
		public static void Serialize<T>(CbWriter writer, T value)
		{
			CbConverter.GetConverter<T>().Write(writer, value);
		}

		/// <summary>
		/// Serialize a named property to the given writer
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="writer"></param>
		/// <param name="name"></param>
		/// <param name="value"></param>
		public static void Serialize<T>(CbWriter writer, Utf8String name, T value)
		{
			CbConverter.GetConverter<T>().WriteNamed(writer, name, value);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbObject"/>
		/// </summary>
		/// <param name="field"></param>
		/// <param name="type">Type of the object to read</param>
		/// <returns></returns>
		public static object? Deserialize(CbField field, Type type)
		{
			return CbConverter.GetConverter(type).ReadObject(field);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbField"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="field"></param>
		/// <returns></returns>
		public static T Deserialize<T>(CbField field)
		{
			return CbConverter.GetConverter<T>().Read(field);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbObject"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="obj"></param>
		/// <returns></returns>
		public static T Deserialize<T>(CbObject obj) => Deserialize<T>(obj.AsField());

		/// <summary>
		/// Deserialize an object from a block of memory
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="data"></param>
		/// <returns></returns>
		public static T Deserialize<T>(ReadOnlyMemory<byte> data) => Deserialize<T>(new CbField(data));
	}
}
