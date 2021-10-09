// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;

namespace HordeServer.Models
{
	/// <summary>
	/// Represents a node in the graph
	/// </summary>
	public interface INode
	{
		/// <summary>
		/// The name of this node 
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Indices of nodes which must have succeeded for this node to run
		/// </summary>
		public NodeRef[] InputDependencies { get; }

		/// <summary>
		/// Indices of nodes which must have completed for this node to run
		/// </summary>
		public NodeRef[] OrderDependencies { get; }

		/// <summary>
		/// The priority that this node should be run at, within this job
		/// </summary>
		public Priority Priority { get; }

		/// <summary>
		/// Whether this node can be run multiple times
		/// </summary>
		public bool AllowRetry { get; }

		/// <summary>
		/// This node can start running early, before dependencies of other nodes in the same group are complete
		/// </summary>
		public bool RunEarly { get; }

		/// <summary>
		/// Whether to include warnings in the output (defaults to true)
		/// </summary>
		public bool Warnings { get; }

		/// <summary>
		/// List of credentials required for this node. Each entry maps an environment variable name to a credential in the form "CredentialName.PropertyName".
		/// </summary>
		public IReadOnlyDictionary<string, string>? Credentials { get; }

		/// <summary>
		/// Properties for this node
		/// </summary>
		public IReadOnlyDictionary<string, string>? Properties { get; }
	}

	/// <summary>
	/// Information about a sequence of nodes which can execute on a single agent
	/// </summary>
	public interface INodeGroup
	{
		/// <summary>
		/// The type of agent to execute this group
		/// </summary>
		public string AgentType { get; }

		/// <summary>
		/// Nodes in this group
		/// </summary>
		public IReadOnlyList<INode> Nodes { get; }
	}

	/// <summary>
	/// Reference to a node within another grup
	/// </summary>
	[DebuggerDisplay("Group: {GroupIdx}, Node: {NodeIdx}")]
	public class NodeRef
	{
		/// <summary>
		/// The group index of the referenced node
		/// </summary>
		public int GroupIdx { get; set; }

		/// <summary>
		/// The node index of the referenced node
		/// </summary>
		public int NodeIdx { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		[BsonConstructor]
		private NodeRef()
		{
			GroupIdx = 0;
			NodeIdx = 0;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="GroupIdx">Index of thr group containing the node</param>
		/// <param name="NodeIdx">Index of the node within the group</param>
		public NodeRef(int GroupIdx, int NodeIdx)
		{
			this.GroupIdx = GroupIdx;
			this.NodeIdx = NodeIdx;
		}

		/// <inheritdoc/>
		public override bool Equals(object? Other)
		{
			NodeRef? OtherNodeRef = Other as NodeRef;
			return OtherNodeRef != null && OtherNodeRef.GroupIdx == GroupIdx && OtherNodeRef.NodeIdx == NodeIdx;
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return HashCode.Combine(GroupIdx, NodeIdx);
		}

		/// <summary>
		/// Converts this reference to a node name
		/// </summary>
		/// <param name="Groups">List of groups that this reference points to</param>
		/// <returns>Name of the referenced node</returns>
		public INode ToNode(IReadOnlyList<INodeGroup> Groups)
		{
			return Groups[GroupIdx].Nodes[NodeIdx];
		}
	}

	/// <summary>
	/// An collection of node references
	/// </summary>
	public interface IAggregate
	{
		/// <summary>
		/// Name of the aggregate
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// List of nodes for the aggregate to be valid
		/// </summary>
		public IReadOnlyList<NodeRef> Nodes { get; }
	}

	/// <summary>
	/// Label indicating the status of a set of nodes
	/// </summary>
	public interface ILabel
	{
		/// <summary>
		/// Label to show in the dashboard. Null if does not need to be shown.
		/// </summary>
		public string? DashboardName { get; }

		/// <summary>
		/// Category for the label. May be null.
		/// </summary>
		public string? DashboardCategory { get; }

		/// <summary>
		/// Name to display for this label in UGS
		/// </summary>
		public string? UgsName { get; }

		/// <summary>
		/// Project which this label applies to, for UGS
		/// </summary>
		public string? UgsProject { get; }

		/// <summary>
		/// Which change to display the label on
		/// </summary>
		public LabelChange Change { get; }

		/// <summary>
		/// List of required nodes for the aggregate to be valid
		/// </summary>
		public List<NodeRef> RequiredNodes { get; }

		/// <summary>
		/// List of optional nodes to include in the aggregate state
		/// </summary>
		public List<NodeRef> IncludedNodes { get; }
	}

	/// <summary>
	/// Extension methods for ILabel
	/// </summary>
	public static class LabelExtensions
	{
		/// <summary>
		/// Enumerate all the required dependencies of this node group
		/// </summary>
		/// <param name="Label">The label instance</param>
		/// <param name="Groups">List of groups for the job containing this aggregate</param>
		/// <returns>Sequence of nodes</returns>
		public static IEnumerable<INode> GetDependencies(this ILabel Label, IReadOnlyList<INodeGroup> Groups)
		{
			foreach (NodeRef RequiredNode in Label.RequiredNodes)
			{
				yield return RequiredNode.ToNode(Groups);
			}
			foreach (NodeRef IncludedNode in Label.IncludedNodes)
			{
				yield return IncludedNode.ToNode(Groups);
			}
		}
	}

	/// <summary>
	/// A unique dependency graph instance
	/// </summary>
	public interface IGraph
	{
		/// <summary>
		/// Hash of this graph
		/// </summary>
		public ContentHash Id { get; }

		/// <summary>
		/// Schema version for this document
		/// </summary>
		public int Schema { get; }

		/// <summary>
		/// List of groups for this graph
		/// </summary>
		public IReadOnlyList<INodeGroup> Groups { get; }

		/// <summary>
		/// List of aggregates for this graph
		/// </summary>
		public IReadOnlyList<IAggregate> Aggregates { get; }

		/// <summary>
		/// Status labels for this graph
		/// </summary>
		public IReadOnlyList<ILabel> Labels { get; }
	}

	/// <summary>
	/// Extension methods for graphs
	/// </summary>
	public static class GraphExtensions
	{
		/// <summary>
		/// Gets the node from a node reference
		/// </summary>
		/// <param name="Graph">The graph instance</param>
		/// <param name="Ref">The node reference</param>
		/// <returns>The node for the given reference</returns>
		public static INode GetNode(this IGraph Graph, NodeRef Ref)
		{
			return Graph.Groups[Ref.GroupIdx].Nodes[Ref.NodeIdx];
		}

		/// <summary>
		/// Tries to find a node by name
		/// </summary>
		/// <param name="Graph">The graph to search</param>
		/// <param name="NodeName">Name of the node</param>
		/// <param name="Ref">Receives the node reference</param>
		/// <returns>True if the node was found, false otherwise</returns>
		public static bool TryFindNode(this IGraph Graph, string NodeName, out NodeRef Ref)
		{
			for (int GroupIdx = 0; GroupIdx < Graph.Groups.Count; GroupIdx++)
			{
				INodeGroup Group = Graph.Groups[GroupIdx];
				for (int NodeIdx = 0; NodeIdx < Group.Nodes.Count; NodeIdx++)
				{
					INode Node = Group.Nodes[NodeIdx];
					if (String.Equals(Node.Name, NodeName, StringComparison.OrdinalIgnoreCase))
					{
						Ref = new NodeRef(GroupIdx, NodeIdx);
						return true;
					}
				}
			}

			Ref = new NodeRef(0, 0);
			return false;
		}

		/// <summary>
		/// Tries to find a node by name
		/// </summary>
		/// <param name="Graph">The graph to search</param>
		/// <param name="NodeName">Name of the node</param>
		/// <param name="Node">Receives the node</param>
		/// <returns>True if the node was found, false otherwise</returns>
		public static bool TryFindNode(this IGraph Graph, string NodeName, out INode? Node)
		{
			NodeRef Ref;
			if (TryFindNode(Graph, NodeName, out Ref))
			{
				Node = Graph.Groups[Ref.GroupIdx].Nodes[Ref.NodeIdx];
				return true;
			}
			else
			{
				Node = null;
				return false;
			}
		}

		/// <summary>
		/// Tries to find a node by name
		/// </summary>
		/// <param name="Graph">The graph to search</param>
		/// <param name="Name">Name of the node</param>
		/// <param name="AggregateIdx">Receives the aggregate index</param>
		/// <returns>True if the node was found, false otherwise</returns>
		public static bool TryFindAggregate(this IGraph Graph, string Name, out int AggregateIdx)
		{
			AggregateIdx = Graph.Aggregates.FindIndex(x => x.Name.Equals(Name, StringComparison.OrdinalIgnoreCase));
			return AggregateIdx != -1;
		}

		/// <summary>
		/// Tries to find a node by name
		/// </summary>
		/// <param name="Graph">The graph to search</param>
		/// <param name="Name">Name of the node</param>
		/// <param name="Aggregate">Receives the aggregate</param>
		/// <returns>True if the node was found, false otherwise</returns>
		public static bool TryFindAggregate(this IGraph Graph, string Name, [NotNullWhen(true)] out IAggregate? Aggregate)
		{
			int AggregateIdx;
			if (TryFindAggregate(Graph, Name, out AggregateIdx))
			{
				Aggregate = Graph.Aggregates[AggregateIdx];
				return true;
			}
			else
			{
				Aggregate = null;
				return false;
			}
		}

		/// <summary>
		/// Gets a list of dependencies for the given node
		/// </summary>
		/// <param name="Graph">The graph instance</param>
		/// <param name="Node">The node to return dependencies for</param>
		/// <returns>List of dependencies</returns>
		public static IEnumerable<INode> GetDependencies(this IGraph Graph, INode Node)
		{
			return Enumerable.Concat(Node.InputDependencies, Node.OrderDependencies).Select(x => Graph.GetNode(x));
		}
	}

	/// <summary>
	/// Information required to create a node
	/// </summary>
	public class NewNode
	{
		/// <summary>
		/// The name of this node 
		/// </summary>
		public string Name { get; set; } = null!;

		/// <summary>
		/// List of nodes which must succeed for this node to run
		/// </summary>
		public List<string>? InputDependencies { get; set; }

		/// <summary>
		/// List of nodes which must have completed for this node to run
		/// </summary>
		public List<string>? OrderDependencies { get; set; }

		/// <summary>
		/// The priority of this node
		/// </summary>
		public Priority? Priority { get; set; }

		/// <summary>
		/// This node can be run multiple times
		/// </summary>
		public bool? AllowRetry { get; set; }

		/// <summary>
		/// This node can start running early, before dependencies of other nodes in the same group are complete
		/// </summary>
		public bool? RunEarly { get; set; }

		/// <summary>
		/// Whether to include warnings in the diagnostic output
		/// </summary>
		public bool? Warnings { get; set; }

		/// <summary>
		/// Credentials required for this node to run. This dictionary maps from environment variable names to a credential property in the format 'CredentialName.PropertyName'.
		/// </summary>
		public Dictionary<string, string>? Credentials { get; set; }

		/// <summary>
		/// Properties for this node
		/// </summary>
		public Dictionary<string, string>? Properties { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the node</param>
		/// <param name="InputDependencies">List of nodes which must have completed succesfully for this node to run</param>
		/// <param name="OrderDependencies">List of nodes which must have completed for this node to run</param>
		/// <param name="Priority">Priority of this node</param>
		/// <param name="AllowRetry">Whether the node can be run multiple times</param>
		/// <param name="RunEarly">Whether the node can run early, before dependencies of other nodes in the same group complete</param>
		/// <param name="Warnings">Whether to include warnings in the diagnostic output (defaults to true)</param>
		/// <param name="Credentials">Credentials required for this node to run</param>
		/// <param name="Properties">Properties for the node</param>
		public NewNode(string Name, List<string>? InputDependencies = null, List<string>? OrderDependencies = null, Priority? Priority = null, bool? AllowRetry = null, bool? RunEarly = null, bool? Warnings = null, Dictionary<string, string>? Credentials = null, Dictionary<string, string>? Properties = null)
		{
			this.Name = Name;
			this.InputDependencies = InputDependencies;
			this.OrderDependencies = OrderDependencies;
			this.Priority = Priority;
			this.AllowRetry = AllowRetry;
			this.RunEarly = RunEarly;
			this.Warnings = Warnings;
			this.Credentials = Credentials;
			this.Properties = Properties;
		}
	}

	/// <summary>
	/// Information about a group of nodes
	/// </summary>
	public class NewGroup
	{
		/// <summary>
		/// The type of agent to execute this group
		/// </summary>
		public string AgentType { get; set; }

		/// <summary>
		/// Nodes in the group
		/// </summary>
		public List<NewNode> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AgentType">The type of agent to execute this group</param>
		/// <param name="Nodes">Nodes in this group</param>
		public NewGroup(string AgentType, List<NewNode> Nodes)
		{
			this.AgentType = AgentType;
			this.Nodes = Nodes;
		}
	}

	/// <summary>
	/// Information about a group of nodes
	/// </summary>
	public class NewLabel
	{
		/// <summary>
		/// Category for this label
		/// </summary>
		[Obsolete("Use DashboardCategory instead")]
		public string? Category => DashboardCategory;

		/// <summary>
		/// Name of the aggregate
		/// </summary>
		[Obsolete("Use DashboardName instead")]
		public string? Name => DashboardName;

		/// <summary>
		/// Name of the aggregate
		/// </summary>
		public string? DashboardName { get; set; }

		/// <summary>
		/// Category for this label
		/// </summary>
		public string? DashboardCategory { get; set; }

		/// <summary>
		/// Name of the badge in UGS
		/// </summary>
		public string? UgsName { get; set; }

		/// <summary>
		/// Project to show this label for in UGS
		/// </summary>
		public string? UgsProject { get; set; }

		/// <summary>
		/// Which change the label applies to
		/// </summary>
		public LabelChange Change { get; set; }

		/// <summary>
		/// Nodes which must be part of the job for the aggregate to be valid
		/// </summary>
		public List<string> RequiredNodes { get; set; } = new List<string>();

		/// <summary>
		/// Nodes which must be part of the job for the aggregate to be valid
		/// </summary>
		public List<string> IncludedNodes { get; set; } = new List<string>();
	}

	/// <summary>
	/// Information about a group of nodes
	/// </summary>
	public class NewAggregate
	{
		/// <summary>
		/// Name of the aggregate
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Nodes which must be part of the job for the aggregate to be valid
		/// </summary>
		public List<string> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of this aggregate</param>
		/// <param name="Nodes">Nodes which must be part of the job for the aggregate to be shown</param>
		public NewAggregate(string Name, List<string> Nodes)
		{
			this.Name = Name;
			this.Nodes = Nodes;
		}
	}
}


