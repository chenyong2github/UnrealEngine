// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Specification for a diagnostic message
	/// </summary>
	public class BgDiagnosticSpec : IBgExpr<BgDiagnosticSpec>
	{
		/// <summary>
		/// Verbosity to output the message at
		/// </summary>
		public LogLevel Level;

		/// <summary>
		/// The message to display
		/// </summary>
		public BgString Message;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Level">Output level</param>
		/// <param name="Message"></param>
		public BgDiagnosticSpec(LogLevel Level, BgString Message)
		{
			this.Level = Level;
			this.Message = Message;
		}

		/// <summary>
		/// Adds a diagnostic to the graph
		/// </summary>
		internal void AddToGraph(BgExprContext Context, BgGraph Graph, BgAgent? EnclosingAgent, BgNode? EnclosingNode)
		{
			BgScriptLocation Location = new BgScriptLocation("(unknown)", "(unknown)", 1);
			string MessageValue = Message.Compute(Context);
			Graph.Diagnostics.Add(new BgGraphDiagnostic(Location, (LogEventType)Level, MessageValue, EnclosingNode, EnclosingAgent));
		}

		/// <inheritdoc/>
		public BgDiagnosticSpec IfThen(BgBool Condition, BgDiagnosticSpec ValueIfTrue) => throw new NotImplementedException();

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext Context) => this;

		/// <inheritdoc/>
		public BgString ToBgString() => Message;
	}
}
