// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms("Win64")]
public abstract class DatasmithSketchUpRubyBaseTarget : TargetRules
{
	public DatasmithSketchUpRubyBaseTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs/Datasmith";
		bBuildInSolutionByDefault = false;

		bShouldCompileAsDLL = true;
		LinkType = TargetLinkType.Monolithic;

		WindowsPlatform.ModuleDefinitionFile = "Programs/Enterprise/Datasmith/DatasmithSketchUpRubyExporter/DatasmithSketchUpRubyExporter.def";

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bCompileICU = false;
		bUsesSlate = false;

		bHasExports = true;
		bForceEnableExceptions = true;
	}

	protected void AddCopyPostBuildStep(TargetInfo Target)
	{
		string OutputName = "$(TargetName)";

		string SrcOutputFileName = string.Format(@"$(EngineDir)/Binaries/Win64/{0}/{1}.dll", ExeBinariesSubFolder, OutputName);
		string DstOutputFileName = string.Format(@"$(EngineDir)/Binaries/Win64/{0}/Plugin/UnrealDatasmithSketchUp2020/{1}.so", ExeBinariesSubFolder, OutputName);

		PostBuildSteps.Add("echo on");
		PostBuildSteps.Add(string.Format("echo F|xcopy /Y /R /F \"{0}\" \"{1}\"", SrcOutputFileName, DstOutputFileName));

		PostBuildSteps.Add(string.Format("xcopy /Y /R /F \"{0}\" \"{1}\"",
			string.Format(@"$(EngineDir)/Source/Programs/Enterprise/Datasmith/DatasmithSketchUpRubyExporter/Plugin/plugin_main.rb"),
			string.Format(@"$(EngineDir)/Binaries/Win64/{0}/Plugin/UnrealDatasmithSketchUp2020", ExeBinariesSubFolder)
			));

		// UnrealDatasmithSketchUp2020.rb copied above plugin_main and .so(it will be placed as the root of Plugins folder of SketchUp
		PostBuildSteps.Add(string.Format("xcopy /Y /R /F \"{0}\" \"{1}\"",
			string.Format(@"$(EngineDir)/Source/Programs/Enterprise/Datasmith/DatasmithSketchUpRubyExporter/Plugin/UnrealDatasmithSketchUp2020.rb"),
			string.Format(@"$(EngineDir)/Binaries/Win64/{0}/Plugin", ExeBinariesSubFolder)
			));
	}

}

public class DatasmithSketchUpRuby2020Target : DatasmithSketchUpRubyBaseTarget
{
	public DatasmithSketchUpRuby2020Target(TargetInfo Target)
		: base(Target)
	{
		LaunchModuleName = "DatasmithSketchUpRuby2020";
		ExeBinariesSubFolder = @"SketchUpRuby/2020";

		AddCopyPostBuildStep(Target);
	}
}
