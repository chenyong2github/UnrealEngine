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
	public class TurnkeyManifest
	{
		static private string StandardManifestName = "TurnkeyManifest.xml";

		public SdkInfo[] SdkInfos = null;

		[XmlArrayItem(ElementName = "Manifest")]
		public string[] AdditionalManifests = null;

		public SavedSetting[] SavedSettings = null;

		internal void PostDeserialize()
		{
			if (SdkInfos != null)
			{
				Array.ForEach(SdkInfos, x => x.PostDeserialize());
			}

			// load any settings set in the xml
			if (SavedSettings != null)
			{
				Array.ForEach(SavedSettings, x => TurnkeyUtils.SetVariable(x.Variable, x.Value));
			}
		}

		static List<SdkInfo> DiscoveredSdks = null;

		public static List<SdkInfo> GetDiscoveredSdks()
		{
			// hunt down manifests if needed
			DiscoverManifests();

			return DiscoveredSdks;
		}
		public static void AddCreatedSdk(SdkInfo Sdk)
		{
			DiscoveredSdks.Add(Sdk);
		}

		public static void DiscoverManifests()
		{
			// this is the indicator that we haven't run yet
			if (DiscoveredSdks == null)
			{
				DiscoveredSdks = new List<SdkInfo>();

				// @todo turnkey: look in the Sdk Download directory!~
				if (TurnkeySettings.HasSetUserSetting("User_QuickSwitchSdkLocation"))
				{
					DiscoveredSdks.AddRange(GetSdkInfosFromProvider("file:$(User_QuickSwitchSdkLocation)/*/*/TurnkeyQuickSwitch.xml"));
				}


				// some known standard locations to the chain with
				DiscoveredSdks.AddRange(GetSdkInfosFromProvider("file:$(EngineDir)/Build/TurnkeyManifest.xml"));
				if (Environment.GetEnvironmentVariable("UE_SDKS_ROOT") != null)
				{
					DiscoveredSdks.AddRange(GetSdkInfosFromProvider("file:$(UE_SDKS_ROOT)/TurnkeyManifest.xml"));
				}
				if (Environment.GetEnvironmentVariable("UE_STUDIO_TURNKEY_LOCATION") != null)
				{
					DiscoveredSdks.AddRange(GetSdkInfosFromProvider("$(UE_STUDIO_TURNKEY_LOCATION)"));
				}
			}
		}

		public static List<SdkInfo> GetSdkInfosFromProvider(string EnumerationOperation)
		{
			// gather the 
			List<SdkInfo> Sdks = new List<SdkInfo>();
			LoadManifestsFromProvider(EnumerationOperation).ForEach(x =>
			{
				if (x.SdkInfos != null)
				{
					Sdks.AddRange(x.SdkInfos);
				}
			});

			return Sdks;
		}

		public static List<TurnkeyManifest> LoadManifestsFromProvider(string EnumerationOperation)
		{
			List<TurnkeyManifest> Manifests = new List<TurnkeyManifest>();

			string[] EnumeratedSources = CopyProvider.ExecuteEnumerate(EnumerationOperation);
			if (EnumeratedSources != null)
			{
				foreach (string Source in EnumeratedSources)
				{
					// retrieve the actual file locally
					string LocalManifestPath = CopyProvider.ExecuteCopy(Source);

					// if it doesn't exist, that's okay
					if (LocalManifestPath == null)
					{
						continue;
					}

					string ThisManifestDir = Path.GetDirectoryName(Source);

					// if a directory is returned, look for the standardized manifest name, as we have not much else we can do with a directory
					if (LocalManifestPath.EndsWith("/") || LocalManifestPath.EndsWith("\\"))
					{
						LocalManifestPath = LocalManifestPath + StandardManifestName;
						if (!File.Exists(LocalManifestPath))
						{
							continue;
						}

						// if we had a directory, then ThisManifestDir above would have the parent of this directory, not this directory itself, fix it
						ThisManifestDir = Source;
					}

// 					TurnkeyUtils.Log("Info: Setting ManifestDir to {0}", ThisManifestDir);

					// allow this manifest's directory to be used in further copy, but needs to be in a stack
					string PrevManifestDir = TurnkeyUtils.SetVariable("ThisManifestDir", ThisManifestDir);

//					TurnkeyUtils.Log("Info: Processing manifest: {0}", LocalManifestPath);

					// read in the .xml
					TurnkeyManifest Manifest = Utils.ReadClass<TurnkeyManifest>(LocalManifestPath);
					Manifest.PostDeserialize();

					Manifests.Add(Manifest);

					// now process any more referenced Sdks
					if (Manifest.AdditionalManifests != null)
					{
						foreach (string ManifestPath in Manifest.AdditionalManifests)
						{
							// now recurse on the extra manifests
							Manifests.AddRange(LoadManifestsFromProvider(ManifestPath));
						}
					}
					TurnkeyUtils.SetVariable("ThisManifestDir", PrevManifestDir);
// 					TurnkeyUtils.Log("Info: Resetting ManifestDir to {0}", PrevManifestDir);
				}
			}

			return Manifests;
		}

		public void Write(string Path)
		{
			Utils.WriteClass<TurnkeyManifest>(this, Path, "");
		}
	}
}
