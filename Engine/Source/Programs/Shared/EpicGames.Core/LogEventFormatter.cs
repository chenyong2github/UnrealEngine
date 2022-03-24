// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Reflection;

namespace EpicGames.Core
{
	/// <summary>
	/// Interface for formatting objects for log events
	/// </summary>
	public interface ILogEventFormatter
	{
		/// <summary>
		/// Format a value for a log file.
		/// </summary>
		/// <param name="value">The value to format</param>
		/// <returns>Value to be written to the log event. This may be a primitive type (int, string, etc...) or a LogValue object. If it is neither, Result.ToString() is used.</returns>
		public object Format(object value);
	}

	/// <summary>
	/// Attribute which specifies a type name for the object in log events
	/// </summary>
	[AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class)]
	public class LogEventTypeAttribute : Attribute
	{
		/// <summary>
		/// Type name to use in log events
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="name">Type name to use in log events</param>
		public LogEventTypeAttribute(string name)
		{
			Name = name;
		}
	}

	/// <summary>
	/// Specifies a type to use for serializing objects to a log event
	/// </summary>
	[AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class)]
	public class LogEventFormatterAttribute : Attribute
	{
		/// <summary>
		/// Type of the formatter
		/// </summary>
		public Type Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="type"></param>
		public LogEventFormatterAttribute(Type type)
		{
			Type = type;
		}
	}

	/// <summary>
	/// Utility methods for manipulating formatters
	/// </summary>
	public static class LogEventFormatter
	{
		/// <summary>
		/// Default formatter for a type; just calls the ToString() method.
		/// </summary>
		public class DefaultFormatter : ILogEventFormatter
		{
			/// <inheritdoc/>
			public object Format(object value) => value.ToString() ?? String.Empty;
		}

		/// <summary>
		/// Formatter which passes the object through unmodified
		/// </summary>
		public class PassThroughFormatter : ILogEventFormatter
		{
			/// <inheritdoc/>
			public object Format(object value) => value;
		}

		/// <summary>
		/// Formatter which adds a type annotation to the object's ToString() method.
		/// </summary>
		public class AnnotateTypeFormatter : ILogEventFormatter
		{
			readonly string _name;

			public AnnotateTypeFormatter(string name)
			{
				_name = name;
			}

			/// <inheritdoc/>
			public object Format(object value) => new LogValue(_name, value.ToString() ?? String.Empty);
		}

		static readonly DefaultFormatter s_defaultFormatterInstance = new DefaultFormatter();

		static readonly ConcurrentDictionary<Type, ILogEventFormatter> s_cachedFormatters = GetDefaultFormatters();

		static ConcurrentDictionary<Type, ILogEventFormatter> GetDefaultFormatters()
		{
			PassThroughFormatter passThroughFormatter = new PassThroughFormatter();

			ConcurrentDictionary<Type, ILogEventFormatter> formatters = new ConcurrentDictionary<Type, ILogEventFormatter>();
			formatters.TryAdd(typeof(bool), passThroughFormatter);
			formatters.TryAdd(typeof(int), passThroughFormatter);
			formatters.TryAdd(typeof(string), passThroughFormatter);
			formatters.TryAdd(typeof(LogValue), passThroughFormatter);

			return formatters;
		}

		/// <summary>
		/// Registers a formatter for a particular type
		/// </summary>
		/// <param name="type"></param>
		/// <param name="formatter"></param>
		public static void RegisterFormatter(Type type, ILogEventFormatter formatter)
		{
			s_cachedFormatters.TryAdd(type, formatter);
		}

		/// <summary>
		/// Gets a formatter for the specified type
		/// </summary>
		/// <param name="type"></param>
		/// <returns></returns>
		public static ILogEventFormatter GetFormatter(Type type)
		{
			for (; ; )
			{
				ILogEventFormatter? formatter;
				if (s_cachedFormatters.TryGetValue(type, out formatter))
				{
					return formatter;
				}

				formatter = CreateFormatter(type);

				if (s_cachedFormatters.TryAdd(type, formatter))
				{
					return formatter;
				}
			}
		}

		static ILogEventFormatter CreateFormatter(Type type)
		{
			LogEventTypeAttribute? typeAttribute = type.GetCustomAttribute<LogEventTypeAttribute>();
			if (typeAttribute != null)
			{
				return new AnnotateTypeFormatter(type.Name);
			}

			LogEventFormatterAttribute? formatterAttribute = type.GetCustomAttribute<LogEventFormatterAttribute>();
			if (formatterAttribute != null)
			{
				return (ILogEventFormatter)Activator.CreateInstance(formatterAttribute.Type)!;
			}

			return s_defaultFormatterInstance;
		}

		public static object Format(object value)
		{
			ILogEventFormatter formatter = GetFormatter(value.GetType());
			return formatter.Format(value);
		}
	}
}
