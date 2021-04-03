// Copyright Epic Games, Inc. All Rights Reserved.


using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using nDisplayLauncher.Cluster.Config;
using nDisplayLauncher.Cluster.Events;
using nDisplayLauncher.Log;
using nDisplayLauncher.Settings;

using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;


namespace nDisplayLauncher.Cluster
{
	public partial class Launcher : INotifyPropertyChanged
	{
		// Current configuration version (config file format)
		// NOTE: Don't forget to update this value with newer version.
		public const ConfigurationVersion CurrentVersion = ConfigurationVersion.Ver23;

		// net
		public static int DefaultListenerPort = 41000;
		public const string ArgListenerPort   = "listener_port=";
		private const string ListenerAppName  = "nDisplayListener.exe";

		// cluster commands
		private const string CommandStartApp  = "start";
		private const string CommandKillApp   = "kill";
		private const string CommandStatus    = "status";
		private const string CommandRestart   = "restart";

		// run application params\keys
		private const string ArgMandatory = "-dc_cluster -nosplash -fixedseed";

		private const string ArgConfig = "-dc_cfg";
		private const string ArgNode   = "-dc_node";
		private const string ArgGpu    = "-dc_gpu";

		// switches
		private const string ArgFullscreen           = "-fullscreen";
		private const string ArgWindowed             = "-windowed";
		private const string ArgNoTextureStreaming   = "-notexturestreaming";
		private const string ArgUseAllAvailableCores = "-useallavailablecores";
		private const string ArgForceRes             = "-forceres";

		public enum ClusterCommandType
		{
			RunApp,
			KillApp,
			RestartComputers,
			SendEvent
		}

		#region Launcher_Params
		private Dictionary<string, string> _RenderApiParams = new Dictionary<string, string>
		{
			{"DirectX 11", "-dx11" },
			{"DirectX 12", "-dx12" }
		};
		public Dictionary<string, string> RenderApiParams
		{
			get { return _RenderApiParams; }
			set { Set(ref _RenderApiParams, value, "RenderApiParams"); }
		}

		// Selected RHI parameter
		private KeyValuePair<string, string> _SelectedRenderApiParam;
		public KeyValuePair<string, string> SelectedRenderApiParam
		{
			get { return _SelectedRenderApiParam; }
			set
			{
				Set(ref _SelectedRenderApiParam, value, "SelectedRenderApiParam");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsRenderApiName, value.Key);
			}
		}

		private Dictionary<string, string> _RenderModeParams = new Dictionary<string, string>
		{
			{"Mono",             "-dc_dev_mono" },
			{"Frame sequential", "-quad_buffer_stereo" },
			{"Side-by-side",     "-dc_dev_side_by_side" },
			{"Top-bottom",       "-dc_dev_top_bottom" }
		};
		public Dictionary<string, string> RenderModeParams
		{
			get { return _RenderModeParams; }
			set { Set(ref _RenderModeParams, value, "RenderModeParams"); }
		}

		private KeyValuePair<string, string> _SelectedRenderModeParam;
		public KeyValuePair<string, string> SelectedRenderModeParam
		{
			get { return _SelectedRenderModeParam; }
			set
			{
				Set(ref _SelectedRenderModeParam, value, "SelectedRenderModeParam");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsRenderModeName, value.Key);
			}
		}

		private bool _IsUseAllCores = false;
		public bool IsUseAllCores
		{
			get { return _IsUseAllCores; }
			set
			{
				Set(ref _IsUseAllCores, value, "IsUseAllCores");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsIsAllCoresName, value);
			}
		}

		private bool _IsNotextureStreaming = false;
		public bool IsNotextureStreaming
		{
			get { return _IsNotextureStreaming; }
			set
			{
				Set(ref _IsNotextureStreaming, value, "IsNotextureStreaming");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsIsNoTexStreamingName, value);
			}
		}

		// Applications list
		private List<string> _Applications = new List<string>();
		public List<string> Applications
		{
			get { return _Applications; }
			set { Set(ref _Applications, value, "Applications"); }
		}

		// Configs list
		private List<string> _Configs = new List<string>();
		public List<string> Configs
		{
			get { return _Configs; }
			set { Set(ref _Configs, value, "Configs"); }
		}

		// Additional command line params
		private string _CustomCommonParams = string.Empty;
		public string CustomCommonParams
		{
			get { return _CustomCommonParams; }
			set
			{
				Set(ref _CustomCommonParams, value, "CustomCommonParams");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsAdditionalParamsName, value);
			}
		}

		// ExecCmds arguments
		private string _CustomCommonExecCmds = string.Empty;
		public string CustomCommonExecCmds
		{
			get { return _CustomCommonExecCmds; }
			set
			{
				Set(ref _CustomCommonExecCmds, value, "CustomCommonExecCmds");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsAdditionalExecCmds, value);
			}
		}

		

		// Command line string with app path
		private string _CommandLine = string.Empty;
		public string CommandLine
		{
			get { return _CommandLine; }
			set { Set(ref _CommandLine, value, "CommandLine"); }
		}

		// Selected Application
		private string _SelectedApplication = string.Empty;
		public string SelectedApplication
		{
			get { return _SelectedApplication; }
			set
			{
				Set(ref _SelectedApplication, value, "SelectedApplication");
			}
		}

		// Selected Config file
		private string _SelectedConfig = string.Empty;
		public string SelectedConfig
		{
			get { return _SelectedConfig; }
			set
			{
				Set(ref _SelectedConfig, value, "SelectedConfig");

			}
		}
		#endregion

		#region Log_Params
		private bool _IsCustomLogsUsed = false;
		public bool IsCustomLogsUsed
		{
			get { return _IsCustomLogsUsed; }
			set
			{
				Set(ref _IsCustomLogsUsed, value, "IsCustomLogsUsed");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsUseCustomLogs, value);
			}
		}

		public static UELogVerbosity UELogVerbosity_FromString(string From, UELogVerbosity Default)
		{			
			if (string.IsNullOrEmpty(From))
			{
				return Default;
			}

			From = From.Trim();

			if (string.Compare(From, "All", true) == 0)
			{
				return UELogVerbosity.All;
			}
			else if (string.Compare(From, "Verbose", true) == 0)
			{
				return UELogVerbosity.Verbose;
			}
			else if (string.Compare(From, "Log", true) == 0)
			{
				return UELogVerbosity.Log;
			}
			else if (string.Compare(From, "Display", true) == 0)
			{
				return UELogVerbosity.Display;
			}
			else if (string.Compare(From, "Warning", true) == 0)
			{
				return UELogVerbosity.Warning;
			}
			else if (string.Compare(From, "Error", true) == 0)
			{
				return UELogVerbosity.Error;
			}
			else if (string.Compare(From, "Fatal", true) == 0)
			{
				return UELogVerbosity.Fatal;
			}

			return Default;
		}

		public IEnumerable<UELogVerbosity> UELogVerbosityEnumTypeValues
		{
			get
			{
				return Enum.GetValues(typeof(UELogVerbosity)).Cast<UELogVerbosity>();
			}
		}

		private UELogVerbosity _SelectedVerbocityPlugin = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityPlugin
		{
			get { return _SelectedVerbocityPlugin; }
			set
			{
				Set(ref _SelectedVerbocityPlugin, value, "SelectedVerbocityPlugin");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityPlugin, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityEngine = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityEngine
		{
			get { return _SelectedVerbocityEngine; }
			set
			{
				Set(ref _SelectedVerbocityEngine, value, "SelectedVerbocityEngine");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityEngine, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityBlueprint = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityBlueprint
		{
			get { return _SelectedVerbocityBlueprint; }
			set
			{
				Set(ref _SelectedVerbocityBlueprint, value, "SelectedVerbocityBlueprint");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityBlueprint, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityConfig = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityConfig
		{
			get { return _SelectedVerbocityConfig; }
			set
			{
				Set(ref _SelectedVerbocityConfig, value, "SelectedVerbocityConfig");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityConfig, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityCluster = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityCluster
		{
			get { return _SelectedVerbocityCluster; }
			set
			{
				Set(ref _SelectedVerbocityCluster, value, "SelectedVerbocityCluster");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityCluster, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityGame = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityGame
		{
			get { return _SelectedVerbocityGame; }
			set
			{
				Set(ref _SelectedVerbocityGame, value, "SelectedVerbocityGame");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityGame, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityGameMode = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityGameMode
		{
			get { return _SelectedVerbocityGameMode; }
			set
			{
				Set(ref _SelectedVerbocityGameMode, value, "SelectedVerbocityGameMode");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityGameMode, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityInput = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityInput
		{
			get { return _SelectedVerbocityInput; }
			set
			{
				Set(ref _SelectedVerbocityInput, value, "SelectedVerbocityInput");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityInput, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityVrpn = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityVrpn
		{
			get { return _SelectedVerbocityVrpn; }
			set
			{
				Set(ref _SelectedVerbocityVrpn, value, "SelectedVerbocityVrpn");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityInputVrpn, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityNetwork = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityNetwork
		{
			get { return _SelectedVerbocityNetwork; }
			set
			{
				Set(ref _SelectedVerbocityNetwork, value, "SelectedVerbocityNetwork");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityNetwork, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityNetworkMsg = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityNetworkMsg
		{
			get { return _SelectedVerbocityNetworkMsg; }
			set
			{
				Set(ref _SelectedVerbocityNetworkMsg, value, "SelectedVerbocityNetworkMsg");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityNetworkMsg, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityRender = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityRender
		{
			get { return _SelectedVerbocityRender; }
			set
			{
				Set(ref _SelectedVerbocityRender, value, "SelectedVerbocityRender");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityRender, value);
			}
		}

		private UELogVerbosity _SelectedVerbocityRenderSync = UELogVerbosity.Log;
		public UELogVerbosity SelectedVerbocityRenderSync
		{
			get { return _SelectedVerbocityRenderSync; }
			set
			{
				Set(ref _SelectedVerbocityRenderSync, value, "SelectedVerbocityRenderSync");
				RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityRenderSync, value);
			}
		}
		#endregion

		public Launcher()
		{
			InitializeOptions();
		}

		// Implementation of INotifyPropertyChanged method for TwoWay binding
		public event PropertyChangedEventHandler PropertyChanged;

		protected void OnNotifyPropertyChanged(string propertyName)
		{
			if (PropertyChanged != null)
				PropertyChanged(this, new PropertyChangedEventArgs(propertyName));
		}

		// Set property with OnNotifyPropertyChanged call
		protected void Set<T>(ref T field, T newValue, string propertyName)
		{
			field = newValue;
			OnNotifyPropertyChanged(propertyName);
		}

		private void SetSelectedApp()
		{
			SelectedApplication = string.Empty;
			string selected = RegistrySaver.FindSelectedRegValue(RegistrySaver.RegCategoryAppList);
			if (!string.IsNullOrEmpty(selected))
			{
				SelectedApplication = Applications.Find(x => x == selected);
			}
		}

		public void SetSelectedConfig()
		{
			SelectedConfig = string.Empty;
			string selected = RegistrySaver.FindSelectedRegValue(RegistrySaver.RegCategoryConfigList);
			if (!string.IsNullOrEmpty(selected))
			{
				SelectedConfig = Configs.Find(x => x == selected);
			}
		}

		private void InitializeOptions()
		{
			InitializeTabLaunch();
			InitializeTabLog();

			AppLogger.Log("User setttings have been loaded successfulyy");
		}

		private void InitializeTabLaunch()
		{
			try
			{
				SelectedRenderApiParam = RenderApiParams.First(x => x.Key == RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsRenderApiName));
			}
			catch (Exception)
			{
				SelectedRenderApiParam = RenderApiParams.SingleOrDefault(x => x.Key == "DirectX 11");
			}

			try
			{
				SelectedRenderModeParam = RenderModeParams.First(x => x.Key == RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsRenderModeName));
			}
			catch (Exception)
			{
				SelectedRenderApiParam = RenderModeParams.SingleOrDefault(x => x.Key == "Mono");
			}

			CustomCommonParams = RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsAdditionalParamsName);
			IsUseAllCores = RegistrySaver.ReadBoolValue(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsIsAllCoresName);
			IsNotextureStreaming = RegistrySaver.ReadBoolValue(RegistrySaver.RegCategoryParamsList, RegistrySaver.RegParamsIsNoTexStreamingName);

			Applications = RegistrySaver.ReadStringsFromRegistry(RegistrySaver.RegCategoryAppList);
			SetSelectedApp();

			Configs = RegistrySaver.ReadStringsFromRegistry(RegistrySaver.RegCategoryConfigList);
			SetSelectedConfig();
		}

		private void InitializeTabLog()
		{
			try
			{
				IsCustomLogsUsed            = RegistrySaver.ReadBoolValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsUseCustomLogs);
				SelectedVerbocityPlugin     = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityPlugin), UELogVerbosity.Log);
				SelectedVerbocityEngine     = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityEngine), UELogVerbosity.Log);
				SelectedVerbocityConfig     = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityConfig), UELogVerbosity.Log);
				SelectedVerbocityCluster    = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityCluster), UELogVerbosity.Log);
				SelectedVerbocityGame       = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityGame), UELogVerbosity.Log);
				SelectedVerbocityGameMode   = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityGameMode), UELogVerbosity.Log);
				SelectedVerbocityInput      = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityInput), UELogVerbosity.Log);
				SelectedVerbocityVrpn       = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityInputVrpn), UELogVerbosity.Log);
				SelectedVerbocityNetwork    = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityNetwork), UELogVerbosity.Log);
				SelectedVerbocityNetworkMsg = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityNetworkMsg), UELogVerbosity.Log);
				SelectedVerbocityBlueprint  = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityBlueprint), UELogVerbosity.Log);
				SelectedVerbocityRender     = UELogVerbosity_FromString(RegistrySaver.ReadStringValue(RegistrySaver.RegCategoryLogParams, RegistrySaver.RegLogParamsVerbosityRender), UELogVerbosity.Log);
			}
			catch (Exception ex)
			{
				AppLogger.Log(ex.Message);
			}
		}

		public void ProcessCommand(ClusterCommandType Cmd, object[] Args = null)
		{
			Configuration Config = Parser.Parse(SelectedConfig);
			if (Config == null)
			{
				AppLogger.Log("Couldn't parse the config file. Please make sure it's correct.");
				return;
			}

			if (Config.ClusterNodes.Count < 1)
			{
				AppLogger.Log("No cluster nodes found in the config file");
				return;
			}

			try
			{
				switch (Cmd)
				{
					case ClusterCommandType.RunApp:
						ProcessCommandStartApp(Config);
						break;

					case ClusterCommandType.KillApp:
						ProcessCommandKillApp(Config);
						break;

					case ClusterCommandType.RestartComputers:
						ProcessCommandRestartComputers(Config);
						break;

					case ClusterCommandType.SendEvent:
						ProcessCommandSendClusterEvent(Config, Args[0] as List<ClusterEvent>);
						break;

					default:
						break;
				}
			}
			catch (Exception ex)
			{
				AppLogger.Log(ex.Message);
			}
		}

		private int SendDaemonCommand(string nodeAddress, int port, string cmd, bool bQuiet = false)
		{
			int ResponseCode = 1;
			TcpClient nodeClient = new TcpClient();

			if (!bQuiet)
			{
				AppLogger.Log(string.Format("Sending command {0} to {1}...", cmd, nodeAddress));
			}

			try
			{
				// Connect to the listener
				nodeClient.Connect(nodeAddress, port);
				NetworkStream networkStream = nodeClient.GetStream();

				byte[] OutData = System.Text.Encoding.ASCII.GetBytes(cmd);
				networkStream.Write(OutData, 0, OutData.Length);

				byte[] InData = new byte[1024];
				int InBytesCount = networkStream.Read(InData, 0, InData.Length);

				// Receive response
				ResponseCode = BitConverter.ToInt32(InData, 0);
			}
			catch (Exception ex)
			{
				if (!bQuiet)
				{
					AppLogger.Log("An error occurred while sending a command to " + nodeAddress + ". EXCEPTION: " + ex.Message);
				}
			}
			finally
			{
				nodeClient.Close();
			}

			return ResponseCode;
		}

		private void SendClusterCommand(string nodeAddress, int port, string cmd, bool bQuiet = false)
		{
			TcpClient nodeClient = new TcpClient();

			if (!bQuiet)
			{
				AppLogger.Log(string.Format("Sending command {0} to {1}...", cmd, nodeAddress));
			}

			try
			{
				// Connect to the listener
				nodeClient.Connect(nodeAddress, port);
				NetworkStream networkStream = nodeClient.GetStream();

				byte[] OutData = ASCIIEncoding.ASCII.GetBytes(cmd);
				byte[] OutSize = BitConverter.GetBytes((Int32)OutData.Length);

				networkStream.Write(OutSize, 0, OutSize.Length);
				networkStream.Write(OutData, 0, OutData.Length);
				AppLogger.Log("Event sent");
			}
			catch (Exception ex)
			{
				if (!bQuiet)
				{
					AppLogger.Log("An error occurred while sending a command to " + nodeAddress + ". EXCEPTION: " + ex.Message);
				}
			}
			finally
			{
				nodeClient.Close();
			}
		}

		public void AddApplication(string appPath)
		{
			if (!Applications.Contains(appPath))
			{
				Applications.Add(appPath);
				RegistrySaver.AddRegistryValue(RegistrySaver.RegCategoryAppList, appPath);
				AppLogger.Log("Application [" + appPath + "] added to list");
			}
			else
			{
				AppLogger.Log("WARNING! Application [" + appPath + "] is already in the list");
			}

		}

		public void DeleteApplication()
		{
			Applications.Remove(SelectedApplication);
			RegistrySaver.RemoveRegistryValue(RegistrySaver.RegCategoryAppList, SelectedApplication);
			AppLogger.Log("Application [" + SelectedApplication + "] removed from the list");

			SelectedApplication = null;
		}

		public void ChangeConfigSelection(string configPath)
		{
			try
			{
				foreach (string config in Configs)
				{
					if (config != configPath)
					{
						RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryConfigList, config, false);
					}
					else
					{
						RegistrySaver.UpdateRegistry(RegistrySaver.RegCategoryConfigList, config, true);
					}
				}
			}
			catch (Exception exception)
			{
				AppLogger.Log("ERROR while changing config selection. EXCEPTION: " + exception.Message);
			}
		}

		public void AddConfig(string configPath)
		{
			try
			{
				Configs.Add(configPath);
				SelectedConfig = Configs.Find(x => x == configPath);
				RegistrySaver.AddRegistryValue(RegistrySaver.RegCategoryConfigList, configPath);
				ChangeConfigSelection(configPath);
				AppLogger.Log("Configuration file [" + configPath + "] added to list");
			}
			catch (Exception)
			{
				AppLogger.Log("ERROR! Can not add configuration file [" + configPath + "] to list");
			}
		}

		public void DeleteConfig()
		{
			Configs.Remove(SelectedConfig);
			RegistrySaver.RemoveRegistryValue(RegistrySaver.RegCategoryConfigList, SelectedConfig);
			AppLogger.Log("Configuration file [" + SelectedConfig + "] deleted");
			SelectedConfig = Configs.FirstOrDefault();
		}
	}
}
