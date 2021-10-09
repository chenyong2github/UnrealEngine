// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Information required to create a node
	/// </summary>
	public class GetNodeResponse
	{
		/// <summary>
		/// The name of this node 
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Indices of nodes which must have succeeded for this node to run
		/// </summary>
		public List<string> InputDependencies { get; set; }

		/// <summary>
		/// Indices of nodes which must have completed for this node to run
		/// </summary>
		public List<string> OrderDependencies { get; set; }

		/// <summary>
		/// The priority of this node
		/// </summary>
		public Priority Priority { get; set; }

		/// <summary>
		/// Whether this node can be retried
		/// </summary>
		public bool AllowRetry { get; set; }

		/// <summary>
		/// This node can start running early, before dependencies of other nodes in the same group are complete
		/// </summary>
		public bool RunEarly { get; set; }

		/// <summary>
		/// Whether to include warnings in diagnostic output
		/// </summary>
		public bool Warnings { get; set; }

		/// <summary>
		/// Average time to execute this node based on historical trends
		/// </summary>
		public float? AverageDuration { get; set; }

		/// <summary>
		/// Credentials required for this node to run. This dictionary maps from environment variable names to a credential property in the format 'CredentialName.PropertyName'.
		/// </summary>
		public IReadOnlyDictionary<string, string>? Credentials { get; set; }

		/// <summary>
		/// Properties for this node
		/// </summary>
		public IReadOnlyDictionary<string, string>? Properties { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Node">The node to construct from</param>
		/// <param name="Groups">The groups in the graph</param>
		public GetNodeResponse(INode Node, IReadOnlyList<INodeGroup> Groups)
		{
			this.Name = Node.Name;
			this.InputDependencies = new List<string>(Node.InputDependencies.Select(x => Groups[x.GroupIdx].Nodes[x.NodeIdx].Name)); ;
			this.OrderDependencies = new List<string>(Node.OrderDependencies.Select(x => Groups[x.GroupIdx].Nodes[x.NodeIdx].Name)); ;
			this.Priority = Node.Priority;
			this.AllowRetry = Node.AllowRetry;
			this.RunEarly = Node.RunEarly;
			this.Warnings = Node.Warnings;
			this.Credentials = Node.Credentials;
			this.Properties = Node.Properties;
		}
	}

	/// <summary>
	/// Information about a group of nodes
	/// </summary>
	public class GetGroupResponse
	{
		/// <summary>
		/// The type of agent to execute this group
		/// </summary>
		public string AgentType { get; set; }

		/// <summary>
		/// Nodes in the group
		/// </summary>
		public List<GetNodeResponse> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Group">The group to construct from</param>
		/// <param name="Groups">Other groups in this graph</param>
		public GetGroupResponse(INodeGroup Group, IReadOnlyList<INodeGroup> Groups)
		{
			this.AgentType = Group.AgentType;
			this.Nodes = Group.Nodes.ConvertAll(x => new GetNodeResponse(x, Groups));
		}
	}

	/// <summary>
	/// Information about an aggregate
	/// </summary>
	public class GetAggregateResponse
	{
		/// <summary>
		/// Name of the aggregate
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Nodes which must be part of the job for the aggregate to be shown
		/// </summary>
		public List<string> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Aggregate">The aggregate to construct from</param>
		/// <param name="Groups">List of groups in this graph</param>
		public GetAggregateResponse(IAggregate Aggregate, IReadOnlyList<INodeGroup> Groups)
		{
			Name = Aggregate.Name;
			Nodes = Aggregate.Nodes.ConvertAll(x => x.ToNode(Groups).Name);
		}
	}

	/// <summary>
	/// Information about a label
	/// </summary>
	public class GetLabelResponse
	{
		/// <summary>
		/// Category of the aggregate
		/// </summary>
		public string? Category => DashboardCategory;

		/// <summary>
		/// Label for this aggregate
		/// </summary>
		public string? Name => DashboardName;

		/// <summary>
		/// Name to show for this label on the dashboard
		/// </summary>
		public string? DashboardName { get; set; }

		/// <summary>
		/// Category to show this label in on the dashboard
		/// </summary>
		public string? DashboardCategory { get; set; }

		/// <summary>
		/// Name to show for this label in UGS
		/// </summary>
		public string? UgsName { get; set; }

		/// <summary>
		/// Project to display this label for in UGS
		/// </summary>
		public string? UgsProject { get; set; }

		/// <summary>
		/// Nodes which must be part of the job for the aggregate to be shown
		/// </summary>
		public List<string> RequiredNodes { get; set; }

		/// <summary>
		/// Nodes to include in the status of this aggregate, if present in the job
		/// </summary>
		public List<string> IncludedNodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">The label to construct from</param>
		/// <param name="Groups">List of groups in this graph</param>
		public GetLabelResponse(ILabel Label, IReadOnlyList<INodeGroup> Groups)
		{
			DashboardName = Label.DashboardName;
			DashboardCategory = Label.DashboardCategory;
			UgsName = Label.UgsName;
			UgsProject = Label.UgsProject;
			RequiredNodes = Label.RequiredNodes.ConvertAll(x => x.ToNode(Groups).Name);
			IncludedNodes = Label.IncludedNodes.ConvertAll(x => x.ToNode(Groups).Name);
		}
	}

	/// <summary>
	/// Information about a graph
	/// </summary>
	public class GetGraphResponse
	{
		/// <summary>
		/// The hash of the graph
		/// </summary>
		public string Hash { get; set; }

		/// <summary>
		/// Array of nodes for this job
		/// </summary>
		public List<GetGroupResponse>? Groups { get; set; }

		/// <summary>
		/// List of aggregates
		/// </summary>
		public List<GetAggregateResponse>? Aggregates { get; set; }

		/// <summary>
		/// List of labels for the graph
		/// </summary>
		public List<GetLabelResponse>? Labels { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Graph">The graph to construct from</param>
		public GetGraphResponse(IGraph Graph)
		{
			Hash = Graph.Id.ToString();
			if (Graph.Groups.Count > 0)
			{
				Groups = Graph.Groups.ConvertAll(x => new GetGroupResponse(x, Graph.Groups));
			}
			if (Graph.Aggregates.Count > 0)
			{
				Aggregates = Graph.Aggregates.ConvertAll(x => new GetAggregateResponse(x, Graph.Groups));
			}
			if (Graph.Labels.Count > 0)
			{
				Labels = Graph.Labels.ConvertAll(x => new GetLabelResponse(x, Graph.Groups));
			}
		}
	}
}
