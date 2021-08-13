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
	[AttributeUsage(AttributeTargets.Class)]
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
