// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of graph documents
	/// </summary>
	public class GraphCollection : IGraphCollection
	{
		/// <summary>
		/// Represents a node in the graph
		/// </summary>
		[DebuggerDisplay("{Name}")]
		class Node : INode
		{
			[BsonRequired]
			public string Name { get; set; }

			[BsonIgnoreIfNull]
			public NodeRef[] InputDependencies { get; set; }

			[BsonIgnoreIfNull]
			public NodeRef[] OrderDependencies { get; set; }

			public Priority Priority { get; set; }
			public bool AllowRetry { get; set; } = true;
			public bool RunEarly { get; set; }
			public bool Warnings { get; set; } = true;

			[BsonIgnoreIfNull]
			public Dictionary<string, string>? Credentials { get; set; }

			[BsonIgnoreIfNull]
			public Dictionary<string, string>? Properties { get; set; }

			IReadOnlyDictionary<string, string>? INode.Credentials => Credentials;
			IReadOnlyDictionary<string, string>? INode.Properties => Properties;

			[BsonConstructor]
			private Node()
			{
				Name = null!;
				InputDependencies = null!;
				OrderDependencies = null!;
			}

			public Node(string Name, NodeRef[] InputDependencies, NodeRef[] OrderDependencies, Priority Priority, bool AllowRetry, bool RunEarly, bool Warnings, Dictionary<string, string>? Credentials, Dictionary<string, string>? Properties)
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

		class NodeGroup : INodeGroup
		{
			public string AgentType { get; set; }
			public List<Node> Nodes { get; set; }

			IReadOnlyList<INode> INodeGroup.Nodes => Nodes;

			/// <summary>
			/// Private constructor for BSON serializer
			/// </summary>
			[BsonConstructor]
			private NodeGroup()
			{
				AgentType = null!;
				Nodes = null!;
			}

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="AgentType">The type of agent to execute this group</param>
			/// <param name="Nodes">Nodes to execute</param>
			public NodeGroup(string AgentType, List<Node> Nodes)
			{
				this.AgentType = AgentType;
				this.Nodes = Nodes;
			}
		}

		class Aggregate : IAggregate
		{
			public string Name { get; set; }
			public List<NodeRef> Nodes { get; set; }

			IReadOnlyList<NodeRef> IAggregate.Nodes => Nodes;

			private Aggregate()
			{
				Name = null!;
				Nodes = new List<NodeRef>();
			}

			public Aggregate(string Name, List<NodeRef> Nodes)
			{
				this.Name = Name;
				this.Nodes = Nodes;
			}
		}

		class Label : ILabel
		{
			[BsonIgnoreIfNull]
			public string? Name { get; set; }

			[BsonIgnoreIfNull]
			public string? Category { get; set; }

			[BsonIgnoreIfNull]
			public string? DashboardName { get; set; }

			[BsonIgnoreIfNull]
			public string? DashboardCategory { get; set; }

			[BsonIgnoreIfNull]
			public string? UgsName { get; set; }

			[BsonIgnoreIfNull]
			public string? UgsProject { get; set; }

			[BsonIgnoreIfNull]
			public LabelChange Change { get; set; }

			public List<NodeRef> RequiredNodes { get; set; }
			public List<NodeRef> IncludedNodes { get; set; }

			string? ILabel.DashboardName => DashboardName ?? Name;
			string? ILabel.DashboardCategory => DashboardCategory ?? Category;

			private Label()
			{
				RequiredNodes = new List<NodeRef>();
				IncludedNodes = new List<NodeRef>();
			}

			public Label(string? DashboardName, string? DashboardCategory, string? UgsName, string? UgsProject, LabelChange Change, List<NodeRef> RequiredNodes, List<NodeRef> IncludedNodes)
			{
				this.DashboardName = DashboardName;
				this.DashboardCategory = DashboardCategory;
				this.UgsName = UgsName;
				this.UgsProject = UgsProject;
				this.Change = Change;
				this.RequiredNodes = RequiredNodes;
				this.IncludedNodes = IncludedNodes;
			}
		}

		class GraphDocument : IGraph
		{
			public static GraphDocument Empty { get; } = new GraphDocument(new List<NodeGroup>(), new List<Aggregate>(), new List<Label>());

			[BsonRequired, BsonId]
			public ContentHash Id { get; private set; } = ContentHash.Empty;

			public int Schema { get; set; }
			public List<NodeGroup> Groups { get; private set; } = new List<NodeGroup>();
			public List<Aggregate> Aggregates { get; private set; } = new List<Aggregate>();
			public List<Label> Labels { get; private set; } = new List<Label>();

			[BsonIgnore]
			IReadOnlyDictionary<string, NodeRef>? CachedNodeNameToRef;

			IReadOnlyList<INodeGroup> IGraph.Groups => Groups;
			IReadOnlyList<IAggregate> IGraph.Aggregates => Aggregates;
			IReadOnlyList<ILabel> IGraph.Labels => Labels;

			[BsonConstructor]
			private GraphDocument()
			{
			}

			public GraphDocument(List<NodeGroup> Groups, List<Aggregate> Aggregates, List<Label> Labels)
			{
				this.Groups = Groups;
				this.Aggregates = Aggregates;
				this.Labels = Labels;
				this.Id = ContentHash.SHA1(BsonExtensionMethods.ToBson(this));
			}

			public GraphDocument(GraphDocument BaseGraph, List<NewGroup>? NewGroupRequests, List<NewAggregate>? NewAggregateRequests, List<NewLabel>? NewLabelRequests)
			{
				Dictionary<string, NodeRef> NodeNameToRef = new Dictionary<string, NodeRef>(BaseGraph.GetNodeNameToRef(), StringComparer.OrdinalIgnoreCase);

				// Update the new list of groups
				List<NodeGroup> NewGroups = new List<NodeGroup>(BaseGraph.Groups);
				if (NewGroupRequests != null)
				{
					foreach (NewGroup NewGroupRequest in NewGroupRequests)
					{
						List<Node> Nodes = new List<Node>();
						foreach (NewNode NewNodeRequest in NewGroupRequest.Nodes)
						{
							int NodeIdx = Nodes.Count;

							Priority Priority = NewNodeRequest.Priority ?? Priority.Normal;
							bool bAllowRetry = NewNodeRequest.AllowRetry ?? true;
							bool bRunEarly = NewNodeRequest.RunEarly ?? false;
							bool bWarnings = NewNodeRequest.Warnings ?? true;

							NodeRef[] InputDependencies = (NewNodeRequest.InputDependencies == null) ? Array.Empty<NodeRef>() : NewNodeRequest.InputDependencies.Select(x => NodeNameToRef[x]).ToArray();
							NodeRef[] OrderDependencies = (NewNodeRequest.OrderDependencies == null) ? Array.Empty<NodeRef>() : NewNodeRequest.OrderDependencies.Select(x => NodeNameToRef[x]).ToArray();
							OrderDependencies = OrderDependencies.Union(InputDependencies).ToArray();
							Nodes.Add(new Node(NewNodeRequest.Name, InputDependencies, OrderDependencies, Priority, bAllowRetry, bRunEarly, bWarnings, NewNodeRequest.Credentials, NewNodeRequest.Properties));

							NodeNameToRef.Add(NewNodeRequest.Name, new NodeRef(NewGroups.Count, NodeIdx));
						}
						NewGroups.Add(new NodeGroup(NewGroupRequest.AgentType, Nodes));
					}
				}

				// Update the list of aggregates
				List<Aggregate> NewAggregates = new List<Aggregate>(BaseGraph.Aggregates);
				if (NewAggregateRequests != null)
				{
					foreach (NewAggregate NewAggregateRequest in NewAggregateRequests)
					{
						List<NodeRef> Nodes = NewAggregateRequest.Nodes.ConvertAll(x => NodeNameToRef[x]);
						NewAggregates.Add(new Aggregate(NewAggregateRequest.Name, Nodes));
					}
				}

				// Update the list of labels
				List<Label> NewLabels = new List<Label>(BaseGraph.Labels);
				if (NewLabelRequests != null)
				{
					foreach (NewLabel NewLabelRequest in NewLabelRequests)
					{
						List<NodeRef> RequiredNodes = NewLabelRequest.RequiredNodes.ConvertAll(x => NodeNameToRef[x]);
						List<NodeRef> IncludedNodes = NewLabelRequest.IncludedNodes.ConvertAll(x => NodeNameToRef[x]);
						NewLabels.Add(new Label(NewLabelRequest.DashboardName, NewLabelRequest.DashboardCategory, NewLabelRequest.UgsName, NewLabelRequest.UgsProject, NewLabelRequest.Change, RequiredNodes, IncludedNodes));
					}
				}

				// Create the new arrays
				Groups = NewGroups;
				Aggregates = NewAggregates;
				Labels = NewLabels;

				// Create the new graph, and save the generated node lookup into it
				CachedNodeNameToRef = NodeNameToRef;

				// Compute the hash
				Id = ContentHash.SHA1(BsonExtensionMethods.ToBson(this));
			}

			public IReadOnlyDictionary<string, NodeRef> GetNodeNameToRef()
			{
				if (CachedNodeNameToRef == null)
				{
					Dictionary<string, NodeRef> NodeNameToRef = new Dictionary<string, NodeRef>(StringComparer.OrdinalIgnoreCase);
					for (int GroupIdx = 0; GroupIdx < Groups.Count; GroupIdx++)
					{
						List<Node> Nodes = Groups[GroupIdx].Nodes;
						for (int NodeIdx = 0; NodeIdx < Nodes.Count; NodeIdx++)
						{
							Node Node = Nodes[NodeIdx];
							NodeNameToRef[Node.Name] = new NodeRef(GroupIdx, NodeIdx);
						}
					}
					CachedNodeNameToRef = NodeNameToRef;
				}
				return CachedNodeNameToRef;
			}
		}

		/// <summary>
		/// Stores information about a cached graph
		/// </summary>
		class CachedGraph
		{
			/// <summary>
			/// Time at which the graph was last accessed
			/// </summary>
			public long LastAccessTime
			{
				get { return LastAccessTimePrivate; }
			}

			/// <summary>
			/// Backing value for <see cref="LastAccessTime"/>
			/// </summary>
			private long LastAccessTimePrivate;

			/// <summary>
			/// The graph instance
			/// </summary>
			public IGraph Graph { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Graph">The graph to store</param>
			public CachedGraph(IGraph Graph)
			{
				this.LastAccessTimePrivate = Stopwatch.GetTimestamp();
				this.Graph = Graph;
			}

			/// <summary>
			/// Update the last access time
			/// </summary>
			public void Touch()
			{
				for (; ; )
				{
					long Time = Stopwatch.GetTimestamp();
					long LastAccessTimeCopy = LastAccessTimePrivate;
					if (Time < LastAccessTimeCopy || Interlocked.CompareExchange(ref LastAccessTimePrivate, Time, LastAccessTimeCopy) == LastAccessTimeCopy)
					{
						break;
					}
				}
			}
		}

		/// <summary>
		/// The jobs collection
		/// </summary>
		IMongoCollection<GraphDocument> Graphs;

		/// <summary>
		/// Maximum number of graphs to keep in the cache
		/// </summary>
		const int MaxGraphs = 1000;

		/// <summary>
		/// Cache of recently accessed graphs
		/// </summary>
		ConcurrentDictionary<ContentHash, CachedGraph> CachedGraphs = new ConcurrentDictionary<ContentHash, CachedGraph>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public GraphCollection(DatabaseService DatabaseService)
		{
			Graphs = DatabaseService.GetCollection<GraphDocument>("Graphs");
		}

		/// <summary>
		/// Adds a new graph document
		/// </summary>
		/// <param name="Graph">The graph to add</param>
		/// <returns>Async task</returns>
		async Task AddAsync(GraphDocument Graph)
		{
			if (!await Graphs.Find(x => x.Id == Graph.Id).AnyAsync())
			{
				try
				{
					await Graphs.InsertOneAsync(Graph);
				}
				catch (MongoWriteException Ex)
				{
					if (Ex.WriteError.Category != ServerErrorCategory.DuplicateKey)
					{
						throw;
					}
				}
			}
		}

		/// <inheritdoc/>
		public async Task<IGraph> AddAsync(ITemplate Template)
		{
			Node Node = new Node(IJob.SetupNodeName, Array.Empty<NodeRef>(), Array.Empty<NodeRef>(), Priority.High, true, false, true, null, null);
			NodeGroup Group = new NodeGroup(Template.InitialAgentType ?? "Win64", new List<Node> { Node });

			GraphDocument Graph = new GraphDocument(new List<NodeGroup> { Group }, new List<Aggregate>(), new List<Label>());
			await AddAsync(Graph);
			return Graph;
		}

		/// <inheritdoc/>
		public async Task<IGraph> AppendAsync(IGraph? BaseGraph, List<NewGroup>? NewGroupRequests, List<NewAggregate>? NewAggregateRequests, List<NewLabel>? NewLabelRequests)
		{
			GraphDocument Graph = new GraphDocument((GraphDocument?)BaseGraph ?? GraphDocument.Empty, NewGroupRequests, NewAggregateRequests, NewLabelRequests);
			await AddAsync(Graph);
			return Graph;
		}

		/// <inheritdoc/>
		public async Task<IGraph> GetAsync(ContentHash? Hash)
		{
			// Special case for an empty graph request
			if (Hash == null || Hash == ContentHash.Empty || Hash == GraphDocument.Empty.Id)
			{
				return GraphDocument.Empty;
			}

			// Try to read the graph from the cache
			CachedGraph? CachedGraph;
			if (CachedGraphs.TryGetValue(Hash, out CachedGraph))
			{
				// Update the last access time
				CachedGraph.Touch();
			}
			else
			{
				// Trim the cache
				while (CachedGraphs.Count > MaxGraphs)
				{
					ContentHash? RemoveHash = CachedGraphs.OrderBy(x => x.Value.LastAccessTime).Select(x => x.Key).FirstOrDefault();
					if (RemoveHash == null || RemoveHash == ContentHash.Empty)
					{
						break;
					}
					CachedGraphs.TryRemove(RemoveHash, out _);
				}

				// Create the new entry
				CachedGraph = new CachedGraph(await Graphs.Find<GraphDocument>(x => x.Id == Hash).FirstAsync());
				CachedGraphs.TryAdd(Hash, CachedGraph);
			}
			return CachedGraph.Graph;
		}

		/// <inheritdoc/>
		public async Task<List<IGraph>> FindAllAsync(ContentHash[]? Hashes, int? Index, int? Count)
		{
			FilterDefinitionBuilder<GraphDocument> FilterBuilder = Builders<GraphDocument>.Filter;

			FilterDefinition<GraphDocument> Filter = FilterBuilder.Empty;
			if (Hashes != null)
			{
				Filter &= FilterBuilder.In(x => x.Id, Hashes);
			}

			List<GraphDocument> Results;
			IFindFluent<GraphDocument, GraphDocument> Search = Graphs.Find(Filter);
			if (Index != null)
			{
				Search = Search.Skip(Index.Value);
			}
			if (Count != null)
			{
				Search = Search.Limit(Count.Value);
			}

			Results = await Search.ToListAsync();
			return Results.ConvertAll<IGraph>(x => x);
		}
	}
}
