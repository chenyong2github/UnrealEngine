// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using HordeAgent.Utility;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers.Text;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Parser
{
	/// <summary>
	/// Concrete implementation of <see cref="ILogContext"/>
	/// </summary>
	public class LogParserContext : ILogContext
	{
		/// <inheritdoc/>
		public DirectoryReference? WorkspaceDir { get; set; }

		/// <inheritdoc/>
		public string? PerforceStream { get; set; }

		/// <inheritdoc/>
		public int? PerforceChange { get; set; }

		/// <inheritdoc/>
		public bool HasLoggedErrors { get; set; }
	}

	/// <summary>
	/// Turns raw text output into structured logging events
	/// </summary>
	public class LogParser : LogEventParser
	{
		LogParserContext Context;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Logger">The logger to receive parsed output messages</param>
		/// <param name="Context">Context for parsing this log</param>
		/// <param name="IgnorePatterns">List of patterns to ignore</param>
		public LogParser(ILogger Logger, LogParserContext Context, List<string> IgnorePatterns)
			: base(Logger)
		{
			this.Context = Context;
			foreach (string IgnorePattern in IgnorePatterns)
			{
				this.IgnorePatterns.Add(new Regex(IgnorePattern));
			}

			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (Type.IsClass && typeof(ILogEventMatcher).IsAssignableFrom(Type))
				{
					ILogEventMatcher Matcher;
					if (Type.GetConstructor(new[] { typeof(ILogContext) }) != null)
					{
						Matcher = (ILogEventMatcher)Activator.CreateInstance(Type, (ILogContext)Context)!;
					}
					else
					{
						Matcher = (ILogEventMatcher)Activator.CreateInstance(Type)!;
					}
					Matchers.Add(Matcher);
				}
			}
		}

		/// <inheritdoc/>
		protected override void WriteEvents(List<LogEvent> Events)
		{
			base.WriteEvents(Events);

			// Also flag that an error has occurred for future add the error to the log context, so that future errors can examine it
			if (Events.Any(x => x.Level >= LogLevel.Error))
			{
				Context.HasLoggedErrors = true;
			}
		}

		/// <summary>
		/// Static constructor
		/// </summary>
		static LogParser()
		{
			System.Text.RegularExpressions.Regex.CacheSize = 1000;
		}
	}
}
