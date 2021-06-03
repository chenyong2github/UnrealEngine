// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace UnrealBuildTool
{
	/// <summary>
	/// General deploy support functions, exported for UAT
	/// </summary>
	public class DeployExports
	{
		/// <summary>
		/// The max length of the deploy folder name.
		/// </summary>
		public const int DeployFolderMaxLength = 50;

		/// <summary>
		/// Gets the default deploy folder name in the format RootDirectoryName-ShortProjectName-UserName-MachineNameHash,
		/// truncated to DeployFolderMaxLength characters, with dots '.' and whitespace ' ' stripped.
		/// </summary>
		/// <param name="ShortProjectName">Project to deploy</param>
		/// <returns>The default deploy folder name.</returns>
		public static string GetDefaultDeployFolder(string ShortProjectName)
		{
			string DeployFolder = string.Format("{0}-{1}-{2}-{3}",
				UnrealBuildTool.RootDirectory.GetDirectoryName(),
				ShortProjectName,
				Environment.UserName,
				Environment.MachineName.GetHashCode().ToString()).ToLower();

			DeployFolder = DeployFolder.Replace(" ", "");
			DeployFolder = DeployFolder.Replace(".", "");

			if (DeployFolder.Length > DeployFolderMaxLength)
			{
				DeployFolder = DeployFolder.Substring(0, DeployFolderMaxLength);
				char[] CharsToTrim = {'-'};
				DeployFolder = DeployFolder.Trim(CharsToTrim);
			}

			return DeployFolder;
		}
	}
}
