// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Xml.Serialization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;
using UnrealBuildTool;
using AutomationTool;

namespace Turnkey
{
	public class TurnkeyManifest
	{
		static private string StandardManifestName = "TurnkeyManifest.xml";

		public SdkInfo[] SdkInfos = null;
		public BuildSource[] BuildSources = null;

		[XmlArrayItem(ElementName = "Manifest")]
		public string[] AdditionalManifests = null;

		public SavedSetting[] SavedSettings = null;

		internal void PostDeserialize()
		{
			// load any settings set in the xml
			if (SavedSettings != null)
			{
				Array.ForEach(SavedSettings, x => TurnkeyUtils.SetVariable(x.Variable, x.Value));
			}

			if (BuildSources != null)
			{
				Array.ForEach(BuildSources, x => x.PostDeserialize());
			}

			if (SdkInfos != null)
			{
				Array.ForEach(SdkInfos, x => x.PostDeserialize());

				SdkInfo[] ExpandedSdks = Array.FindAll(SdkInfos, x => x.Expansion != null && x.Expansion.Length > 0);

				if (ExpandedSdks.Length > 0)
				{
					// we will only have one active Expansion
					List<SdkInfo> NewSdks = SdkInfos.Where(x => !ExpandedSdks.Contains(x)).ToList();

					// now expand
					foreach (SdkInfo Sdk in ExpandedSdks)
					{
						// we only allow one expansion active
						if (Sdk.Expansion.Length > 1)
						{
							throw new AutomationTool.AutomationException("SdkInfo {0} had multiple expansions active on this platform. THat is not allowed", Sdk.DisplayName);
						}

						// now enumerate and get the values
						List<List<string>> Expansions = new List<List<string>>();
						string[] ExpandedInstallerResults = CopyProvider.ExecuteEnumerate(Sdk.Expansion[0].Copy, Expansions);

						// expansion may not work
						if (ExpandedInstallerResults == null)
						{
							continue;
						}

						if (Expansions.Count != ExpandedInstallerResults.Length)
						{
							throw new AutomationException(string.Format("Bad expansions output from CopyProvider ({0} returned {1} count, expected {2}, from {3}",
								Sdk.Expansion[0].Copy, Expansions.Count, ExpandedInstallerResults.Length, string.Join(", ", ExpandedInstallerResults)));
						}

						// @todo turnkey: this will be uysed in Builds also, make it a function with a lambda
						// make a new SdkInfo for each expansion
						int MaxIndex = 0;
						for (int ResultIndex=0; ResultIndex < ExpandedInstallerResults.Length; ResultIndex++)
						{
							// set temp variables potentially used somewhere in the Sdks
							TurnkeyUtils.SetVariable("Expansion", ExpandedInstallerResults[ResultIndex]);
							for (int ExpansionIndex = 0; ExpansionIndex < Expansions[ResultIndex].Count; ExpansionIndex++)
							{
								TurnkeyUtils.SetVariable(ExpansionIndex.ToString(), Expansions[ResultIndex][ExpansionIndex]);
							}

							// remember how many we beed to unset
							MaxIndex = Math.Max(MaxIndex, Expansions[ResultIndex].Count);

							// make a new Sdk for each result in the expansion
							NewSdks.Add(Sdk.CloneForExpansion());
						}

						// clear temp variables
						TurnkeyUtils.ClearVariable("Expansion");
						for (int Index = 0; Index <= MaxIndex; Index++)
						{
							TurnkeyUtils.ClearVariable(Index.ToString());
						}
					}

					SdkInfos = NewSdks.ToArray();
				}
			}
		}

		static List<SdkInfo> DiscoveredSdks = null;
		static List<BuildSource> DiscoveredBuildSources = null;

		public static List<SdkInfo> GetDiscoveredSdks()
		{
			// hunt down manifests if needed
			DiscoverManifests();

			return DiscoveredSdks;
		}

		public static List<BuildSource> GetDiscoveredBuildSources()
		{
			// hunt down manifests if needed
			DiscoverManifests();

			return DiscoveredBuildSources;
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
				DiscoveredBuildSources = new List<BuildSource>();

				// known location to branch from, this will include a few other locations
				string RootOperation = "file:$(EngineDir)/Build/Turnkey/TurnkeyManifest.xml";

				LoadManifestsFromProvider(RootOperation).ForEach(x =>
				{
					if (x.SdkInfos != null)
					{
						DiscoveredSdks.AddRange(x.SdkInfos);
					}
					if (x.BuildSources != null)
					{
						DiscoveredBuildSources.AddRange(x.BuildSources);
					}
				});
			}
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

					string ThisManifestDir = Path.GetDirectoryName(Source).Replace("\\", "/");

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
