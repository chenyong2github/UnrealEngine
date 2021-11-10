// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Defines a badge which gives an at-a-glance summary of part of the build, and can be displayed in UGS
	/// </summary>
	public class BgBadge
	{
		/// <summary>
		/// Name of this badge
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Depot path to the project that this badge applies to. Used for filtering in UGS.
		/// </summary>
		public string Project { get; }

		/// <summary>
		/// The changelist to post the badge for
		/// </summary>
		public int Change { get; }

		/// <summary>
		/// Set of nodes that this badge reports the status of
		/// </summary>
		public HashSet<BgNode> Nodes { get; } = new HashSet<BgNode>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of this report</param>
		/// <param name="InProject">Depot path to the project that this badge applies to</param>
		/// <param name="InChange">The changelist to post the badge for</param>
		public BgBadge(string InName, string InProject, int InChange)
		{
			Name = InName;
			Project = InProject;
			Change = InChange;
		}

		/// <summary>
		/// Get the name of this badge
		/// </summary>
		/// <returns>The name of this badge</returns>
		public override string ToString()
		{
			return Name;
		}
	}
}
