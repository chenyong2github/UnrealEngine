// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	abstract class UEToolChain
	{
		public UEToolChain()
		{
		}

		public virtual void SetEnvironmentVariables()
		{
		}

		public virtual void GetVersionInfo(List<string> Lines)
		{
		}

		public abstract CPPOutput CompileCPPFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, string ModuleName, List<Action> Actions);

		public virtual CPPOutput CompileRCFiles(CppCompileEnvironment Environment, List<FileItem> InputFiles, DirectoryReference OutputDir, List<Action> Actions)
		{
			CPPOutput Result = new CPPOutput();
			return Result;
		}

		public virtual CPPOutput CompileISPCFiles(CppCompileEnvironment Environment, List<FileItem> InputFiles, DirectoryReference OutputDir,List<Action> Actions)
		{
			CPPOutput Result = new CPPOutput();
			return Result;
		}
		public virtual CPPOutput GenerateISPCHeaders(CppCompileEnvironment Environment, List<FileItem> InputFiles, DirectoryReference OutputDir, List<Action> Actions)
		{
			CPPOutput Result = new CPPOutput();
			return Result;
		}

		public abstract FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, List<Action> Actions);
		public virtual FileItem[] LinkAllFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, List<Action> Actions)
		{
			return new FileItem[] { LinkFiles(LinkEnvironment, bBuildImportLibraryOnly, Actions) };
		}


		/// <summary>
		/// Get the name of the response file for the current linker environment and output file
		/// </summary>
		/// <param name="LinkEnvironment"></param>
		/// <param name="OutputFile"></param>
		/// <returns></returns>
		public static FileReference GetResponseFileName(LinkEnvironment LinkEnvironment, FileItem OutputFile)
		{
			// Construct a relative path for the intermediate response file
			return FileReference.Combine(LinkEnvironment.IntermediateDirectory, OutputFile.Location.GetFileName() + ".response");
		}

		public virtual ICollection<FileItem> PostBuild(FileItem Executable, LinkEnvironment ExecutableLinkEnvironment, List<Action> Actions)
		{
			return new List<FileItem>();
		}

		public virtual void SetUpGlobalEnvironment(ReadOnlyTargetRules Target)
		{
		}

		public virtual void ModifyBuildProducts(ReadOnlyTargetRules Target, UEBuildBinary Binary, List<string> Libraries, List<UEBuildBundleResource> BundleResources, Dictionary<FileReference, BuildProductType> BuildProducts)
		{
		}

		public virtual void FinalizeOutput(ReadOnlyTargetRules Target, TargetMakefile Makefile)
		{
		}

		/// <summary>
		/// Adds a build product and its associated debug file to a receipt.
		/// </summary>
		/// <param name="OutputFile">Build product to add</param>
		/// <param name="OutputType">The type of build product</param>
		public virtual bool ShouldAddDebugFileToReceipt(FileReference OutputFile, BuildProductType OutputType)
		{
			return true;
		}
		
		public virtual FileReference GetDebugFile(FileReference OutputFile, string DebugExtension)
		{
			//  by default, just change the extension to the debug extension
			return OutputFile.ChangeExtension(DebugExtension);
		}


		public virtual void SetupBundleDependencies(List<UEBuildBinary> Binaries, string GameName)
		{

		}

        public virtual string GetSDKVersion()
        {
            return "Not Applicable";
        }
	};
}