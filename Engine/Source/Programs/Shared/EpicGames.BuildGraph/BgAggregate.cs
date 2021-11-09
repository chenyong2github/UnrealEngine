// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Defines a agggregate within a graph, which give the combined status of one or more job steps, and allow building several steps at once.
	/// </summary>
	public class BgAggregate
	{
		/// <summary>
		/// Name of this badge
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Set of nodes that must be run for this label to be shown.
		/// </summary>
		public HashSet<BgNode> RequiredNodes { get; } = new HashSet<BgNode>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of this aggregate</param>
		public BgAggregate(string InName)
		{
			Name = InName;
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
