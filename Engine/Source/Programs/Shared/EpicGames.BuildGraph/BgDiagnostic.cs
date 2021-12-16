// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Diagnostic message from the graph script. These messages are parsed at startup, then culled along with the rest of the graph nodes before output. Doing so
	/// allows errors and warnings which are only output if a node is part of the graph being executed.
	/// </summary>
	public class BgGraphDiagnostic
	{
		/// <summary>
		/// Location of the diagnostic
		/// </summary>
		public BgScriptLocation Location;

		/// <summary>
		/// The diagnostic event type
		/// </summary>
		public LogEventType EventType;

		/// <summary>
		/// The message to display
		/// </summary>
		public string Message;

		/// <summary>
		/// The node which this diagnostic is declared in. If the node is culled from the graph, the message will not be displayed.
		/// </summary>
		public BgNode? EnclosingNode;

		/// <summary>
		/// The agent that this diagnostic is declared in. If the entire agent is culled from the graph, the message will not be displayed.
		/// </summary>
		public BgAgent? EnclosingAgent;

		/// <summary>
		/// Constructor
		/// </summary>
		public BgGraphDiagnostic(BgScriptLocation Location, LogEventType EventType, string Message, BgNode? EnclosingNode, BgAgent? EnclosingAgent)
		{
			this.Location = Location;
			this.EventType = EventType;
			this.Message = Message;
			this.EnclosingNode = EnclosingNode;
			this.EnclosingAgent = EnclosingAgent;
		}
	}
}
