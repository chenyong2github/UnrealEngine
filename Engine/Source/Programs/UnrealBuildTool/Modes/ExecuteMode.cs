// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Builds a target
	/// </summary>
	[ToolMode("Execute", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms | ToolModeOptions.SingleInstance | ToolModeOptions.StartPrefetchingEngine | ToolModeOptions.ShowExecutionTime)]
	class ExecuteMode : ToolMode
	{
		/// <summary>
		/// Whether we should just export the outdated actions list
		/// </summary>
		[CommandLine("-Actions=", Required = true)]
		public FileReference ActionsFile = null;

		/// <summary>
		/// Main entry point
		/// </summary>
		/// <param name="Arguments">Command-line arguments</param>
		/// <returns>One of the values of ECompilationResult</returns>
		public override int Execute(CommandLineArguments Arguments)
		{
			Arguments.ApplyTo(this);

			// Read the XML configuration files
			XmlConfig.ApplyTo(this);

			// Create the build configuration object, and read the settings
			BuildConfiguration BuildConfiguration = new BuildConfiguration();
			XmlConfig.ApplyTo(BuildConfiguration);
			Arguments.ApplyTo(BuildConfiguration);

			// Read the actions file
			List<Action> Actions;
			using(Timeline.ScopeEvent("ActionGraph.ReadActions()"))
			{
				Actions = ActionGraph.ImportJson(ActionsFile);
			}

			// Link the action graph
			using(Timeline.ScopeEvent("ActionGraph.Link()"))
			{
				ActionGraph.Link(Actions);
			}

			// Execute the actions
			using (Timeline.ScopeEvent("ActionGraph.ExecuteActions()"))
			{
				ActionGraph.ExecuteActions(BuildConfiguration, Actions);
			}

			return 0;
		}
	}
}

