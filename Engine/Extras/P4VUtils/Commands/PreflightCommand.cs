// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace P4VUtils.Commands
{
	class PreflightCommand : Command
	{
		public override string Description => "Runs a preflight of the given changelist on Horde";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Horde: Preflight...", "%p");

		public override async Task<int> Execute(string[] Args, IReadOnlyDictionary<string, string> ConfigValues, ILogger Logger)
		{
			int Change;
			if (Args.Length < 2)
			{
				Logger.LogError("Missing changelist number");
				return 1;
			}
			else if (!int.TryParse(Args[1], out Change))
			{
				Logger.LogError("'{Argument}' is not a numbered changelist", Args[1]);
				return 1;
			}

			PerforceConnection Perforce = new PerforceConnection(null, null, Logger);

			ClientRecord Client = await Perforce.GetClientAsync(null, CancellationToken.None);
			if(Client.Stream == null)
			{
				Logger.LogError("Not being run from a stream client");
				return 1;
			}

			await Perforce.ShelveAsync(Change, ShelveOptions.Overwrite, new[] { "//..." }, CancellationToken.None);

			List<DescribeRecord> Describe = await Perforce.DescribeAsync(DescribeOptions.Shelved, -1, new[] { Change }, CancellationToken.None);
			if (Describe[0].Files.Count == 0)
			{
				Logger.LogError("No files are shelved in the given changelist");
				return 1;
			}

			StreamRecord Stream = await Perforce.GetStreamAsync(Client.Stream, false, CancellationToken.None);
			while (Stream.Type == "virtual" && Stream.Parent != null)
			{
				Stream = await Perforce.GetStreamAsync(Stream.Parent, false, CancellationToken.None);
			}

			string Url = GetUrl(Stream.Stream, Change, ConfigValues);
			Logger.LogInformation("Opening {Url}", Url);
			OpenUrl(Url);

			return 0;
		}

		public virtual string GetUrl(string Stream, int Change, IReadOnlyDictionary<string, string> ConfigValues)
		{
			string? BaseUrl;
			if (!ConfigValues.TryGetValue("HordeServer", out BaseUrl))
			{
				BaseUrl = "https://configure-server-url-in-p4vutils.ini";
			}
			return $"{BaseUrl.TrimEnd('/')}/preflight?stream={Stream}&change={Change}";
		}

		void OpenUrl(string Url)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				Process.Start(new ProcessStartInfo(Url) { UseShellExecute = true });
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				Process.Start("xdg-open", Url);
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				Process.Start("open", Url);
			}
		}
	}

	class PreflightAndSubmitCommand : PreflightCommand
	{
		public override string Description => "Runs a preflight of the given changelist on Horde and submits it";

		public override CustomToolInfo CustomTool => new CustomToolInfo("Horde: Preflight and submit", "%p");

		public override string GetUrl(string Stream, int Change, IReadOnlyDictionary<string, string> ConfigValues)
		{
			return base.GetUrl(Stream, Change, ConfigValues) + "&defaulttemplate=true&submit=true";
		}
	}
}
