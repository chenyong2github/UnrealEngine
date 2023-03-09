// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;
using System.IO;
using EpicGames.Core;
using System.Collections.Generic;
using System.Linq;
using UnrealBuildBase;
using Microsoft.Extensions.Logging;

namespace AutomationTool
{
	[Help("Auto-detects P4 settings based on the current path and creates a p4config file with the relevant settings.")]
	[ParamHelp("SetIgnore", "Adds a P4IGNORE to the default file (p4ignore in the root, or Engine/Extras/Perforce/p4ignore)", ParamType = typeof(bool), Flag = "-SetIgnore")]
	[ParamHelp("Path", "Write to a path other than the current directory")]
	[ParamHelp("p4port=<server:port>", "Optional hint/override of the server to use during lookup")]
	[ParamHelp("p4user=<username>", "Optional hint/override of the username to use during lookup")]
	public class P4WriteConfig : BuildCommand
	{
		public override ExitCode Execute()
		{
			Logger.LogInformation("Setting up Perforce environment.");

			// User can specify these to help auto detection
			string Port = ParseParamValue("p4port", "");
			string User = ParseParamValue("p4user", "");

			bool SetIgnore = ParseParam("setignore");
			bool ListOnly = ParseParam("listonly");

			string OutputPath = ParseParamValue("path", "");

			// apply any hints
			if (!string.IsNullOrEmpty(Port))
			{
				Environment.SetEnvironmentVariable(EnvVarNames.P4Port, Port);
			}

			if (!string.IsNullOrEmpty(User))
			{
				Environment.SetEnvironmentVariable(EnvVarNames.User, User);
			}

			// try to init P4
			try
			{
				CommandUtils.InitP4Environment();
				CommandUtils.InitDefaultP4Connection();
			}
			catch (Exception Ex)
			{
				Logger.LogError("Unable to find matching Perforce info. If the below does not help try P4WriteConfig -p4port=<server:port> and -p4user=<username> to supply more info");
				Logger.LogError(Ex, "{Message}", Ex.Message);
				return ExitCode.Error_Arguments;
			}

			// store all our settings
			StringBuilder P4Config = new StringBuilder();

			P4Config.AppendLine($"P4PORT={P4Env.ServerAndPort}");
			P4Config.AppendLine($"P4USER={P4Env.User}");
			P4Config.AppendLine($"P4CLIENT={P4Env.Client}");

			if (SetIgnore)
			{
				// If a p4 ignore file is in the root, prefer it
				string IgnorePath = Directory.EnumerateFiles(Unreal.RootDirectory.FullName, "*p4ignore*", SearchOption.TopDirectoryOnly).FirstOrDefault();
				if (string.IsNullOrEmpty(IgnorePath) )
				{
					IgnorePath = Path.Combine(Unreal.EngineDirectory.ToString(), "Extras", "Perforce", "p4ignore");
				}
				P4Config.AppendLine($"P4IGNORE={IgnorePath}");
			}

			string P4Settings = P4Config.ToString();

			if (!string.IsNullOrEmpty(OutputPath))
			{
				if (!DirectoryExists(OutputPath) && !FileExists(OutputPath))
				{
					throw new AutomationException("Path {0} does not exist.", OutputPath);
				}
			}
			else
			{
				OutputPath = Environment.CurrentDirectory;
			}

			if (!File.Exists(OutputPath))
			{
				OutputPath = Path.Combine(OutputPath, "p4config.txt");
			}

			Console.WriteLine("***\nWriting\n{0}to - {1}\n***", P4Settings, OutputPath);

			if (!ListOnly)
			{
				File.WriteAllText(OutputPath, P4Settings);

				string OutputFile = Path.GetFileName(OutputPath);

				Logger.LogInformation("Wrote P4 settings to {OutputPath}", OutputPath);

				P4.P4(string.Format("set P4CONFIG={0}", OutputFile));
				Logger.LogInformation("set P4CONFIG={OutputFile}", OutputFile);
			}
			else
			{
				Logger.LogInformation("Skipped write");
			}

			return ExitCode.Success;
		}
	}
}
