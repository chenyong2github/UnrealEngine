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
	public class CreateNodeRequest
	{
		/// <summary>
		/// The name of this node 
		/// </summary>
		[Required]
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
		/// Private constructor for serialization
		/// </summary>
		private CreateNodeRequest()
		{
			this.Name = null!;
		}

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
		public CreateNodeRequest(string Name, List<string>? InputDependencies = null, List<string>? OrderDependencies = null, Priority? Priority = null, bool? AllowRetry = null, bool? RunEarly = null, bool? Warnings = null, Dictionary<string, string>? Credentials = null, Dictionary<string, string>? Properties = null)
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
	public class CreateGroupRequest
	{
		/// <summary>
		/// The type of agent to execute this group
		/// </summary>
		[Required]
		public string AgentType { get; set; }

		/// <summary>
		/// Nodes in the group
		/// </summary>
		[Required]
		public List<CreateNodeRequest> Nodes { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private CreateGroupRequest()
		{
			AgentType = null!;
			Nodes = new List<CreateNodeRequest>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AgentType">The type of agent to execute this group</param>
		/// <param name="Nodes">Nodes in this group</param>
		public CreateGroupRequest(string AgentType, List<CreateNodeRequest> Nodes)
		{
			this.AgentType = AgentType;
			this.Nodes = Nodes;
		}
	}

	/// <summary>
	/// Information about a group of nodes
	/// </summary>
	public class CreateLabelRequest
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

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private CreateLabelRequest()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DashboardName">Name of this label</param>
		/// <param name="DashboardCategory">Category of this label</param>
		/// <param name="UgsName">Name of this label for UGS</param>
		/// <param name="UgsProject">Name of the project to show this label for in UGS</param>
		/// <param name="Change">Which change the label applies to</param>
		/// <param name="RequiredNodes">Nodes which must be part of the job for the aggregate to be shown</param>
		/// <param name="IncludedNodes">Nodes to include in the status of this aggregate, if present in the job</param>
		public CreateLabelRequest(string? DashboardName, string? DashboardCategory, string? UgsName, string? UgsProject, LabelChange Change, List<string> RequiredNodes, List<string> IncludedNodes)
		{
			this.DashboardName = String.IsNullOrEmpty(DashboardName)? null : DashboardName;
			this.DashboardCategory = String.IsNullOrEmpty(DashboardCategory)? null : DashboardCategory;
			this.UgsName = String.IsNullOrEmpty(UgsName)? null : UgsName;
			this.UgsProject = String.IsNullOrEmpty(UgsProject)? null : UgsProject;
			this.Change = Change;
			this.RequiredNodes = RequiredNodes;
			this.IncludedNodes = IncludedNodes;
		}
	}

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
		/// Parameterless constructor for serialization
		/// </summary>
		private GetNodeResponse()
		{
			this.Name = null!;
			this.InputDependencies = new List<string>();
			this.OrderDependencies = new List<string>();
			this.Credentials = new Dictionary<string, string>();
			this.Properties = new Dictionary<string, string>();
		}

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
