// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using EpicGames.Core;
using Horde.Agent.Parser.Interfaces;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Parser
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
		readonly LogParserContext _context;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="logger">The logger to receive parsed output messages</param>
		/// <param name="context">Context for parsing this log</param>
		/// <param name="ignorePatterns">List of patterns to ignore</param>
		public LogParser(ILogger logger, LogParserContext context, List<string> ignorePatterns)
			: base(logger)
		{
			_context = context;
			foreach (string ignorePattern in ignorePatterns)
			{
				IgnorePatterns.Add(new Regex(ignorePattern));
			}

			foreach (Type type in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (type.IsClass && typeof(ILogEventMatcher).IsAssignableFrom(type))
				{
					ILogEventMatcher matcher;
					if (type.GetConstructor(new[] { typeof(ILogContext) }) != null)
					{
						matcher = (ILogEventMatcher)Activator.CreateInstance(type, (ILogContext)context)!;
					}
					else
					{
						matcher = (ILogEventMatcher)Activator.CreateInstance(type)!;
					}
					Matchers.Add(matcher);
				}
			}
		}

		/// <inheritdoc/>
		protected override void WriteEvents(List<LogEvent> events)
		{
			base.WriteEvents(events);

			// Also flag that an error has occurred for future add the error to the log context, so that future errors can examine it
			if (events.Any(x => x.Level >= LogLevel.Error))
			{
				_context.HasLoggedErrors = true;
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
