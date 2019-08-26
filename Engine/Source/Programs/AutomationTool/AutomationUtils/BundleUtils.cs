// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using AutomationTool;
using UnrealBuildTool;

namespace AutomationUtils.Automation
{
	public class BundleUtils
	{
		public interface IReadOnlyBundleSettings
		{
			string Name { get; }
			List<string> Tags { get; }
			bool bContainsShaderLibrary { get; }
		}


		public class BundleSettings : IReadOnlyBundleSettings
		{
			public string Name { get; set; }
			public List<string> Tags { get; set; }
			public List<BundleSettings> Children { get; set; }
			public List<string> FileRegex { get; set; }
			public bool bFoundParent { get; set; }
			public bool bContainsShaderLibrary { get; set; }
			public int Priority { get; set; }
			public string ExecFileName { get; set; }
		};
		public static bool LoadBundleConfig(string BundleIniFile, ref Dictionary<string, BundleSettings> Bundles)
		{
			if ( System.IO.File.Exists(BundleIniFile) == false )
			{
				CommandUtils.LogWarning("Unable to find bundle config ini file  {0}", BundleIniFile);
				return false;
			}

			FileReference BundleFileReference = new FileReference(BundleIniFile);

			ConfigHierarchy BundleConfig = new ConfigHierarchy(new ConfigFile[] { new ConfigFile(BundleFileReference) });
			int PriorityCounter = 0;
			foreach (string SectionName in BundleConfig.SectionNames)
			{
				BundleSettings Bundle = new BundleSettings();
				Bundle.Name = SectionName;
				Bundle.Priority = PriorityCounter;
				++PriorityCounter;
				{
					string ExecFileName;
					BundleConfig.GetString(SectionName, "ExecFileName", out ExecFileName);
					Bundle.ExecFileName = ExecFileName;
				}
				{
					List<string> Tags;
					BundleConfig.GetArray(SectionName, "Tags", out Tags);
					Bundle.Tags = Tags;
				}
				{
					List<string> FileRegex;
					BundleConfig.GetArray(SectionName, "FileRegex", out FileRegex);
					Bundle.FileRegex = FileRegex;
				}
				{
					bool bContainsShaderLibrary;
					BundleConfig.GetBool(SectionName, "ContainsShaderLibrary", out bContainsShaderLibrary);
					Bundle.bContainsShaderLibrary = bContainsShaderLibrary;
				}
				if (Bundle.Tags == null)
				{
					Bundle.Tags = new List<string>();
				}

				Bundles.Add(SectionName, Bundle);
			}

			return true;
		}
	}
}
