// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System.Linq;
using System.IO;
using System;

namespace UnrealBuildTool
{
	/// <summary>
	/// Base class to handle deploy of a target for a given platform
	/// </summary>
	abstract class UEBuildDeploy
	{
		protected bool CopyFilesRecursively(string FromDir, string ToDir)
		{
			var FromDirInfo = new DirectoryInfo(FromDir);

			if (!FromDirInfo.Exists)
			{
				Log.TraceError($"Cannot copy files, source directory not found: {FromDirInfo.FullName}");
				return false;
			}

			DirectoryInfo[] DirsToCopy = FromDirInfo.GetDirectories();

			Directory.CreateDirectory(ToDir);

			foreach (FileInfo FromFile in FromDirInfo.GetFiles())
			{
				try
				{
					FromFile.CopyTo(Path.Combine(ToDir, FromFile.Name), true);
				} catch (Exception ex)
				{
					Log.TraceError($"Error while copying {FromFile.Name} from {FromDir} to {ToDir}: {ex}");
					return false;
				}
			}

			foreach (DirectoryInfo SubDir in DirsToCopy)
			{
				CopyFilesRecursively(SubDir.FullName, Path.Combine(ToDir, SubDir.Name));
			}
			return true;
		}

		/// <summary>
		/// Prepare the target for deployment
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		/// <returns>True if successful, false if not</returns>
		public virtual bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			if (Receipt.IsTestTarget)
			{
				ReceiptProperty? TestResourcesFolder = Receipt.AdditionalProperties.FirstOrDefault(Prop => Prop.Name == "ResourcesFolder");
				if (TestResourcesFolder != null && Receipt.Launch != null)
				{
					DirectoryInfo ResourceDirectoryInfo = new DirectoryInfo(TestResourcesFolder.Value);
					if (ResourceDirectoryInfo.Exists)
					{
						return CopyFilesRecursively(TestResourcesFolder.Value, Path.Combine(Receipt.Launch.Directory.FullName, ResourceDirectoryInfo.Name));
					}
				}
			}
			return true;
		}
	}
}
