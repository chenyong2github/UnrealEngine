// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using System.Diagnostics;
using Tools.DotNETCommon;

namespace HoloLens.Automation
{
	class HoloLensLauncherCreatedProcess : IProcessResult
	{
		Process ExternallyLaunchedProcess;
		System.Threading.Thread LogTailThread;
		bool AllowSpew;
		LogEventType SpewVerbosity;

		public HoloLensLauncherCreatedProcess(Process InExternallyLaunchedProcess, string LogFile, bool InAllowSpew, LogEventType InSpewVerbosity)
		{
			ExternallyLaunchedProcess = InExternallyLaunchedProcess;
			AllowSpew = InAllowSpew;
			SpewVerbosity = InSpewVerbosity;

			// Can't redirect stdout, so tail the log file
			if (AllowSpew)
			{
				LogTailThread = new System.Threading.Thread(() => TailLogFile(LogFile));
				LogTailThread.Start();
			}
			else
			{
				LogTailThread = null;
			}
		}

		~HoloLensLauncherCreatedProcess()
		{
			if (ExternallyLaunchedProcess != null)
			{
				ExternallyLaunchedProcess.Dispose();
			}
		}

		public int ExitCode
		{
			get
			{
				// Can't access the exit code of a process we didn't start
				return 0;
			}

			set
			{
				throw new NotImplementedException();
			}
		}

		public bool HasExited
		{
			get
			{
				// Avoid potential access of the exit code.
				return ExternallyLaunchedProcess != null && ExternallyLaunchedProcess.HasExited;
			}
		}

		public string Output
		{
			get
			{
				return string.Empty;
			}
		}

		public Process ProcessObject
		{
			get
			{
				return ExternallyLaunchedProcess;
			}
		}

		public void DisposeProcess()
		{
			ExternallyLaunchedProcess.Dispose();
			ExternallyLaunchedProcess = null;
		}

		public string GetProcessName()
		{
			return ExternallyLaunchedProcess.ProcessName;
		}

		public void OnProcessExited()
		{
			ProcessManager.RemoveProcess(this);
			if (LogTailThread != null)
			{
				LogTailThread.Join();
				LogTailThread = null;
			}
		}

		public void StdErr(object sender, DataReceivedEventArgs e)
		{
			throw new NotImplementedException();
		}

		public void StdOut(object sender, DataReceivedEventArgs e)
		{
			throw new NotImplementedException();
		}

		public void StopProcess(bool KillDescendants = true)
		{
			string ProcessNameForLogging = GetProcessName();
			try
			{
				Process ProcessToKill = ExternallyLaunchedProcess;
				ExternallyLaunchedProcess = null;
				if (!ProcessToKill.CloseMainWindow())
				{
					CommandUtils.LogWarning("{0} did not respond to close request.  Killing...", ProcessNameForLogging);
					ProcessToKill.Kill();
					ProcessToKill.WaitForExit(60000);
				}
				if (!ProcessToKill.HasExited)
				{
					CommandUtils.LogLog("Process {0} failed to exit.", ProcessNameForLogging);
				}
				else
				{
					CommandUtils.LogLog("Process {0} successfully exited.", ProcessNameForLogging);
					OnProcessExited();
				}
				ProcessToKill.Close();
			}
			catch (Exception Ex)
			{
				CommandUtils.LogWarning("Exception while trying to kill process {0}:", ProcessNameForLogging);
				CommandUtils.LogWarning(LogUtils.FormatException(Ex));
			}
		}

		public void WaitForExit()
		{
			ExternallyLaunchedProcess.WaitForExit();
			if (LogTailThread != null)
			{
				LogTailThread.Join();
				LogTailThread = null;
			}
		}

		private void TailLogFile(string LogFilePath)
		{
			string LogName = GetProcessName();
			while (!HasExited && !File.Exists(LogFilePath))
			{
				System.Threading.Thread.Sleep(1000);
			}

			if (File.Exists(LogFilePath))
			{
				using (StreamReader LogReader = new StreamReader(new FileStream(LogFilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)))
				{
					while (!HasExited)
					{
						System.Threading.Thread.Sleep(5000);
						while (!LogReader.EndOfStream)
						{
							string LogLine = LogReader.ReadLine();
							Log.WriteLine(1, SpewVerbosity, "{0} : {1}", LogName, LogLine);
						}
					}
				}
			}
		}
	}

#if !__MonoCS__
	class HoloLensDevicePortalCreatedProcess : IProcessResult
	{
		object StateLock;
		Microsoft.Tools.WindowsDevicePortal.DevicePortal Portal;
		uint ProcId;
		string PackageName;
		string FriendlyName;

		bool ProcessHasExited;

		public HoloLensDevicePortalCreatedProcess(Microsoft.Tools.WindowsDevicePortal.DevicePortal InPortal, string InPackageName, string InFriendlyName, uint InProcId)
		{
			Portal = InPortal;
			ProcId = InProcId;
			PackageName = InPackageName;
			FriendlyName = InFriendlyName;
			StateLock = new object();
			Portal.RunningProcessesMessageReceived += Portal_RunningProcessesMessageReceived;
			Portal.StartListeningForRunningProcessesAsync().Wait();
			//MonitorThread = new System.Threading.Thread(()=>(MonitorProcess))

			// No ETW on Xbox One, so can't collect trace that way
			if (Portal.Platform != Microsoft.Tools.WindowsDevicePortal.DevicePortal.DevicePortalPlatforms.XboxOne)
			{
				Guid MicrosoftWindowsDiagnoticsLoggingChannelId = new Guid(0x4bd2826e, 0x54a1, 0x4ba9, 0xbf, 0x63, 0x92, 0xb7, 0x3e, 0xa1, 0xac, 0x4a);
				Portal.ToggleEtwProviderAsync(MicrosoftWindowsDiagnoticsLoggingChannelId).Wait();
				Portal.RealtimeEventsMessageReceived += Portal_RealtimeEventsMessageReceived;
				Portal.StartListeningForEtwEventsAsync().Wait();
			}
		}

		private void Portal_RealtimeEventsMessageReceived(Microsoft.Tools.WindowsDevicePortal.DevicePortal sender, Microsoft.Tools.WindowsDevicePortal.WebSocketMessageReceivedEventArgs<Microsoft.Tools.WindowsDevicePortal.DevicePortal.EtwEvents> args)
		{
			foreach (var EtwEvent in args.Message.Events)
			{
				if (EtwEvent.ContainsKey("ProviderName") && EtwEvent.ContainsKey("StringMessage"))
				{
					if (EtwEvent["ProviderName"] == FriendlyName)
					{
						Log.WriteLine(1, GetLogVerbosityFromEventLevel(EtwEvent.Level), "{0} : {1}", FriendlyName, EtwEvent["StringMessage"].Trim('\"'));
					}
				}
			}
		}

		private LogEventType GetLogVerbosityFromEventLevel(uint EtwLevel)
		{
			switch (EtwLevel)
			{
				case 4:
					return LogEventType.Console;
				case 3:
					return LogEventType.Warning;
				case 2:
					return LogEventType.Error;
				case 1:
					return LogEventType.Fatal;
				default:
					return LogEventType.Verbose;
			}
		}

		private void Portal_RunningProcessesMessageReceived(Microsoft.Tools.WindowsDevicePortal.DevicePortal sender, Microsoft.Tools.WindowsDevicePortal.WebSocketMessageReceivedEventArgs<Microsoft.Tools.WindowsDevicePortal.DevicePortal.RunningProcesses> args)
		{
			foreach (var P in args.Message.Processes)
			{
				if (P.ProcessId == ProcId)
				{
					return;
				}
			}
			ProcessHasExited = true;
		}

		public int ExitCode
		{
			get
			{
				return 0;
			}

			set
			{
				throw new NotImplementedException();
			}
		}

		public bool HasExited
		{
			get
			{
				return ProcessHasExited;
			}
		}

		public string Output
		{
			get
			{
				return string.Empty;
			}
		}

		public Process ProcessObject
		{
			get
			{
				return null;
			}
		}

		public void DisposeProcess()
		{
		}

		public string GetProcessName()
		{
			return FriendlyName;
		}

		public void OnProcessExited()
		{
			Portal.StopListeningForRunningProcessesAsync().Wait();
			if (Portal.Platform != Microsoft.Tools.WindowsDevicePortal.DevicePortal.DevicePortalPlatforms.XboxOne)
			{
				Portal.StopListeningForEtwEventsAsync().Wait();
			}
		}

		public void StdErr(object sender, DataReceivedEventArgs e)
		{
			throw new NotImplementedException();
		}

		public void StdOut(object sender, DataReceivedEventArgs e)
		{
			throw new NotImplementedException();
		}

		public void StopProcess(bool KillDescendants = true)
		{
			Portal.TerminateApplicationAsync(PackageName).Wait();
		}

		public void WaitForExit()
		{
			while (!ProcessHasExited)
			{
				System.Threading.Thread.Sleep(1000);
			}
		}
	}
#endif

	public class HoloLensPlatform : Platform
    {
		private WindowsArchitecture[] ActualArchitectures = { };

		private FileReference MakeAppXPath;
		private FileReference PDBCopyPath;
		private FileReference SignToolPath;
		private FileReference MakeCertPath;
		private FileReference Pvk2PfxPath;
		private string Windows10SDKVersion;


		public HoloLensPlatform()
			: base(UnrealTargetPlatform.HoloLens)
		{

		}

		public override void PreBuildAgenda(UE4Build Build, UE4Build.BuildAgenda Agenda, ProjectParams Params)
		{
			if(ActualArchitectures.Length == 0)
			{
				throw new AutomationException(ExitCode.Error_Arguments, "No target architecture selected on \'Platforms/HoloLens/OS Info\' page. Please select at last one.");
			}

			foreach (var BuildConfig in Params.ClientConfigsToBuild)
			{
				foreach(var target in Params.ClientCookedTargets)
				{
					var ReceiptFileName = TargetReceipt.GetDefaultPath(Params.RawProjectPath.Directory, target, UnrealTargetPlatform.HoloLens, BuildConfig, "Multi");
					if(File.Exists(ReceiptFileName.FullName))
					{
						FileReference.Delete(ReceiptFileName);
					}

					foreach (var Arch in ActualArchitectures)
					{
						Agenda.Targets.Add(new UE4Build.BuildTarget()
						{
							TargetName = target,
							Platform = UnrealTargetPlatform.HoloLens,
							Config = BuildConfig,
							UprojectPath = Params.CodeBasedUprojectPath,
							UBTArgs = " -remoteini=\"" + Params.RawProjectPath.Directory.FullName + "\" -Architecture=" + WindowsExports.GetArchitectureSubpath(Arch),
						});
					}
				}
			}
		}

		public override bool CanBeCompiled() //block auto compilation
		{
			return false;
		}


		public override void PlatformSetupParams(ref ProjectParams ProjParams)
		{
			base.PlatformSetupParams(ref ProjParams);

			if (ProjParams.Deploy && !ProjParams.Package)
			{
				foreach (string DeviceAddress in ProjParams.DeviceNames)
				{
					if (!IsLocalDevice(DeviceAddress))
					{
						LogWarning("Project will be packaged to support deployment to remote HoloLens device {0}.", DeviceAddress);
						ProjParams.Package = true;
						break;
					}
				}
			}

			ConfigHierarchy PlatformEngineConfig = null;
			if (ProjParams.EngineConfigs.TryGetValue(PlatformType, out PlatformEngineConfig))
			{
				List<string> ThumbprintsFromConfig = new List<string>();
				var ArchList = new List<WindowsArchitecture>();
				bool bBuildForEmulation = false;
				bool bBuildForDevice = false;

				if (PlatformEngineConfig.GetArray("/Script/HoloLensPlatformEditor.HoloLensTargetSettings", "AcceptThumbprints", out ThumbprintsFromConfig))
				{
					AcceptThumbprints.AddRange(ThumbprintsFromConfig);
				}

				if (PlatformEngineConfig.GetBool("/Script/HoloLensPlatformEditor.HoloLensTargetSettings", "bBuildForEmulation", out bBuildForEmulation))
				{
					if (bBuildForEmulation)
					{
						ArchList.Add(WindowsArchitecture.x64);
					}
				}

				if (PlatformEngineConfig.GetBool("/Script/HoloLensPlatformEditor.HoloLensTargetSettings", "bBuildForDevice", out bBuildForDevice))
				{
					// If both are unchecked, then we build for device
					if (bBuildForDevice || (!bBuildForEmulation && !bBuildForDevice))
					{
						ArchList.Add(WindowsArchitecture.ARM64);
					}
				}

				ActualArchitectures = ArchList.ToArray();

				PlatformEngineConfig.GetString("/Script/HoloLensPlatformEditor.HoloLensTargetSettings", "Windows10SDKVersion", out Windows10SDKVersion);
			}


			ProjParams.SpecifiedArchitecture = "Multi";
			ProjParams.ConfigOverrideParams.Add("Architecture=Multi");

			FindInstruments();
			ConfigHierarchy CameConfig = null;
			if (ProjParams.GameConfigs.TryGetValue(PlatformType, out CameConfig))
			{
				GenerateSigningCertificate(ProjParams, CameConfig);
			}
		}

		private void FindInstruments()
		{
			if(string.IsNullOrEmpty(Windows10SDKVersion))
			{
				Windows10SDKVersion = "Latest";
			}

			if(!HoloLensExports.InitWindowsSdkToolPath(Windows10SDKVersion))
			{
				throw new AutomationException(ExitCode.Error_Arguments, "Wrong WinSDK toolchain selected on \'Platforms/HoloLens/Toolchain\' page. Please check.");
			}

			// VS 2017 puts MSBuild stuff (where PDBCopy lives) under the Visual Studio Installation directory
			DirectoryReference VSInstallDir;
			DirectoryReference MSBuildInstallDir = new DirectoryReference(Path.Combine(WindowsExports.GetMSBuildToolPath(), "..", "..", ".."));

			DirectoryReference SDKFolder;
			Version SDKVersion;

			if(WindowsExports.TryGetWindowsSdkDir(Windows10SDKVersion, out SDKVersion, out SDKFolder))
			{
				DirectoryReference WindowsSdkBinDir = DirectoryReference.Combine(SDKFolder, "bin", SDKVersion.ToString(), Environment.Is64BitProcess ? "x64" : "x86");
				if (!DirectoryReference.Exists(WindowsSdkBinDir))
				{
					throw new AutomationException(ExitCode.Error_Arguments, "WinSDK toolchain selected on \'Platforms/HoloLens/Toolchain\' page isn't exist anymore. Please select new one.");
				}
				if (PDBCopyPath == null || !FileReference.Exists(PDBCopyPath))
				{
					PDBCopyPath = FileReference.Combine(SDKFolder, "Debuggers", Environment.Is64BitProcess ? "x64" : "x86", "PDBCopy.exe");
				}
			}


			MakeAppXPath = HoloLensExports.GetWindowsSdkToolPath("makeappx.exe");
			SignToolPath = HoloLensExports.GetWindowsSdkToolPath("signtool.exe");
			MakeCertPath = HoloLensExports.GetWindowsSdkToolPath("makecert.exe");
			Pvk2PfxPath = HoloLensExports.GetWindowsSdkToolPath("pvk2pfx.exe");

			if (PDBCopyPath == null || !FileReference.Exists(PDBCopyPath))
			{
				if (WindowsExports.TryGetVSInstallDir(WindowsCompiler.VisualStudio2017, out VSInstallDir))
				{
					PDBCopyPath = FileReference.Combine(VSInstallDir, "MSBuild", "Microsoft", "VisualStudio", "v15.0", "AppxPackage", "PDBCopy.exe");
				}
			}

			// Earlier versions use a separate MSBuild install location
			if (PDBCopyPath == null || !FileReference.Exists(PDBCopyPath))
			{
				PDBCopyPath = FileReference.Combine(MSBuildInstallDir, "Microsoft", "VisualStudio", "v14.0", "AppxPackage", "PDBCopy.exe");
			}

			if (PDBCopyPath == null || !FileReference.Exists(PDBCopyPath))
			{
				PDBCopyPath = FileReference.Combine(MSBuildInstallDir, "Microsoft", "VisualStudio", "v12.0", "AppxPackage", "PDBCopy.exe");
			}

			if (!FileReference.Exists(PDBCopyPath))
			{
				PDBCopyPath = null;
			}

		}

		public override void Deploy(ProjectParams Params, DeploymentContext SC)
		{
			foreach (string DeviceAddress in Params.DeviceNames)
			{
				if (IsLocalDevice(DeviceAddress))
				{
					// Special case - we can use PackageManager to allow loose apps, plus we need to
					// apply the loopback exemption in case of cook-on-the-fly
					DeployToLocalDevice(Params, SC);
				}
				else
				{
					DeployToRemoteDevice(DeviceAddress, Params, SC);
				}
			}
		}

		public override void GetFilesToDeployOrStage(ProjectParams Params, DeploymentContext SC)
		{
		}

		public override string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
		{
			return PlatformType.ToString();
		}

		static void FillMapfile(DirectoryReference DirectoryName, string SearchPath, string Mask, StringBuilder AppXRecipeBuiltFiles)
		{
			string StageDir = DirectoryName.FullName;
			if (!StageDir.EndsWith("\\") && !StageDir.EndsWith("/"))
			{
				StageDir += Path.DirectorySeparatorChar;
			}
			Uri StageDirUri = new Uri(StageDir);
			foreach (var pf in Directory.GetFiles(SearchPath, Mask, SearchOption.AllDirectories))
			{
				var FileUri = new Uri(pf);
				var relativeUri = StageDirUri.MakeRelativeUri(FileUri);
				var relativePath = Uri.UnescapeDataString(relativeUri.ToString());
				var newPath = relativePath.Replace('/', Path.DirectorySeparatorChar);

				AppXRecipeBuiltFiles.AppendLine(String.Format("\"{0}\"\t\"{1}\"", pf, newPath));
			}
		}

		static void FillMapfile(DirectoryReference DirectoryName, FileReference ManifestFile, StringBuilder AppXRecipeBuiltFiles)
		{
			if (FileReference.Exists(ManifestFile))
			{
				string[] Lines = FileReference.ReadAllLines(ManifestFile);
				foreach (string Line in Lines)
				{
					string[] Pair = Line.Split('\t');
					if (Pair.Length > 1)
					{
						AppXRecipeBuiltFiles.AppendLine(String.Format("\"{0}\"\t\"{1}\"", FileReference.Combine(DirectoryName, Pair[0]), Pair[0]));
					}
				}
			}
		}

		private void PackagePakFiles(ProjectParams Params, DeploymentContext SC, string OutputNameBase)
		{
			string IntermediateDirectory = Path.Combine(SC.ProjectRoot.FullName, "Intermediate", "Deploy", "neutral");
LogWarning("PackagePakFiles intermediate dir {0}", IntermediateDirectory);
			var ListResources = new HoloLensManifestGenerator().CreateAssetsManifest(SC.StageTargetPlatform.PlatformType, SC.StageDirectory.FullName, IntermediateDirectory, SC.RawProjectPath, SC.ProjectRoot.FullName);

			string OutputName = OutputNameBase + "_pak";

			var AppXRecipeBuiltFiles = new StringBuilder();

			string MapFilename = Path.Combine(SC.StageDirectory.FullName, OutputName + ".pkgmap");
			AppXRecipeBuiltFiles.AppendLine(@"[Files]");

			string OutputAppX = Path.Combine(SC.StageDirectory.FullName, OutputName + ".appx");

			if(Params.UsePak(this))
			{
				FillMapfile(SC.StageDirectory, SC.StageDirectory.FullName, "*.pak", AppXRecipeBuiltFiles);
			}
			else
			{
				FillMapfile(SC.StageDirectory, FileReference.Combine(SC.StageDirectory, SC.GetUFSDeployedManifestFileName(null)), AppXRecipeBuiltFiles);
			}

			FillMapfile(SC.StageDirectory, FileReference.Combine(SC.StageDirectory, SC.GetNonUFSDeployedManifestFileName(null)), AppXRecipeBuiltFiles);

			{
				DirectoryReference ResourceFolder = SC.StageDirectory;
				foreach(var ResourcePath in ListResources)
				{
					var ResourceFile = new FileReference(ResourcePath);
					if(ResourceFile.IsUnderDirectory(ResourceFolder))
					{
						AppXRecipeBuiltFiles.AppendLine(String.Format("\"{0}\"\t\"{1}\"", ResourceFile.FullName, ResourceFile.MakeRelativeTo(ResourceFolder)));
					}
				}
			}

			AppXRecipeBuiltFiles.AppendLine(String.Format("\"{0}\"\t\"{1}\"", Path.Combine(SC.StageDirectory.FullName, "AppxManifest_assets.xml"), "AppxManifest.xml"));

			File.WriteAllText(MapFilename, AppXRecipeBuiltFiles.ToString(), Encoding.UTF8);

			string MakeAppXCommandLine = string.Format(@"pack /o /f ""{0}"" /p ""{1}""", MapFilename, OutputAppX);
			RunAndLog(CmdEnv, MakeAppXPath.FullName, MakeAppXCommandLine, null, 0, null, ERunOptions.None);
			SignPackage(Params, SC, OutputAppX);
		}

		private void UpdateCodePackagesWithData(ProjectParams Params, DeploymentContext SC, string OutputNameBase)
		{
			foreach (StageTarget Target in SC.StageTargets)
			{
				foreach (BuildProduct Product in Target.Receipt.BuildProducts)
				{
					if (Product.Type != BuildProductType.MapFile)
					{
						continue;
					}

					string MapFilename = Product.Path.FullName; 

					var AppXRecipeBuiltFiles = new StringBuilder();

					string OutputAppX = Path.Combine(SC.StageDirectory.FullName, Product.Path.GetFileNameWithoutExtension() + ".appx");

					if (Params.UsePak(this))
					{
						FillMapfile(SC.StageDirectory, SC.StageDirectory.FullName, "*.pak", AppXRecipeBuiltFiles);
					}
					else
					{
						FillMapfile(SC.StageDirectory, FileReference.Combine(SC.StageDirectory, SC.GetUFSDeployedManifestFileName(null)), AppXRecipeBuiltFiles);
					}

					FillMapfile(SC.StageDirectory, FileReference.Combine(SC.StageDirectory, SC.GetNonUFSDeployedManifestFileName(null)), AppXRecipeBuiltFiles);

					File.AppendAllText(MapFilename, AppXRecipeBuiltFiles.ToString(), Encoding.UTF8);

					string MakeAppXCommandLine = string.Format(@"pack /o /f ""{0}"" /p ""{1}""", MapFilename, OutputAppX);
					RunAndLog(CmdEnv, MakeAppXPath.FullName, MakeAppXCommandLine, null, 0, null, ERunOptions.None);

					SignPackage(Params, SC, OutputAppX);
				}
			}
		}

		void MakeBundle(ProjectParams Params, DeploymentContext SC, string OutputNameBase, bool SeparateAssetPackaging)
		{
			string OutputName = OutputNameBase;

			var AppXRecipeBuiltFiles = new StringBuilder();
			
			string MapFilename = Path.Combine(SC.StageDirectory.FullName, OutputName + "_bundle.pkgmap");
			AppXRecipeBuiltFiles.AppendLine(@"[Files]");

			string OutputAppX = Path.Combine(SC.StageDirectory.FullName, OutputName + ".appxbundle");
			if(SeparateAssetPackaging)
			{
				foreach (StageTarget Target in SC.StageTargets)
				{
					foreach (BuildProduct Product in Target.Receipt.BuildProducts)
					{
						if (Product.Type == BuildProductType.Package)
						{
							AppXRecipeBuiltFiles.AppendLine(String.Format("\"{0}\"\t\"{1}\"", Product.Path.FullName, Path.GetFileName(Product.Path.FullName)));
						}
					}
				}
				
				AppXRecipeBuiltFiles.AppendLine(String.Format("\"{0}\"\t\"{1}\"", Path.Combine(SC.StageDirectory.FullName, OutputName + "_pak.appx"), OutputName + "_pak.appx"));//assets from pak file
			}
			else
			{
				FillMapfile(SC.StageDirectory, SC.StageDirectory.FullName, "*.appx", AppXRecipeBuiltFiles);
			}

			File.WriteAllText(MapFilename, AppXRecipeBuiltFiles.ToString(), Encoding.UTF8);

			string MakeAppXCommandLine = string.Format(@"bundle /o /f ""{0}"" /p ""{1}""", MapFilename, OutputAppX);
			RunAndLog(CmdEnv, MakeAppXPath.FullName, MakeAppXCommandLine, null, 0, null, ERunOptions.None);
			SignPackage(Params, SC, OutputName + ".appxbundle", true);
		}

		private void CopyVCLibs(ProjectParams Params, DeploymentContext SC)
		{
			TargetRules Rules = Params.ProjectTargets.Find(x => x.Rules.Type == TargetType.Game).Rules;

			bool UseDebugCrt = false;
			WindowsCompiler compiler = WindowsCompiler.VisualStudio2017;

			//TODO: Why is this null?
			if (Rules != null)
			{
				UseDebugCrt = Params.ClientConfigsToBuild.Contains(UnrealTargetConfiguration.Debug) && Rules.bDebugBuildsActuallyUseDebugCRT;
				compiler = Rules.WindowsPlatform.Compiler;
			}

			foreach (string vcLib in GetPathToVCLibsPackages(UseDebugCrt, compiler))
			{
				CopyFile(vcLib, Path.Combine(SC.StageDirectory.FullName, Path.GetFileName(vcLib)));
			}
		}

		private void GenerateSigningCertificate(ProjectParams Params, ConfigHierarchy ConfigFile)
		{
			string SigningCertificate = @"Build\HoloLens\SigningCertificate.pfx";
			string SigningCertificatePath = Path.Combine(Params.RawProjectPath.Directory.FullName, SigningCertificate);
			if (!File.Exists(SigningCertificatePath))
			{
				if (!IsBuildMachine && !Params.Unattended)
				{
					LogError("Certificate is required.  Please go to Project Settings > HoloLens > Create Signing Certificate");
				}
				else
				{
					LogWarning("No certificate found at {0} and temporary certificate cannot be generated (running unattended).", SigningCertificatePath);
				}
			}
		}

		private void SignPackage(ProjectParams Params, DeploymentContext SC, string OutputName, bool GenerateCer = false)
		{
			string OutputNameBase = Path.GetFileNameWithoutExtension(OutputName);
			string OutputAppX = Path.Combine(SC.StageDirectory.FullName, OutputName);
			string SigningCertificate = @"Build\HoloLens\SigningCertificate.pfx";

			if (GenerateCer)
			{
				// Emit a .cer file adjacent to the appx so it can be installed to enable packaged deployment
				System.Security.Cryptography.X509Certificates.X509Certificate2 ActualCert = new System.Security.Cryptography.X509Certificates.X509Certificate2(Path.Combine(SC.ProjectRoot.FullName, SigningCertificate));
				File.WriteAllText(Path.Combine(SC.StageDirectory.FullName, OutputNameBase + ".cer"), Convert.ToBase64String(ActualCert.Export(System.Security.Cryptography.X509Certificates.X509ContentType.Cert)));
			}

			string CertFile = Path.Combine(SC.ProjectRoot.FullName, SigningCertificate);
			if(File.Exists(CertFile))
			{
				string SignToolCommandLine = string.Format(@"sign /a /f ""{0}"" /fd SHA256 ""{1}""", CertFile, OutputAppX);
				RunAndLog(CmdEnv, SignToolPath.FullName, SignToolCommandLine, null, 0, null, ERunOptions.None);
			}
		}

		public override bool RequiresPackageToDeploy
		{
			get { return true; }
		}

		public override void Package(ProjectParams Params, DeploymentContext SC, int WorkingCL)
		{
			//GenerateDLCManifestIfNecessary(Params, SC);

			string OutputNameBase = Params.HasDLCName ? Params.DLCFile.GetFileNameWithoutExtension() : Params.ShortProjectName;
			string OutputAppX = Path.Combine(SC.StageDirectory.FullName, OutputNameBase + ".appxbundle");

			UpdateCodePackagesWithData(Params, SC, OutputNameBase);

			MakeBundle(Params, SC, OutputNameBase, false);

			CopyVCLibs(Params, SC);

			// If the user indicated that they will distribute this build, then let's also generate an
			// appxupload file suitable for submission to the Windows Store.  This file zips together
			// the appx package and the public symbols (themselves zipped) for the binaries.
			if (Params.Distribution)
			{
				List<FileReference> SymbolFilesToZip = new List<FileReference>();
				DirectoryReference StageDirRef = new DirectoryReference(SC.StageDirectory.FullName);
				DirectoryReference PublicSymbols = DirectoryReference.Combine(StageDirRef, "PublicSymbols");
				DirectoryReference.CreateDirectory(PublicSymbols);
				foreach (StageTarget Target in SC.StageTargets)
				{
					foreach (BuildProduct Product in Target.Receipt.BuildProducts)
					{
						if (Product.Type == BuildProductType.SymbolFile)
						{
							FileReference FullSymbolFile = new FileReference(Product.Path.FullName);
							FileReference TempStrippedSymbols = FileReference.Combine(PublicSymbols, FullSymbolFile.GetFileName());
							StripSymbols(FullSymbolFile, TempStrippedSymbols);
							SymbolFilesToZip.Add(TempStrippedSymbols);
						}
					}
				}
				FileReference AppxSymFile = FileReference.Combine(StageDirRef, OutputNameBase + ".appxsym");
				ZipFiles(AppxSymFile, PublicSymbols, SymbolFilesToZip);

				FileReference AppxUploadFile = FileReference.Combine(StageDirRef, OutputNameBase + ".appxupload");
				ZipFiles(AppxUploadFile, StageDirRef,
					new FileReference[]
					{
							new FileReference(OutputAppX),
							AppxSymFile
					});
			}
		}

		public override IProcessResult RunClient(ERunOptions ClientRunFlags, string ClientApp, string ClientCmdLine, ProjectParams Params)
		{
			IProcessResult ProcResult = null;
			foreach (string DeviceAddress in Params.DeviceNames)
			{
				// Prefer launcher tool for local device since it avoids having to deal with certificate issues on the
				// device portal connection.
				if (IsLocalDevice(DeviceAddress))
				{
					ProcResult = RunUsingLauncherTool(DeviceAddress, ClientRunFlags, ClientApp, ClientCmdLine, Params);
				}
				else
				{
					ProcResult = RunUsingDevicePortal(DeviceAddress, ClientRunFlags, ClientApp, ClientCmdLine, Params);
				}
			}
			return ProcResult;
		}

        public override List<FileReference> GetExecutableNames(DeploymentContext SC)
		{
			// If we're calling this for the purpose of running the app then the string we really
			// need is the AUMID.  We can't form a full AUMID here without making assumptions about 
			// how the PFN is built, which (while straightforward) does not appear to be officially
			// documented.  So we'll save off the path to the manifest, which the launch process can
			// parse later for information that, in conjunction with the target device, will allow
			// for looking up the true AUMID.
			List<FileReference> Exes = new List<FileReference>();
			Exes.Add(new FileReference(GetAppxManifestPath(SC)));
			return Exes;
		}

		public override bool IsSupported { get { return true; } }
		public override bool UseAbsLog { get { return false; } }
		public override bool LaunchViaUFE { get { return false; } }

		public override List<string> GetDebugFileExtensions()
		{
			return new List<string> { ".pdb", ".map" };
		}

		public override void StripSymbols(FileReference SourceFile, FileReference TargetFile)
		{
			bool bStripInPlace = false;

			if (SourceFile == TargetFile)
			{
				// PDBCopy only supports creation of a brand new stripped file so we have to create a temporary filename
				TargetFile = new FileReference(Path.Combine(TargetFile.Directory.FullName, Guid.NewGuid().ToString() + TargetFile.GetExtension()));
				bStripInPlace = true;
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();

			if (PDBCopyPath == null || !FileReference.Exists(PDBCopyPath))
			{
				throw new AutomationException(ExitCode.Error_SDKNotFound, "Debugging Tools for Windows aren't installed. Please follow https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/debugger-download-tools");
			}

			StartInfo.FileName = PDBCopyPath.FullName;
			StartInfo.Arguments = String.Format("\"{0}\" \"{1}\" -p", SourceFile.FullName, TargetFile.FullName);
			StartInfo.UseShellExecute = false;
			StartInfo.CreateNoWindow = true;
			Utils.RunLocalProcessAndLogOutput(StartInfo);

			if (bStripInPlace)
			{
				// Copy stripped file to original location and delete the temporary file
				File.Copy(TargetFile.FullName, SourceFile.FullName, true);
				FileReference.Delete(TargetFile);
			}
		}

		private void GenerateSigningCertificate(string InCertificatePath, string InPublisher)
		{
			// Ensure the output directory exists otherwise makecert will fail.
			InternalUtils.SafeCreateDirectory(Path.GetDirectoryName(InCertificatePath));

			// MakeCert.exe -r -h 0 -n "CN=No Publisher, O=No Publisher" -eku 1.3.6.1.5.5.7.3.3 -pe -sv "Signing Certificate.pvk" "Signing Certificate.cer"
			// pvk2pfx -pvk "Signing Certificate.pvk" -spc "Signing Certificate.cer" -pfx "Signing Certificate.pfx"
			string CerFile = Path.ChangeExtension(InCertificatePath, ".cer");
			string PvkFile = Path.ChangeExtension(InCertificatePath, ".pvk");

			string MakeCertCommandLine = string.Format(@"-r -h 0 -n ""{0}"" -eku 1.3.6.1.5.5.7.3.3 -pe -sv ""{1}"" ""{2}""", InPublisher, PvkFile, CerFile);
			RunAndLog(CmdEnv, MakeCertPath.FullName, MakeCertCommandLine, null, 0, null, ERunOptions.None);

			string Pvk2PfxCommandLine = string.Format(@"-pvk ""{0}"" -spc ""{1}"" -pfx ""{2}""", PvkFile, CerFile, InCertificatePath);
			RunAndLog(CmdEnv, Pvk2PfxPath.FullName, Pvk2PfxCommandLine, null, 0, null, ERunOptions.None);
		}

		private bool IsLocalDevice(string DeviceAddress)
		{
			try
			{
				return new Uri(DeviceAddress).IsLoopback;
			}
			catch
			{
				// If we can't parse the address as a Uri we default to local
				return true;
			}
		}

		private void DeployToLocalDevice(ProjectParams Params, DeploymentContext SC)
		{
#if !__MonoCS__
            if (Utils.IsRunningOnMono)
            {
                return;
            }

            bool bRequiresPackage = Params.Package || SC.StageTargetPlatform.RequiresPackageToDeploy;
			string AppxManifestPath = GetAppxManifestPath(SC);
			string Name;
			string Publisher;
			GetPackageInfo(AppxManifestPath, out Name, out Publisher);
			Windows.Management.Deployment.PackageManager PackMgr = new Windows.Management.Deployment.PackageManager();
			try
			{
				var ExistingPackage = PackMgr.FindPackagesForUser("", Name, Publisher).FirstOrDefault();

				// Only remove an existing package if it's in development mode; otherwise the removal might silently delete stuff
				// that the user wanted.
				if (ExistingPackage != null)
				{
					if (ExistingPackage.IsDevelopmentMode)
					{
						PackMgr.RemovePackageAsync(ExistingPackage.Id.FullName, Windows.Management.Deployment.RemovalOptions.PreserveApplicationData).AsTask().Wait();
					}
					else if (!bRequiresPackage)
					{
						throw new AutomationException(ExitCode.Error_AppInstallFailed, "A packaged version of the application already exists.  It must be uninstalled manually - note this will remove user data.");
					}
				}

				if (!bRequiresPackage)
				{
					// Register appears to expect dependency packages to be loose too, which the VC runtime is not, so skip it and hope 
					// that it's already installed on the local machine (almost certainly true given that we aren't currently picky about exact version)
					PackMgr.RegisterPackageAsync(new Uri(AppxManifestPath), null, Windows.Management.Deployment.DeploymentOptions.DevelopmentMode).AsTask().Wait();
				}
				else
				{
					string PackageName = Params.ShortProjectName;
					string PackagePath = Path.Combine(SC.StageDirectory.FullName, PackageName + ".appxbundle");

					List<Uri> Dependencies = new List<Uri>();
					TargetRules Rules = Params.ProjectTargets.Find(x => x.Rules.Type == TargetType.Game).Rules;
					bool UseDebugCrt = Params.ClientConfigsToBuild.Contains(UnrealTargetConfiguration.Debug) && Rules.bDebugBuildsActuallyUseDebugCRT;
					foreach(var RT in GetPathToVCLibsPackages(UseDebugCrt, Rules.WindowsPlatform.Compiler))
					{
						Dependencies.Add(new Uri(RT));
					}

					PackMgr.AddPackageAsync(new Uri(PackagePath), Dependencies, Windows.Management.Deployment.DeploymentOptions.None).AsTask().Wait();
				}
			}
			catch (AggregateException agg)
			{
				throw new AutomationException(ExitCode.Error_AppInstallFailed, agg.InnerException, agg.InnerException.Message);
			}

			// Package should now be installed.  Locate it and make sure it's permitted to connect over loopback.
			try
			{
				var InstalledPackage = PackMgr.FindPackagesForUser("", Name, Publisher).FirstOrDefault();
				string LoopbackExemptCmdLine = string.Format("loopbackexempt -a -n={0}", InstalledPackage.Id.FamilyName);
				RunAndLog(CmdEnv, "checknetisolation.exe", LoopbackExemptCmdLine, null, 0, null, ERunOptions.None);
			}
			catch
			{
				LogWarning("Failed to apply a loopback exemption to the deployed app.  Connection to a local cook server will fail.");
			}
#endif
		}

		private void DeployToRemoteDevice(string DeviceAddress, ProjectParams Params, DeploymentContext SC)
		{
#if !__MonoCS__
			if (Utils.IsRunningOnMono)
            {
                return;
            }

			if (Params.Package || SC.StageTargetPlatform.RequiresPackageToDeploy)
			{
				Microsoft.Tools.WindowsDevicePortal.DefaultDevicePortalConnection conn = new Microsoft.Tools.WindowsDevicePortal.DefaultDevicePortalConnection(DeviceAddress, Params.DeviceUsername, Params.DevicePassword);
				Microsoft.Tools.WindowsDevicePortal.DevicePortal portal = new Microsoft.Tools.WindowsDevicePortal.DevicePortal(conn);
				portal.UnvalidatedCert += (sender, certificate, chain, sslPolicyErrors) =>
				{
					return ShouldAcceptCertificate(new System.Security.Cryptography.X509Certificates.X509Certificate2(certificate), Params.Unattended);
				};
				portal.ConnectionStatus += Portal_ConnectionStatus;
				try
				{
					portal.ConnectAsync().Wait();
					string PackageName = Params.ShortProjectName;
					string PackagePath = Path.Combine(SC.StageDirectory.FullName, PackageName + ".appxbundle");
					string CertPath = Path.Combine(SC.StageDirectory.FullName, PackageName + ".cer");

					List<string> Dependencies = new List<string>();
					bool UseDebugCrt = false;
					WindowsCompiler Compiler = WindowsCompiler.Default;
					TargetRules Rules = null;
					if (Params.HasGameTargetDetected)
					{
						//Rules = Params.ProjectTargets[TargetType.Game].Rules;
						Rules = Params.ProjectTargets.Find(x => x.Rules.Type == TargetType.Game).Rules;
					}
					else if (Params.HasClientTargetDetected)
					{
						Rules = Params.ProjectTargets.Find(x => x.Rules.Type == TargetType.Game).Rules;
					}

					if (Rules != null)
					{
						UseDebugCrt = Params.ClientConfigsToBuild.Contains(UnrealTargetConfiguration.Debug) && Rules.bDebugBuildsActuallyUseDebugCRT;
						Compiler = Rules.WindowsPlatform.Compiler;
					}

					Dependencies.AddRange(GetPathToVCLibsPackages(UseDebugCrt, Compiler));

					portal.AppInstallStatus += Portal_AppInstallStatus;
					portal.InstallApplicationAsync(string.Empty, PackagePath, Dependencies, CertPath).Wait();
				}
				catch (AggregateException e)
				{
					if (e.InnerException is AutomationException)
					{
						throw e.InnerException;
					}
					else
					{
						throw new AutomationException(ExitCode.Error_AppInstallFailed, e.InnerException, e.InnerException.Message);
					}
				}
				catch (Exception e)
				{
					throw new AutomationException(ExitCode.Error_AppInstallFailed, e, e.Message);
				}
			}
			else
			{
				throw new AutomationException(ExitCode.Error_AppInstallFailed, "Remote deployment of unpackaged apps is not supported.");
			}
#endif
		}

		private IProcessResult RunUsingLauncherTool(string DeviceAddress, ERunOptions ClientRunFlags, string ClientApp, string ClientCmdLine, ProjectParams Params)
		{
#if !__MonoCS__
            if (Utils.IsRunningOnMono)
            {
                return null;
            }

            string Name;
			string Publisher;
			string PrimaryAppId = "App";
			GetPackageInfo(ClientApp, out Name, out Publisher);

			Windows.Management.Deployment.PackageManager PackMgr = new Windows.Management.Deployment.PackageManager();
			Windows.ApplicationModel.Package InstalledPackage = PackMgr.FindPackagesForUser("", Name, Publisher).FirstOrDefault();

			if (InstalledPackage == null)
			{
				throw new AutomationException(ExitCode.Error_LauncherFailed, "Could not find installed app (Name: {0}, Publisher: {1}", Name, Publisher);
			}

			string Aumid = string.Format("{0}!{1}", InstalledPackage.Id.FamilyName, PrimaryAppId);
			DirectoryReference SDKFolder;
			Version SDKVersion;

			if (!WindowsExports.TryGetWindowsSdkDir("Latest", out SDKVersion, out SDKFolder))
			{
				return null;
			}

			string LauncherPath = DirectoryReference.Combine(SDKFolder, "App Certification Kit", "microsoft.windows.softwarelogo.appxlauncher.exe").FullName;
			IProcessResult LauncherProc = Run(LauncherPath, Aumid);
			LauncherProc.WaitForExit();
			string LogFile;
			if (Params.CookOnTheFly)
			{
				LogFile = Path.Combine(Params.RawProjectPath.Directory.FullName, "Saved", "Cooked", PlatformType.ToString(), Params.ShortProjectName, "HoloLensLocalAppData", Params.ShortProjectName, "Saved", "Logs", Params.ShortProjectName + ".log");
			}
			else
			{
				LogFile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Packages", InstalledPackage.Id.FamilyName, "LocalState", Params.ShortProjectName, "Saved", "Logs", Params.ShortProjectName + ".log");
			}
			System.Diagnostics.Process Proc = System.Diagnostics.Process.GetProcessById(LauncherProc.ExitCode);
			bool AllowSpew = ClientRunFlags.HasFlag(ERunOptions.AllowSpew);
			LogEventType SpewVerbosity = ClientRunFlags.HasFlag(ERunOptions.SpewIsVerbose) ? LogEventType.Verbose : LogEventType.Console;
			HoloLensLauncherCreatedProcess HoloLensProcessResult = new HoloLensLauncherCreatedProcess(Proc, LogFile, AllowSpew, SpewVerbosity);
			ProcessManager.AddProcess(HoloLensProcessResult);
			if (!ClientRunFlags.HasFlag(ERunOptions.NoWaitForExit))
			{
				HoloLensProcessResult.WaitForExit();
				HoloLensProcessResult.OnProcessExited();
				HoloLensProcessResult.DisposeProcess();
			}
			return HoloLensProcessResult;
#else
			return null;
#endif
		}

		private IProcessResult RunUsingDevicePortal(string DeviceAddress, ERunOptions ClientRunFlags, string ClientApp, string ClientCmdLine, ProjectParams Params)
		{
#if !__MonoCS__
            if (Utils.IsRunningOnMono)
            {
                return null;
            }

            string Name;
			string Publisher;
			GetPackageInfo(ClientApp, out Name, out Publisher);

			Microsoft.Tools.WindowsDevicePortal.DefaultDevicePortalConnection conn = new Microsoft.Tools.WindowsDevicePortal.DefaultDevicePortalConnection(DeviceAddress, Params.DeviceUsername, Params.DevicePassword);
			Microsoft.Tools.WindowsDevicePortal.DevicePortal portal = new Microsoft.Tools.WindowsDevicePortal.DevicePortal(conn);
			portal.UnvalidatedCert += (sender, certificate, chain, sslPolicyErrors) =>
			{
				return ShouldAcceptCertificate(new System.Security.Cryptography.X509Certificates.X509Certificate2(certificate), Params.Unattended);
			};

			try
			{
				portal.ConnectAsync().Wait();
				string Aumid = string.Empty;
				string FullName = string.Empty;
				var AllAppsTask = portal.GetInstalledAppPackagesAsync();
				AllAppsTask.Wait();
				foreach (var App in AllAppsTask.Result.Packages)
				{
					// App.Name seems to report the localized name.
					if (App.FamilyName.StartsWith(Name) && App.Publisher == Publisher)
					{
						Aumid = App.AppId;
						FullName = App.FullName;
						break;
					}
				}

				var LaunchTask = portal.LaunchApplicationAsync(Aumid, FullName);
				LaunchTask.Wait();
				IProcessResult Result = new HoloLensDevicePortalCreatedProcess(portal, FullName, Params.ShortProjectName, LaunchTask.Result);

				ProcessManager.AddProcess(Result);
				if (!ClientRunFlags.HasFlag(ERunOptions.NoWaitForExit))
				{
					Result.WaitForExit();
					Result.OnProcessExited();
					Result.DisposeProcess();
				}
				return Result;
			}
			catch (AggregateException e)
			{
				if (e.InnerException is AutomationException)
				{
					throw e.InnerException;
				}
				else
				{
					throw new AutomationException(ExitCode.Error_LauncherFailed, e.InnerException, e.InnerException.Message);
				}
			}
			catch (Exception e)
			{
				throw new AutomationException(ExitCode.Error_LauncherFailed, e, e.Message);
			}
#else
			return null;
#endif
		}

		private string GetAppxManifestPath(DeploymentContext SC)
		{
			return Path.Combine(SC.StageDirectory.FullName, "AppxManifest_assets.xml");
		}

		private void GetPackageInfo(string AppxManifestPath, out string Name, out string Publisher)
		{
            System.Xml.Linq.XDocument Doc = System.Xml.Linq.XDocument.Load(AppxManifestPath);
			System.Xml.Linq.XElement Package = Doc.Root;
			System.Xml.Linq.XElement Identity = Package.Element(System.Xml.Linq.XName.Get("Identity", Package.Name.NamespaceName));
			Name = Identity.Attribute("Name").Value;
			Publisher = Identity.Attribute("Publisher").Value;
		}

		public override void GetFilesToArchive(ProjectParams Params, DeploymentContext SC)
		{
			string OutputNameBase = Params.HasDLCName ? Params.DLCFile.GetFileNameWithoutExtension() : Params.ShortProjectName;
			string PackagePath = SC.StageDirectory.FullName;

			SC.ArchiveFiles(PackagePath, "*.appxbundle");
			SC.ArchiveFiles(PackagePath, OutputNameBase + ".cer");
			SC.ArchiveFiles(PackagePath, "*.appxupload");
			
			SC.ArchiveFiles(PackagePath, "*VCLibs*.appx");
		}

		private bool ShouldAcceptCertificate(System.Security.Cryptography.X509Certificates.X509Certificate2 Certificate, bool Unattended)
		{
#if !__MonoCS__
            if (Utils.IsRunningOnMono)
            {
                return false;
            }

            if (AcceptThumbprints.Contains(Certificate.Thumbprint))
			{
				return true;
			}

			if (Unattended)
			{
				throw new AutomationException(ExitCode.Error_CertificateNotFound, "Cannot connect to remote device: certificate is untrusted and cannot prompt for consent (running unattended).");
			}

			System.Windows.Forms.DialogResult AcceptResult = System.Windows.Forms.MessageBox.Show(
				string.Format("Do you want to accept the following certificate?\n\nThumbprint:\n\t{0}\nIssues:\n\t{1}", Certificate.Thumbprint, Certificate.Issuer),
				"Untrusted Certificate Detected",
				System.Windows.Forms.MessageBoxButtons.YesNo,
				System.Windows.Forms.MessageBoxIcon.Question,
				System.Windows.Forms.MessageBoxDefaultButton.Button2);
			if (AcceptResult == System.Windows.Forms.DialogResult.Yes)
			{
				AcceptThumbprints.Add(Certificate.Thumbprint);
				return true;
			}

			throw new AutomationException(ExitCode.Error_CertificateNotFound, "Cannot connect to remote device: certificate is untrusted and user declined to accept.");
#else
			return false;
#endif
		}

#if !__MonoCS__
		private void Portal_AppInstallStatus(Microsoft.Tools.WindowsDevicePortal.DevicePortal sender, Microsoft.Tools.WindowsDevicePortal.ApplicationInstallStatusEventArgs args)
		{
			if (args.Status == Microsoft.Tools.WindowsDevicePortal.ApplicationInstallStatus.Failed)
			{
				throw new AutomationException(ExitCode.Error_AppInstallFailed, args.Message);
			}
			else
			{
				LogLog(args.Message);
			}
		}

		private void Portal_ConnectionStatus(Microsoft.Tools.WindowsDevicePortal.DevicePortal sender, Microsoft.Tools.WindowsDevicePortal.DeviceConnectionStatusEventArgs args)
		{
			if (args.Status == Microsoft.Tools.WindowsDevicePortal.DeviceConnectionStatus.Failed)
			{
				throw new AutomationException(args.Message);
			}
			else
			{
				LogLog(args.Message);
			}
		}
#endif

        private string[] GetPathToVCLibsPackages(bool UseDebugCrt, WindowsCompiler Compiler)
		{
			string VCVersionFragment;
            switch (Compiler)
			{
				case WindowsCompiler.VisualStudio2019:
				case WindowsCompiler.VisualStudio2017:
				//Compiler version is still 14 for 2017
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
				case WindowsCompiler.Default:
					VCVersionFragment = "14";
					break;

				default:
					VCVersionFragment = "Unsupported_VC_Version";
					break;
			}

			List<string> Runtimes = new List<string>();

			foreach(var Arch in ActualArchitectures)
			{
				string ArchitectureFragment = WindowsExports.GetArchitectureSubpath(Arch);

				string RuntimePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
					"Microsoft SDKs",
					"Windows Kits",
					"10",
					"ExtensionSDKs",
					"Microsoft.VCLibs",
					string.Format("{0}.0", VCVersionFragment),
					"Appx",
					UseDebugCrt ? "Debug" : "Retail",
					ArchitectureFragment,
					string.Format("Microsoft.VCLibs.{0}.{1}.00.appx", ArchitectureFragment, VCVersionFragment));

				Runtimes.Add(RuntimePath);

			}
			return Runtimes.ToArray();
		}

		private void GenerateDLCManifestIfNecessary(ProjectParams Params, DeploymentContext SC)
		{
			// Only required for DLC
			if (!Params.HasDLCName)
			{
				return;
			}

			// Only required for the first stage (package or deploy) that requires a manifest.
			// Assumes that the staging directory is pre-cleaned
			if (FileReference.Exists(FileReference.Combine(SC.StageDirectory, "AppxManifest.xml")))
			{
				return;
			}


			HoloLensExports.CreateManifestForDLC(Params.DLCFile, SC.StageDirectory);
		}

		private static List<string> AcceptThumbprints = new List<string>();
	}
}
