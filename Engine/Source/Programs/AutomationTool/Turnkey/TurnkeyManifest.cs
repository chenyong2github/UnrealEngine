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

		[XmlElement("FileSource")]
		public FileSource[] FileSources = null;

		public BuildSource[] BuildSources = null;

		[XmlArrayItem(ElementName = "Manifest")]
		public string[] AdditionalManifests = null;

		public SavedSetting[] SavedSettings = null;

		internal void PostDeserialize()
		{
			// load any settings set in the xml
			if (SavedSettings != null)
			{
				Array.ForEach(SavedSettings, x => TurnkeyUtils.SetVariable(x.Variable, TurnkeyUtils.ExpandVariables(x.Value)); );
			}

			if (BuildSources != null)
			{
				Array.ForEach(BuildSources, x => x.PostDeserialize());
			}

			if (FileSources != null)
			{
				Array.ForEach(FileSources, x => x.PostDeserialize());

				// look for list expansion and fixup file sources (filexpansion will come later, on demand, to improve speed)
				List<FileSource> ExpandedSources = new List<FileSource>();
				List<FileSource> NonExpandedSources = new List<FileSource>();

				foreach (FileSource Source in FileSources)
				{
					List<FileSource> Expansions = Source.ConditionalExpandLists();
					if (Expansions != null)
					{
						ExpandedSources.AddRange(Expansions);
					}
					else
					{
						// add to new list, instead of removing from FileSources since we are iterating
						NonExpandedSources.Add(Source);
					}
				}

				// now combine them and replace FileSources
				NonExpandedSources.AddRange(ExpandedSources);
				FileSources = NonExpandedSources.ToArray();
			}
		}

		static List<FileSource> DiscoveredFileSources = null;
		static List<BuildSource> DiscoveredBuildSources = null;
		public static List<UnrealTargetPlatform> GetPlatformsWithSdks()
		{
			DiscoverManifests();

			// this can handle FileSources with pending Expansions, so get the unique set of Platforms represented by pre or post expansion sources
			// skip over Misc type, since this wants just Sdk types
			List<UnrealTargetPlatform> Platforms = new List<UnrealTargetPlatform>();
			DiscoveredFileSources.FindAll(x => x.Type != FileSource.SourceType.Misc).ForEach(x => Platforms.AddRange(x.GetPlatforms()));

			return Platforms.Distinct().ToList();
		}


		public static List<FileSource> GetAllDiscoveredFileSources()
		{
			return FilterDiscoveredFileSources(null, null);
		}
		
		public static List<FileSource> FilterDiscoveredFileSources(UnrealTargetPlatform? Platform, FileSource.SourceType? Type)
		{
			// hunt down manifests if needed
			DiscoverManifests();

			List<FileSource> Matching;
			if (Platform == null && Type == null)
			{
				Matching = DiscoveredFileSources;
			}
			else
			{
				// if the platform is from expansion (possible with AutoSDKs in particular), then we need to expand it in case this platform will be matched
				Matching = DiscoveredFileSources.FindAll(x => (Platform == null || (x.PlatformString?.StartsWith("$(")).GetValueOrDefault() || x.SupportsPlatform(Platform.Value)) && (Type == null || x.Type == Type.Value));
			}

			// get the set that needs to expand
			List<FileSource> WorkingSet = Matching.FindAll(x => x.NeedsFileExpansion());

			// remove them, then we add expansions below
			DiscoveredFileSources = DiscoveredFileSources.FindAll(x => !WorkingSet.Contains(x));

			foreach (FileSource Source in WorkingSet)
			{
				DiscoveredFileSources.AddRange(Source.ExpandCopySource());
			}

			if (Platform == null && Type == null)
			{
				return DiscoveredFileSources;
			}
			return DiscoveredFileSources.FindAll(x => (Platform == null || x.SupportsPlatform(Platform.Value)) && (Type == null || x.Type == Type.Value));
		}

		public static List<BuildSource> GetDiscoveredBuildSources()
		{
			// hunt down manifests if needed
			DiscoverManifests();

			return DiscoveredBuildSources;
		}

		public static void DiscoverManifests()
		{
			// this is the indicator that we haven't run yet
			if (DiscoveredFileSources == null)
			{
				DiscoveredFileSources = new List<FileSource>();
				DiscoveredBuildSources = new List<BuildSource>();

				// known location to branch from, this will include a few other locations
				string RootOperation = "file:$(EngineDir)/Build/Turnkey/TurnkeyManifest.xml";

				LoadManifestsFromProvider(RootOperation).ForEach(x =>
				{
					if (x.FileSources != null)
					{
						DiscoveredFileSources.AddRange(x.FileSources);
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
