// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using EpicGames.Perforce.Managed;

namespace HordeAgent.Commands.Workspace
{
	abstract class WorkspaceCommand : Command
	{
		[CommandLine("-Server")]
		[Description("Specifies the Perforce server and port")]
		protected string? ServerAndPort = null;

		[CommandLine("-User")]
		[Description("Specifies the Perforce username")]
		protected string? UserName = null;

		[CommandLine("-BaseDir", Required = true)]
		[Description("Base directory to use for syncing workspaces")]
		protected DirectoryReference BaseDir = null!;

		[CommandLine("-Overwrite")]
		[Description("")]
		protected bool bOverwrite = false;

		protected PerforceConnection Perforce = null!;

		public override void Configure(CommandLineArguments Arguments, ILogger Logger)
		{
			base.Configure(Arguments, Logger);

			if(BaseDir == null)
			{
				for (DirectoryReference? ParentDir = DirectoryReference.GetCurrentDirectory(); ParentDir != null; ParentDir = ParentDir.ParentDirectory)
				{
					if (ManagedWorkspace.Exists(ParentDir))
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

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			Perforce = new PerforceConnection(ServerAndPort, UserName, null, Logger);

			InfoRecord Info = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken.None);

			ILogger RepoLogger = new Logging.HordeLoggerProvider().CreateLogger("Repository");
			ManagedWorkspace Repo = await ManagedWorkspace.LoadOrCreateAsync(Info.ClientHost!, BaseDir, bOverwrite, RepoLogger, CancellationToken.None);
			await ExecuteAsync(Repo, Logger);
			return 0;
		}

		protected abstract Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger);

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
