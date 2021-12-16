// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph;
using System;
using System.Collections.Generic;
using System.Text;

namespace AutomationTool
{
	/// <summary>
	/// Base class for any user defined graphs
	/// </summary>
	public abstract class BgGraphBuilder
	{
		/// <summary>
		/// Callback used to instantiate the graph
		/// </summary>
		/// <param name="Graph">The graph object</param>
		public abstract void SetupGraph(BgGraphSpec Graph);
	}
}
