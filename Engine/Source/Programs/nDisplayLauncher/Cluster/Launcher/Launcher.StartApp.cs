// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;

using nDisplayLauncher.Cluster.Config;
using nDisplayLauncher.Cluster.Config.Entity;
using nDisplayLauncher.Helpers;
using nDisplayLauncher.Log;


namespace nDisplayLauncher.Cluster
{
	public partial class Launcher
	{
		private void ProcessCommandStartApp(Configuration Config)
		{
			if (!File.Exists(SelectedConfig))
			{
				AppLogger.Log("No config file found: " + SelectedConfig);
				return;
			}

			// Send start command to the listeners
			foreach (EntityClusterNode Node in Config.ClusterNodes.Values)
			{
				string cmd = GenerateStartCommand(Node, Config);
				SendDaemonCommand(Node.Addr, DefaultListenerPort, cmd);
			}
		}

		private string GenerateStartCommand(EntityClusterNode Node, Configuration Config)
		{
			string commandCmd = string.Empty;
			string Application = SelectedApplication;
			string ExtraAppParams = string.Empty;

			// Start command at first
			commandCmd = CommandStartApp;

			// Get file(s)
			List<string> Params = SplitAppLineParameters(SelectedApplication);
			foreach (string Param in Params)
			{
				commandCmd = string.Format("{0} {1}", commandCmd, Param);
			}

			// Custom common arguments
			if (!string.IsNullOrWhiteSpace(CustomCommonParams))
			{
				commandCmd = string.Format("{0} {1}", commandCmd, CustomCommonParams.Trim());
			}
			// Mandatory arguments
			commandCmd = string.Format("{0} {1}", commandCmd, ArgMandatory);
			// Config file
			commandCmd = string.Format("{0} {1}=\"{2}\"", commandCmd, ArgConfig, SelectedConfig);
			// Render API and mode
			commandCmd = string.Format("{0} {1} {2}", commandCmd, SelectedRenderApiParam.Value, SelectedRenderModeParam.Value);

			// No texture streaming
			if (IsNotextureStreaming)
			{
				commandCmd = string.Format("{0} {1}", commandCmd, ArgNoTextureStreaming);
			}
			// Use all available cores
			if (IsUseAllCores)
			{
				commandCmd = string.Format("{0} {1}", commandCmd, ArgUseAllAvailableCores);
			}

			if (!Config.Windows.ContainsKey(Node.Window))
			{
				throw new Exception("Node {0} has no windows property specified");
			}

			// Get window settings for the node
			EntityWindow Window = Config.Windows[Node.Window];

			// Fullscreen/windowed
			commandCmd = string.Format("{0} {1}", commandCmd, Window.IsFullscreen ? ArgFullscreen : ArgWindowed );

			// Window location and size
			if (Window.ResX > 0 && Window.ResY > 0)
			{
				commandCmd = string.Format("{0} {1} {2} {3} {4}",
					commandCmd,
					string.Format("WinX={0}", Window.WinX),
					string.Format("WinY={0}", Window.WinY),
					string.Format("ResX={0}", Window.ResX),
					string.Format("ResY={0}", Window.ResY));
			}

			// Node ID
			commandCmd = string.Format("{0} {1}={2}", commandCmd, ArgNode, Node.Id);

			// Log file
			commandCmd = string.Format("{0} Log={1}.log", commandCmd, Node.Id);

			// Logging verbosity
			if (IsCustomLogsUsed)
			{
				string LogCmds = string.Format(
					"LogDisplayClusterPlugin {0}, " +
					"LogDisplayClusterEngine {1}, " +
					"LogDisplayClusterConfig {2}, " +
					"LogDisplayClusterCluster {3}, " + 
					"LogDisplayClusterGame {4}, " +
					"LogDisplayClusterGameMode {5}, " +
					"LogDisplayClusterInput {6}, " +
					"LogDisplayClusterInputVRPN {7}, " +
					"LogDisplayClusterNetwork {8}, " +
					"LogDisplayClusterNetworkMsg {9}, " +
					"LogDisplayClusterRender {10}, " +
					"LogDisplayClusterBlueprint {11}"
					, SelectedVerbocityPlugin
					, SelectedVerbocityEngine
					, SelectedVerbocityConfig
					, SelectedVerbocityCluster
					, SelectedVerbocityGame
					, SelectedVerbocityGameMode
					, SelectedVerbocityInput
					, SelectedVerbocityVrpn
					, SelectedVerbocityNetwork
					, SelectedVerbocityNetworkMsg
					, SelectedVerbocityRender
					, SelectedVerbocityBlueprint);

				commandCmd = string.Format("{0} -LogCmds=\"{1}\"", commandCmd, LogCmds);
			}

			// Custom ExecCmds
			string CleanCustomCommonExecCmds = CustomCommonExecCmds.Trim();
			if (!string.IsNullOrEmpty(CleanCustomCommonExecCmds))
			{
				commandCmd = string.Format("{0} -ExecCmds=\"{1}\"", commandCmd, CleanCustomCommonExecCmds);
			}

			return commandCmd;
		}

		private List<string> SplitAppLineParameters(string Line)
		{
			const string ExecExtension = ".exe";
			const string DashGameParam = "-game";

			List<string> Params = new List<string>();

			// Detect either we have -game line or single .exe/.bat/.cmd
			bool IsDashGame = StringHelper.Contains(Line, DashGameParam, StringComparison.CurrentCultureIgnoreCase);

			// In -game mode we have two files. One is the Editor executable, another is a project.
			if (IsDashGame)
			{
				// Extract Editor and project files
				int Idx = Line.IndexOf(ExecExtension, StringComparison.CurrentCultureIgnoreCase);
				if (Idx > 0)
				{
					// 1. Add executable to output
					string EditorFile = string.Format("\"{0}\"", Line.Substring(0, Idx + ExecExtension.Length).Trim());
					Params.Add(EditorFile);

					// 2. Remove everything except of .uproject file and store it to output
					Line = Line.Remove(0, Idx + ExecExtension.Length);
					Line = Line.Remove(Line.IndexOf(DashGameParam));
					string ProjectFile = string.Format("\"{0}\"", Line.Trim());
					Params.Add(ProjectFile);

					// 3. Finally, add -game
					Params.Add(DashGameParam);
				}
			}
			// Otherwise we have one executable so nothing special to do
			else
			{
				string ExecFile = string.Format("\"{0}\"", Line.Trim());
				Params.Add(ExecFile);
			}

			return Params;
		}
	}
}
