// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run.Interfaces;
using BuildAgent.Run.Listeners;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Run
{
	/// <summary>
	/// Executes an external command and parses the output. Echoes the output to the calling process, and parses structured errors that can be used for posting build health info to UnrealGameSync.
	/// </summary>
	[ProgramMode("Run", "Executes a command, processing stdout for structured errors.")]
	class RunMode : ProgramMode
	{
		[CommandLine("-Input=")]
		[Description("Log file to parse rather than executing an external program.")]
		FileReference InputFile = null;

		[CommandLine("-Ignore=")]
		[Description("Path to a file containing error patterns to ignore, one regex per line.")]
		FileReference IgnorePatternsFile = null;

		[CommandLine]
		[Description("The program to run.")]
		FileReference Program = null;

		string[] ProgramArguments;

		[CommandLine]
		[Description("Amount of time to leave before killing the child process.")]
		TimeSpan? Timeout = null;

		[CommandLine("-NoWarnings")]
		[Description("Ignores any warnings")]
		bool bNoWarnings = false;

		[CommandLine("-DebugListener")]
		[Description("Enables the debug listener")]
		bool bDebugListener = false;

		[CommandLine("-ECListener")]
		[Description("Enables the ElectricCommander listener")]
		bool bElectricCommanderListener = false;

		[CommandLine("-Stream=")]
		[Description("Specifies the current stream (for issues output)")]
		string Stream;

		[CommandLine("-Change=")]
		[Description("Specifies the current CL (for issues output)")]
		int Change;

		[CommandLine("-JobName=")]
		[Description("Specifies the current job name (for issues output)")]
		string JobName;

		[CommandLine("-JobUrl=")]
		[Description("Specifies the current job url (for issues output)")]
		string JobUrl;

		[CommandLine("-JobStepName=")]
		[Description("Specifies the current job step name (for issues output)")]
		string JobStepName;

		[CommandLine("-JobStepUrl=")]
		[Description("Specifies the current job step url (for issues output)")]
		string JobStepUrl;

		[CommandLine("-LineUrl=")]
		[Description("Specifies a template for the url to a specific output line (for issues output)")]
		string LineUrl;

		[CommandLine("-BaseDir=")]
		[Description("Specifies the base directory (for issues output)")]
		string BaseDir;

		[CommandLine("-IssuesOutput=")]
		[Description("Specifies an output file for build issues")]
		FileReference IssuesOutputFile = null;

		public override void Configure(CommandLineArguments Arguments)
		{
			base.Configure(Arguments);

			if (InputFile == null && Program == null)
			{
				throw new CommandLineArgumentException(String.Format("Either -{0}=... or -{1}=... must be specified.", nameof(InputFile), nameof(Program)));
			}

			if (!bElectricCommanderListener && !String.IsNullOrEmpty(Environment.GetEnvironmentVariable("COMMANDER_JOBSTEPID")))
			{
				bElectricCommanderListener = true;
			}

			if (IssuesOutputFile != null)
			{
				if (Stream == null)
				{
					throw new CommandLineArgumentException("Missing -Stream=... argument when specifying -IssuesOutput=...");
				}
				if (Change == 0)
				{
					throw new CommandLineArgumentException("Missing -Change=... argument when specifying -IssuesOutput=...");
				}
				if (JobName == null)
				{
					throw new CommandLineArgumentException("Missing -JobName=... argument when specifying -IssuesOutput=...");
				}
				if (JobUrl == null)
				{
					throw new CommandLineArgumentException("Missing -JobUrl=... argument when specifying -IssuesOutput=...");
				}
				if (JobStepName == null)
				{
					throw new CommandLineArgumentException("Missing -JobStepName=... argument when specifying -IssuesOutput=...");
				}
				if (JobStepUrl == null)
				{
					throw new CommandLineArgumentException("Missing -JobStepUrl=... argument when specifying -IssuesOutput=...");
				}
			}

			ProgramArguments = Arguments.GetPositionalArguments();
		}

		public override int Execute()
		{
			int ExitCode = 0;

			// Auto-register all the known matchers in this assembly
			List<IErrorMatcher> Matchers = new List<IErrorMatcher>();
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (Type.GetCustomAttribute<AutoRegisterAttribute>() != null)
				{
					object Instance = Activator.CreateInstance(Type);
					if (typeof(IErrorMatcher).IsAssignableFrom(Type))
					{
						Matchers.Add((IErrorMatcher)Instance);
					}
					else
					{
						throw new Exception(String.Format("Unable to auto-register object of type {0}", Type.Name));
					}
				}
			}

			// Read all the ignore patterns
			List<string> IgnorePatterns = new List<string>();
			if(IgnorePatternsFile != null)
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

			// Create the output listeners
			List<IErrorListener> Listeners = new List<IErrorListener>();
			try
			{
				if (bDebugListener)
				{
					Listeners.Add(new DebugOutputListener());
				}
				if (bElectricCommanderListener)
				{
					Listeners.Add(new ElectricCommanderListener());
				}
				if (IssuesOutputFile != null)
				{
					Listeners.Add(new IssuesListener(Stream, Change, JobName, JobUrl, JobStepName, JobStepUrl, LineUrl, BaseDir, IssuesOutputFile));
				}

				// Process the input
				if (InputFile != null)
				{
					if (!FileReference.Exists(InputFile))
					{
						throw new FatalErrorException("Specified input file '{0}' does not exist", InputFile);
					}

					using (StreamReader Reader = new StreamReader(InputFile.FullName))
					{
						LineFilter Filter = new LineFilter(() => Reader.ReadLine());
						ProcessErrors(Filter.ReadLine, Matchers, IgnorePatterns, Listeners, bNoWarnings);
					}
				}
				else
				{
					CancellationTokenSource CancellationTokenSource = new CancellationTokenSource();
					if (Timeout.HasValue)
					{
						CancellationTokenSource.CancelAfter(Timeout.Value);
					}

					CancellationToken CancellationToken = CancellationTokenSource.Token;
					using (ManagedProcess Process = new ManagedProcess(null, Program.FullName, CommandLineArguments.Join(ProgramArguments), null, null, null, ProcessPriorityClass.Normal))
					{
						Func<string> ReadLine = new LineFilter(() => ReadProcessLine(Process, CancellationToken)).ReadLine;
						ProcessErrors(ReadLine, Matchers, IgnorePatterns, Listeners, bNoWarnings);
						ExitCode = Process.ExitCode;
					}
				}
			}
			finally
			{
				foreach(IErrorListener Listener in Listeners)
				{
					Listener.Dispose();
				}
			}

			// Kill off any remaining child processes
			ProcessUtils.TerminateChildProcesses();
			return ExitCode;
		}

		/// <summary>
		/// Reads a line of output from the given process
		/// </summary>
		/// <param name="Process">The process to read from</param>
		/// <param name="CancellationToken">Cancellation token for when the timeout expires</param>
		/// <returns>The line that was read</returns>
		static string ReadProcessLine(ManagedProcess Process, CancellationToken CancellationToken)
		{
			string Line;
			if (Process.TryReadLine(out Line, CancellationToken))
			{
				Log.TraceInformation("{0}", Line);
			}
			return Line;
		}

		/// <summary>
		/// Process all the errors obtained by calling the ReadLine() function, and forward them to an array of listeners
		/// </summary>
		/// <param name="ReadLine">Delegate used to retrieve each output line</param>
		/// <param name="Matchers">List of matchers to run against the text</param>
		/// <param name="IgnorePatterns">List of patterns to ignore</param>
		/// <param name="Listeners">Set of listeners for processing the errors</param>
		/// <param name="bNoWarnings">Does not output warnings</param>
		static void ProcessErrors(Func<string> ReadLine, List<IErrorMatcher> Matchers, List<string> IgnorePatterns, List<IErrorListener> Listeners, bool bNoWarnings)
		{
			System.Text.RegularExpressions.Regex.CacheSize = 1000;

			LineBuffer Buffer = new LineBuffer(ReadLine, 50);
			ReadOnlyLineBuffer ReadOnlyBuffer = new ReadOnlyLineBuffer(Buffer);

			string FirstLine;
			while (Buffer.TryGetLine(0, out FirstLine))
			{
				// Try to match an error
				ErrorMatch Error = null;
				foreach (IErrorMatcher Matcher in Matchers)
				{
					ErrorMatch NewError = Matcher.Match(ReadOnlyBuffer);
					if (NewError != null && (Error == null || NewError.Priority > Error.Priority))
					{
						Error = NewError;
					}
				}

				// If we matched a warning and don't want it, clear it out
				if (Error != null && Error.Severity == ErrorSeverity.Warning && bNoWarnings)
				{
					Error = null;
				}

				// If we did match something, check if it's not negated by an ignore pattern. We typically have relatively few errors and many more ignore patterns than matchers, so it's quicker 
				// to check them in response to an identified error than to treat them as matchers of their own.
				if (Error != null)
				{
					foreach (string IgnorePattern in IgnorePatterns)
					{
						if(Regex.IsMatch(Buffer[0], IgnorePattern))
						{
							Error = null;
							break;
						}
					}
				}

				// Report the error to the listeners
				int AdvanceLines = 1;
				if (Error != null)
				{
					foreach (IErrorListener Listener in Listeners)
					{
						Listener.OnErrorMatch(Error);
					}
					AdvanceLines = Error.MaxLineNumber + 1 - Buffer.CurrentLineNumber;
				}

				// Move forwards
				Buffer.Advance(AdvanceLines);
			}
		}
	}
}
