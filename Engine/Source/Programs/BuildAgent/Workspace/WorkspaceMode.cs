// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace
{
	abstract class WorkspaceMode : ProgramMode
	{
		[CommandLine("-Server")]
		[Description("Specifies the Perforce server and port")]
		protected string ServerAndPort = null;

		[CommandLine("-User")]
		[Description("Specifies the Perforce username")]
		protected string UserName = null;

		[CommandLine("-BaseDir")]
		[Description("Base directory to use for syncing workspaces")]
		protected DirectoryReference BaseDir = null;

		[CommandLine("-Overwrite")]
		[Description("")]
		protected bool bOverwrite = false;

		[CommandLine("-Verbose")]
		[Description("Enables verbose logging")]
		protected bool bVerbose = false;

		public override void Configure(CommandLineArguments Arguments)
		{
			base.Configure(Arguments);

			if(BaseDir == null)
			{
				for (DirectoryReference ParentDir = DirectoryReference.GetCurrentDirectory(); ParentDir != null; ParentDir = ParentDir.ParentDirectory)
				{
					if (Repository.Exists(ParentDir))
					{
						BaseDir = ParentDir;
						break;
					}
				}

				if (BaseDir == null)
				{
					throw new FatalErrorException("Unable to find existing repository in current working tree. Specify -BaseDir=... explicitly.");
				}
			}
		}

		public override int Execute()
		{
			// Create the repo
			Repository Repo = CreateOrLoadRepository(ServerAndPort, UserName, BaseDir, bOverwrite);

			// Delete any old log files
			FileReference LogFile = FileReference.Combine(BaseDir, "Logs", "Log.txt");
			DirectoryReference.CreateDirectory(LogFile.Directory);
			BackupLogFile(LogFile, TimeSpan.FromDays(3));
			if (bVerbose)
			{
				Log.OutputLevel = LogEventType.Verbose;
			}
			Trace.Listeners.Add(new TextWriterTraceListener(new StreamWriter(LogFile.FullName), "LogTraceListener"));

			Execute(Repo);
			return 0;
		}

		protected abstract void Execute(Repository Repo);

		static void BackupLogFile(FileReference LogFile, TimeSpan RetentionSpan)
		{
			string LogPrefix = LogFile.GetFileNameWithoutExtension() + "-backup-";
			string LogSuffix = LogFile.GetExtension();

			if (FileReference.Exists(LogFile))
			{
				string Timestamp = FileReference.GetLastWriteTime(LogFile).ToString("yyyy.MM.dd-HH.mm.ss");

				string UniqueSuffix = "";
				for (int NextSuffixIdx = 2; ; NextSuffixIdx++)
				{
					FileReference BackupLogFile = FileReference.Combine(LogFile.Directory, String.Format("{0}{1}{2}{3}", LogPrefix, Timestamp, UniqueSuffix, LogSuffix));
					if (!FileReference.Exists(BackupLogFile))
					{
						try
						{
							FileReference.Move(LogFile, BackupLogFile);
						}
						catch (Exception Ex)
						{
							Log.TraceWarning("Unable to backup {0} to {1} ({2})", LogFile, BackupLogFile, Ex.Message);
							Log.TraceLog(ExceptionUtils.FormatException(Ex));
						}
						break;
					}
					UniqueSuffix = String.Format("_{0}", NextSuffixIdx);
				}
			}

			DateTime DeleteTime = DateTime.UtcNow - RetentionSpan;
			foreach (FileInfo File in new DirectoryInfo(LogFile.Directory.FullName).EnumerateFiles(LogPrefix + "*" + LogSuffix))
			{
				if (File.LastWriteTimeUtc < DeleteTime)
				{
					File.Delete();
				}
			}
		}

		static Repository CreateOrLoadRepository(string ServerAndPort, string UserName, DirectoryReference BaseDir, bool bOverwrite)
		{
			if (Repository.Exists(BaseDir))
			{
				try
				{
					return Repository.Load(ServerAndPort, UserName, BaseDir);
				}
				catch (Exception Ex)
				{
					if (bOverwrite)
					{
						Log.TraceWarning("Unable to load existing repository: {0}", Ex.Message);
						Log.TraceVerbose("{0}", ExceptionUtils.FormatExceptionDetails(Ex));
					}
					else
					{
						throw;
					}

				}
			}

			return Repository.Create(ServerAndPort, UserName, BaseDir);
		}

		protected static long ParseSize(string Size)
		{
			long Value;
			if (Size.EndsWith("gb", StringComparison.OrdinalIgnoreCase))
			{
				string SizeValue = Size.Substring(0, Size.Length - 2).TrimEnd();
				if (long.TryParse(SizeValue, out Value))
				{
					return Value * (1024 * 1024 * 1024);
				}
			}
			else if (Size.EndsWith("mb", StringComparison.OrdinalIgnoreCase))
			{
				string SizeValue = Size.Substring(0, Size.Length - 2).TrimEnd();
				if (long.TryParse(SizeValue, out Value))
				{
					return Value * (1024 * 1024);
				}
			}
			else if (Size.EndsWith("kb"))
			{
				string SizeValue = Size.Substring(0, Size.Length - 2).TrimEnd();
				if (long.TryParse(SizeValue, out Value))
				{
					return Value * 1024;
				}
			}
			else
			{
				if (long.TryParse(Size, out Value))
				{
					return Value;
				}
			}
			throw new FatalErrorException("Invalid size '{0}'", Size);
		}

		protected static int ParseChangeNumber(string Change)
		{
			int ChangeNumber;
			if (int.TryParse(Change, out ChangeNumber) && ChangeNumber > 0)
			{
				return ChangeNumber;
			}
			throw new FatalErrorException("Unable to parse change number from '{0}'", Change);
		}

		protected static int ParseChangeNumberOrLatest(string Change)
		{
			if (Change.Equals("Latest", StringComparison.OrdinalIgnoreCase))
			{
				return -1;
			}
			else
			{
				return ParseChangeNumber(Change);
			}
		}

		protected static List<KeyValuePair<string, string>> ParseClientAndStreams(List<string> ClientAndStreamParams)
		{
			List<KeyValuePair<string, string>> ClientAndStreams = new List<KeyValuePair<string, string>>();
			foreach (string ClientAndStreamParam in ClientAndStreamParams)
			{
				int Idx = ClientAndStreamParam.IndexOf(':');
				if (Idx == -1)
				{
					throw new FatalErrorException("Expected -ClientAndStream=<ClientName>:<StreamName>");
				}

				string ClientName = ClientAndStreamParam.Substring(0, Idx);
				string StreamName = ClientAndStreamParam.Substring(Idx + 1);
				if (!ClientAndStreams.Any(cas => cas.Key == ClientName && cas.Value == StreamName))
				{
					ClientAndStreams.Add(new KeyValuePair<string, string>(ClientName, StreamName));
				}
			}
			return ClientAndStreams;
		}

		protected static List<string> ExpandFilters(List<string> Arguments)
		{
			List<string> Filters = new List<string>();
			foreach (string Argument in Arguments)
			{
				foreach (string SingleArgument in Argument.Split(';').Select(x => x.Trim()))
				{
					if (SingleArgument.Length > 0)
					{
						Filters.Add(SingleArgument);
					}
				}
			}
			return Filters;
		}
	}
}
