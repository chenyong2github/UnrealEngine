using EpicGames.Core;
using HordeAgent.Parser.Interfaces;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace HordeAgent.Parser
{
	/// <summary>
	/// Extension methods to allow adding markup to log event spans
	/// </summary>
	static class LogEventMarkup
	{
		enum MarkupType
		{
			Asset,
			SourceFile,
			Channel,
			Severity,
			Line,
			Column,
			ErrorCode,
			Message,
			Symbol,
			UnitTest,
			ScreenshotTest,
			ToolName,
		}

		/// <summary>
		/// Marks a span of text as a source file
		/// </summary>
		/// <param name="Span">The span to mark</param>
		/// <param name="Context">The current log context</param>
		/// <param name="BaseDir">The base directory for relative paths</param>
		public static void MarkAsSourceFile(this LogEventSpan Span, ILogContext Context, string BaseDir)
		{
			if (Context.WorkspaceDir != null && Context.PerforceStream != null && Context.PerforceChange != null)
			{
				FileReference Location = FileReference.Combine(Context.WorkspaceDir, BaseDir.Replace('\\', Path.DirectorySeparatorChar), Span.Text.Replace('\\', Path.DirectorySeparatorChar));
				if (Location.IsUnderDirectory(Context.WorkspaceDir) && !Location.ContainsName("Intermediate", Context.WorkspaceDir))
				{
					Span.Properties.Add("type", MarkupType.SourceFile);

					string RelativePath = Location.MakeRelativeTo(Context.WorkspaceDir).Replace('\\', '/');
					Span.Properties.Add("relativePath", RelativePath);

					string DepotPath = $"{Context.PerforceStream.TrimEnd('/')}/{RelativePath.Replace(Path.DirectorySeparatorChar, '/')}@{Context.PerforceChange.Value}";
					Span.Properties.Add("depotPath", DepotPath);
				}
			}
		}

		/// <summary>
		/// Marks a span of text as a source file
		/// </summary>
		/// <param name="Span">The span to mark</param>
		/// <param name="Context">The current log context</param>
		public static void MarkAsAsset(this LogEventSpan Span, ILogContext Context)
		{
			if (Context.WorkspaceDir != null && Context.PerforceStream != null && Context.PerforceChange != null)
			{
				FileReference Location = FileReference.Combine(DirectoryReference.Combine(Context.WorkspaceDir, "Engine", "Binaries", "Win64"), Span.Text);
				if (Location.IsUnderDirectory(Context.WorkspaceDir) && !Location.ContainsName("Intermediate", Context.WorkspaceDir))
				{
					Span.Properties.Add("type", MarkupType.Asset);

					string RelativePath = Location.MakeRelativeTo(Context.WorkspaceDir);
					Span.Properties.Add("relativePath", RelativePath);

					string DepotPath = $"{Context.PerforceStream.TrimEnd('/')}/{RelativePath.Replace(Path.DirectorySeparatorChar, '/')}@{Context.PerforceChange.Value}";
					Span.Properties.Add("depotPath", DepotPath);
				}
			}
		}

		/// <summary>
		/// Marks a span of text as a channel name
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsChannel(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.Channel);
		}

		/// <summary>
		/// Marks a span of text as a severity indicator
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsSeverity(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.Severity);
		}

		/// <summary>
		/// Marks a span of text as a line number
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsLineNumber(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.Line);
			Span.Properties.Add("line", int.Parse(Span.Text));
		}

		/// <summary>
		/// Marks a span of text as a column number
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsColumnNumber(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.Column);
			Span.Properties.Add("column", int.Parse(Span.Text));
		}

		/// <summary>
		/// Marks a span of text as an error code
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsErrorCode(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.ErrorCode);
		}

		/// <summary>
		/// Marks a span of text as an error code
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsErrorMessage(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.Message);
		}

		/// <summary>
		/// Marks a span of text as a symbol
		/// </summary>
		/// <param name="Span">The span to mark</param>
		public static void MarkAsSymbol(this LogEventSpan Span)
		{
			string Identifier = Span.Text;

			// Remove any __declspec qualifiers
			Identifier = Regex.Replace(Identifier, "(?<![^a-zA-Z_])__declspec\\([^\\)]+\\)", "");

			// Remove any argument lists for functions (anything after the first paren)
			Identifier = Regex.Replace(Identifier, "\\(.*$", "");

			// Remove any decorators and type information (greedy match up to the last space)
			Identifier = Regex.Replace(Identifier, "^.* ", "");

			// Add it to the list
			Span.Properties.Add("type", MarkupType.Symbol);
			Span.Properties.Add("identifier", Identifier);
		}

		/// <summary>
		/// Marks a span of text as a unit test
		/// </summary>
		/// <param name="Span">The span of text</param>
		/// <param name="Group">Name of the test group</param>
		public static void MarkAsUnitTest(this LogEventSpan Span, string Group)
		{
			Match Match = Regex.Match(Span.Text, @"^\s*([^:]+):\s*([^\s]+)\s*$");
			if (Match.Success)
			{
				Span.Properties.Add("type", MarkupType.UnitTest);
				Span.Properties.Add("group", Group);
				Span.Properties.Add("friendly_name", Match.Groups[1].Value);
				Span.Properties.Add("name", Match.Groups[2].Value);
			}
		}

		/// <summary>
		/// Marks a span of text as a unit test
		/// </summary>
		/// <param name="Span">The span of text</param>
		public static void MarkAsScreenshotTest(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.ScreenshotTest);
		}

		/// <summary>
		/// Marks a span of text as a tool name
		/// </summary>
		/// <param name="Span">The span of text</param>
		public static void MarkAsToolName(this LogEventSpan Span)
		{
			Span.Properties.Add("type", MarkupType.ToolName);
		}
	}
}
