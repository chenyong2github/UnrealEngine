// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Text;

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
		/// <param name="Value">The value to format</param>
		/// <returns>Value to be written to the log event. This may be a primitive type (int, string, etc...) or a LogValue object. If it is neither, Result.ToString() is used.</returns>
		public object Format(object Value);
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
		/// <param name="Name">Type name to use in log events</param>
		public LogEventTypeAttribute(string Name)
		{
			this.Name = Name;
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
		/// <param name="Type"></param>
		public LogEventFormatterAttribute(Type Type)
		{
			this.Type = Type;
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
			public object Format(object Value) => Value.ToString() ?? String.Empty;
		}

		/// <summary>
		/// Formatter which passes the object through unmodified
		/// </summary>
		public class PassThroughFormatter : ILogEventFormatter
		{
			/// <inheritdoc/>
			public object Format(object Value) => Value;
		}

		/// <summary>
		/// Formatter which adds a type annotation to the object's ToString() method.
		/// </summary>
		public class AnnotateTypeFormatter : ILogEventFormatter
		{
			readonly string Name;

			public AnnotateTypeFormatter(string Name)
			{
				this.Name = Name;
			}

			/// <inheritdoc/>
			public object Format(object Value) => new LogValue(Name, Value.ToString() ?? String.Empty);
		}

		static DefaultFormatter DefaultFormatterInstance = new DefaultFormatter();

		static ConcurrentDictionary<Type, ILogEventFormatter> CachedFormatters = GetDefaultFormatters();

		static ConcurrentDictionary<Type, ILogEventFormatter> GetDefaultFormatters()
		{
			PassThroughFormatter PassThroughFormatter = new PassThroughFormatter();

			ConcurrentDictionary<Type, ILogEventFormatter> Formatters = new ConcurrentDictionary<Type, ILogEventFormatter>();
			Formatters.TryAdd(typeof(bool), PassThroughFormatter);
			Formatters.TryAdd(typeof(int), PassThroughFormatter);
			Formatters.TryAdd(typeof(string), PassThroughFormatter);
			Formatters.TryAdd(typeof(LogValue), PassThroughFormatter);

			return Formatters;
		}

		/// <summary>
		/// Registers a formatter for a particular type
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Formatter"></param>
		public static void RegisterFormatter(Type Type, ILogEventFormatter Formatter)
		{
			CachedFormatters.TryAdd(Type, Formatter);
		}

		/// <summary>
		/// Gets a formatter for the specified type
		/// </summary>
		/// <param name="Type"></param>
		/// <returns></returns>
		public static ILogEventFormatter GetFormatter(Type Type)
		{
			for (; ; )
			{
				ILogEventFormatter? Formatter;
				if (CachedFormatters.TryGetValue(Type, out Formatter))
				{
					return Formatter;
				}

				Formatter = CreateFormatter(Type);

				if (CachedFormatters.TryAdd(Type, Formatter))
				{
					return Formatter;
				}
			}
		}

		static ILogEventFormatter CreateFormatter(Type Type)
		{
			LogEventTypeAttribute? TypeAttribute = Type.GetCustomAttribute<LogEventTypeAttribute>();
			if (TypeAttribute != null)
			{
				return new AnnotateTypeFormatter(Type.Name);
			}

			LogEventFormatterAttribute? FormatterAttribute = Type.GetCustomAttribute<LogEventFormatterAttribute>();
			if (FormatterAttribute != null)
			{
				return (ILogEventFormatter)Activator.CreateInstance(FormatterAttribute.Type)!;
			}

			return DefaultFormatterInstance;
		}


		public static object Format(object Value)
		{
			ILogEventFormatter Formatter = GetFormatter(Value.GetType());
			return Formatter.Format(Value);
		}
	}
}
