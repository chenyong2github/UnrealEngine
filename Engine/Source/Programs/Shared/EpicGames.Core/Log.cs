// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using JetBrains.Annotations;
using System.Buffers;
using System.ComponentModel.Design;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace EpicGames.Core
{
	/// <summary>
	/// Log Event Type
	/// </summary>
	public enum LogEventType
	{
		/// <summary>
		/// The log event is a fatal error
		/// </summary>
		Fatal = LogLevel.Critical,

		/// <summary>
		/// The log event is an error
		/// </summary>
		Error = LogLevel.Error,

		/// <summary>
		/// The log event is a warning
		/// </summary>
		Warning = LogLevel.Warning,

		/// <summary>
		/// Output the log event to the console
		/// </summary>
		Console = LogLevel.Information,

		/// <summary>
		/// Output the event to the on-disk log
		/// </summary>
		Log = LogLevel.Debug,

		/// <summary>
		/// The log event should only be displayed if verbose logging is enabled
		/// </summary>
		Verbose = LogLevel.Trace,

		/// <summary>
		/// The log event should only be displayed if very verbose logging is enabled
		/// </summary>
		VeryVerbose = LogLevel.Trace
	}

	/// <summary>
	/// Options for formatting messages
	/// </summary>
	[Flags]
	public enum LogFormatOptions
	{
		/// <summary>
		/// Format normally
		/// </summary>
		None = 0,

		/// <summary>
		/// Never write a severity prefix. Useful for pre-formatted messages that need to be in a particular format for, eg. the Visual Studio output window
		/// </summary>
		NoSeverityPrefix = 1,

		/// <summary>
		/// Do not output text to the console
		/// </summary>
		NoConsoleOutput = 2,
	}

	/// <summary>
	/// UAT/UBT Custom log system.
	/// 
	/// This lets you use any TraceListeners you want, but you should only call the static 
	/// methods below, not call Trace.XXX directly, as the static methods
	/// This allows the system to enforce the formatting and filtering conventions we desire.
	///
	/// For posterity, we cannot use the Trace or TraceSource class directly because of our special log requirements:
	///   1. We possibly capture the method name of the logging event. This cannot be done as a macro, so must be done at the top level so we know how many layers of the stack to peel off to get the real function.
	///   2. We have a verbose filter we would like to apply to all logs without having to have each listener filter individually, which would require our string formatting code to run every time.
	///   3. We possibly want to ensure severity prefixes are logged, but Trace.WriteXXX does not allow any severity info to be passed down.
	/// </summary>
	static public class Log
	{
		/// <summary>
		/// Singleton instance of the default logger
		/// </summary>
		private static DefaultLogger DefaultLogger { get; } = new DefaultLogger();

		/// <summary>
		/// The public logging interface
		/// </summary>
		public static ILogger Logger { get; set; } = DefaultLogger;

		/// <summary>
		/// When true, verbose logging is enabled.
		/// </summary>
		public static LogEventType OutputLevel
		{
			get => (LogEventType)DefaultLogger.OutputLevel;
			set => DefaultLogger.OutputLevel = (LogLevel)value;
		}

		/// <summary>
		/// Whether to include timestamps on each line of log output
		/// </summary>
		public static bool IncludeTimestamps
		{
			get => DefaultLogger.IncludeTimestamps;
			set => DefaultLogger.IncludeTimestamps = value;
		}

		/// <summary>
		/// When true, warnings and errors will have a WARNING: or ERROR: prefix, respectively.
		/// </summary>
		public static bool IncludeSeverityPrefix { get; set; } = true;

		/// <summary>
		/// When true, warnings and errors will have a prefix suitable for display by MSBuild (avoiding error messages showing as (EXEC : Error : ")
		/// </summary>
		public static bool IncludeProgramNameWithSeverityPrefix { get; set; }

		/// <summary>
		/// When true, will detect warnings and errors and set the console output color to yellow and red.
		/// </summary>
		public static bool ColorConsoleOutput
		{
			get => DefaultLogger.ColorConsoleOutput;
			set => DefaultLogger.ColorConsoleOutput = value;
		}

		/// <summary>
		/// When true, a timestamp will be written to the log file when the first listener is added
		/// </summary>
		public static bool IncludeStartingTimestamp
		{
			get => DefaultLogger.IncludeStartingTimestamp;
			set => DefaultLogger.IncludeStartingTimestamp = value;
		}

		/// <summary>
		/// When true, create a backup of any log file that would be overwritten by a new log
		/// Log.txt will be backed up with its UTC creation time in the name e.g.
		/// Log-backup-2021.10.29-19.53.17.txt
		/// </summary>
		public static bool BackupLogFiles = true;
		
		/// <summary>
		/// The number of backups to be preserved - when there are more than this, the oldest backups will be deleted.
		/// Backups will not be deleted if BackupLogFiles is false.
		/// </summary>
		public static int LogFileBackupCount = 10;
		
		/// <summary>
		/// Path to the log file being written to. May be null.
		/// </summary>
		public static FileReference? OutputFile => DefaultLogger?.OutputFile;


		/// <summary>
		/// A collection of strings that have been already written once
		/// </summary>
		private static HashSet<string> WriteOnceSet = new HashSet<string>();

		/// <summary>
		/// Adds a trace listener that writes to a log file.
		/// If Log.DuplicateLogFiles is true, two files will be created - one with the requested name,
		/// another with a timestamp appended before any extension.
		/// If a StartupTraceListener was in use, this function will copy its captured data to the log file(s)
		/// and remove the startup listener from the list of registered listeners.
		/// </summary>
		/// <param name="OutputFile">The file to write to</param>
		/// <returns>The created trace listener</returns>
		public static void AddFileWriter(string Name, FileReference OutputFile)
		{
			Log.TraceInformation($"Log file: {OutputFile}");

			if (Log.BackupLogFiles && FileReference.Exists(OutputFile))
			{
				// before creating a new backup, cap the number of existing files
				string FilenameWithoutExtension = OutputFile.GetFileNameWithoutExtension();
				string Extension = OutputFile.GetExtension();

				Regex BackupForm =
					new Regex(FilenameWithoutExtension + @"-backup-\d\d\d\d\.\d\d\.\d\d-\d\d\.\d\d\.\d\d" + Extension);

				foreach (FileReference OldBackup in DirectoryReference
					.EnumerateFiles(OutputFile.Directory)
					// find files that match the way that we name backup files
					.Where(x => BackupForm.IsMatch(x.GetFileName()))
					// sort them from newest to oldest
					.OrderByDescending(x => x.GetFileName())
					// skip the newest ones that are to be kept; -1 because we're about to create another backup.
					.Skip(Log.LogFileBackupCount - 1))
				{
					Log.TraceLog($"Deleting old log file: {OldBackup}");
					FileReference.Delete(OldBackup);
				}

				// Ensure that the backup gets a unique name, in the extremely unlikely case that UBT was run twice during
				// the same second.
				DateTime FileTime = File.GetCreationTimeUtc(OutputFile.FullName);
					
				FileReference BackupFile;
				for (;;)
				{
					string Timestamp = $"{FileTime:yyyy.MM.dd-HH.mm.ss}";
					BackupFile = FileReference.Combine(OutputFile.Directory,
						$"{FilenameWithoutExtension}-backup-{Timestamp}{Extension}");
					if (!FileReference.Exists(BackupFile))
					{
						break;
					}

					FileTime = FileTime.AddSeconds(1);
				}

				FileReference.Move(OutputFile, BackupFile);
			}
			
			TextWriterTraceListener FirstTextWriter = DefaultLogger.AddFileWriter(Name, OutputFile);
			
			// find the StartupTraceListener in the listeners that was added early on
			IEnumerable<StartupTraceListener> StartupListeners = Trace.Listeners.OfType<StartupTraceListener>();
			if (StartupListeners.Any())
			{
				StartupTraceListener StartupListener = StartupListeners.First();
				StartupListener.CopyTo(FirstTextWriter);
				Trace.Listeners.Remove(StartupListener);
			}
		}

		/// <summary>
		/// Adds a <see cref="TraceListener"/> to the collection in a safe manner.
		/// </summary>
		/// <param name="TraceListener">The <see cref="TraceListener"/> to add.</param>
		public static void AddTraceListener(TraceListener TraceListener)
		{
			DefaultLogger.AddTraceListener(TraceListener);
		}

		/// <summary>
		/// Removes a <see cref="TraceListener"/> from the collection in a safe manner.
		/// </summary>
		/// <param name="TraceListener">The <see cref="TraceListener"/> to remove.</param>
		public static void RemoveTraceListener(TraceListener TraceListener)
		{
			DefaultLogger.RemoveTraceListener(TraceListener);
		}

		/// <summary>
		/// Determines if a TextWriterTraceListener has been added to the list of trace listeners
		/// </summary>
		/// <returns>True if a TextWriterTraceListener has been added</returns>
		public static bool HasFileWriter()
		{
			return DefaultLogger.HasFileWriter();
		}

		/// <summary>
		/// Converts a LogEventType into a log prefix. Only used when bLogSeverity is true.
		/// </summary>
		/// <param name="Severity"></param>
		/// <returns></returns>
		private static string GetSeverityPrefix(LogEventType Severity)
		{
			switch (Severity)
			{
				case LogEventType.Fatal:
					return "FATAL ERROR: ";
				case LogEventType.Error:
					return "ERROR: ";
				case LogEventType.Warning:
					return "WARNING: ";
				case LogEventType.Console:
					return "";
				case LogEventType.Verbose:
					return "VERBOSE: ";
				default:
					return "";
			}
		}

		/// <summary>
		/// Writes a formatted message to the console. All other functions should boil down to calling this method.
		/// </summary>
		/// <param name="bWriteOnce">If true, this message will be written only once</param>
		/// <param name="Verbosity">Message verbosity level. We only meaningfully use values up to Verbose</param>
		/// <param name="FormatOptions">Options for formatting messages</param>
		/// <param name="Format">Message format string.</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		private static void WriteLinePrivate(bool bWriteOnce, LogEventType Verbosity, LogFormatOptions FormatOptions, string Format, params object?[] Args)
		{
			if (Logger.IsEnabled((LogLevel)Verbosity))
			{
				StringBuilder Message = new StringBuilder();

				// Get the severity prefix for this message
				if (IncludeSeverityPrefix && ((FormatOptions & LogFormatOptions.NoSeverityPrefix) == 0))
				{
					Message.Append(GetSeverityPrefix(Verbosity));
					if (Message.Length > 0 && IncludeProgramNameWithSeverityPrefix)
					{
						// Include the executable name when running inside MSBuild. If unspecified, MSBuild re-formats them with an "EXEC :" prefix.
						Message.Insert(0, $"{Path.GetFileNameWithoutExtension(Assembly.GetEntryAssembly()!.Location)}: ");
					}
				}

				// Append the formatted string
				int IndentLen = Message.Length;
				if (Args.Length == 0)
				{
					Message.Append(Format);
				}
				else
				{
					Message.AppendFormat(Format, Args);
				}

				// Replace any Windows \r\n sequences with \n
				Message.Replace("\r\n", "\n");

				// Remove any trailing whitespace
				int TrimLen = Message.Length;
				while (TrimLen > 0 && " \t\r\n".Contains(Message[TrimLen - 1]))
				{
					TrimLen--;
				}
				Message.Remove(TrimLen, Message.Length - TrimLen);

				// Update the indent length to include any whitespace at the start of the message
				while (IndentLen < Message.Length && Message[IndentLen] == ' ')
				{
					IndentLen++;
				}

				// If there are multiple lines, insert a prefix at the start of each one
				for (int Idx = 0; Idx < Message.Length; Idx++)
				{
					if (Message[Idx] == '\n')
					{
						Message.Insert(Idx + 1, " ", IndentLen);
						Idx += IndentLen;
					}
				}

				// if we want this message only written one time, check if it was already written out
				if (bWriteOnce && !WriteOnceSet.Add(Message.ToString()))
				{
					return;
				}

				// Forward it on to the internal logger
				Logger.Log((LogLevel)Verbosity, new EventId(), Message, null, (Message, Ex) => Message.ToString());
			}
		}

		/// <summary>
		/// Similar to Trace.WriteLineIf
		/// </summary>
		/// <param name="Condition"></param>
		/// <param name="Verbosity"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[StringFormatMethod("Format")]
		public static void WriteLineIf(bool Condition, LogEventType Verbosity, string Format, params object?[] Args)
		{
			if (Condition)
			{
				WriteLinePrivate(false, Verbosity, LogFormatOptions.None, Format, Args);
			}
		}

		/// <summary>
		/// Mostly an internal function, but expose StackFramesToSkip to allow UAT to use existing wrapper functions and still get proper formatting.
		/// </summary>
		/// <param name="StackFramesToSkip"></param>
		/// <param name="Verbosity"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[Obsolete("StackFramesToSkip has been deprecated since 5.0. Please call an override without this parameter")]
		[StringFormatMethod("Format")]
		public static void WriteLine(int StackFramesToSkip, LogEventType Verbosity, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, Verbosity, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Mostly an internal function, but expose StackFramesToSkip to allow UAT to use existing wrapper functions and still get proper formatting.
		/// </summary>
		/// <param name="StackFramesToSkip"></param>
		/// <param name="Verbosity"></param>
		/// <param name="FormatOptions"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[Obsolete("StackFramesToSkip has been deprecated since 5.0. Please call an override without this parameter")]
		[StringFormatMethod("Format")]
		public static void WriteLine(int StackFramesToSkip, LogEventType Verbosity, LogFormatOptions FormatOptions, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, Verbosity, FormatOptions, Format, Args);
		}

		/// <summary>
		/// Similar to Trace.WriteLine
		/// </summary>
		/// <param name="Verbosity"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[StringFormatMethod("Format")]
		public static void WriteLine(LogEventType Verbosity, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, Verbosity, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Similar to Trace.WriteLine
		/// </summary>
		/// <param name="Verbosity"></param>
		/// <param name="FormatOptions"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[StringFormatMethod("Format")]
		public static void WriteLine(LogEventType Verbosity, LogFormatOptions FormatOptions, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, Verbosity, FormatOptions, Format, Args);
		}

		/// <summary>
		/// Formats an exception for display in the log. The exception message is shown as an error, and the stack trace is included in the log.
		/// </summary>
		/// <param name="Ex">The exception to display</param>
		/// <param name="LogFileName">The log filename to display, if any</param>
		public static void WriteException(Exception Ex, string? LogFileName)
		{
			string LogSuffix = (LogFileName == null) ? "" : String.Format("\n(see {0} for full exception trace)", LogFileName);
			TraceLog("==============================================================================");
			TraceError("{0}{1}", ExceptionUtils.FormatException(Ex), LogSuffix);
			TraceLog("\n{0}", ExceptionUtils.FormatExceptionDetails(Ex));
			TraceLog("==============================================================================");
		}

		/// <summary>
		/// Writes an error message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceError(string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Error, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes an error message to the console, in a format suitable for Visual Studio to parse.
		/// </summary>
		/// <param name="File">The file containing the error</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceErrorTask(FileReference File, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Error, LogFormatOptions.NoSeverityPrefix, "{0}: error: {1}", File, String.Format(Format, Args));
		}

		/// <summary>
		/// Writes an error message to the console, in a format suitable for Visual Studio to parse.
		/// </summary>
		/// <param name="File">The file containing the error</param>
		/// <param name="Line">Line number of the error</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceErrorTask(FileReference File, int Line, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Error, LogFormatOptions.NoSeverityPrefix, "{0}({1}): error: {2}", File, Line, String.Format(Format, Args));
		}

		/// <summary>
		/// Writes a verbose message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[Conditional("TRACE")]
		[StringFormatMethod("Format")]
		public static void TraceVerbose(string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Verbose, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceInformation(string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Console, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a warning message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceWarning(string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Warning, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a warning message to the console, in a format suitable for Visual Studio to parse.
		/// </summary>
		/// <param name="File">The file containing the warning</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceWarningTask(FileReference File, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}: warning: {1}", File, String.Format(Format, Args));
		}

		/// <summary>
		/// Writes a warning message to the console, in a format suitable for Visual Studio to parse.
		/// </summary>
		/// <param name="File">The file containing the warning</param>
		/// <param name="Line">Line number of the warning</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceWarningTask(FileReference File, int Line, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}({1}): warning: {2}", File, Line, String.Format(Format, Args));
		}

		/// <summary>
		/// Writes a message to the console.
		/// </summary>
		/// <param name="File">The file containing the message</param>
		/// <param name="Line">Line number of the message</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		public static void TraceConsoleTask(FileReference File, int Line, string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Console, LogFormatOptions.NoSeverityPrefix, "{0}({1}): {2}", File, Line, String.Format(Format, Args));
		}


		/// <summary>
		/// Writes a very verbose message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[Conditional("TRACE")]
		[StringFormatMethod("Format")]
		public static void TraceVeryVerbose(string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.VeryVerbose, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a message to the log only.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[Conditional("TRACE")]
		[StringFormatMethod("Format")]
		public static void TraceLog(string Format, params object?[] Args)
		{
			WriteLinePrivate(false, LogEventType.Log, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Similar to Trace.WriteLine
		/// </summary>
		/// <param name="Verbosity"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[StringFormatMethod("Format")]
		public static void WriteLineOnce(LogEventType Verbosity, string Format, params object?[] Args)
		{
			WriteLinePrivate(true, Verbosity, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Similar to Trace.WriteLine
		/// </summary>
		/// <param name="Verbosity"></param>
		/// <param name="Options"></param>
		/// <param name="Format"></param>
		/// <param name="Args"></param>
		[StringFormatMethod("Format")]
		public static void WriteLineOnce(LogEventType Verbosity, LogFormatOptions Options, string Format, params object?[] Args)
		{
			WriteLinePrivate(true, Verbosity, Options, Format, Args);
		}

		/// <summary>
		/// Writes an error message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceErrorOnce(string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.Error, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a verbose message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[Conditional("TRACE")]
		[StringFormatMethod("Format")]
		public static void TraceVerboseOnce(string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.Verbose, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceInformationOnce(string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.Console, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a warning message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceWarningOnce(string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.Warning, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a warning message to the console.
		/// </summary>
		/// <param name="File">The file containing the error</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceWarningOnce(FileReference File, string Format, params object?[] Args)
		{
			WriteLinePrivate( true, LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}: warning: {1}", File, String.Format(Format, Args));
		}

		/// <summary>
		/// Writes a warning message to the console.
		/// </summary>
		/// <param name="File">The file containing the error</param>
		/// <param name="Line">Line number of the error</param>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[StringFormatMethod("Format")]
		public static void TraceWarningOnce(FileReference File, int Line, string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}({1}): warning: {2}", File, Line, String.Format(Format, Args));
		}

		/// <summary>
		/// Writes a very verbose message to the console.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[Conditional("TRACE")]
		[StringFormatMethod("Format")]
		public static void TraceVeryVerboseOnce(string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.VeryVerbose, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Writes a message to the log only.
		/// </summary>
		/// <param name="Format">Message format string</param>
		/// <param name="Args">Optional arguments</param>
		[Conditional("TRACE")]
		[StringFormatMethod("Format")]
		public static void TraceLogOnce(string Format, params object?[] Args)
		{
			WriteLinePrivate(true, LogEventType.Log, LogFormatOptions.None, Format, Args);
		}

		/// <summary>
		/// Enter a scope with the given status message. The message will be written to the console without a newline, allowing it to be updated through subsequent calls to UpdateStatus().
		/// The message will be written to the log immediately. If another line is written while in a status scope, the initial status message is flushed to the console first.
		/// </summary>
		/// <param name="Message">The status message</param>
		[Conditional("TRACE")]
		public static void PushStatus(string Message)
		{
			DefaultLogger.PushStatus(Message);
		}

		/// <summary>
		/// Updates the current status message. This will overwrite the previous status line.
		/// </summary>
		/// <param name="Message">The status message</param>
		[Conditional("TRACE")]
		public static void UpdateStatus(string Message)
		{
			DefaultLogger.UpdateStatus(Message);
		}

		/// <summary>
		/// Updates the Pops the top status message from the stack. The mess
		/// </summary>
		/// <param name="Message"></param>
		[Conditional("TRACE")]
		public static void PopStatus()
		{
			DefaultLogger.PopStatus();
		}
	}

	/// <summary>
	/// Default log output device
	/// </summary>
	class DefaultLogger : ILogger
	{
		/// <summary>
		/// Temporary status message displayed on the console.
		/// </summary>
		[DebuggerDisplay("{HeadingText}")]
		class StatusMessage
		{
			/// <summary>
			/// The heading for this status message.
			/// </summary>
			public string HeadingText;

			/// <summary>
			/// The current status text.
			/// </summary>
			public string CurrentText;

			/// <summary>
			/// Whether the heading has been written to the console. Before the first time that lines are output to the log in the midst of a status scope, the heading will be written on a line of its own first.
			/// </summary>
			public bool bHasFlushedHeadingText;

			/// <summary>
			/// Constructor
			/// </summary>
			public StatusMessage(string HeadingText, string CurrentText)
			{
				this.HeadingText = HeadingText;
				this.CurrentText = CurrentText;
			}
		}

		/// <summary>
		/// Object used for synchronization
		/// </summary>
		private object SyncObject = new object();

		/// <summary>
		/// Minimum level for outputting messages
		/// </summary>
		public LogLevel OutputLevel
		{
			get; set;
		}

		/// <summary>
		/// Whether to include timestamps on each line of log output
		/// </summary>
		public bool IncludeTimestamps
		{
			get; set;
		}

		/// <summary>
		/// When true, will detect warnings and errors and set the console output color to yellow and red.
		/// </summary>
		public bool ColorConsoleOutput
		{
			get; set;
		}

		/// <summary>
		/// Whether to write JSON to stdout
		/// </summary>
		public bool WriteJsonToStdOut
		{
			get; set;
		}

		/// <summary>
		/// When true, a timestamp will be written to the log file when the first listener is added
		/// </summary>
		public bool IncludeStartingTimestamp
		{
			get; set;
		}
		private bool IncludeStartingTimestampWritten = false;

		/// <summary>
		/// Path to the log file being written to. May be null.
		/// </summary>
		public FileReference? OutputFile
		{
			get; private set;
		}

		/// <summary>
		/// Whether console output is redirected. This prevents writing status updates that rely on moving the cursor.
		/// </summary>
		private bool AllowStatusUpdates
		{
			get { return !Console.IsOutputRedirected; }
		}

		/// <summary>
		/// When configured, this tracks time since initialization to prepend a timestamp to each log.
		/// </summary>
		private Stopwatch Timer = Stopwatch.StartNew();

		/// <summary>
		/// Stack of status scope information.
		/// </summary>
		private Stack<StatusMessage> StatusMessageStack = new Stack<StatusMessage>();

		/// <summary>
		/// The currently visible status text
		/// </summary>
		private string StatusText = "";

		/// <summary>
		/// Last time a status message was pushed to the stack
		/// </summary>
		private Stopwatch StatusTimer = new Stopwatch();

		ArrayBufferWriter<byte> JsonBufferWriter;
		Utf8JsonWriter JsonWriter;

		/// <summary>
		/// Constructor
		/// </summary>
		public DefaultLogger()
		{
			OutputLevel = LogLevel.Debug;
			ColorConsoleOutput = true;
			IncludeStartingTimestamp = true;

			string? EnvVar = Environment.GetEnvironmentVariable("UE_STDOUT_JSON");
			if(EnvVar != null && int.TryParse(EnvVar, out int Value) && Value != 0)
			{
				WriteJsonToStdOut = true;
			}

			JsonBufferWriter = new ArrayBufferWriter<byte>();
			JsonWriter = new Utf8JsonWriter(JsonBufferWriter);
		}

		/// <summary>
		/// Adds a trace listener that writes to a log file
		/// </summary>
		/// <param name="Name">Listener name</param>
		/// <param name="OutputFile">The file to write to</param>
		/// <returns>The created trace listener</returns>
		public TextWriterTraceListener AddFileWriter(string Name, FileReference OutputFile)
		{
			try
			{
				this.OutputFile = OutputFile;
				DirectoryReference.CreateDirectory(OutputFile.Directory);
				TextWriterTraceListener LogTraceListener = new TextWriterTraceListener(new StreamWriter(OutputFile.FullName), Name);
				lock (SyncObject)
				{
					Trace.Listeners.Add(LogTraceListener);
					WriteInitialTimestamp();
				}
				return LogTraceListener;
			}
			catch (Exception Ex)
			{
				throw new Exception($"Error while creating log file \"{OutputFile}\"", Ex);
			}
		}

		/// <summary>
		/// Adds a <see cref="TraceListener"/> to the collection in a safe manner.
		/// </summary>
		/// <param name="TraceListener">The <see cref="TraceListener"/> to add.</param>
		public void AddTraceListener(TraceListener TraceListener)
		{
			lock (SyncObject)
			{
				if (!Trace.Listeners.Contains(TraceListener))
				{
					Trace.Listeners.Add(TraceListener);
					WriteInitialTimestamp();
				}
			}
		}

		/// <summary>
		/// Write a timestamp to the log, once. To be called when a new listener is added.
		/// </summary>
		private void WriteInitialTimestamp()
		{
			if (IncludeStartingTimestamp && !IncludeStartingTimestampWritten)
			{
				DateTime Now = DateTime.Now;
				this.LogDebug("{Message}", $"Log started at {Now} ({Now.ToUniversalTime():yyyy-MM-ddTHH\\:mm\\:ssZ})");
				IncludeStartingTimestampWritten = true;
			}
		}

		/// <summary>
		/// Removes a <see cref="TraceListener"/> from the collection in a safe manner.
		/// </summary>
		/// <param name="TraceListener">The <see cref="TraceListener"/> to remove.</param>
		public void RemoveTraceListener(TraceListener TraceListener)
		{
			lock (SyncObject)
			{
				if (Trace.Listeners.Contains(TraceListener))
				{
					Trace.Listeners.Remove(TraceListener);
				}
			}
		}

		/// <summary>
		/// Determines if a TextWriterTraceListener has been added to the list of trace listeners
		/// </summary>
		/// <returns>True if a TextWriterTraceListener has been added</returns>
		public static bool HasFileWriter()
		{
			foreach (TraceListener? Listener in Trace.Listeners)
			{
				if (Listener is TextWriterTraceListener)
				{
					return true;
				}
			}
			return false;
		}

		public IDisposable BeginScope<TState>(TState State)
		{
			throw new NotImplementedException();
		}

		public bool IsEnabled(LogLevel LogLevel)
		{
			return LogLevel >= OutputLevel;
		}

		public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception Exception, Func<TState, Exception, string> Formatter)
		{
			string[] Lines = Formatter(State, Exception).Split('\n');
			lock (SyncObject)
			{
				// Output to all the other trace listeners
				string TimePrefix = String.Format("[{0:hh\\:mm\\:ss\\.fff}] ", Timer.Elapsed);
				foreach (TraceListener? Listener in Trace.Listeners)
				{
					if (Listener != null)
					{
						string TimePrefixActual =
							IncludeTimestamps &&
							!(Listener is DefaultTraceListener) // no timestamps when writing to the Visual Studio debug window
							? TimePrefix
							: String.Empty;

						foreach (string Line in Lines)
						{
							string LineWithTime = TimePrefixActual + Line;
							Listener.WriteLine(Line);
							Listener.Flush();
						}
					}
				}

				// Handle the console output separately; we format things differently
				if (LogLevel >= LogLevel.Information)
				{
					FlushStatusHeading();

					bool bResetConsoleColor = false;
					if (ColorConsoleOutput)
					{
						if (LogLevel == LogLevel.Warning)
						{
							Console.ForegroundColor = ConsoleColor.Yellow;
							bResetConsoleColor = true;
						}
						if (LogLevel >= LogLevel.Error)
						{
							Console.ForegroundColor = ConsoleColor.Red;
							bResetConsoleColor = true;
						}
					}
					try
					{
						if (WriteJsonToStdOut)
						{
							JsonBufferWriter.Clear();
							JsonWriter.Reset();

							LogEvent Event = new LogEvent(DateTime.UtcNow, LogLevel, EventId, Formatter(State, Exception), null, null, LogException.FromException(Exception));
							Event.Write(JsonWriter);

							JsonWriter.Flush();
							Console.WriteLine(Encoding.UTF8.GetString(JsonBufferWriter.WrittenSpan));
						}
						else
						{
							foreach (string Line in Lines)
							{
								Console.WriteLine(Line);
							}
						}
					}
					catch (IOException)
					{
						// Potential file access/sharing issue on std out
						// This can occur on some versions of mono (e.g. macOS 6.12.0) if writing to a full pipe
						// during IPC when the reader isn't consuming it quick enough
					}
					finally
					{
						// make sure we always put the console color back.
						if (bResetConsoleColor)
						{
							Console.ResetColor();
						}
					}

					if (StatusMessageStack.Count > 0 && AllowStatusUpdates)
					{
						SetStatusText(StatusMessageStack.Peek().CurrentText);
					}
				}
			}
		}

		/// <summary>
		/// Flushes the current status text before writing out a new log line or status message
		/// </summary>
		void FlushStatusHeading()
		{
			if (StatusMessageStack.Count > 0)
			{
				StatusMessage CurrentStatus = StatusMessageStack.Peek();
				if (CurrentStatus.HeadingText.Length > 0 && !CurrentStatus.bHasFlushedHeadingText && AllowStatusUpdates)
				{
					SetStatusText(CurrentStatus.HeadingText);
					Console.WriteLine();
					StatusText = "";
					CurrentStatus.bHasFlushedHeadingText = true;
				}
				else
				{
					SetStatusText("");
				}
			}
		}

		/// <summary>
		/// Enter a scope with the given status message. The message will be written to the console without a newline, allowing it to be updated through subsequent calls to UpdateStatus().
		/// The message will be written to the log immediately. If another line is written while in a status scope, the initial status message is flushed to the console first.
		/// </summary>
		/// <param name="Message">The status message</param>
		[Conditional("TRACE")]
		public void PushStatus(string Message)
		{
			lock (SyncObject)
			{
				FlushStatusHeading();

				StatusMessage NewStatusMessage = new StatusMessage(Message, Message);
				StatusMessageStack.Push(NewStatusMessage);

				StatusTimer.Restart();

				if (Message.Length > 0)
				{
					this.LogDebug("{Message}", Message);
					SetStatusText(Message);
				}
			}
		}

		/// <summary>
		/// Updates the current status message. This will overwrite the previous status line.
		/// </summary>
		/// <param name="Message">The status message</param>
		[Conditional("TRACE")]
		public void UpdateStatus(string Message)
		{
			lock (SyncObject)
			{
				StatusMessage CurrentStatusMessage = StatusMessageStack.Peek();
				CurrentStatusMessage.CurrentText = Message;

				if (AllowStatusUpdates || StatusTimer.Elapsed.TotalSeconds > 10.0)
				{
					SetStatusText(Message);
					StatusTimer.Restart();
				}
			}
		}

		/// <summary>
		/// Updates the Pops the top status message from the stack. The mess
		/// </summary>
		/// <param name="Message"></param>
		[Conditional("TRACE")]
		public void PopStatus()
		{
			lock (SyncObject)
			{
				StatusMessage CurrentStatusMessage = StatusMessageStack.Peek();
				SetStatusText(CurrentStatusMessage.CurrentText);

				if (StatusText.Length > 0)
				{
					Console.WriteLine();
					StatusText = "";
				}

				StatusMessageStack.Pop();
			}
		}

		/// <summary>
		/// Update the status text. For internal use only; does not modify the StatusMessageStack objects.
		/// </summary>
		/// <param name="NewStatusText">New status text to display</param>
		private void SetStatusText(string NewStatusText)
		{
			if (NewStatusText.Length > 0)
			{
				NewStatusText = LogIndent.Current + NewStatusText;
			}

			if (StatusText != NewStatusText)
			{
				int NumCommonChars = 0;
				while (NumCommonChars < StatusText.Length && NumCommonChars < NewStatusText.Length && StatusText[NumCommonChars] == NewStatusText[NumCommonChars])
				{
					NumCommonChars++;
				}

				if (!AllowStatusUpdates && NumCommonChars < StatusText.Length)
				{
					// Prevent writing backspace characters if the console doesn't support it
					Console.WriteLine();
					StatusText = "";
					NumCommonChars = 0;
				}

				StringBuilder Text = new StringBuilder();
				Text.Append('\b', StatusText.Length - NumCommonChars);
				Text.Append(NewStatusText, NumCommonChars, NewStatusText.Length - NumCommonChars);
				if (NewStatusText.Length < StatusText.Length)
				{
					int NumChars = StatusText.Length - NewStatusText.Length;
					Text.Append(' ', NumChars);
					Text.Append('\b', NumChars);
				}
				Console.Write(Text.ToString());

				StatusText = NewStatusText;
				StatusTimer.Restart();
			}
		}
	}

	/// <summary>
	/// Provider for default logger instances
	/// </summary>
	public class DefaultLoggerProvider : ILoggerProvider
	{
		/// <inheritdoc/>
		public ILogger CreateLogger(string CategoryName)
		{
			return new DefaultLogger();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
		}
	}

	/// <summary>
	/// Extension methods to support the default logger
	/// </summary>
	public static class DefaultLoggerExtensions
	{
		/// <summary>
		/// Adds a regular Epic logger to the builder
		/// </summary>
		/// <param name="Builder">Logging builder</param>
		public static void AddEpicDefault(this ILoggingBuilder Builder)
		{
			Builder.Services.AddSingleton<ILoggerProvider, DefaultLoggerProvider>();
		}
	}
}
