// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Serialization;
using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;

namespace EpicGames.Redis
{
	/// <summary>
	/// Attribute specifying the converter type to use for a class
	/// </summary>
	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public class RedisConverterAttribute : Attribute
	{
		/// <summary>
		/// Type of the converter to use
		/// </summary>
		public Type ConverterType { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ConverterType">The converter type</param>
		public RedisConverterAttribute(Type ConverterType)
		{
			this.ConverterType = ConverterType;
		}
	}

	/// <summary>
	/// Converter to and from RedisValue types
	/// </summary>
	public interface IRedisConverter<T>
	{
		/// <summary>
		/// Serailize an object to a RedisValue
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		RedisValue ToRedisValue(T Value);

		/// <summary>
		/// Deserialize an object from a RedisValue
		/// </summary>
		/// <param name="Value"></param>
		/// <returns></returns>
		T FromRedisValue(RedisValue Value);
	}

	/// <summary>
	/// Redis serializer that uses compact binary to serialize objects
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public sealed class RedisCbConverter<T> : IRedisConverter<T>
	{
		/// <inheritdoc/>
		public RedisValue ToRedisValue(T Value)
		{
			return CbSerializer.Serialize(Value).GetView();
		}

		/// <inheritdoc/>
		public T FromRedisValue(RedisValue Value)
		{
			return CbSerializer.Deserialize<T>(new CbField((byte[])Value));
		}
	}

	/// <summary>
	/// Handles serialization of types to RedisValue instances
	/// </summary>
	public static class RedisSerializer
	{
		class RedisStringConverter<T> : IRedisConverter<T>
		{
			TypeConverter TypeConverter;

			public RedisStringConverter(TypeConverter TypeConverter)
			{
				this.TypeConverter = TypeConverter;
			}

			public RedisValue ToRedisValue(T Value) => (string)TypeConverter.ConvertTo(Value, typeof(string));
			public T FromRedisValue(RedisValue Value) => (T)TypeConverter.ConvertFrom((string)Value);
		}

		class RedisNativeConverter<T> : IRedisConverter<T>
		{
			Func<RedisValue, T> FromRedisValueFunc;
			Func<T, RedisValue> ToRedisValueFunc;

			public RedisNativeConverter(Func<RedisValue, T> FromRedisValueFunc, Func<T, RedisValue> ToRedisValueFunc)
			{
				this.FromRedisValueFunc = FromRedisValueFunc;
				this.ToRedisValueFunc = ToRedisValueFunc;
			}

			public T FromRedisValue(RedisValue Value) => FromRedisValueFunc(Value);
			public RedisValue ToRedisValue(T Value) => ToRedisValueFunc(Value);
		}

		static Dictionary<Type, object> NativeConverters = CreateNativeConverterLookup();

		static Dictionary<Type, object> CreateNativeConverterLookup()
		{
			KeyValuePair<Type, object>[] Converters =
			{
				CreateNativeConverter(x => (bool)x, x => x),
				CreateNativeConverter(x => (int)x, x => x),
				CreateNativeConverter(x => (int?)x, x => x),
				CreateNativeConverter(x => (uint)x, x => x),
				CreateNativeConverter(x => (uint?)x, x => x),
				CreateNativeConverter(x => (long)x, x => x),
				CreateNativeConverter(x => (long?)x, x => x),
				CreateNativeConverter(x => (ulong)x, x => x),
				CreateNativeConverter(x => (ulong?)x, x => x),
				CreateNativeConverter(x => (double)x, x => x),
				CreateNativeConverter(x => (double?)x, x => x),
				CreateNativeConverter(x => (ReadOnlyMemory<byte>)x, x => x),
				CreateNativeConverter(x => (byte[])x, x => x),
				CreateNativeConverter(x => (string)x, x => x),
			};
			return new Dictionary<Type, object>(Converters);
		}

		static KeyValuePair<Type, object> CreateNativeConverter<T>(Func<RedisValue, T> FromRedisValueFunc, Func<T, RedisValue> ToRedisValueFunc)
		{
			return new KeyValuePair<Type, object>(typeof(T), new RedisNativeConverter<T>(FromRedisValueFunc, ToRedisValueFunc));
		}

		/// <summary>
		/// Static class for caching converter lookups
		/// </summary>
		/// <typeparam name="T"></typeparam>
		class CachedConverter<T>
		{
			public static IRedisConverter<T> Converter = CreateConverter();

			static IRedisConverter<T> CreateConverter()
			{
				Type Type = typeof(T);

				// Check for a custom converter
				RedisConverterAttribute? Attribute = Type.GetCustomAttribute<RedisConverterAttribute>();
				if (Attribute != null)
				{
					Type ConverterType = Attribute.ConverterType;
					if (ConverterType.IsGenericTypeDefinition)
					{
						ConverterType = ConverterType.MakeGenericType(Type);
					}
					return (IRedisConverter<T>)Activator.CreateInstance(ConverterType)!;
				}

				// Check for known basic types
				object? NativeConverter;
				if (NativeConverters.TryGetValue(typeof(T), out NativeConverter))
                {
					return (IRedisConverter<T>)NativeConverter;
                }

				// Check if there's a regular converter we can use to convert to/from a string
				TypeConverter? Converter = TypeDescriptor.GetConverter(Type);
				if (Converter != null && Converter.CanConvertFrom(typeof(string)) && Converter.CanConvertTo(typeof(string)))
				{
					return new RedisStringConverter<T>(Converter);
				}

				throw new Exception($"Unable to find Redis converter for {Type.Name}");
			}
		}

		/// <summary>
		/// Gets the converter for a particular type
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <returns></returns>
		public static IRedisConverter<T> GetConverter<T>()
		{
			return CachedConverter<T>.Converter;
		}

		/// <summary>
		/// Serialize an object to a <see cref="RedisValue"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static RedisValue Serialize<T>(T Value)
		{
			return CachedConverter<T>.Converter.ToRedisValue(Value);
		}

		/// <summary>
		/// Deserialize a <see cref="RedisValue"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Value"></param>
		/// <returns></returns>
		public static T Deserialize<T>(RedisValue Value)
		{
			return CachedConverter<T>.Converter.FromRedisValue(Value);
		}
	}
}
