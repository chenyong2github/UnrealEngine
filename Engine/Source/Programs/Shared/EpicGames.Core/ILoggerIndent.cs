// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Class to apply a log indent for the lifetime of an object 
	/// </summary>
	public interface ILoggerIndent
	{
		/// <summary>
		/// The indent to apply
		/// </summary>
		string Indent { get; }
	}

	/// <summary>
	/// Wrapper class for ILogger classes which supports LoggerStatusScope
	/// </summary>
	public class DefaultLoggerIndentHandler : ILogger
	{
		/// <summary>
		/// Scoped indent message
		/// </summary>
		class Scope : IDisposable
		{
			/// <summary>
			/// Owning object
			/// </summary>
			DefaultLoggerIndentHandler Owner;

			/// <summary>
			/// The indent scope object
			/// </summary>
			public ILoggerIndent Indent { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			public Scope(DefaultLoggerIndentHandler Owner, ILoggerIndent Indent)
			{
				this.Owner = Owner;
				this.Indent = Indent;

				lock (Owner.Scopes)
				{
					Owner.Scopes.Add(this);
				}
			}

			/// <summary>
			/// Remove this indent from the list
			/// </summary>
			public void Dispose()
			{
				lock (Owner.Scopes)
				{
					Owner.Scopes.Remove(this);
				}
			}
		}

		/// <summary>
		/// Struct to wrap a formatted set of log values with applied indent
		/// </summary>
		/// <typeparam name="TState">Arbitrary type parameter</typeparam>
		public struct FormattedLogValues<TState> : IEnumerable<KeyValuePair<string, object>>
		{
			/// <summary>
			/// The indent to apply
			/// </summary>
			string Indent;

			/// <summary>
			/// The inner state
			/// </summary>
			TState State;

			/// <summary>
			/// Formatter for the inner state
			/// </summary>
			Func<TState, Exception, string> Formatter;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Indent">The indent to apply</param>
			/// <param name="State">The inner state</param>
			/// <param name="Formatter">Formatter for the inner state</param>
			public FormattedLogValues(string Indent, TState State, Func<TState, Exception, string> Formatter)
			{
				this.Indent = Indent;
				this.State = State;
				this.Formatter = Formatter;
			}

			/// <inheritdoc/>
			public IEnumerator<KeyValuePair<string, object>> GetEnumerator()
			{
				IEnumerable<KeyValuePair<string, object>>? InnerEnumerable = State as IEnumerable<KeyValuePair<string, object>>;
				if (InnerEnumerable != null)
				{
					foreach (KeyValuePair<string, object> Pair in InnerEnumerable)
					{
						if (Pair.Key.Equals("{OriginalFormat}", StringComparison.Ordinal))
						{
							yield return new KeyValuePair<string, object>(Pair.Key, Indent + Pair.Value.ToString());
						}
						else
						{
							yield return Pair;
						}
					}
				}
			}

			/// <inheritdoc/>
			IEnumerator IEnumerable.GetEnumerator()
			{
				throw new NotImplementedException();
			}

			/// <summary>
			/// Formats an instance of this object
			/// </summary>
			/// <param name="Values">The object instance</param>
			/// <param name="Exception">The exception to format</param>
			/// <returns>The formatted string</returns>
			public static string Format(FormattedLogValues<TState> Values, Exception Exception)
			{
				return Values.Indent + Values.Formatter(Values.State, Exception);
			}
		}

		/// <summary>
		/// The internal logger
		/// </summary>
		ILogger Inner;

		/// <summary>
		/// Current list of indents
		/// </summary>
		List<Scope> Scopes = new List<Scope>();

		/// <summary>
		/// The current indent text
		/// </summary>
		public string Indent
		{
			get;
			private set;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The logger to wrap</param>
		public DefaultLoggerIndentHandler(ILogger Inner)
		{
			this.Inner = Inner;
			this.Indent = "";
		}

		/// <inheritdoc/>
		public IDisposable BeginScope<TState>(TState State)
		{
			ILoggerIndent? Indent = State as ILoggerIndent;
			if (Indent != null)
			{
				return new Scope(this, Indent);
			}

			return Inner.BeginScope(State);
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel LogLevel)
		{
			return Inner.IsEnabled(LogLevel);
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception Exception, Func<TState, Exception, string> Formatter)
		{
			if (Scopes.Count > 0)
			{
				string Indent = String.Join("", Scopes.Select(x => x.Indent.Indent));
				Inner.Log(LogLevel, EventId, new FormattedLogValues<TState>(Indent, State, Formatter), Exception, FormattedLogValues<TState>.Format);
				return;
			}

			Inner.Log(LogLevel, EventId, State, Exception, Formatter);
		}
	}

	/// <summary>
	/// Extension methods for creating an indent
	/// </summary>
	public static class LoggerIndentExtensions
	{
		/// <summary>
		/// Class to apply a log indent for the lifetime of an object 
		/// </summary>
		class LoggerIndent : ILoggerIndent
		{
			/// <summary>
			/// The previous indent
			/// </summary>
			public string Indent { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Indent">Indent to append to the existing indent</param>
			public LoggerIndent(string Indent)
			{
				this.Indent = Indent;
			}
		}

		/// <summary>
		/// Create an indent
		/// </summary>
		/// <param name="Logger">Logger interface</param>
		/// <param name="Indent">The indent to apply</param>
		/// <returns>Disposable object</returns>
		public static IDisposable BeginIndentScope(this ILogger Logger, string Indent)
		{
			return Logger.BeginScope(new LoggerIndent(Indent));
		}
	}
}
