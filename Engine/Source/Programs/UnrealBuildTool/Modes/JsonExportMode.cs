// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Exports a target as a JSON file
	/// </summary>
	[ToolMode("JsonExport", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms | ToolModeOptions.SingleInstance)]
	class JsonExportMode : ToolMode
	{
		/// <summary>
		/// Execute any actions which result in code generation (eg. ISPC compilation)
		/// </summary>
		[CommandLine("-ExecCodeGenActions")]
		public bool bExecCodeGenActions = false;

		/// <summary>
		/// Execute this command
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <returns>Exit code (always zero)</returns>
		public override int Execute(CommandLineArguments Arguments)
		{
			Arguments.ApplyTo(this);

			List<TargetDescriptor> TargetDescriptors = TargetDescriptor.ParseCommandLine(Arguments, false, false);
			foreach(TargetDescriptor TargetDescriptor in TargetDescriptors)
			{
				// Create the target
				UEBuildTarget Target = UEBuildTarget.Create(TargetDescriptor, false, false);

				// Get the output file
				FileReference OutputFile = TargetDescriptor.AdditionalArguments.GetFileReferenceOrDefault("-OutputFile=", null);
				if(OutputFile == null)
				{
					OutputFile = Target.ReceiptFileName.ChangeExtension(".json");
				}

				// Execute code generation actions
				if (bExecCodeGenActions)
				{
					using (ISourceFileWorkingSet WorkingSet = new EmptySourceFileWorkingSet())
					{
						// Create the build configuration object, and read the settings
						BuildConfiguration BuildConfiguration = new BuildConfiguration();
						XmlConfig.ApplyTo(BuildConfiguration);
						Arguments.ApplyTo(BuildConfiguration);

						// Create the makefile
						const bool bIsAssemblingBuild = true;
						TargetMakefile Makefile = Target.Build(BuildConfiguration, WorkingSet, bIsAssemblingBuild, TargetDescriptor.SingleFileToCompile);
						ActionGraph.Link(Makefile.Actions);

						// Filter all the actions to execute
						HashSet<FileItem> PrerequisiteItems = new HashSet<FileItem>(Makefile.Actions.SelectMany(x => x.ProducedItems).Where(x => x.HasExtension(".h") || x.HasExtension(".cpp")));
						List<Action> PrerequisiteActions = ActionGraph.GatherPrerequisiteActions(Makefile.Actions, PrerequisiteItems);

						// Execute these actions
						if (PrerequisiteActions.Count > 0)
						{
							Log.TraceInformation("Exeucting actions that produce source files...");
							ActionGraph.ExecuteActions(BuildConfiguration, PrerequisiteActions);
						}
					}
				}

				// Write the output file
				Log.TraceInformation("Writing {0}...", OutputFile);
				Target.ExportJson(OutputFile);
			}
			return 0;
		}
	}
}
