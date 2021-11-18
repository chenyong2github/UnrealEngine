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
		
		public override void CleanProjectFiles(DirectoryReference InMasterProjectDirectory, string InMasterProjectName, DirectoryReference InIntermediateProjectFilesPath)
		{
			FileReference MasterProjDeleteFilename = FileReference.Combine(InMasterProjectDirectory, InMasterProjectName + ".wkst");
			if (FileReference.Exists(MasterProjDeleteFilename))
			{
				File.Delete(MasterProjDeleteFilename.FullName);
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
			WorksetDataContent.Append("AddWorkset \"" + MasterProjectName + ".wkst\" \"" + MasterProjectPath + "\"" + ProjectFileGenerator.NewLine);
			
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
			AddProjectsFunction(MasterProjectName, RootFolder.SubFolders);
			
			string ProjectName = MasterProjectName;
			string FilePath = MasterProjectPath + "/" + ProjectName + ".wkst";
			
			bSuccess = WriteFileIfChanged(FilePath, WorksetDataContent.ToString(), new UTF8Encoding());
			
			return bSuccess;
		}
		
		protected override bool WriteMasterProjectFile(ProjectFile? UBTProject, PlatformProjectGeneratorCollection PlatformProjectGenerators)
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