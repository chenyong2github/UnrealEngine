// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Diagnostics;
using System.Xml;
using System.Text.RegularExpressions;
using System.Linq;
using System.Reflection;
using Microsoft.Win32;
using System.Text;
using Tools.DotNETCommon;
using System.Net;
using System.Net.NetworkInformation;
using System.Runtime.InteropServices;

namespace UnrealBuildTool
{
	class XGE : ActionExecutor
	{
		/// <summary>
		/// Whether to use the no_watchdog_thread option to prevent VS2015 toolchain stalls.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		bool bXGENoWatchdogThread = false;

		/// <summary>
		/// Whether to display the XGE build monitor.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		bool bShowXGEMonitor = false;

		/// <summary>
		/// When enabled, XGE will stop compiling targets after a compile error occurs.  Recommended, as it saves computing resources for others.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration")]
		bool bStopXGECompilationAfterErrors = false;

		/// <summary>
		/// When set to false, XGE will not be When enabled, XGE will stop compiling targets after a compile error occurs.  Recommended, as it saves computing resources for others.
		/// </summary>
		[XmlConfigFile(Category = "XGE")]
		static bool bAllowOverVpn = true;

		/// <summary>
		/// List of subnets containing IP addresses assigned by VPN
		/// </summary>
		[XmlConfigFile(Category = "XGE")]
		static string[] VpnSubnets = null;

		private const string ProgressMarkupPrefix = "@action";

		public XGE()
		{
			XmlConfig.ApplyTo(this);
		}

		public override string Name
		{
			get { return "XGE"; }
		}

		public static bool TryGetXgConsoleExecutable(out string OutXgConsoleExe)
		{
			// Try to get the path from the registry
			if(BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				string XgConsoleExe;
				if(TryGetXgConsoleExecutableFromRegistry(RegistryView.Registry32, out XgConsoleExe))
				{
					OutXgConsoleExe = XgConsoleExe;
					return true;
				}
				if(TryGetXgConsoleExecutableFromRegistry(RegistryView.Registry64, out XgConsoleExe))
				{
					OutXgConsoleExe = XgConsoleExe;
					return true;
				}
			}

			// Get the name of the XgConsole executable.
			string XgConsole = "xgConsole";
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				XgConsole = "xgConsole.exe";
			}
			else if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Linux)
			{
				XgConsole = "ib_console";
			}

			// Search the path for it
			string PathVariable = Environment.GetEnvironmentVariable("PATH");
			foreach (string SearchPath in PathVariable.Split(Path.PathSeparator))
			{
				try
				{
					string PotentialPath = Path.Combine(SearchPath, XgConsole);
					if(File.Exists(PotentialPath))
					{
						OutXgConsoleExe = PotentialPath;
						return true;
					}
				}
				catch(ArgumentException)
				{
					// PATH variable may contain illegal characters; just ignore them.
				}
			}

			OutXgConsoleExe = null;
			return false;
		}

		private static bool TryGetXgConsoleExecutableFromRegistry(RegistryView View, out string OutXgConsoleExe)
		{
			try
			{
				using(RegistryKey BaseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, View))
				{
					using (RegistryKey Key = BaseKey.OpenSubKey("SOFTWARE\\Xoreax\\IncrediBuild\\Builder", false))
					{
						if(Key != null)
						{
							string Folder = Key.GetValue("Folder", null) as string;
							if(!String.IsNullOrEmpty(Folder))
							{
								string FileName = Path.Combine(Folder, "xgConsole.exe");
								if(File.Exists(FileName))
								{
									OutXgConsoleExe = FileName;
									return true;
								}
							}
						}
					}
				}
			}
			catch(Exception Ex)
			{
				Log.WriteException(Ex, null);
			}

			OutXgConsoleExe = null;
			return false;
		}

		static bool TryReadRegistryValue(RegistryHive Hive, RegistryView View, string KeyName, string ValueName, out string OutCoordinator)
		{
			using (RegistryKey BaseKey = RegistryKey.OpenBaseKey(Hive, View))
			{
				using (RegistryKey SubKey = BaseKey.OpenSubKey(KeyName))
				{
					if (SubKey != null)
					{
						string Coordinator = SubKey.GetValue(ValueName) as string;
						if (!String.IsNullOrEmpty(Coordinator))
						{
							OutCoordinator = Coordinator;
							return true;
						}
					}
				}
			}

			OutCoordinator = null;
			return false;
		}

		static bool TryGetCoordinatorHost(out string OutCoordinator)
		{
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				const string KeyName = @"SOFTWARE\Xoreax\IncrediBuild\BuildService";
				const string ValueName = "CoordHost";

				return TryReadRegistryValue(RegistryHive.CurrentUser, RegistryView.Registry64, KeyName, ValueName, out OutCoordinator) ||
					TryReadRegistryValue(RegistryHive.CurrentUser, RegistryView.Registry32, KeyName, ValueName, out OutCoordinator) ||
					TryReadRegistryValue(RegistryHive.LocalMachine, RegistryView.Registry64, KeyName, ValueName, out OutCoordinator) ||
					TryReadRegistryValue(RegistryHive.LocalMachine, RegistryView.Registry32, KeyName, ValueName, out OutCoordinator);
			}
			else
			{
				OutCoordinator = null;
				return false;
			}
		}

		[DllImport("iphlpapi")]
		static extern int GetBestInterface(uint dwDestAddr, ref int pdwBestIfIndex);

		static NetworkInterface GetInterfaceForHost(string Host)
		{
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				IPHostEntry HostEntry = Dns.GetHostEntry(Host);
				foreach (IPAddress HostAddress in HostEntry.AddressList)
				{
					int InterfaceIdx = 0;
					if (GetBestInterface(BitConverter.ToUInt32(HostAddress.GetAddressBytes(), 0), ref InterfaceIdx) == 0)
					{
						foreach (NetworkInterface Interface in NetworkInterface.GetAllNetworkInterfaces())
						{
							IPv4InterfaceProperties Properties = Interface.GetIPProperties().GetIPv4Properties();
							if (Properties.Index == InterfaceIdx)
							{
								return Interface;
							}
						}
					}
				}
			}
			return null;
		}

		public static bool IsHostOnVpn(string HostName)
		{
			// If there aren't any defined subnets, just early out
			if (VpnSubnets == null || VpnSubnets.Length == 0)
			{
				return false;
			}

			// Parse all the subnets from the config file
			List<Subnet> ParsedVpnSubnets = new List<Subnet>();
			foreach (string VpnSubnet in VpnSubnets)
			{
				ParsedVpnSubnets.Add(Subnet.Parse(VpnSubnet));
			}

			// Check if any network adapters have an IP within one of these subnets
			try
			{
				NetworkInterface Interface = GetInterfaceForHost(HostName);
				if (Interface != null && Interface.OperationalStatus == OperationalStatus.Up)
				{
					IPInterfaceProperties Properties = Interface.GetIPProperties();
					foreach (UnicastIPAddressInformation UnicastAddressInfo in Properties.UnicastAddresses)
					{
						byte[] AddressBytes = UnicastAddressInfo.Address.GetAddressBytes();
						foreach (Subnet Subnet in ParsedVpnSubnets)
						{
							if (Subnet.Contains(AddressBytes))
							{
								Log.TraceInformationOnce("XGE coordinator {0} will be not be used over VPN (adapter '{1}' with IP {2} is in subnet {3}). Set <XGE><bAllowOverVpn>true</bAllowOverVpn></XGE> in BuildConfiguration.xml to override.", HostName, Interface.Description, UnicastAddressInfo.Address, Subnet);
								return true;
							}
						}
					}
				}
			}
			catch (Exception Ex)
			{
				Log.TraceWarning("Unable to check whether host {0} is connected to VPN:\n{1}", HostName, ExceptionUtils.FormatExceptionDetails(Ex));
			}
			return false;
		}

		public static bool IsAvailable()
		{
			string XgConsoleExe;
			if (!TryGetXgConsoleExecutable(out XgConsoleExe))
			{
				return false;
			}

			// on windows check the service is actually running
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				try
				{
					// will throw if the service doesn't exist, which it should if IB is present but just incase...
					System.ServiceProcess.ServiceController SC = new System.ServiceProcess.ServiceController("Incredibuild Agent");
					if (SC.Status != System.ServiceProcess.ServiceControllerStatus.Running)
					{
						return false;
					}
				}
				catch(Exception Ex)
				{
					Log.TraceLog("Unable to query for status of Incredibuild service: {0}", ExceptionUtils.FormatExceptionDetails(Ex));
					return false;
				}
			}

			// Check if we're connected over VPN
			if (!bAllowOverVpn && VpnSubnets != null && VpnSubnets.Length > 0)
			{
				string CoordinatorHost;
				if (TryGetCoordinatorHost(out CoordinatorHost) && IsHostOnVpn(CoordinatorHost))
				{
					return false;
				}
			}

			return true;
		}

		// precompile the Regex needed to parse the XGE output (the ones we want are of the form "File (Duration at +time)"
		//private static Regex XGEDurationRegex = new Regex(@"(?<Filename>.*) *\((?<Duration>[0-9:\.]+) at [0-9\+:\.]+\)", RegexOptions.ExplicitCapture);

		public static void ExportActions(List<Action> ActionsToExecute)
		{
			for(int FileNum = 0;;FileNum++)
			{
				string OutFile = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Intermediate", "Build", String.Format("UBTExport.{0}.xge.xml", FileNum.ToString("D3")));
				if(!File.Exists(OutFile))
				{
					ExportActions(ActionsToExecute, OutFile);
					break;
				}
			}
		}

		public static void ExportActions(List<Action> ActionsToExecute, string OutFile)
		{
			WriteTaskFile(ActionsToExecute, OutFile, ProgressWriter.bWriteMarkup, bXGEExport: true);
			Log.TraceInformation("XGEEXPORT: Exported '{0}'", OutFile);
		}

		public override bool ExecuteActions(List<Action> ActionsToExecute, bool bLogDetailedActionStats)
		{
			bool XGEResult = true;

			// Batch up XGE execution by actions with the same output event handler.
			List<Action> ActionBatch = new List<Action>();
			ActionBatch.Add(ActionsToExecute[0]);
			for (int ActionIndex = 1; ActionIndex < ActionsToExecute.Count && XGEResult; ++ActionIndex)
			{
				Action CurrentAction = ActionsToExecute[ActionIndex];
				ActionBatch.Add(CurrentAction);
			}
			if (ActionBatch.Count > 0 && XGEResult)
			{
				XGEResult = ExecuteActionBatch(ActionBatch);
				ActionBatch.Clear();
			}

			return XGEResult;
		}

		bool ExecuteActionBatch(List<Action> Actions)
		{
			bool XGEResult = true;
			if (Actions.Count > 0)
			{
				// Write the actions to execute to a XGE task file.
				string XGETaskFilePath = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Intermediate", "Build", "XGETasks.xml").FullName;
				WriteTaskFile(Actions, XGETaskFilePath, ProgressWriter.bWriteMarkup, false);

				XGEResult = ExecuteTaskFileWithProgressMarkup(XGETaskFilePath, Actions.Count);
			}
			return XGEResult;
		}

		/// <summary>
		/// Writes a XGE task file containing the specified actions to the specified file path.
		/// </summary>
		static void WriteTaskFile(List<Action> InActions, string TaskFilePath, bool bProgressMarkup, bool bXGEExport)
		{
			Dictionary<string, string> ExportEnv = new Dictionary<string, string>();

			List<Action> Actions = InActions;
			if (bXGEExport)
			{
				IDictionary CurrentEnvironment = Environment.GetEnvironmentVariables();
				foreach (System.Collections.DictionaryEntry Pair in CurrentEnvironment)
				{
					if (!UnrealBuildTool.InitialEnvironment.Contains(Pair.Key) || (string)(UnrealBuildTool.InitialEnvironment[Pair.Key]) != (string)(Pair.Value))
					{
						ExportEnv.Add((string)(Pair.Key), (string)(Pair.Value));
					}
				}
			}

			XmlDocument XGETaskDocument = new XmlDocument();

			// <BuildSet FormatVersion="1">...</BuildSet>
			XmlElement BuildSetElement = XGETaskDocument.CreateElement("BuildSet");
			XGETaskDocument.AppendChild(BuildSetElement);
			BuildSetElement.SetAttribute("FormatVersion", "1");

			// <Environments>...</Environments>
			XmlElement EnvironmentsElement = XGETaskDocument.CreateElement("Environments");
			BuildSetElement.AppendChild(EnvironmentsElement);

			// <Environment Name="Default">...</CompileEnvironment>
			XmlElement EnvironmentElement = XGETaskDocument.CreateElement("Environment");
			EnvironmentsElement.AppendChild(EnvironmentElement);
			EnvironmentElement.SetAttribute("Name", "Default");

			// <Tools>...</Tools>
			XmlElement ToolsElement = XGETaskDocument.CreateElement("Tools");
			EnvironmentElement.AppendChild(ToolsElement);

			if (ExportEnv.Count > 0)
			{
				// <Variables>...</Variables>
				XmlElement VariablesElement = XGETaskDocument.CreateElement("Variables");
				EnvironmentElement.AppendChild(VariablesElement);

				foreach (KeyValuePair<string, string> Pair in ExportEnv)
				{
					// <Variable>...</Variable>
					XmlElement VariableElement = XGETaskDocument.CreateElement("Variable");
					VariablesElement.AppendChild(VariableElement);
					VariableElement.SetAttribute("Name", Pair.Key);
					VariableElement.SetAttribute("Value", Pair.Value);
				}
			}

			for (int ActionIndex = 0; ActionIndex < Actions.Count; ActionIndex++)
			{
				Action Action = Actions[ActionIndex];

				// <Tool ... />
				XmlElement ToolElement = XGETaskDocument.CreateElement("Tool");
				ToolsElement.AppendChild(ToolElement);
				ToolElement.SetAttribute("Name", string.Format("Tool{0}", ActionIndex));
				ToolElement.SetAttribute("AllowRemote", Action.bCanExecuteRemotely.ToString());

				// The XGE documentation says that 'AllowIntercept' must be set to 'true' for all tools where 'AllowRemote' is enabled
				ToolElement.SetAttribute("AllowIntercept", Action.bCanExecuteRemotely.ToString());

				string OutputPrefix = "";
				if (bProgressMarkup)
				{
					OutputPrefix += ProgressMarkupPrefix;
				}
				if (Action.bShouldOutputStatusDescription)
				{
					OutputPrefix += Action.StatusDescription;
				}
				if (OutputPrefix.Length > 0)
				{
					ToolElement.SetAttribute("OutputPrefix", OutputPrefix);
				}
				if(Action.GroupNames.Count > 0)
				{
					ToolElement.SetAttribute("GroupPrefix", String.Format("** For {0} **", String.Join(" + ", Action.GroupNames)));
				}

				ToolElement.SetAttribute("Params", Action.CommandArguments);
				ToolElement.SetAttribute("Path", Action.CommandPath.FullName);
				ToolElement.SetAttribute("SkipIfProjectFailed", "true");
				if (Action.bIsGCCCompiler)
				{
					ToolElement.SetAttribute("AutoReserveMemory", "*.gch");
				}
				else
				{
					ToolElement.SetAttribute("AutoReserveMemory", "*.pch");
				}
				ToolElement.SetAttribute(
					"OutputFileMasks",
					string.Join(
						",",
						Action.ProducedItems.ConvertAll<string>(
							delegate(FileItem ProducedItem) { return ProducedItem.Location.GetFileName(); }
							).ToArray()
						)
					);

				if(Action.ActionType == ActionType.Link)
				{
					ToolElement.SetAttribute("AutoRecover", "Unexpected PDB error; OK (0)");
				}
			}

			// <Project Name="Default" Env="Default">...</Project>
			XmlElement ProjectElement = XGETaskDocument.CreateElement("Project");
			BuildSetElement.AppendChild(ProjectElement);
			ProjectElement.SetAttribute("Name", "Default");
			ProjectElement.SetAttribute("Env", "Default");

			for (int ActionIndex = 0; ActionIndex < Actions.Count; ActionIndex++)
			{
				Action Action = Actions[ActionIndex];

				// <Task ... />
				XmlElement TaskElement = XGETaskDocument.CreateElement("Task");
				ProjectElement.AppendChild(TaskElement);
				TaskElement.SetAttribute("SourceFile", "");
				if (!Action.bShouldOutputStatusDescription)
				{
					// If we were configured to not output a status description, then we'll instead
					// set 'caption' text for this task, so that the XGE coordinator has something
					// to display within the progress bars.  For tasks that are outputting a
					// description, XGE automatically displays that text in the progress bar, so we
					// only need to do this for tasks that output their own progress.
					TaskElement.SetAttribute("Caption", Action.StatusDescription);
				}
				TaskElement.SetAttribute("Name", string.Format("Action{0}", ActionIndex));
				TaskElement.SetAttribute("Tool", string.Format("Tool{0}", ActionIndex));
				TaskElement.SetAttribute("WorkingDir", Action.WorkingDirectory.FullName);
				TaskElement.SetAttribute("SkipIfProjectFailed", "true");
				TaskElement.SetAttribute("AllowRestartOnLocal", "true");

				// Create a semi-colon separated list of the other tasks this task depends on the results of.
				List<string> DependencyNames = new List<string>();
				foreach(Action PrerequisiteAction in Action.PrerequisiteActions)
				{
					if (Actions.Contains(PrerequisiteAction))
					{
						DependencyNames.Add(string.Format("Action{0}", Actions.IndexOf(PrerequisiteAction)));
					}
				}

				if (DependencyNames.Count > 0)
				{
					TaskElement.SetAttribute("DependsOn", string.Join(";", DependencyNames.ToArray()));
				}
			}

			// Write the XGE task XML to a temporary file.
			using (FileStream OutputFileStream = new FileStream(TaskFilePath, FileMode.Create, FileAccess.Write))
			{
				XGETaskDocument.Save(OutputFileStream);
			}
		}

		/// <summary>
		/// The possible result of executing tasks with XGE.
		/// </summary>
		enum ExecutionResult
		{
			Unavailable,
			TasksFailed,
			TasksSucceeded,
		}

		/// <summary>
		/// Executes the tasks in the specified file.
		/// </summary>
		/// <param name="TaskFilePath">- The path to the file containing the tasks to execute in XGE XML format.</param>
		/// <param name="OutputEventHandler"></param>
		/// <param name="ActionCount"></param>
		/// <returns>Indicates whether the tasks were successfully executed.</returns>
		bool ExecuteTaskFile(string TaskFilePath, DataReceivedEventHandler OutputEventHandler, int ActionCount)
		{
			// A bug in the UCRT can cause XGE to hang on VS2015 builds. Figure out if this hang is likely to effect this build and workaround it if able.
			// @todo: There is a KB coming that will fix this. Once that KB is available, test if it is present. Stalls will not be a problem if it is.
			//
			// Stalls are possible. However there is a workaround in XGE build 1659 and newer that can avoid the issue.
			string XGEVersion = (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64) ? (string)Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Xoreax\IncrediBuild\Builder", "Version", null) : null;
			if (XGEVersion != null)
			{
				int XGEBuildNumber;
				if (Int32.TryParse(XGEVersion, out XGEBuildNumber))
				{
					// Per Xoreax support, subtract 1001000 from the registry value to get the build number of the installed XGE.
					if (XGEBuildNumber - 1001000 >= 1659)
					{
						bXGENoWatchdogThread = true;
					}
					// @todo: Stalls are possible and we don't have a workaround. What should we do? Most people still won't encounter stalls, we don't really
					// want to disable XGE on them if it would have worked.
				}
			}

			string XgConsolePath;
			if(!TryGetXgConsoleExecutable(out XgConsolePath))
			{
				throw new BuildException("Unable to find xgConsole executable.");
			}

			bool bSilentCompileOutput = false;
			string SilentOption = bSilentCompileOutput ? "/Silent" : "";

			ProcessStartInfo XGEStartInfo = new ProcessStartInfo(
				XgConsolePath,
				string.Format("\"{0}\" /Rebuild /NoWait {1} /NoLogo {2} /ShowAgent /ShowTime {3}",
					TaskFilePath,
					bStopXGECompilationAfterErrors ? "/StopOnErrors" : "",
					SilentOption,
					bXGENoWatchdogThread ? "/no_watchdog_thread" : "")
				);
			XGEStartInfo.UseShellExecute = false;

			// Use the IDE-integrated Incredibuild monitor to display progress.
			XGEStartInfo.Arguments += " /UseIdeMonitor";

			// Optionally display the external XGE monitor.
			if (bShowXGEMonitor)
			{
				XGEStartInfo.Arguments += " /OpenMonitor";
			}

			try
			{
				// Start the process, redirecting stdout/stderr if requested.
				Process XGEProcess = new Process();
				XGEProcess.StartInfo = XGEStartInfo;
				bool bShouldRedirectOuput = OutputEventHandler != null;
				if (bShouldRedirectOuput)
				{
					XGEStartInfo.RedirectStandardError = true;
					XGEStartInfo.RedirectStandardOutput = true;
					XGEProcess.EnableRaisingEvents = true;
					XGEProcess.OutputDataReceived += OutputEventHandler;
					XGEProcess.ErrorDataReceived += OutputEventHandler;
				}
				XGEProcess.Start();
				if (bShouldRedirectOuput)
				{
					XGEProcess.BeginOutputReadLine();
					XGEProcess.BeginErrorReadLine();
				}

				Log.TraceInformation("Distributing {0} action{1} to XGE",
					ActionCount,
					ActionCount == 1 ? "" : "s");

				// Wait until the process is finished and return whether it all the tasks successfully executed.
				XGEProcess.WaitForExit();
				return XGEProcess.ExitCode == 0;
			}
			catch (Exception Ex)
			{
				Log.WriteException(Ex, null);
				return false;
			}
		}

		/// <summary>
		/// Executes the tasks in the specified file, parsing progress markup as part of the output.
		/// </summary>
		bool ExecuteTaskFileWithProgressMarkup(string TaskFilePath, int NumActions)
		{
			using (ProgressWriter Writer = new ProgressWriter("Compiling C++ source files...", false))
			{
				int NumCompletedActions = 0;

				// Create a wrapper delegate that will parse the output actions
				DataReceivedEventHandler EventHandlerWrapper = (Sender, Args) =>
				{
					if(Args.Data != null)
					{
						string Text = Args.Data;
						if (Text.StartsWith(ProgressMarkupPrefix))
						{
							Writer.Write(++NumCompletedActions, NumActions);

							// Strip out anything that is just an XGE timer. Some programs don't output anything except the progress text.
							Text = Args.Data.Substring(ProgressMarkupPrefix.Length);
							if(Text.StartsWith(" (") && Text.EndsWith(")"))
							{
								return;
							}
						}
						Log.TraceInformation(Text);
					}
				};

				// Run through the standard XGE executor
				return ExecuteTaskFile(TaskFilePath, EventHandlerWrapper, NumActions);
			}
		}
	}
}
