// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization.Converters;
using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;
using System.Text;

namespace EpicGames.Serialization
{
	/// <summary>
	/// Attribute declaring the converter to use for a type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct | AttributeTargets.Property)]
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
	public interface ICbConverter
	{
		/// <summary>
		/// Reads an object from a field
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		object? ReadObject(CbField Field);

		/// <summary>
		/// Writes an object to a field
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Value"></param>
		void WriteObject(CbWriter Writer, object? Value);

		/// <summary>
		/// Writes an object to a named field, if not equal to the default value
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		void WriteNamedObject(CbWriter Writer, Utf8String Name, object? Value);
	}

	/// <summary>
	/// Converter for a particular type
	/// </summary>
	public interface ICbConverter<T> : ICbConverter
	{
		/// <summary>
		/// Reads an object from a field
		/// </summary>
		/// <param name="Field"></param>
		/// <returns></returns>
		T Read(CbField Field);

		/// <summary>
		/// Writes an object to a field
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Value"></param>
		void Write(CbWriter Writer, T Value);

		/// <summary>
		/// Writes an object to a named field, if not equal to the default value
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Name"></param>
		/// <param name="Value"></param>
		void WriteNamed(CbWriter Writer, Utf8String Name, T Value);
	}

	/// <summary>
	/// Interface for obtaining static methods that can be invoked directly
	/// </summary>
	public interface ICbConverterMethods
	{
		/// <summary>
		/// Method with the signature CbField -> T
		/// </summary>
		public MethodInfo ReadMethod { get; }

		/// <summary>
		/// Method with the signature CbWriter, T -> void
		/// </summary>
		public MethodInfo WriteMethod { get; }

		/// <summary>
		/// Method with the signature CbWriter, Utf8String, T -> void
		/// </summary>
		public MethodInfo WriteNamedMethod { get; }
	}

	/// <summary>
	/// Helper class for wrapping regular converters in a ICbConverterMethods interface
	/// </summary>
	public static class CbConverterMethods
	{
		class CbConverterMethodsWrapper<TConverter, T> : ICbConverterMethods where TConverter : class, ICbConverter<T>
		{
			static TConverter StaticConverter = null!;

			static T Read(CbField Field) => StaticConverter.Read(Field);
			static void Write(CbWriter Writer, T Value) => StaticConverter.Write(Writer, Value);
			static void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => StaticConverter.WriteNamed(Writer, Name, Value);

			public MethodInfo ReadMethod { get; } = GetMethodInfo(() => Read(null!));
			public MethodInfo WriteMethod { get; } = GetMethodInfo(() => Write(null!, default!));
			public MethodInfo WriteNamedMethod { get; } = GetMethodInfo(() => WriteNamed(null!, null!, default!));

			public CbConverterMethodsWrapper(TConverter Converter)
			{
				StaticConverter = Converter;
			}

			static MethodInfo GetMethodInfo(Expression<Action> Expr)
			{
				return ((MethodCallExpression)Expr.Body).Method;
			}
		}

		static Dictionary<PropertyInfo, ICbConverterMethods> PropertyToMethods = new Dictionary<PropertyInfo, ICbConverterMethods>();
		static Dictionary<Type, ICbConverterMethods> TypeToMethods = new Dictionary<Type, ICbConverterMethods>();

		static ICbConverterMethods CreateWrapper(Type Type, ICbConverter Converter)
		{
			Type ConverterMethodsType = typeof(CbConverterMethodsWrapper<,>).MakeGenericType(Converter.GetType(), Type);
			return (ICbConverterMethods)Activator.CreateInstance(ConverterMethodsType, new object[] { Converter })!;
		}

		/// <summary>
		/// Gets a <see cref="ICbConverterMethods"/> interface for the given type
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static ICbConverterMethods Get(PropertyInfo Property)
		{
			ICbConverterMethods? Methods;
			if (!PropertyToMethods.TryGetValue(Property, out Methods))
			{
				ICbConverter Converter = CbConverter.GetConverter(Property);
				Methods = (Converter as ICbConverterMethods) ?? CreateWrapper(Property.PropertyType, Converter);
				PropertyToMethods.Add(Property, Methods);
			}
			return Methods;
		}

		/// <summary>
		/// Gets a <see cref="ICbConverterMethods"/> interface for the given type
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static ICbConverterMethods Get(Type Type)
		{
			ICbConverterMethods? Methods;
			if (!TypeToMethods.TryGetValue(Type, out Methods))
			{
				ICbConverter Converter = CbConverter.GetConverter(Type);
				Methods = (Converter as ICbConverterMethods) ?? CreateWrapper(Type, Converter);
				TypeToMethods.Add(Type, Methods);
			}
			return Methods;
		}
	}

	/// <summary>
	/// Base class for converter implementations 
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public abstract class CbConverterBase<T> : ICbConverter<T>
	{
		/// <inheritdoc/>
		public object? ReadObject(CbField Field) => Read(Field);

		/// <inheritdoc/>
		public void WriteObject(CbWriter Writer, object? Value) => Write(Writer, (T)Value!);

		/// <inheritdoc/>
		public void WriteNamedObject(CbWriter Writer, Utf8String Name, object? Value) => WriteNamed(Writer, Name, (T)Value!);

		/// <inheritdoc/>
		public abstract T Read(CbField Field);

		/// <inheritdoc/>
		public abstract void Write(CbWriter Writer, T Value);

		/// <inheritdoc/>
		public abstract void WriteNamed(CbWriter Writer, Utf8String Name, T Value);
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
		public abstract ICbConverter? CreateConverter(Type Type);
	}

	/// <summary>
	/// Utility functions for creating converters
	/// </summary>
	public static class CbConverter
	{
		/// <summary>
		/// Object used for locking access to shared objects
		/// </summary>
		static object LockObject = new object();

		/// <summary>
		/// Cache of property to converter
		/// </summary>
		static Dictionary<PropertyInfo, ICbConverter> PropertyToConverter = new Dictionary<PropertyInfo, ICbConverter>();

		/// <summary>
		/// Cache of type to converter
		/// </summary>
		public static Dictionary<Type, ICbConverter> TypeToConverter = new Dictionary<Type, ICbConverter>()
		{
			[typeof(bool)] = new CbPrimitiveConverter<bool>(x => x.AsBool(), (w, v) => w.WriteBoolValue(v), (w, n, v) => w.WriteBool(n, v)),
			[typeof(int)] = new CbPrimitiveConverter<int>(x => x.AsInt32(), (w, v) => w.WriteIntegerValue(v), (w, n, v) => w.WriteInteger(n, v)),
			[typeof(long)] = new CbPrimitiveConverter<long>(x => x.AsInt64(), (w, v) => w.WriteIntegerValue(v), (w, n, v) => w.WriteInteger(n, v)),
			[typeof(double)] = new CbPrimitiveConverter<double>(x => x.AsDouble(), (w, v) => w.WriteDoubleValue(v), (w, n, v) => w.WriteDouble(n, v)),
			[typeof(Utf8String)] = new CbPrimitiveConverter<Utf8String>(x => x.AsUtf8String(), (w, v) => w.WriteUtf8StringValue(v), (w, n, v) => w.WriteUtf8String(n, v)),
			[typeof(IoHash)] = new CbPrimitiveConverter<IoHash>(x => x.AsHash(), (w, v) => w.WriteHashValue(v), (w, n, v) => w.WriteHash(n, v)),
			[typeof(CbObjectAttachment)] = new CbPrimitiveConverter<CbObjectAttachment>(x => x.AsObjectAttachment(), (w, v) => w.WriteObjectAttachmentValue(v.Hash), (w, n, v) => w.WriteObjectAttachment(n, v.Hash)),
			[typeof(CbBinaryAttachment)] = new CbPrimitiveConverter<CbBinaryAttachment>(x => x.AsBinaryAttachment(), (w, v) => w.WriteBinaryAttachmentValue(v.Hash), (w, n, v) => w.WriteBinaryAttachment(n, v.Hash)),
			[typeof(DateTime)] = new CbPrimitiveConverter<DateTime>(x => x.AsDateTime(), (w, v) => w.WriteDateTimeValue(v), (w, n, v) => w.WriteDateTime(n, v)),
			[typeof(string)] = new CbPrimitiveConverter<string>(x => x.AsString(), (w, v) => w.WriteStringValue(v), (w, n, v) => w.WriteString(n, v)),
			[typeof(ReadOnlyMemory<byte>)] = new CbPrimitiveConverter<ReadOnlyMemory<byte>>(x => x.AsBinary(), (w, v) => w.WriteBinaryValue(v), (w, n, v) => w.WriteBinary(n, v)),
			[typeof(byte[])] = new CbPrimitiveConverter<byte[]>(x => x.AsBinaryArray(), (w, v) => w.WriteBinaryArrayValue(v), (w, n, v) => w.WriteBinaryArray(n, v)),
			[typeof(CbField)] = new CbFieldConverter(),
			[typeof(CbObject)] = new CbObjectConverter()
		};

		/// <summary>
		/// List of converter factories. Must be 
		/// </summary>
		public static List<CbConverterFactory> ConverterFactories = new List<CbConverterFactory>
		{
			new CbClassConverterFactory(),
			new CbEnumConverterFactory(),
			new CbListConverterFactory(),
			new CbArrayConverterFactory(),
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
			public static ICbConverter<T> Instance { get; } = CreateConverter();

			/// <summary>
			/// Create a typed converter
			/// </summary>
			/// <returns></returns>
			static ICbConverter<T> CreateConverter()
			{
				ICbConverter Converter = GetConverter(typeof(T));
				return (Converter as ICbConverter<T>) ?? new CbConverterWrapper(Converter);
			}

			/// <summary>
			/// Wrapper class to convert an untyped converter into a typed one
			/// </summary>
			class CbConverterWrapper : CbConverterBase<T>
			{
				ICbConverter Inner;

				public CbConverterWrapper(ICbConverter Inner) => this.Inner = Inner;

				/// <inheritdoc/>
				public override T Read(CbField Field) => (T)Inner.ReadObject(Field)!;

				/// <inheritdoc/>
				public override void Write(CbWriter Writer, T Value) => Inner.WriteObject(Writer, Value);

				/// <inheritdoc/>
				public override void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => Inner.WriteNamedObject(Writer, Name, Value);
			}
		}

		/// <summary>
		/// Gets the converter for a particular property
		/// </summary>
		/// <param name="Property"></param>
		/// <returns></returns>
		public static ICbConverter GetConverter(PropertyInfo Property)
		{
			CbConverterAttribute? ConverterAttribute = Property.GetCustomAttribute<CbConverterAttribute>();
			if (ConverterAttribute != null)
			{
				Type ConverterType = ConverterAttribute.ConverterType;
				lock (LockObject)
				{
					ICbConverter? Converter;
					if (!PropertyToConverter.TryGetValue(Property, out Converter))
					{
						Converter = (ICbConverter?)Activator.CreateInstance(ConverterType)!;
						PropertyToConverter.Add(Property, Converter);
					}
					return Converter;
				}
			}
			return GetConverter(Property.PropertyType);
		}

		/// <summary>
		/// Gets the converter for a particular type
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static ICbConverter GetConverter(Type Type)
		{
			ICbConverter? Converter;
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
						Converter = (ICbConverter?)Activator.CreateInstance(ConverterType)!;
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
		public static ICbConverter<T> GetConverter<T>()
		{
			return CbConverterCache<T>.Instance;
		}
	}
}

