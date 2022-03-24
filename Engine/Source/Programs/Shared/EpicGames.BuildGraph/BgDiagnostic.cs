// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

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
		public BgScriptLocation Location { get; }

		/// <summary>
		/// The diagnostic event type
		/// </summary>
		public LogEventType EventType { get; }

		/// <summary>
		/// The message to display
		/// </summary>
		public string Message { get; }

		/// <summary>
		/// The node which this diagnostic is declared in. If the node is culled from the graph, the message will not be displayed.
		/// </summary>
		public BgNode? EnclosingNode { get; }

		/// <summary>
		/// The agent that this diagnostic is declared in. If the entire agent is culled from the graph, the message will not be displayed.
		/// </summary>
		public BgAgent? EnclosingAgent { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgGraphDiagnostic(BgScriptLocation location, LogEventType eventType, string message, BgNode? enclosingNode, BgAgent? enclosingAgent)
		{
			Location = location;
			EventType = eventType;
			Message = message;
			EnclosingNode = enclosingNode;
			EnclosingAgent = enclosingAgent;
		}
	}
}
