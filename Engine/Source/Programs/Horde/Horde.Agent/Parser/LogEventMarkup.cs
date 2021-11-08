// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser
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
		public static void AnnotateSourceFile(this LogEventBuilder Builder, Group Group, ILogContext Context, string BaseDir)
		{
			LogValue? Value = null;
			if (Context.WorkspaceDir != null && Context.PerforceStream != null && Context.PerforceChange != null)
			{
				FileReference Location = FileReference.Combine(Context.WorkspaceDir, BaseDir.Replace('\\', Path.DirectorySeparatorChar), Group.Value.Replace('\\', Path.DirectorySeparatorChar));
				if (Location.IsUnderDirectory(Context.WorkspaceDir) && !Location.ContainsName("Intermediate", Context.WorkspaceDir))
				{
					string RelativePath = Location.MakeRelativeTo(Context.WorkspaceDir).Replace('\\', '/');
					string DepotPath = $"{Context.PerforceStream.TrimEnd('/')}/{RelativePath.Replace(Path.DirectorySeparatorChar, '/')}@{Context.PerforceChange.Value}";

					Dictionary<string, object> Properties = new Dictionary<string, object>();
					Properties["relativePath"] = RelativePath;
					Properties["depotPath"] = DepotPath;
					Value = new LogValue("SourceFile", "", Properties);
				}
			}
			Builder.Annotate(Group, Value);
		}

		/// <summary>
		/// Marks a span of text as a source file
		/// </summary>
		public static void AnnotateAsset(this LogEventBuilder Builder, Group Group, ILogContext Context)
		{
			if (Context.WorkspaceDir != null && Context.PerforceStream != null && Context.PerforceChange != null)
			{
				FileReference Location = FileReference.Combine(DirectoryReference.Combine(Context.WorkspaceDir, "Engine", "Binaries", "Win64"), Group.Value);
				if (Location.IsUnderDirectory(Context.WorkspaceDir) && !Location.ContainsName("Intermediate", Context.WorkspaceDir))
				{
					string RelativePath = Location.MakeRelativeTo(Context.WorkspaceDir);
					string DepotPath = $"{Context.PerforceStream.TrimEnd('/')}/{RelativePath.Replace(Path.DirectorySeparatorChar, '/')}@{Context.PerforceChange.Value}";

					Dictionary<string, object> Properties = new Dictionary<string, object>();
					Properties["relativePath"] = RelativePath;
					Properties["depotPath"] = DepotPath;
					Builder.Annotate(Group, new LogValue("Asset", "", Properties));
				}
			}
		}

		/// <summary>
		/// Marks a span of text as a symbol
		/// </summary>
		public static void AnnotateSymbol(this LogEventBuilder Builder, Group Group)
		{
			string Identifier = Group.Value;

			// Remove any __declspec qualifiers
			Identifier = Regex.Replace(Identifier, "(?<![^a-zA-Z_])__declspec\\([^\\)]+\\)", "");

			// Remove any argument lists for functions (anything after the first paren)
			Identifier = Regex.Replace(Identifier, "\\(.*$", "");

			// Remove any decorators and type information (greedy match up to the last space)
			Identifier = Regex.Replace(Identifier, "^.* ", "");

			// Add it to the list
			Dictionary<string, object> Properties = new Dictionary<string, object>();
			Properties["identifier"] = Identifier;
			Builder.Annotate(Group, new LogValue("symbol", "", Properties));
		}
	}
}
