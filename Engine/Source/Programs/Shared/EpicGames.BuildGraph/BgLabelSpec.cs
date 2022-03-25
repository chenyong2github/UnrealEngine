// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using EpicGames.BuildGraph.Expressions;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Configuration for a label
	/// </summary>
	public class BgLabelConfig
	{
		/// <summary>
		/// Name of this badge
		/// </summary>
		public BgString? DashboardName { get; set; }

		/// <summary>
		/// Category for this label
		/// </summary>
		public BgString? DashboardCategory { get; set; }

		/// <summary>
		/// Name of the badge in UGS
		/// </summary>
		public BgString? UgsBadge { get; set; }

		/// <summary>
		/// Path to the project folder in UGS
		/// </summary>
		public BgString? UgsProject { get; set; }

		/// <summary>
		/// Which change to show the badge for
		/// </summary>
		public BgString? Change { get; set; }

		/// <summary>
		/// Set of nodes that must be run for this label to be shown.
		/// </summary>
		public BgList<BgFileSet> RequiredNodes { get; set; } = BgList<BgFileSet>.Empty;

		/// <summary>
		/// Set of nodes that will be included in this label if present.
		/// </summary>
		public BgList<BgFileSet> IncludedNodes { get; set; } = BgList<BgFileSet>.Empty;
	}

	/// <summary>
	/// Specification for a label
	/// </summary>
	public class BgLabelSpec
	{
		private readonly BgLabelConfig _config;

		internal BgLabelSpec(BgLabelConfig config)
		{
			_config = config;
		}

		internal void AddToGraph(BgExprContext context, BgGraph graph)
		{
			string? dashboardName = _config.DashboardName?.Compute(context);
			string? dashboardCategory = _config.DashboardCategory?.Compute(context);
			string? ugsBadge = _config.UgsBadge?.Compute(context);
			string? ugsProject = _config.UgsBadge?.Compute(context);

			BgLabelChange labelChange;
			if (_config.Change is null)
			{
				labelChange = BgLabelChange.Current;
			}
			else
			{
				labelChange = Enum.Parse<BgLabelChange>(_config.Change.Compute(context));
			}

			BgLabel label = new BgLabel(dashboardName, dashboardCategory, ugsBadge, ugsProject, labelChange);
			label.RequiredNodes.UnionWith(_config.RequiredNodes.ComputeTags(context).Select(x => graph.TagNameToNodeOutput[x].ProducingNode));
			label.IncludedNodes.UnionWith(_config.IncludedNodes.ComputeTags(context).Select(x => graph.TagNameToNodeOutput[x].ProducingNode));
			graph.Labels.Add(label);
		}
	}
}
