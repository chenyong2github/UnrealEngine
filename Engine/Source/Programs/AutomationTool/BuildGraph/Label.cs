// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AutomationTool
{
	/// <summary>
	/// Which changelist to show a UGS badge for
	/// </summary>
	enum LabelChange
	{
		/// <summary>
		/// The current changelist being built
		/// </summary>
		Current,

		/// <summary>
		/// The last code changelist
		/// </summary>
		Code,
	}

	/// <summary>
	/// Defines a label within a graph. Labels are similar to badges, and give the combined status of one or more job steps. Unlike badges, they
	/// separate the requirements for its status and optional nodes to be included in its status, allowing this to be handled externally.
	/// </summary>
	class Label
	{
		/// <summary>
		/// Name of this badge
		/// </summary>
		public readonly string DashboardName;

		/// <summary>
		/// Category for this label
		/// </summary>
		public readonly string DashboardCategory;

		/// <summary>
		/// Name of the badge in UGS
		/// </summary>
		public readonly string UgsBadge;

		/// <summary>
		/// Path to the project folder in UGS
		/// </summary>
		public readonly string UgsProject;

		/// <summary>
		/// Which change to show the badge for
		/// </summary>
		public readonly LabelChange Change;

		/// <summary>
		/// Set of nodes that must be run for this label to be shown.
		/// </summary>
		public HashSet<Node> RequiredNodes = new HashSet<Node>();

		/// <summary>
		/// Set of nodes that will be included in this label if present.
		/// </summary>
		public HashSet<Node> IncludedNodes = new HashSet<Node>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InDashboardName">Name of this label</param>
		/// <param name="InDashboardCategory">Type of this label</param>
		/// <param name="InUgsBadge">The UGS badge name</param>
		/// <param name="InUgsProject">Project to display this badge for</param>
		/// <param name="InChange">The change to show this badge on in UGS</param>
		public Label(string InDashboardName, string InDashboardCategory, string InUgsBadge, string InUgsProject, LabelChange InChange)
		{
			DashboardName = InDashboardName;
			DashboardCategory = InDashboardCategory;
			UgsBadge = InUgsBadge;
			UgsProject = InUgsProject;
			Change = InChange;
		}

		/// <summary>
		/// Get the name of this label
		/// </summary>
		/// <returns>The name of this label</returns>
		public override string ToString()
		{
			if (!String.IsNullOrEmpty(DashboardName))
			{
				return String.Format("{0}/{1}", DashboardCategory, DashboardName);
			}
			else
			{
				return UgsBadge;
			}
		}
	}
}
