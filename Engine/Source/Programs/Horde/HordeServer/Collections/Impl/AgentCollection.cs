// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Redis;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeCommon.Rpc.Tasks;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;
	using PoolId = StringId<IPool>;

	/// <summary>
	/// Collection of agent documents
	/// </summary>
	public class AgentCollection : IAgentCollection
	{
		/// <summary>
		/// Legacy information about a device attached to an agent
		/// </summary>
		class DeviceCapabilities
		{
			[BsonIgnoreIfNull]
			public HashSet<string>? Properties { get; set; }

			[BsonIgnoreIfNull]
			public Dictionary<string, int>? Resources { get; set; }
		}

		/// <summary>
		/// Legacy capabilities of an agent
		/// </summary>
		class AgentCapabilities
		{
			public List<DeviceCapabilities> Devices { get; set; } = new List<DeviceCapabilities>();

			[BsonIgnoreIfNull]
			public HashSet<string>? Properties { get; set; }
		}

		/// <summary>
		/// Concrete implementation of an agent document
		/// </summary>
		class AgentDocument : IAgent
		{
			static IReadOnlyList<AgentLease> EmptyLeases = new List<AgentLease>();

			[BsonRequired, BsonId]
			public AgentId Id { get; set; }

			public ObjectId? SessionId { get; set; }
			public DateTime? SessionExpiresAt { get; set; }

			public AgentStatus Status { get; set; }

			[BsonRequired]
			public bool Enabled { get; set; }

			public bool Ephemeral { get; set; }

			[BsonIgnoreIfDefault, BsonDefaultValue(false)]
			public bool Deleted { get; set; }

			[BsonElement("Version2")]
			public string? Version { get; set; }

			public List<string>? Properties { get; set; }
			public Dictionary<string, int>? Resources { get; set; }

			[BsonIgnoreIfNull]
			public AgentSoftwareChannelName? Channel { get; set; }

			[BsonIgnoreIfNull]
			public string? LastUpgradeVersion { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? LastUpgradeTime { get; set; }

			public List<PoolId> DynamicPools { get; set; } = new List<PoolId>();
			public List<PoolId> Pools { get; set; } = new List<PoolId>();
			public bool RequestConform { get; set; }

			[BsonIgnoreIfNull]
			public bool RequestRestart { get; set; }

			[BsonIgnoreIfNull]
			public bool RequestShutdown { get; set; }

			public List<AgentWorkspace> Workspaces { get; set; } = new List<AgentWorkspace>();
			public DateTime LastConformTime { get; set; }

			[BsonIgnoreIfNull]
			public int? ConformAttemptCount { get; set; }

			public AgentCapabilities Capabilities { get; set; } = new AgentCapabilities();
			public List<AgentLease>? Leases { get; set; }
			public Acl? Acl { get; set; }
			public DateTime UpdateTime { get; set; }
			public uint UpdateIndex { get; set; }
			public string? Comment { get; set; }

			IReadOnlyList<PoolId> IAgent.DynamicPools => DynamicPools;
			IReadOnlyList<PoolId> IAgent.ExplicitPools => Pools;
			IReadOnlyList<AgentWorkspace> IAgent.Workspaces => Workspaces;
			IReadOnlyList<AgentLease> IAgent.Leases => Leases ?? EmptyLeases;
			IReadOnlyList<string> IAgent.Properties => Properties ?? Capabilities.Devices.FirstOrDefault()?.Properties?.ToList() ?? new List<string>();
			IReadOnlyDictionary<string, int> IAgent.Resources => Resources ?? Capabilities.Devices.FirstOrDefault()?.Resources ?? new Dictionary<string, int>();

			[BsonConstructor]
			private AgentDocument()
			{
			}

			public AgentDocument(AgentId Id, bool bEnabled, AgentSoftwareChannelName? Channel, List<PoolId> Pools)
			{
				this.Id = Id;
				this.Acl = new Acl();
				this.Enabled = bEnabled;
				this.Channel = Channel;
				this.Pools = Pools;
			}
		}

		readonly IMongoCollection<AgentDocument> Agents;
		readonly IAuditLog<AgentId> AuditLog;
		readonly RedisService RedisService;
		readonly RedisChannel<AgentId> UpdateEventChannel;

		/// <summary>
		/// Constructor
		/// </summary>
		public AgentCollection(DatabaseService DatabaseService, RedisService RedisService, IAuditLog<AgentId> AuditLog)
		{
			this.Agents = DatabaseService.GetCollection<AgentDocument>("Agents");
			this.RedisService = RedisService;
			this.UpdateEventChannel = new RedisChannel<AgentId>("agents/notify");
			this.AuditLog = AuditLog;

			if (!DatabaseService.ReadOnlyMode)
			{
				Agents.Indexes.CreateOne(new CreateIndexModel<AgentDocument>(Builders<AgentDocument>.IndexKeys.Ascending(x => x.Deleted).Ascending(x => x.Id).Ascending(x => x.Pools)));
			}
		}

		/// <inheritdoc/>
		public async Task<IAgent> AddAsync(AgentId Id, bool bEnabled, AgentSoftwareChannelName? Channel, List<PoolId>? Pools)
		{
			AgentDocument Agent = new AgentDocument(Id, bEnabled, Channel, Pools ?? new List<PoolId>());
			await Agents.InsertOneAsync(Agent);
			return Agent;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryDeleteAsync(IAgent AgentInterface)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			UpdateDefinition<AgentDocument> Update = Builders<AgentDocument>.Update.Set(x => x.Deleted, true);
			return await TryUpdateAsync(Agent, Update);
		}

		/// <inheritdoc/>
		public async Task ForceDeleteAsync(AgentId AgentId)
		{
			await Agents.DeleteOneAsync(x => x.Id == AgentId);
		}

		/// <inheritdoc/>
		public async Task<IAgent?> GetAsync(AgentId AgentId)
		{
			return await Agents.Find<AgentDocument>(x => x.Id == AgentId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<List<IAgent>> FindAsync(ObjectId? Pool, string? PoolId, DateTime? ModifiedAfter, AgentStatus? Status, int? Index, int? Count)
		{
			FilterDefinitionBuilder<AgentDocument> FilterBuilder = new FilterDefinitionBuilder<AgentDocument>();

			FilterDefinition<AgentDocument> Filter = FilterBuilder.Ne(x => x.Deleted, true);
			if (Pool != null)
			{
				Filter &= FilterBuilder.Eq(nameof(AgentDocument.Pools), Pool);
			}
			
			if (PoolId != null)
			{
				Filter &= FilterBuilder.Eq(nameof(AgentDocument.Pools), PoolId);
			}
			
			if (ModifiedAfter != null)
			{
				Filter &= FilterBuilder.Gt(x => x.UpdateTime, ModifiedAfter.Value);
			}
			
			if (Status != null)
			{
				Filter &= FilterBuilder.Eq(x => x.Status, Status.Value);
			}

			IFindFluent<AgentDocument, AgentDocument> Search = Agents.Find(Filter);
			if (Index != null)
			{
				Search = Search.Skip(Index.Value);
			}
			if (Count != null)
			{
				Search = Search.Limit(Count.Value);
			}

			List<AgentDocument> Results = await Search.ToListAsync();
			return Results.ConvertAll<IAgent>(x => x);
		}

		/// <inheritdoc/>
		public async Task<List<IAgent>> FindExpiredAsync(DateTime UtcNow, int MaxAgents)
		{
			List<AgentDocument> Results = await Agents.Find(x => x.SessionId.HasValue && !(x.SessionExpiresAt > UtcNow)).Limit(MaxAgents).ToListAsync();
			return Results.ConvertAll<IAgent>(x => x);
		}

		/// <summary>
		/// Update a single document
		/// </summary>
		/// <param name="Current">The document to update</param>
		/// <param name="Update">The update definition</param>
		/// <returns>True if the agent was updated</returns>
		private async Task<AgentDocument?> TryUpdateAsync(AgentDocument Current, UpdateDefinition<AgentDocument> Update)
		{
			uint PrevUpdateIndex = Current.UpdateIndex++;
			Current.UpdateTime = DateTime.UtcNow;

			Expression<Func<AgentDocument, bool>> Filter = x => x.Id == Current.Id && x.UpdateIndex == PrevUpdateIndex;
			UpdateDefinition<AgentDocument> UpdateWithIndex = Update.Set(x => x.UpdateIndex, Current.UpdateIndex).Set(x => x.UpdateTime, Current.UpdateTime);

			return await Agents.FindOneAndUpdateAsync<AgentDocument>(Filter, UpdateWithIndex, new FindOneAndUpdateOptions<AgentDocument, AgentDocument> { ReturnDocument = ReturnDocument.After });
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryUpdateSettingsAsync(IAgent AgentInterface, bool? Enabled = null, bool? RequestConform = null, bool? RequestRestart = null, bool? RequestShutdown = null, AgentSoftwareChannelName? Channel = null, List<PoolId>? Pools = null, Acl? Acl = null, string? Comment = null)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			// Update the database
			UpdateDefinitionBuilder<AgentDocument> UpdateBuilder = new UpdateDefinitionBuilder<AgentDocument>();

			List<UpdateDefinition<AgentDocument>> Updates = new List<UpdateDefinition<AgentDocument>>();
			if (Pools != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Pools, Pools));
			}
			if (Enabled != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Enabled, Enabled.Value));
			}
			if (RequestConform != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.RequestConform, RequestConform.Value));
				Updates.Add(UpdateBuilder.Unset(x => x.ConformAttemptCount));
			}
			if (RequestRestart != null)
			{
				if (RequestRestart.Value)
				{
					Updates.Add(UpdateBuilder.Set(x => x.RequestRestart, true));
				}
				else
				{
					Updates.Add(UpdateBuilder.Unset(x => x.RequestRestart));
				}
			}
			if (RequestShutdown != null)
			{
				if (RequestShutdown.Value)
				{
					Updates.Add(UpdateBuilder.Set(x => x.RequestShutdown, true));
				}
				else
				{
					Updates.Add(UpdateBuilder.Unset(x => x.RequestShutdown));
				}
			}
			if (Channel != null)
			{
				if (Channel.Value == AgentSoftwareService.DefaultChannelName)
				{
					Updates.Add(UpdateBuilder.Unset(x => x.Channel));
				}
				else
				{
					Updates.Add(UpdateBuilder.Set(x => x.Channel, Channel));
				}
			}
			if (Acl != null)
			{
				Updates.Add(Acl.CreateUpdate<AgentDocument>(x => x.Acl!, Acl));
			}
			if (Comment != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Comment, Comment));
			}

			// Apply the update
			IAgent? NewAgent = await TryUpdateAsync(Agent, UpdateBuilder.Combine(Updates));
			if (NewAgent != null)
			{
				if (NewAgent.RequestRestart != Agent.RequestRestart || NewAgent.RequestConform != Agent.RequestConform || NewAgent.RequestShutdown != Agent.RequestShutdown || NewAgent.Channel != Agent.Channel)
				{
					await PublishUpdateEventAsync(Agent.Id);
				}
			}
			return NewAgent;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryUpdateSessionAsync(IAgent AgentInterface, AgentStatus? Status, DateTime? SessionExpiresAt, IReadOnlyList<string>? Properties, IReadOnlyDictionary<string, int>? Resources, IReadOnlyList<PoolId>? DynamicPools, List<AgentLease>? Leases)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			// Create an update definition for the agent
			UpdateDefinitionBuilder<AgentDocument> UpdateBuilder = Builders<AgentDocument>.Update;
			List<UpdateDefinition<AgentDocument>> Updates = new List<UpdateDefinition<AgentDocument>>();

			if (Status != null && Agent.Status != Status.Value)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Status, Status.Value));
			}
			if (SessionExpiresAt != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.SessionExpiresAt, SessionExpiresAt.Value));
			}
			if (Properties != null)
			{
				List<string> NewProperties = Properties.OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToList();
				if (!AgentInterface.Properties.SequenceEqual(NewProperties, StringComparer.Ordinal))
				{
					Updates.Add(UpdateBuilder.Set(x => x.Properties, NewProperties));
				}
			}
			if (Resources != null && !ResourcesEqual(Resources, AgentInterface.Resources))
			{
				Updates.Add(UpdateBuilder.Set(x => x.Resources, new Dictionary<string, int>(Resources)));
			}
			if (DynamicPools != null && !DynamicPools.SequenceEqual(Agent.DynamicPools))
			{
				Updates.Add(UpdateBuilder.Set(x => x.DynamicPools, new List<PoolId>(DynamicPools)));
			}
			if (Leases != null)
			{
				foreach (AgentLease Lease in Leases)
				{
					if (Lease.Payload != null && (Agent.Leases == null || !Agent.Leases.Any(x => x.Id == Lease.Id)))
					{
						Any Payload = Any.Parser.ParseFrom(Lease.Payload.ToArray());
						if (Payload.TryUnpack(out ConformTask ConformTask))
						{
							int NewConformAttemptCount = (Agent.ConformAttemptCount ?? 0) + 1;
							Updates.Add(UpdateBuilder.Set(x => x.ConformAttemptCount, NewConformAttemptCount));
							Updates.Add(UpdateBuilder.Set(x => x.LastConformTime, DateTime.UtcNow));
						}
						else if (Payload.TryUnpack(out UpgradeTask UpgradeTask))
						{
							Updates.Add(UpdateBuilder.Set(x => x.LastUpgradeVersion, UpgradeTask.SoftwareId));
							Updates.Add(UpdateBuilder.Set(x => x.LastUpgradeTime, DateTime.UtcNow));
						}
					}
				}

				Updates.Add(UpdateBuilder.Set(x => x.Leases, Leases));
			}

			// If there are no new updates, return immediately. This is important for preventing UpdateSession calls from returning immediately.
			if (Updates.Count == 0)
			{
				return Agent;
			}

			// Update the agent, and try to create new lease documents if we succeed
			return await TryUpdateAsync(Agent, UpdateBuilder.Combine(Updates));
		}

		static bool ResourcesEqual(IReadOnlyDictionary<string, int> DictA, IReadOnlyDictionary<string, int> DictB)
		{
			if (DictA.Count != DictB.Count)
			{
				return false;
			}

			foreach (KeyValuePair<string, int> Pair in DictA)
			{
				int Value;
				if (!DictB.TryGetValue(Pair.Key, out Value) || Value != Pair.Value)
				{
					return false;
				}
			}

			return true;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryUpdateWorkspacesAsync(IAgent AgentInterface, List<AgentWorkspace> Workspaces, bool RequestConform)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;
			DateTime LastConformTime = DateTime.UtcNow;

			// Set the new workspaces
			UpdateDefinition<AgentDocument> Update = Builders<AgentDocument>.Update.Set(x => x.Workspaces, Workspaces);
			Update = Update.Set(x => x.LastConformTime, LastConformTime);
			Update = Update.Unset(x => x.ConformAttemptCount);
			Update = Update.Set(x => x.RequestConform, RequestConform);

			// Update the agent
			return await TryUpdateAsync(Agent, Update);
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryStartSessionAsync(IAgent AgentInterface, ObjectId SessionId, DateTime SessionExpiresAt, AgentStatus Status, IReadOnlyList<string> Properties, IReadOnlyDictionary<string, int> Resources, IReadOnlyList<PoolId> DynamicPools, string? Version)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;
			List<string> NewProperties = Properties.OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToList();
			Dictionary<string, int> NewResources = new Dictionary<string, int>(Resources);
			List<PoolId> NewDynamicPools = new List<PoolId>(DynamicPools);

			// Reset the agent to use the new session
			UpdateDefinitionBuilder<AgentDocument> UpdateBuilder = Builders<AgentDocument>.Update;

			List<UpdateDefinition<AgentDocument>> Updates = new List<UpdateDefinition<AgentDocument>>();
			Updates.Add(UpdateBuilder.Set(x => x.SessionId, SessionId));
			Updates.Add(UpdateBuilder.Set(x => x.SessionExpiresAt, SessionExpiresAt));
			Updates.Add(UpdateBuilder.Set(x => x.Status, Status));
			Updates.Add(UpdateBuilder.Unset(x => x.Leases));
			Updates.Add(UpdateBuilder.Unset(x => x.Deleted));
			Updates.Add(UpdateBuilder.Set(x => x.Properties, NewProperties));
			Updates.Add(UpdateBuilder.Set(x => x.Resources, NewResources));
			Updates.Add(UpdateBuilder.Set(x => x.DynamicPools, NewDynamicPools));
			Updates.Add(UpdateBuilder.Set(x => x.Version, Version));
			Updates.Add(UpdateBuilder.Unset(x => x.RequestRestart));
			Updates.Add(UpdateBuilder.Unset(x => x.RequestShutdown));

			// Apply the update
			return await TryUpdateAsync(Agent, UpdateBuilder.Combine(Updates));
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryTerminateSessionAsync(IAgent AgentInterface)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;
			UpdateDefinition<AgentDocument> Update = new BsonDocument();

			Update = Update.Unset(x => x.SessionId);
			Update = Update.Unset(x => x.SessionExpiresAt);
			Update = Update.Unset(x => x.Leases);

			bool bDeleted = Agent.Deleted || Agent.Ephemeral;
			if (bDeleted != Agent.Deleted)
			{
				Update = Update.Set(x => x.Deleted, Agent.Deleted);
			}

			return await TryUpdateAsync(Agent, Update);
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryAddLeaseAsync(IAgent AgentInterface, AgentLease NewLease)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			List<AgentLease> Leases = new List<AgentLease>();
			if (Agent.Leases != null)
			{
				Leases.AddRange(Agent.Leases);
			}
			Leases.Add(NewLease);

			UpdateDefinition<AgentDocument> Update = Builders<AgentDocument>.Update.Set(x => x.Leases, Leases);
			return await TryUpdateAsync(Agent, Update);
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryCancelLeaseAsync(IAgent AgentInterface, int LeaseIdx)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			UpdateDefinition<AgentDocument> Update = Builders<AgentDocument>.Update.Set(x => x.Leases![LeaseIdx].State, LeaseState.Cancelled);
			IAgent? NewAgent = await TryUpdateAsync(Agent, Update);
			if (NewAgent != null)
			{
				await PublishUpdateEventAsync(Agent.Id);
			}
			return NewAgent;
		}

		/// <inheritdoc/>
		public IAuditLogChannel<AgentId> GetLogger(AgentId AgentId)
		{
			return AuditLog[AgentId];
		}

		/// <inheritdoc/>
		Task PublishUpdateEventAsync(AgentId AgentId)
		{
			return RedisService.Database.PublishAsync(UpdateEventChannel, AgentId);
		}

		/// <inheritdoc/>
		public async Task<IDisposable> SubscribeToUpdateEventsAsync(Action<AgentId> OnUpdate)
		{
			return await RedisService.Multiplexer.GetSubscriber().SubscribeAsync(UpdateEventChannel, (Channel, AgentId) => OnUpdate(AgentId));
		}
	}
}
