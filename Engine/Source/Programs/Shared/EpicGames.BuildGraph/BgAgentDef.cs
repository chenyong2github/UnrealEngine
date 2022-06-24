// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Xml;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Stores a list of nodes which can be executed on a single agent
	/// </summary>
	[DebuggerDisplay("{Name}")]
	public class BgAgentDef
	{
		/// <summary>
		/// Name of this agent. Used for display purposes in a build system.
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Array of valid agent types that these nodes may run on. When running in the build system, this determines the class of machine that should
		/// be selected to run these nodes. The first defined agent type for this branch will be used.
		/// </summary>
		public string[] PossibleTypes { get; }

		/// <summary>
		/// List of nodes in this agent group.
		/// </summary>
		public List<BgNodeDef> Nodes { get; set; } = new List<BgNodeDef>();

		/// <summary>
		/// Diagnostics to output if executing this agent
		/// </summary>
		public List<BgDiagnosticDef> Diagnostics { get; } = new List<BgDiagnosticDef>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="inName">Name of this agent group</param>
		/// <param name="inPossibleTypes">Array of valid agent types. See comment for AgentTypes member.</param>
		public BgAgentDef(string inName, string[] inPossibleTypes)
		{
			Name = inName;
			PossibleTypes = inPossibleTypes;
		}
	}
}
