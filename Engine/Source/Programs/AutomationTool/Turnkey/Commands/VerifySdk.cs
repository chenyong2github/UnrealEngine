using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnrealBuildTool;

namespace Turnkey.Commands
{
	class VerifySdk : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			string DeviceName = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);
			bool bUpdateIfNeeded = TurnkeyUtils.ParseParam("UpdateIfNeeded", CommandOptions);
			bool bUnattended = TurnkeyUtils.ParseParam("Unattended", CommandOptions);

			List<UnrealTargetPlatform> ChosenPlatforms = TurnkeyUtils.GetPlatformsFromCommandLineOrUser(CommandOptions, UnrealTargetPlatform.GetValidPlatforms().ToList());

			if (ChosenPlatforms == null)
			{
				return;
			}

			TurnkeyUtils.Log("Installed Sdk validity:");
			TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;

			// check all the platforms
			foreach (UnrealTargetPlatform Platform in ChosenPlatforms)
			{
				// get the platform object
				AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.Platforms[new AutomationTool.TargetPlatformDescriptor(Platform)];

				SdkInfo.LocalAvailability LocalState = SdkInfo.GetLocalAvailability(AutomationPlatform);

				if ((LocalState & (SdkInfo.LocalAvailability.AutoSdk_PlatformExists | SdkInfo.LocalAvailability.InstalledSdk_ValidVersionExists)) == 0)
				{
					TurnkeyUtils.Log("{0}: No AutoSdk or Installed Sdk found matching {1}", Platform, AutomationPlatform.GetAllowedSdks());
					TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
				}
				else
				{
					TurnkeyUtils.Log("{0}: Valid! [{1}]", Platform, (LocalState & (SdkInfo.LocalAvailability.AutoSdk_PlatformExists | SdkInfo.LocalAvailability.InstalledSdk_ValidVersionExists)).ToString());
					//					TurnkeyUtils.Log("{0}: Valid [Installed: '{1}', Required: '{2}']", Platform, PlatformObject.GetInstalledSdk(), PlatformObject.GetAllowedSdks());
				}

				if (bUpdateIfNeeded && (TurnkeyUtils.ExitCode != AutomationTool.ExitCode.Success))
				{
					TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;

					bool bAttemptAutoSdkSetup = false;
					bool bSetupEnvVarAfterInstall = false;
					if (LocalState.HasFlag(SdkInfo.LocalAvailability.AutoSdk_VariableExists))
					{
						bAttemptAutoSdkSetup = true;
					}
					else
					{
						if (!bUnattended)
						{
							// @todo turnkey: help set up UE_SDKS_ROOT if it's not there
							string Response = TurnkeyUtils.ReadInput("AutoSdks are not setup, but your studio has support. Would you like to set it up now? [Y/n]", "Y");
							if (string.Compare(Response, "Y", true) == 0)
							{
								bAttemptAutoSdkSetup = true;
								bSetupEnvVarAfterInstall = true;
							}
						}
					}

					if (bAttemptAutoSdkSetup)
					{
						TurnkeyUtils.Log("{0}: AutoSdk is setup on this compter, will look for available AutoSdk to download", Platform);

						List<SdkInfo> MatchingAutoSdk = SdkInfo.FindMatchingSdks(AutomationPlatform, SdkInfo.SdkType.AutoSdk, bSelectSingleBest: true);

						if (MatchingAutoSdk.Count == 0)
						{
							// no matching AutoSdk found - will fall through to look for full install
							if (bSetupEnvVarAfterInstall)
							{
								TurnkeyUtils.Log("{0}: Unable to find a matching AutoSdk, skipping AutoSdk setup", Platform);
							}
						}
						else
						{
							// make sure this is unset so that we can know if it worked or not after install
							TurnkeyUtils.ClearVariable("CopyOutputPath");

							// now download it (AutoSdks don't "install") on download
							// @todo turnkey: handle errors, handle p4 going to wrong location, handle one Sdk for multiple platforms
							MatchingAutoSdk[0].Install(Platform);

							if (bSetupEnvVarAfterInstall)
							{
								// @todo turnkey - have studio settings 

								// this is where we synced the Sdk to
								string InstalledRoot = TurnkeyUtils.GetVariableValue("CopyOutputPath");

								// failed to install, nothing we can do
								if (string.IsNullOrEmpty(InstalledRoot))
								{
									TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
									continue;
								}

								// walk up to one above Host* directory
								DirectoryInfo AutoSdkSearch;
								if (Directory.Exists(InstalledRoot))
								{
									AutoSdkSearch = new DirectoryInfo(InstalledRoot);
								}
								else
								{
									AutoSdkSearch = new FileInfo(InstalledRoot).Directory;
								}
								while (AutoSdkSearch.Name != "Host" + HostPlatform.Current.HostEditorPlatform.ToString())
								{
									AutoSdkSearch = AutoSdkSearch.Parent;
								}

								// now go one up to the parent of Host
								AutoSdkSearch = AutoSdkSearch.Parent;

								string Response = TurnkeyUtils.ReadInput("Enter directory for root of AutoSdks. Use detected value, or enter another:", AutoSdkSearch.FullName);
								if (string.IsNullOrEmpty(Response))
								{
									continue;
								}

								// set the env var, globally
								TurnkeyUtils.StartTrackingExternalEnvVarChanges();
								Environment.SetEnvironmentVariable("UE_SDKS_ROOT", Response);
								Environment.SetEnvironmentVariable("UE_SDKS_ROOT", Response, EnvironmentVariableTarget.User);
								TurnkeyUtils.EndTrackingExternalEnvVarChanges();
							}

							// @todo turnkey - re-run detection again

							continue;
						}
					}

					if (!bUnattended)
					{
						string Response = TurnkeyUtils.ReadInput("Your Sdk installation is not up to date. Would you like to install a valid Sdk? [Y/n]", "Y");
						if (string.Compare(Response, "Y", true) != 0)
						{
							continue;
						}
					}

					// at this point, AutoSdk isn't viable, so look for a full install
					List<SdkInfo> MatchingInstallableSdks = SdkInfo.FindMatchingSdks(AutomationPlatform, SdkInfo.SdkType.BuildOnly, bSelectSingleBest: bUnattended);
					if (MatchingInstallableSdks.Count == 0)
					{
						MatchingInstallableSdks = SdkInfo.FindMatchingSdks(AutomationPlatform, SdkInfo.SdkType.Full, bSelectSingleBest: bUnattended);
					}

					if (MatchingInstallableSdks.Count == 0)
					{
						TurnkeyUtils.Log("ERROR: {0}: Unable top find any Sdks that could be installed");
						TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
					}

					SdkInfo SdkToInstall = MatchingInstallableSdks[0];

					// if there are multiple, ask the user
					if (MatchingInstallableSdks.Count > 1)
					{
						int Choice = TurnkeyUtils.ReadInputInt("Multiple Sdks found that could be installed. Please select one:", MatchingInstallableSdks.Select(x => x.DisplayName).ToList(), true);
						if (Choice == 0)
						{
							TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Error_SDKNotFound;
							continue;
						}

						SdkToInstall = MatchingInstallableSdks[Choice - 1];
					}

					// finall install the best or chosen Sdk
					SdkToInstall.Install(Platform);

// 					if (bInstallSdk)
// 					{
// 						// reset exit code
// 						TurnkeyUtils.ExitCode = AutomationTool.ExitCode.Success;
// 
// 						InstallSdk InstallCommand = new InstallSdk();
// 						InstallCommand.InternalExecute(new string[] { "-platform=" + Platform, "-BestAvailable" });
// 					}
				}
			}
		}
	}
}
