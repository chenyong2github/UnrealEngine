using EpicGames.Core;
using HordeAgent.Parser;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace HordeAgent.Commands.Parse
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Parse", "Parses a file into structured logging output")]
	class ParseCommand : Command
	{
		[CommandLine("-File=", Required = true)]
		[Description("Log file to parse rather than executing an external program.")]
		FileReference InputFile = null!;

		[CommandLine("-Ignore=")]
		[Description("Path to a file containing error patterns to ignore, one regex per line.")]
		FileReference? IgnorePatternsFile = null;

		[CommandLine("-WorkspaceDir=")]
		[Description("The root workspace directory")]
		DirectoryReference? WorkspaceDir = null;

		[CommandLine("-Stream=")]
		[Description("The stream synced to the workspace")]
		string? Stream = null;

		[CommandLine("-Change=")]
		[Description("The changelist number that has been synced")]
		int? Change = null;

		public override Task<int> ExecuteAsync(ILogger Logger)
		{
			// Read all the ignore patterns
			List<string> IgnorePatterns = new List<string>();
			if (IgnorePatternsFile != null)
			{
				if (!FileReference.Exists(IgnorePatternsFile))
				{
					throw new FatalErrorException("Unable to read '{0}", IgnorePatternsFile);
				}

				// Read all the ignore patterns
				string[] Lines = FileReference.ReadAllLines(IgnorePatternsFile);
				foreach (string Line in Lines)
				{
					string TrimLine = Line.Trim();
					if (TrimLine.Length > 0 && !TrimLine.StartsWith("#"))
					{
						IgnorePatterns.Add(TrimLine);
					}
				}
			}

			// Read the file and pipe it through the event parser
			using (StreamReader Reader = new StreamReader(InputFile.FullName))
			{
				LogParserContext Context = new LogParserContext();
				Context.WorkspaceDir = WorkspaceDir;
				Context.PerforceStream = Stream;
				Context.PerforceChange = Change;

				using (LogParser Parser = new LogParser(Logger, Context, IgnorePatterns))
				{
					for (; ; )
					{
						string? Line = Reader.ReadLine();
						if(Line == null)
						{
							break;
						}
						Parser.WriteLine(Line);
					}
				}
			}
			return Task.FromResult(0);
		}
	}
}
