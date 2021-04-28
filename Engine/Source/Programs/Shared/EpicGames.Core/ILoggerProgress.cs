// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Core
{
	/// <summary>
	/// Manages a status message for a long running operation, which can be updated with progress. Typically transient on consoles, and written to the same line.
	/// </summary>
	public interface ILoggerProgress : IDisposable
	{
		/// <summary>
		/// Prefix message for the status
		/// </summary>
		string Message
		{
			get;
		}

		/// <summary>
		/// The current 
		/// </summary>
		string Progress
		{
			get; set;
		}
	}

	/// <summary>
	/// Extension methods for status objects
	/// </summary>
	public static class LoggerProgressExtensions
	{
		/// <summary>
		/// Concrete implementation of <see cref="ILoggerProgress"/>
		/// </summary>
		class LoggerProgress : ILoggerProgress
		{
			/// <summary>
			/// The logger to output to
			/// </summary>
			private ILogger Logger;

			/// <summary>
			/// Prefix message for the status
			/// </summary>
			public string Message
			{
				get;
			}

			/// <summary>
			/// The current 
			/// </summary>
			public string Progress
			{
				get { return ProgressInternal; }
				set
				{
					ProgressInternal = value;

					if (Timer.Elapsed > TimeSpan.FromSeconds(3.0))
					{
						LastOutput = String.Empty;
						Flush();
						Timer.Restart();
					}
				}
			}

			/// <summary>
			/// The last string that was output
			/// </summary>
			string LastOutput = String.Empty;

			/// <summary>
			/// Backing storage for the Progress string
			/// </summary>
			string ProgressInternal = String.Empty;

			/// <summary>
			/// Timer since the last update
			/// </summary>
			Stopwatch Timer = Stopwatch.StartNew();

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Logger">The logger to write to</param>
			/// <param name="Message">The base message to display</param>
			public LoggerProgress(ILogger Logger, string Message)
			{
				this.Logger = Logger;
				this.Message = Message;
				this.ProgressInternal = String.Empty;

				Logger.LogInformation(Message);
			}

			/// <summary>
			/// Dispose of this object
			/// </summary>
			public void Dispose()
			{
				Flush();
			}

			/// <summary>
			/// Flushes the current output to the log
			/// </summary>
			void Flush()
			{
				string Output = Message;
				if (!String.IsNullOrEmpty(Progress))
				{
					Output += $" {Progress}";
				}
				if (!String.Equals(Output, LastOutput, StringComparison.Ordinal))
				{
					Logger.LogInformation(Output);
					LastOutput = Output;
				}
			}
		}

		/// <summary>
		/// Begins a new progress scope
		/// </summary>
		/// <param name="Logger">The logger being written to</param>
		/// <param name="Message">The message prefix</param>
		/// <returns>Scope object, which should be disposed when finished</returns>
		public static ILoggerProgress BeginProgressScope(this ILogger Logger, string Message)
		{
			return new LoggerProgress(Logger, Message);
		}
	}
}
