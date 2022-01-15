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
		/// Object used for locking access to shared objects
		/// </summary>
		static object LockObject = new object();

		/// <summary>
		/// Cache of type to converter
		/// </summary>
		public static Dictionary<Type, CbConverter> TypeToConverter = new Dictionary<Type, CbConverter>()
		{
			[typeof(CbField)] = new CbFieldConverter(),
			[typeof(CbObject)] = new CbObjectConverter()
		};

		/// <summary>
		/// List of converter factories. Must be 
		/// </summary>
		public static List<CbConverterFactory> ConverterFactories = new List<CbConverterFactory>
		{
			new CbDefaultConverterFactory(),
			new CbListConverterFactory(),
			new CbDictionaryConverterFactory(),
			new CbNullableConverterFactory()
		};
	
		/// <summary>
		/// Class used to cache values returned by CreateConverterInfo without having to do a dictionary lookup.
		/// </summary>
		/// <typeparam name="T">The type to be converted</typeparam>
		class CbConverterCache<T>
		{
			/// <summary>
			/// The converter instance
			/// </summary>
			public static CbConverter<T> Instance { get; } = CreateConverter();

			/// <summary>
			/// Create a typed converter
			/// </summary>
			/// <returns></returns>
			static CbConverter<T> CreateConverter()
			{
				CbConverter Converter = GetConverter(typeof(T));
				return (Converter as CbConverter<T>) ?? new CbConverterWrapper(Converter);
			}

			/// <summary>
			/// Wrapper class to convert an untyped converter into a typed one
			/// </summary>
			class CbConverterWrapper : CbConverter<T>
			{
				CbConverter Inner;

				public CbConverterWrapper(CbConverter Inner) => this.Inner = Inner;

				/// <inheritdoc/>
				public override T Read(CbField Field) => (T)Inner.ReadObject(Field)!;

				/// <inheritdoc/>
				public override void Write(CbWriter Writer, T Value) => Inner.WriteObject(Writer, Value);

				/// <inheritdoc/>
				public override void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => Inner.WriteNamedObject(Writer, Name, Value);
			}
		}

		/// <summary>
		/// Gets the converter for a particular type
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static CbConverter GetConverter(Type Type)
		{
			CbConverter? Converter;
			lock (LockObject)
			{
				if (!TypeToConverter.TryGetValue(Type, out Converter))
				{
					CbConverterAttribute? ConverterAttribute = Type.GetCustomAttribute<CbConverterAttribute>();
					if (ConverterAttribute != null)
					{
						Type ConverterType = ConverterAttribute.ConverterType;
						if (Type.IsGenericType && ConverterType.IsGenericTypeDefinition)
						{
							ConverterType = ConverterType.MakeGenericType(Type.GetGenericArguments());
						}
						Converter = (CbConverter?)Activator.CreateInstance(ConverterType)!;
					}
					else
					{
						for (int Idx = ConverterFactories.Count - 1; Idx >= 0 && Converter == null; Idx--)
						{
							Converter = ConverterFactories[Idx].CreateConverter(Type);
						}

						if (Converter == null)
						{
							throw new CbException($"Unable to create converter for {Type.Name}");
						}
					}
					TypeToConverter.Add(Type, Converter!);
				}
			}
			return Converter;
		}

		/// <summary>
		/// Gets the converter for a given type
		/// </summary>
		/// <typeparam name="T">Type to retrieve the converter for</typeparam>
		/// <returns></returns>
		public static CbConverter<T> GetConverter<T>()
		{
			return CbConverterCache<T>.Instance;
		}

		/// <summary>
		/// Serialize an object
		/// </summary>
		/// <param name="Type">Type of the object to serialize</param>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static CbObject Serialize(Type Type, object Value)
		{
			CbWriter Writer = new CbWriter();
			GetConverter(Type).WriteObject(Writer, Value);
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
			GetConverter<T>().Write(Writer, Value);
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
			GetConverter<T>().Write(Writer, Value);
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
			GetConverter<T>().WriteNamed(Writer, Name, Value);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbObject"/>
		/// </summary>
		/// <param name="Field"></param>
		/// <param name="Type">Type of the object to read</param>
		/// <returns></returns>
		public static object? Deserialize(CbField Field, Type Type)
		{
			return GetConverter(Type).ReadObject(Field);
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbField"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Field"></param>
		/// <returns></returns>
		public static T Deserialize<T>(CbField Field)
		{
			return GetConverter<T>().Read(Field);
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
