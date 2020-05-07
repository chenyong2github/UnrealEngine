using System;
using System.Collections.Generic;
using System.Text;
using System.Xml.Serialization;
using System.IO;
using Tools.DotNETCommon;
using UnrealBuildTool;
using AutomationTool;

namespace Turnkey
{
	public class SdkInfo
	{
		#region Fields
		public enum SdkType
		{
			BuildOnly,
			RunOnly,
			Full,
			Flash,
			Misc,
		};


		[XmlElement("Platform")]
		public string PlatformString = null;

		public CopyAndRun[] Installers = null;
		public string Version = null;
		public string DisplayName = null;
		public SdkType Type = SdkType.Full;
		public string AllowedFlashDeviceTypes = null;

		public string CustomSdkId = null;
		public string CustomSdkParams = null;
		public CopyAndRun[] CustomSdkInputFiles = null;

		[XmlIgnore]
		// if there are custom files, we copy them once, then cache the location for $(CustomSdkInputFiles) on a later execution
		private string CustomVersionLocalFiles = null;

		[XmlIgnore]
		private List<UnrealTargetPlatform> Platforms;

		[XmlIgnore]
		private Dictionary<UnrealTargetPlatform, AutomationTool.Platform> AutomationPlatforms;

// 		private SdkInfo CloneForQuickSwitch(string LocalDirectoryForCopy)
// 		{
// 			SdkInfo Clone = new SdkInfo();
// 			Clone.PlatformString = PlatformString;
// 			Clone.Version = Version;
// 			Clone.DisplayName = DisplayName;
// 			Clone.AllowedFlashDeviceTypes = AllowedFlashDeviceTypes;
// 			Clone.CustomSdkId = CustomSdkId;
// 			Clone.CustomSdkParams = CustomSdkParams;
// 			Clone.PlatformString = PlatformString;
// 
// 			Clone.Type = SdkType.QuickSwitch;
// 
// 			if (Installers != null)
// 			{
// 				List<CopyAndRun> NewInstallers = new List<CopyAndRun>();
// 				foreach (CopyAndRun Installer in Installers)
// 				{
// 					// if we want to execute the installer, then copy it over
// 					if (Installer.ShouldExecute())
// 					{
// 						CopyAndRun NewInstaller = new CopyAndRun(Installer);
// 
// 						// the downloaded location will be the location to install from later. if we blanked this out, the CopyOutputPath variable wouldn't get set appropriately
// 						NewInstaller.Copy = "file:" + LocalDirectoryForCopy;
// 						NewInstallers.Add(NewInstaller);
// 					}
// 				}
// 				Clone.Installers = NewInstallers.ToArray();
// 			}
// 
// 			if (CustomSdkInputFiles != null)
// 			{
// 				throw new AutomationException("CustomSdkInputFiles are not supported yet for QuickSwitch Sdks");
// 			}
// 
// 			return Clone;
// 		}

		#endregion


		static string[] ExtendedVariables =
		{
			"Project",
		};



		public bool SupportsPlatform(UnrealTargetPlatform Platform)
		{
			return Platforms.Contains(Platform);
		}
		public UnrealTargetPlatform[] GetPlatforms()
		{
			return Platforms.ToArray();
		}

		// for all platforms this supports, get all devices
		public DeviceInfo[] GetAllPossibleDevices()
		{
			List<DeviceInfo> AllDevices = new List<DeviceInfo>();
			foreach (var Pair in AutomationPlatforms)
			{
				DeviceInfo[] Devices = Pair.Value.GetDevices();
				if (Devices != null)
				{
					AllDevices.AddRange(Devices);
				}
			}
			return AllDevices.ToArray();
		}

		public DeviceInfo GetDevice(UnrealTargetPlatform Platform, string DeviceName)
		{
			DeviceInfo[] Devices = AutomationPlatforms[Platform].GetDevices();
			if (Devices != null)
			{
				return Array.Find(AutomationPlatforms[Platform].GetDevices(), y => (DeviceName == null && y.bIsDefault) || (DeviceName != null && string.Compare(y.Name, DeviceName, true) == 0));
			}
			return null;
		}

		public bool IsValid(UnrealTargetPlatform Platform, string DeviceName = null)
		{
			if (!SupportsPlatform(Platform))
			{
				return false;
			}

			if (!CheckForExtendedVariables(false))
			{
				// user canceled a choice
				return false;
			}

			if (Type == SdkType.Flash)
			{
				bool bIsValid = TurnkeyUtils.IsValueValid(Version, AutomationPlatforms[Platform].GetAllowedSoftwareVersions());
				// if we were passed a device, also check if this Sdk is valid for that device
				//				if (DeviceName != null)
				{
					DeviceInfo Device = GetDevice(Platform, DeviceName);
					bIsValid = bIsValid && Device != null && TurnkeyUtils.IsValueValid(Device.Type, AllowedFlashDeviceTypes);
				}
				return bIsValid;
			}
			else if (CustomSdkId != null)
			{
				// these are always valid
				return true;// AutomationPlatforms[Platform].IsCustomVersionValid(CustomSdkId, CustomSdkParams);
			}
			else
			{
				return TurnkeyUtils.IsValueValid(Version, AutomationPlatforms[Platform].GetAllowedSdks());
			}
		}

		public bool IsNeeded(UnrealTargetPlatform Platform, string DeviceName = null)
		{
			if (!SupportsPlatform(Platform))
			{
				return false;
			}

			if (!CheckForExtendedVariables(true))
			{
				// user canceled a choice
				return false;
			}

			if (Type == SdkType.Flash)
			{
				// look for default device, or matching device
				DeviceInfo Device = GetDevice(Platform, DeviceName);

				// if we don't have a device, we don't need it!
				if (Device == null)
				{
					return false;
				}

				return Device != null && !TurnkeyUtils.IsValueValid(Device.SoftwareVersion, AutomationPlatforms[Platform].GetAllowedSoftwareVersions());
			}
			// handle custom types, which can't use simple version checking
			else if (CustomSdkId != null)
			{
				// copy files down, which are needed to check if whatever is installed is up to date (only do it once)
				if (CustomVersionLocalFiles == null && CustomSdkInputFiles != null)
				{
					foreach (CopyAndRun CustomCopy in CustomSdkInputFiles)
					{
						if (CustomCopy.ShouldExecute())
						{
							if (CustomVersionLocalFiles != null)
							{
								throw new AutomationTool.AutomationException("CustomSdkInputFiles specified multiple locations to be copied for this platform, which is not supported (only one value of $(CustomVersionLocalFiles) allowed)");
							}

							CustomCopy.Execute();
							CustomVersionLocalFiles = TurnkeyUtils.ExpandVariables("$(CopyOutputPath)");
						}
					}
				}

				TurnkeyUtils.SetVariable("CustomVersionLocalFiles", CustomVersionLocalFiles);
				return AutomationPlatforms[Platform].IsCustomVersionNeeded(CustomSdkId, TurnkeyUtils.ExpandVariables(CustomSdkParams));
			}
			else
			{
				return !TurnkeyUtils.IsValueValid(AutomationPlatforms[Platform].GetInstalledSdk(), AutomationPlatforms[Platform].GetAllowedSdks());
			}
		}

		public void Install(UnrealTargetPlatform Platform)
		{
			if (!CheckForExtendedVariables(true))
			{
				// user canceled a choice
				return;
			}

			// custom sdk installation is enabled if ChosenSdk.CustomVersionId is !null
			if (CustomSdkId != null)
			{
				// in case the custom sdk modifies global env vars, make sure we capture them
				TurnkeyUtils.StartTrackingExternalEnvVarChanges();

				// re-set the path to local files
				TurnkeyUtils.SetVariable("CustomVersionLocalFiles", CustomVersionLocalFiles);
				AutomationPlatforms[Platform].CustomVersionUpdate(CustomSdkId, TurnkeyUtils.ExpandVariables(CustomSdkParams));

				TurnkeyUtils.EndTrackingExternalEnvVarChanges();
			}

			// now run any installers
			foreach (CopyAndRun Install in Installers)
			{
				if (Type == SdkType.BuildOnly)
				{
					// download subdir is based on platform 
					string SubDir = string.Format("{0}/{1}", Platform.ToString(), Version);
					Install.Execute(CopyExecuteSpecialMode.UsePermanentStorage, SubDir);

// 					// now that we are copied, write out a manifest to the root of the output location for later quick switching,
// 					// and not continue to install anything
// 					string ManifestPath = TurnkeyUtils.GetVariableValue("CopyOutputPath");
// 
// 					// if it's a file, then get it's directory
// 					if (File.Exists(ManifestPath))
// 					{
// 						ManifestPath = Path.GetDirectoryName(ManifestPath);
// 					}
// 					Directory.CreateDirectory(ManifestPath);
// 					ManifestPath = Path.Combine(ManifestPath, "TurnkeyQuickSwitch.xml");
// 
// 					// if the manifest already existed as downloaded, then there's no need to make one, we assume it was correct
// 					if (!File.Exists(ManifestPath))
// 					{
// 						TurnkeyManifest QuickSwitchManifest = new TurnkeyManifest();
// 
// 						// copy this Sdk to a new one
// 						SdkInfo NewSdk = CloneForQuickSwitch(Path.GetDirectoryName(ManifestPath));
// 						QuickSwitchManifest.SdkInfos = new SdkInfo[] { NewSdk };
// 
// 						// register the Sdk with th runtime so that we can install it without quitting
// 						TurnkeyManifest.AddCreatedSdk(NewSdk);
// 
// 						// save out a single switch manifest
// 						QuickSwitchManifest.Write(ManifestPath);
//					}
				}
				else
				{
					Install.Execute();
				}

				// grab where the Install copied the files

			}
		}







		private bool CheckForExtendedVariables(bool bIncludeCustomSdk)
		{
			foreach (string Var in ExtendedVariables)
			{
				if (!TurnkeyUtils.HasVariable(Var))
				{
					if (NeedsVariableToBeSet(Var, bIncludeCustomSdk))
					{
						// ask for it!
						TurnkeyUtils.Log("An Sdk ({0}) needs a extended variable ({1}) to be set that isn't set. Asking now...", DisplayName, Var);
						
						if (Var == "Project")
						{
							// we need a project, so choose one now
							List<string> Options = new List<string>();
							Options.Add("Engine");
							Options.Add("FortniteGame");

							// we force the user to select something
							int Choice = TurnkeyUtils.ReadInputInt("Select a Project:", Options, true);

							if (Choice == 0)
							{
								return false;
							}

							// set the projectname
							TurnkeyUtils.SetVariable(Var, Options[Choice - 1]);
						}
					}
				}
			}

			return true;
		}
		 
		private bool NeedsVariableToBeSet(string Variable, bool bIncludeCustomSdk)
		{
			string Format = "$(" + Variable + ")";
			bool bContainsVar = false;
			if (bIncludeCustomSdk && CustomSdkId != null)
			{
				bContainsVar = bContainsVar || (CustomSdkParams != null && CustomSdkParams.Contains(Format));
				if (CustomSdkInputFiles != null)
				{
					foreach (CopyAndRun CustomInput in CustomSdkInputFiles)
					{
						bContainsVar = bContainsVar || (CustomInput.CommandPath != null && CustomInput.CommandPath.Contains(Format));
						bContainsVar = bContainsVar || (CustomInput.CommandLine != null && CustomInput.CommandLine.Contains(Format));
					}
				}
			}
			if (Installers != null)
			{
				foreach (CopyAndRun Installer in Installers)
				{
					bContainsVar = bContainsVar || (Installer.CommandPath != null && Installer.CommandPath.Contains(Format));
					bContainsVar = bContainsVar || (Installer.CommandLine != null && Installer.CommandLine.Contains(Format));
				}
			}

			return bContainsVar;
		}




		internal void PostDeserialize()
		{
			// validate
			if (Version == null && CustomSdkId == null)
			{
				throw new AutomationTool.AutomationException("SdkInfo {0} needs to have either Version or CustomSdkId specified");
			}

			string[] PlatformStrings = PlatformString.Split(",".ToCharArray());
			Platforms = new List<UnrealTargetPlatform>();
			AutomationPlatforms = new Dictionary<UnrealTargetPlatform, Platform>();
			// parse into runtime usable values
			foreach (string Plat in PlatformStrings)
			{
				if (UnrealTargetPlatform.IsValidName(Plat))
				{
					UnrealTargetPlatform TargetPlat = UnrealTargetPlatform.Parse(Plat);
					Platforms.Add(TargetPlat);
					AutomationPlatforms.Add(TargetPlat, AutomationTool.Platform.Platforms[new TargetPlatformDescriptor(TargetPlat)]);
				}
			}

			if (CustomSdkInputFiles != null)
			{
				Array.ForEach(CustomSdkInputFiles, x => x.PostDeserialize());
			}

			if (Installers != null)
			{
				Array.ForEach(Installers, x => x.PostDeserialize());
			}
		}


		private string Indent(int Num)
		{
			return new string(' ', Num);
		}

		public override string ToString()
		{
			return ToString(0);
		}
		public string ToString(int BaseIndent)
		{
			StringBuilder Builder = new StringBuilder();
			Builder.AppendLine("{1}Name: {0}", DisplayName, Indent(BaseIndent));
			Builder.AppendLine("{1}Version: {0}", Version, Indent(BaseIndent + 2));
			Builder.AppendLine("{1}Platform: {0}", string.Join(",", Platforms), Indent(BaseIndent + 2));
			Builder.AppendLine("{1}Type: {0}", Type, Indent(BaseIndent + 2));
			if (Type == SdkType.Flash)
			{
				Builder.AppendLine("{1}AllowedFlashDeviceTypes: {0}", AllowedFlashDeviceTypes, Indent(BaseIndent + 2));
			}
			if (CustomSdkId != null)
			{
				Builder.AppendLine("{0}CustomSdk:", Indent(BaseIndent + 2));
				Builder.AppendLine("{1}CusttomSdkId: {0}", CustomSdkId, Indent(BaseIndent + 4));
				Builder.AppendLine("{1}CustomSdkParams: {0}", CustomSdkParams, Indent(BaseIndent + 4));
				Builder.AppendLine("{0}Input File Sources:", Indent(BaseIndent + 4));
				foreach (CopyAndRun CustomInputFiles in CustomSdkInputFiles)
				{
					Builder.AppendLine("{1}HostPlatform: {0}", CustomInputFiles.Platform, Indent(BaseIndent + 6));
					Builder.AppendLine("{1}FileSource: {0}", CustomInputFiles.Copy, Indent(BaseIndent + 8));
				}
			}
			Builder.AppendLine("{0}Installers:", Indent(BaseIndent + 2));
			foreach (CopyAndRun Installer in Installers)
			{
				Builder.AppendLine("{1}HostPlatform: {0}", Installer.Platform, Indent(BaseIndent + 4));
				Builder.AppendLine("{1}CopyOperation: {0}", Installer.Copy, Indent(BaseIndent + 6));
				Builder.AppendLine("{1}InstallerPath: {0}", Installer.CommandPath, Indent(BaseIndent + 6));
				Builder.AppendLine("{1}InstallerCommandLine: {0}", Installer.CommandLine, Indent(BaseIndent + 6));
			}
			return Builder.ToString().TrimEnd();

		}
	}
}
