// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

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
		public BgString? DashboardCategory { get; }

		/// <summary>
		/// Name of the badge in UGS
		/// </summary>
		public BgString? UgsBadge { get; }

		/// <summary>
		/// Path to the project folder in UGS
		/// </summary>
		public BgString? UgsProject { get; }

		/// <summary>
		/// Which change to show the badge for
		/// </summary>
		public BgString? Change;

		/// <summary>
		/// Set of nodes that must be run for this label to be shown.
		/// </summary>
		public BgList<BgFileSet> RequiredNodes = BgList<BgFileSet>.Empty;

		/// <summary>
		/// Set of nodes that will be included in this label if present.
		/// </summary>
		public BgList<BgFileSet> IncludedNodes = BgList<BgFileSet>.Empty;
	}

	/// <summary>
	/// Specification for a label
	/// </summary>
	public class BgLabelSpec
	{
		BgLabelConfig Config { get; }

		internal BgLabelSpec(BgLabelConfig Config)
		{
			this.Config = Config;
		}

		internal void AddToGraph(BgExprContext Context, BgGraph Graph)
		{
			string? DashboardName = Config.DashboardName?.Compute(Context);
			string? DashboardCategory = Config.DashboardCategory?.Compute(Context);
			string? UgsBadge = Config.UgsBadge?.Compute(Context);
			string? UgsProject = Config.UgsBadge?.Compute(Context);

			BgLabelChange LabelChange;
			if ((object?)Config.Change == null)
			{
				LabelChange = BgLabelChange.Current;
			}
			else
			{
				LabelChange = Enum.Parse<BgLabelChange>(Config.Change.Compute(Context));
			}

			BgLabel Label = new BgLabel(DashboardName, DashboardCategory, UgsBadge, UgsProject, LabelChange);
			Label.RequiredNodes.UnionWith(Config.RequiredNodes.ComputeTags(Context).Select(x => Graph.TagNameToNodeOutput[x].ProducingNode));
			Label.IncludedNodes.UnionWith(Config.IncludedNodes.ComputeTags(Context).Select(x => Graph.TagNameToNodeOutput[x].ProducingNode));
			Graph.Labels.Add(Label);
		}
	}
}
