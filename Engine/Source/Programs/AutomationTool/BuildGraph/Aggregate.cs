// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AutomationTool
{
	/// <summary>
	/// Defines a agggregate within a graph, which give the combined status of one or more job steps, and allow building several steps at once.
	/// </summary>
	class Aggregate
	{
		/// <summary>
		/// Name of this badge
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// Label to show for this aggregate in Horde
		/// </summary>
		public readonly string Label;

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
		/// <param name="InName">Name of this aggregate</param>
		/// <param name="InLabel">Label to show for this aggregate</param>
		public Aggregate(string InName, string InLabel)
		{
			Name = InName;
			Label = InLabel;
		}

		/// <summary>
		/// Get the name of this label
		/// </summary>
		/// <returns>The name of this label</returns>
		public override string ToString()
		{
			return Name;
		}
	}
}
