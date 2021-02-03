// Copyright Epic Games, Inc. All Rights Reserved.
using EpicGames.Core;
using UnrealBuildTool;
using System;
using System.IO;
using System.Runtime.CompilerServices;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public abstract class TextureShareSDKTargetBase : TargetRules
{
	/// <summary>
	/// Finds the innermost parent directory with the provided name. Search is case insensitive.
	/// </summary>
	string InnermostParentDirectoryPathWithName(string ParentName, string CurrentPath)
	{
		DirectoryInfo ParentInfo = Directory.GetParent(CurrentPath);

		if (ParentInfo == null)
		{
			throw new DirectoryNotFoundException("Could not find parent folder '" + ParentName + "'");
		}

		// Case-insensitive check of the parent folder name.
		if (ParentInfo.Name.ToLower() == ParentName.ToLower())
		{
			return ParentInfo.ToString();
		}

		return InnermostParentDirectoryPathWithName(ParentName, ParentInfo.ToString());
	}

	/// <summary>
	/// Returns the path to this .cs file.
	/// </summary>
	string GetCallerFilePath([CallerFilePath] string CallerFilePath = "")
	{
		if (CallerFilePath.Length == 0)
		{
			throw new FileNotFoundException("Could not find the path of our .cs file");
		}

		return CallerFilePath;
	}

	public TextureShareSDKTargetBase(TargetInfo Target, string InSDKRenderTarget) : base(Target)
	{
		Type = TargetType.Program;

		bShouldCompileAsDLL = true;
		LinkType = TargetLinkType.Monolithic;
		SolutionDirectory = "Programs/TextureShare";
		LaunchModuleName = "TextureShareSDK_" + InSDKRenderTarget;

		// We only need minimal use of the engine for this plugin
		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = false;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileICU = false;
		bHasExports = false;
		bUsesSlate = false;

		bBuildInSolutionByDefault = false;

		// This .cs file must be inside the source folder of this Program. We later use this to find other key directories.
		string TargetFilePath = GetCallerFilePath();
		string TextureShareSDKDir = Directory.GetParent(TargetFilePath).ToString();

		// We need to avoid failing to load DLL due to looking for EngineDir() in non-existent folders.
		// By having it build in the same directory as the engine, it will assume the engine is in the same directory
		// as the program, and because this folder always exists, it will not fail the check inside EngineDir().
		// Because this is a Program, we assume that this target file resides under a "Programs" folder.
		string ProgramsDir = InnermostParentDirectoryPathWithName("Programs", TargetFilePath);

		// We assume this Program resides under a Source folder.
		string SourceDir = InnermostParentDirectoryPathWithName("Source", ProgramsDir);

		// The program is assumed to reside inside the "Engine" folder.
		string EngineDir = InnermostParentDirectoryPathWithName("Engine", SourceDir);

		// The default Binaries path is assumed to be a sibling of "Source" folder.
		string DefaultBinDir = Path.GetFullPath(Path.Combine(SourceDir, "..", "Binaries", Platform.ToString()));

		// We assume that the engine exe resides in Engine/Binaries/[Platform]
		string EngineBinariesDir = Path.Combine(EngineDir, "Binaries", Platform.ToString(), "TextureShareSDK");

		// Now we calculate the relative path between the default output directory and the engine binaries,
		// in order to force the output of this program to be in the same folder as th engine.
		ExeBinariesSubFolder = (new DirectoryReference(EngineBinariesDir)).MakeRelativeTo(new DirectoryReference(DefaultBinDir));

		// Setting this is necessary since we are creating the binaries outside of Restricted.
		bLegalToDistributeBinary = true;

		string TextureSharePluginDir = EngineDir+"/Plugins/Runtime/TextureShare/Source/TextureShareCore";

		string PostBuildBinDir = Path.Combine(DefaultBinDir, "TextureShareSDK");

		// Copy Headers
		string HeadesSrcPath1 = Path.Combine(TextureShareSDKDir, "Public"); 
		string HeadesSrcPath2 = Path.Combine(TextureSharePluginDir, "Public", "Containers");
		string PostBuildHeadersDir = Path.Combine(PostBuildBinDir, "Public");

		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}...", HeadesSrcPath1, PostBuildHeadersDir));
		PostBuildSteps.Add(string.Format("xcopy /y /i /v \"{0}\\*.h\" \"{1}\" 1>nul", HeadesSrcPath1, PostBuildHeadersDir));
		
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}...", HeadesSrcPath2, PostBuildHeadersDir));
		PostBuildSteps.Add(string.Format("xcopy /y /i /v \"{0}\\*.h\" \"{1}\" 1>nul", HeadesSrcPath2, PostBuildHeadersDir));

		// Copy documentation
		string DocPath = Path.Combine(TextureShareSDKDir, "Documentation");
		string PostBuildDocDir = Path.Combine(PostBuildBinDir, "Documentation");
		PostBuildSteps.Add(string.Format("echo Copying {0} to {1}...", DocPath, PostBuildDocDir));
		PostBuildSteps.Add(string.Format("xcopy /R /S /y /i /v \"{0}\\*.*\" \"{1}\"  1>nul", DocPath, PostBuildDocDir));
	}
}

public class TextureShareSDK_D3D11Target : TextureShareSDKTargetBase
{
	public TextureShareSDK_D3D11Target(TargetInfo Target) : base(Target, "D3D11")
	{
	}
}
