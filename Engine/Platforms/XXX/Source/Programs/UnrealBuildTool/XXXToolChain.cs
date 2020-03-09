// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class XXXToolChain : UEToolChain
	{
		public XXXToolChain()
		{
		}

		public override CPPOutput CompileCPPFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, string ModuleName, IActionGraphBuilder Graph)
		{
			// Create a compile action for each source file.
			CPPOutput Result = new CPPOutput();
			foreach (FileItem SourceFile in InputFiles)
			{
				FileItem ObjectFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, Path.GetFileName(SourceFile.AbsolutePath) + ".xo"));

				Action CompileAction = Graph.CreateAction(ActionType.Compile);
				CompileAction.CommandDescription = "FakeCompile";
				CompileAction.CommandPath = BuildHostPlatform.Current.Shell;
				// we use type/cat instead of copy so that timestamp gets updated
				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
				{
					CompileAction.CommandArguments = String.Format("/C \"type \"{0}\" > \"{1}\"\"", SourceFile, ObjectFile);
				}
				else
				{
					CompileAction.CommandArguments = String.Format("cat {0} > {1}", Utils.EscapeShellArgument(SourceFile.AbsolutePath), Utils.EscapeShellArgument(ObjectFile.AbsolutePath));
				}
				CompileAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				CompileAction.PrerequisiteItems.Add(SourceFile);
				CompileAction.ProducedItems.Add(ObjectFile);
				CompileAction.StatusDescription = ObjectFile.Location.GetFileName();
				CompileAction.bCanExecuteRemotely = false;
				Result.ObjectFiles.Add(ObjectFile);

				foreach (FileItem ForceIncludeFile in CompileEnvironment.ForceIncludeFiles)
				{
					CompileAction.PrerequisiteItems.Add(ForceIncludeFile);
				}
			}

			return Result;
		}

		public override FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, IActionGraphBuilder Graph)
		{
			FileItem OutputFile = FileItem.GetItemByFileReference(LinkEnvironment.OutputFilePath);

			Action LinkAction = Graph.CreateAction(ActionType.Link);
			LinkAction.CommandDescription = "FakeCompile";
			LinkAction.CommandPath = BuildHostPlatform.Current.Shell;
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				LinkAction.CommandArguments = String.Format("/C echo Linked > {0}", LinkEnvironment.OutputFilePath.FullName);
			}
			else
			{
				LinkAction.CommandArguments = String.Format("echo Linked > {0}", Utils.EscapeShellArgument(LinkEnvironment.OutputFilePath.FullName));
			}
			LinkAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
			foreach (FileItem InputFile in LinkEnvironment.InputFiles)
			{
				LinkAction.PrerequisiteItems.Add(InputFile);
			}
			LinkAction.ProducedItems.Add(OutputFile);
			LinkAction.DeleteItems.Add(OutputFile);
			LinkAction.StatusDescription = OutputFile.Location.GetFileName();
			LinkAction.bCanExecuteRemotely = false;

			return OutputFile;
		}
    }
}
