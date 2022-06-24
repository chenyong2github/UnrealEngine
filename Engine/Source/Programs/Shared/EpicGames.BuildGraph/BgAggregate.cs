// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;

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
		/// <param name="name">Name of this aggregate</param>
		public BgAggregate(string name)
		{
			Name = name;
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

	/// <summary>
	/// Aggregate that was created from bytecode
	/// </summary>
	public class BgBytecodeAggregate : BgAggregate
	{
		/// <summary>
		/// Labels to add this aggregate to
		/// </summary>
		public BgLabel? Label { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgBytecodeAggregate(string name, IEnumerable<BgNode> nodes, BgLabel? label)
			: base(name)
		{
			RequiredNodes.UnionWith(nodes);
			Label = label;
		}
	}
}
