// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using EpicGames.Core;

namespace UnrealBuildTool
{
	class EddieProjectFileGenerator : ProjectFileGenerator
	{
		public EddieProjectFileGenerator(FileReference? InOnlyGameProject)
			: base(InOnlyGameProject)
		{
		}
		
		override public string ProjectFileExtension
		{
			get
			{
				return ".wkst";
			}
		}
		
		public override void CleanProjectFiles(DirectoryReference InPrimaryProjectDirectory, string InPrimaryProjectName, DirectoryReference InIntermediateProjectFilesPath)
		{
			FileReference PrimaryProjDeleteFilename = FileReference.Combine(InPrimaryProjectDirectory, InPrimaryProjectName + ".wkst");
			if (FileReference.Exists(PrimaryProjDeleteFilename))
			{
				File.Delete(PrimaryProjDeleteFilename.FullName);
			}

			// Delete the project files folder
			if (DirectoryReference.Exists(InIntermediateProjectFilesPath))
			{
				try
				{
					Directory.Delete(InIntermediateProjectFilesPath.FullName, true);
				}
				catch (Exception Ex)
				{
					Log.TraceInformation("Error while trying to clean project files path {0}. Ignored.", InIntermediateProjectFilesPath);
					Log.TraceInformation("\t" + Ex.Message);
				}
			}
		}
		
		protected override ProjectFile AllocateProjectFile(FileReference InitFilePath, DirectoryReference BaseDir)
		{
			return new EddieProjectFile(InitFilePath, BaseDir);
		}
		
		private bool WriteEddieWorkset()
		{
			bool bSuccess = false;
			
			StringBuilder WorksetDataContent = new StringBuilder();
			WorksetDataContent.Append("# @Eddie Workset@" + ProjectFileGenerator.NewLine);
			WorksetDataContent.Append("AddWorkset \"" + PrimaryProjectName + ".wkst\" \"" + PrimaryProjectPath + "\"" + ProjectFileGenerator.NewLine);
			
			System.Action< String /*Path*/, List<PrimaryProjectFolder> /* Folders */>? AddProjectsFunction = null;
			AddProjectsFunction = (Path, FolderList) =>
				{
					foreach (PrimaryProjectFolder CurFolder in FolderList)
					{
						String NewPath = Path + "/" + CurFolder.FolderName;
						WorksetDataContent.Append("AddFileGroup \"" + NewPath + "\" \"" + CurFolder.FolderName + "\"" + ProjectFileGenerator.NewLine);

						AddProjectsFunction!(NewPath, CurFolder.SubFolders);

						foreach (ProjectFile CurProject in CurFolder.ChildProjects)
						{
							EddieProjectFile? EddieProject = CurProject as EddieProjectFile;
							if (EddieProject != null)
							{
								WorksetDataContent.Append("AddFile \"" + EddieProject.ToString() + "\" \"" + EddieProject.ProjectFilePath + "\"" + ProjectFileGenerator.NewLine);
							}
						}

						WorksetDataContent.Append("EndFileGroup \"" + NewPath + "\"" + ProjectFileGenerator.NewLine);
					}
				};
			AddProjectsFunction(PrimaryProjectName, RootFolder.SubFolders);
			
			string ProjectName = PrimaryProjectName;
			string FilePath = PrimaryProjectPath + "/" + ProjectName + ".wkst";
			
			bSuccess = WriteFileIfChanged(FilePath, WorksetDataContent.ToString(), new UTF8Encoding());
			
			return bSuccess;
		}
		
		protected override bool WritePrimaryProjectFile(ProjectFile? UBTProject, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			return WriteEddieWorkset();
		}
		
		protected override void ConfigureProjectFileGeneration(string[] Arguments, ref bool IncludeAllPlatforms)
		{
			// Call parent implementation first
			base.ConfigureProjectFileGeneration(Arguments, ref IncludeAllPlatforms);

			if (bGeneratingGameProjectFiles)
			{
				bIncludeEngineSource = true;
			}
		}
	}
}