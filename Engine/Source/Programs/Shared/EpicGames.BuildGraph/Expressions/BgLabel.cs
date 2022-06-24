// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using EpicGames.BuildGraph.Expressions;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Specification for a label
	/// </summary>
	public class BgLabel : BgExpr
	{
		/// <summary>
		/// Name of this badge
		/// </summary>
		public BgString? DashboardName { get; }

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
		public BgString? Change { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgLabel(BgString? name = null, BgString? category = null, BgString? ugsBadge = null, BgString? ugsProject = null, BgString? change = null)
			: base(BgExprFlags.ForceFragment)
		{
			DashboardName = name;
			DashboardCategory = category;
			UgsBadge = ugsBadge;
			UgsProject = ugsProject;
			Change = change;
		}

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.Label);
			writer.WriteExpr(DashboardName ?? BgString.Empty);
			writer.WriteExpr(DashboardCategory ?? BgString.Empty);
			writer.WriteExpr(UgsBadge ?? BgString.Empty);
			writer.WriteExpr(UgsProject ?? BgString.Empty);
			writer.WriteExpr(Change ?? BgString.Empty);
		}

		/// <inheritdoc/>
		public override BgString ToBgString() => "{Label}";
	}
}
