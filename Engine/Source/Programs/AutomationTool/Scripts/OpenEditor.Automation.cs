// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool
{

	[Help("Opens the specified project.")]
	[Help("project=<QAGame>", "Project to open. Will search current path and paths in ueprojectdirs. If omitted will open vanilla UE4Editor")]
	class OpenEditor: BuildCommand
	{
		// exposed as a property so projects can derive and set this directly
		public string ProjectName { get; set; }

		public OpenEditor()
		{
		}
		
		public override ExitCode Execute()
		{
			string EditorPath = HostPlatform.Current.GetUE4ExePath("UE4Editor");

			string EditorArgs = "";

			ProjectName = ParseParamValue("project", ProjectName);

			if (!String.IsNullOrEmpty(ProjectName))
			{
				FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

				if (ProjectFile == null)
				{
					throw new AutomationException("Unable to find uproject file for {0}", ProjectName);
				}

				EditorArgs = ProjectFile.FullName;
			}
	
			Run(EditorPath, EditorArgs, null, ERunOptions.NoWaitForExit);

			return ExitCode.Success;
		}
	}

}