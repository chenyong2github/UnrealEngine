// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Horde.Agent.Parser.Interfaces;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Horde.Agent.Parser
{
	/// <summary>
	/// Extension methods to allow adding markup to log event spans
	/// </summary>
	static class LogEventMarkup
	{
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
		public static void AnnotateSourceFile(this LogEventBuilder builder, Group group, ILogContext context, string baseDir)
		{
			LogValue? value = null;
			if (context.WorkspaceDir != null && context.PerforceStream != null && context.PerforceChange != null)
			{
				FileReference location = FileReference.Combine(context.WorkspaceDir, baseDir.Replace('\\', Path.DirectorySeparatorChar), group.Value.Replace('\\', Path.DirectorySeparatorChar));
				if (location.IsUnderDirectory(context.WorkspaceDir) && !location.ContainsName("Intermediate", context.WorkspaceDir))
				{
					string relativePath = location.MakeRelativeTo(context.WorkspaceDir).Replace('\\', '/');
					string depotPath = $"{context.PerforceStream.TrimEnd('/')}/{relativePath.Replace(Path.DirectorySeparatorChar, '/')}@{context.PerforceChange.Value}";

					Dictionary<string, object> properties = new Dictionary<string, object>();
					properties["relativePath"] = relativePath;
					properties["depotPath"] = depotPath;
					value = new LogValue("SourceFile", "", properties);
				}
			}
			builder.Annotate(group, value);
		}

		/// <summary>
		/// Marks a span of text as a source file
		/// </summary>
		public static void AnnotateAsset(this LogEventBuilder builder, Group group, ILogContext context)
		{
			if (context.WorkspaceDir != null && context.PerforceStream != null && context.PerforceChange != null)
			{
				FileReference location = FileReference.Combine(DirectoryReference.Combine(context.WorkspaceDir, "Engine", "Binaries", "Win64"), group.Value);
				if (location.IsUnderDirectory(context.WorkspaceDir) && !location.ContainsName("Intermediate", context.WorkspaceDir))
				{
					string relativePath = location.MakeRelativeTo(context.WorkspaceDir);
					string depotPath = $"{context.PerforceStream.TrimEnd('/')}/{relativePath.Replace(Path.DirectorySeparatorChar, '/')}@{context.PerforceChange.Value}";

					Dictionary<string, object> properties = new Dictionary<string, object>();
					properties["relativePath"] = relativePath;
					properties["depotPath"] = depotPath;
					builder.Annotate(group, new LogValue("Asset", "", properties));
				}
			}
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
			Dictionary<string, object> properties = new Dictionary<string, object>();
			properties["identifier"] = identifier;
			builder.Annotate(group, new LogValue("symbol", "", properties));
		}
	}
}
