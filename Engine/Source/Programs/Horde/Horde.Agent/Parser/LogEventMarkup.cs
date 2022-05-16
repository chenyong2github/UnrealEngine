// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using EpicGames.Core;

namespace Horde.Agent.Parser
{
	/// <summary>
	/// Extension methods to allow adding markup to log event spans
	/// </summary>
	static class LogEventMarkup
	{
		public static readonly Utf8String Asset = new Utf8String("Asset");
		public static readonly Utf8String SourceFile = new Utf8String("SourceFile");

		public static LogValue Channel => new LogValue("Channel", "");
		public static LogValue Severity => new LogValue("Severity", "");
		public static LogValue Message => new LogValue("Message", "");
		public static LogValue LineNumber => new LogValue("Line", "");
		public static LogValue ColumnNumber => new LogValue("Column", "");
		public static LogValue Symbol => new LogValue("Symbol", "");
		public static LogValue ErrorCode => new LogValue("ErrorCode", "");
		public static LogValue ToolName => new LogValue("ToolName", "");
		public static LogValue ScreenshotTest => new LogValue("ScreenshotTest", "");

		/// <summary>
		/// Marks a span of text as a source file
		/// </summary>
		public static void AnnotateSourceFile(this LogEventBuilder builder, Group group, string? baseDir)
		{
			Dictionary<Utf8String, object>? properties = null;
			if (!String.IsNullOrEmpty(baseDir))
			{
				string file = group.Value;
				if (!Path.IsPathRooted(file))
				{
					properties = new Dictionary<Utf8String, object>();
					properties["file"] = Path.Combine(baseDir, file);
				}
			}
			builder.Annotate(group, new LogValue(SourceFile, group.Value, properties));
		}

		/// <summary>
		/// Marks a span of text as a source file
		/// </summary>
		public static void AnnotateAsset(this LogEventBuilder builder, Group group)
		{
			builder.Annotate(group, new LogValue(Asset, group.Value));
		}

		/// <summary>
		/// Marks a span of text as a symbol
		/// </summary>
		public static void AnnotateSymbol(this LogEventBuilder builder, Group group)
		{
			string identifier = group.Value;

			// Remove any __declspec qualifiers
			identifier = Regex.Replace(identifier, "(?<![^a-zA-Z_])__declspec\\([^\\)]+\\)", "");

			// Remove any argument lists for functions (anything after the first paren)
			identifier = Regex.Replace(identifier, "\\(.*$", "");

			// Remove any decorators and type information (greedy match up to the last space)
			identifier = Regex.Replace(identifier, "^.* ", "");

			// Add it to the list
			Dictionary<Utf8String, object> properties = new Dictionary<Utf8String, object>();
			properties["identifier"] = identifier;
			builder.Annotate(group, new LogValue("symbol", "", properties));
		}
	}
}
