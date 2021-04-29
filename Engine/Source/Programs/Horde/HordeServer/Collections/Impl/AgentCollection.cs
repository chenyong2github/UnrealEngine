// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeCommon.Rpc.Tasks;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Threading.Tasks;

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;
using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of agent documents
	/// </summary>
	public class AgentCollection : IAgentCollection
	{
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
			public AgentSoftwareVersion? Version { get; set; }

			[BsonIgnoreIfNull]
			public AgentSoftwareChannelName? Channel { get; set; }

			[BsonIgnoreIfNull]
			public string? LastUpgradeVersion { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? LastUpgradeTime { get; set; }

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

			IReadOnlyList<PoolId> IAgent.ExplicitPools => Pools;
			IReadOnlyList<AgentWorkspace> IAgent.Workspaces => Workspaces;
			IReadOnlyList<AgentLease> IAgent.Leases => Leases ?? EmptyLeases;

			[BsonConstructor]
			private AgentDocument()
			{
			}

			public AgentDocument(AgentId Id, bool bEnabled, bool bEphemeral, AgentSoftwareChannelName? Channel, List<PoolId> Pools)
			{
				this.Id = Id;
				this.Acl = new Acl();
				this.Enabled = bEnabled;
				this.Ephemeral = bEphemeral;
				this.Deleted = bEphemeral;
				this.Channel = Channel;
				this.Pools = Pools;
			}
		}

		/// <summary>
		/// Collection of agent documents
		/// </summary>
		readonly IMongoCollection<AgentDocument> Agents;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public AgentCollection(DatabaseService DatabaseService)
		{
			Agents = DatabaseService.GetCollection<AgentDocument>("Agents");
			if (!DatabaseService.ReadOnlyMode)
			{
				Agents.Indexes.CreateOne(new CreateIndexModel<AgentDocument>(Builders<AgentDocument>.IndexKeys.Ascending(x => x.Deleted).Ascending(x => x.Id).Ascending(x => x.Pools)));
			}
		}

		/// <inheritdoc/>
		public async Task<IAgent> AddAsync(AgentId Id, bool bEnabled, bool bEphemeral, AgentSoftwareChannelName? Channel, List<PoolId>? Pools)
		{
			AgentDocument Agent = new AgentDocument(Id, bEnabled, bEphemeral, Channel, Pools ?? new List<PoolId>());
			await Agents.InsertOneAsync(Agent);
			return Agent;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryDeleteAsync(IAgent AgentInterface)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			UpdateDefinition<AgentDocument> Update = Builders<AgentDocument>.Update.Set(x => x.Deleted, true);
			if (await TryUpdateAsync(Agent, Update))
			{
				Agent.Deleted = true;
				return Agent;
			}
			return null;
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
		private async Task<bool> TryUpdateAsync(AgentDocument Current, UpdateDefinition<AgentDocument> Update)
		{
			uint PrevUpdateIndex = Current.UpdateIndex++;
			Current.UpdateTime = DateTime.UtcNow;

			Expression<Func<AgentDocument, bool>> Filter = x => x.Id == Current.Id && x.UpdateIndex == PrevUpdateIndex;
			UpdateDefinition<AgentDocument> UpdateWithIndex = Update.Set(x => x.UpdateIndex, Current.UpdateIndex).Set(x => x.UpdateTime, Current.UpdateTime);

			UpdateResult Result = await Agents.UpdateOneAsync<AgentDocument>(Filter, UpdateWithIndex);
			return Result.ModifiedCount == 1;
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
			if (Updates.Count != 0 && !await TryUpdateAsync(Agent, UpdateBuilder.Combine(Updates)))
			{
				return null;
			}

			// Update the document
			if (Pools != null)
			{
				Agent.Pools = Pools;
			}
			if (Enabled != null)
			{
				Agent.Enabled = Enabled.Value;
			}
			if (RequestConform != null)
			{
				Agent.RequestConform = RequestConform.Value;
			}
			if (RequestRestart != null)
			{
				Agent.RequestRestart = RequestRestart.Value;
			}
			if (RequestShutdown != null)
			{
				Agent.RequestShutdown = RequestShutdown.Value;
			}
			if (Channel != null)
			{
				Agent.Channel = (Channel.Value == AgentSoftwareService.DefaultChannelName) ? null : Channel;
			}
			if (Acl != null)
			{
				Agent.Acl = Acl;
			}
			return Agent;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryUpdateSessionAsync(IAgent AgentInterface, AgentStatus? Status, DateTime? SessionExpiresAt, AgentCapabilities? Capabilities, List<AgentLease>? Leases)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			// Create an update definition for the agent
			UpdateDefinitionBuilder<AgentDocument> UpdateBuilder = Builders<AgentDocument>.Update;
			List<UpdateDefinition<AgentDocument>> Updates = new List<UpdateDefinition<AgentDocument>>();

			if (Status != null && Agent.Status != Status.Value)
			{
				Agent.Status = Status.Value;
				Updates.Add(UpdateBuilder.Set(x => x.Status, Agent.Status));
			}
			if (SessionExpiresAt != null)
			{
				Agent.SessionExpiresAt = SessionExpiresAt.Value;
				Updates.Add(UpdateBuilder.Set(x => x.SessionExpiresAt, Agent.SessionExpiresAt));
			}
			if (Capabilities != null)
			{
				Agent.Capabilities = Capabilities;
				Updates.Add(UpdateBuilder.Set(x => x.Capabilities, Agent.Capabilities));
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
							Agent.ConformAttemptCount = (Agent.ConformAttemptCount ?? 0) + 1;
							Updates.Add(UpdateBuilder.Set(x => x.ConformAttemptCount, Agent.ConformAttemptCount));

							Agent.LastConformTime = DateTime.UtcNow;
							Updates.Add(UpdateBuilder.Set(x => x.LastConformTime, Agent.LastConformTime));
						}
						else if (Payload.TryUnpack(out UpgradeTask UpgradeTask))
						{
							Agent.LastUpgradeVersion = UpgradeTask.SoftwareId;
							Updates.Add(UpdateBuilder.Set(x => x.LastUpgradeVersion, Agent.LastUpgradeVersion));

							Agent.LastUpgradeTime = DateTime.UtcNow;
							Updates.Add(UpdateBuilder.Set(x => x.LastUpgradeTime, Agent.LastUpgradeTime));
						}
					}
				}

				Agent.Leases = Leases;
				Updates.Add(UpdateBuilder.Set(x => x.Leases, Agent.Leases));
			}

			// Update the agent, and try to create new lease documents if we succeed
			if (Updates.Count > 0 && !await TryUpdateAsync(Agent, UpdateBuilder.Combine(Updates)))
			{
				return null;
			}

			return Agent;
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
			if (!await TryUpdateAsync(Agent, Update))
			{
				return null;
			}

			// Update the document
			Agent.Workspaces = new List<AgentWorkspace>(Workspaces);
			Agent.LastConformTime = LastConformTime;
			Agent.ConformAttemptCount = null;
			Agent.RequestConform = RequestConform;

			return Agent;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryStartSessionAsync(IAgent AgentInterface, ObjectId SessionId, DateTime SessionExpiresAt, AgentStatus Status, AgentCapabilities Capabilities, AgentSoftwareVersion? Version)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			// Reset the agent to use the new session
			UpdateDefinitionBuilder<AgentDocument> UpdateBuilder = Builders<AgentDocument>.Update;

			List<UpdateDefinition<AgentDocument>> Updates = new List<UpdateDefinition<AgentDocument>>();
			Updates.Add(UpdateBuilder.Set(x => x.SessionId, SessionId));
			Updates.Add(UpdateBuilder.Set(x => x.SessionExpiresAt, SessionExpiresAt));
			Updates.Add(UpdateBuilder.Set(x => x.Status, Status));
			Updates.Add(UpdateBuilder.Unset(x => x.Leases));
			Updates.Add(UpdateBuilder.Unset(x => x.Deleted));
			Updates.Add(UpdateBuilder.Set(x => x.Capabilities, Capabilities));
			Updates.Add(UpdateBuilder.Set(x => x.Version, Version));
			Updates.Add(UpdateBuilder.Unset(x => x.RequestRestart));
			Updates.Add(UpdateBuilder.Unset(x => x.RequestShutdown));

			// Apply the update
			if (!await TryUpdateAsync(Agent, UpdateBuilder.Combine(Updates)))
			{
				return null;
			}

			// Update the document
			Agent.SessionId = SessionId;
			Agent.SessionExpiresAt = SessionExpiresAt;
			Agent.Status = Status;
			Agent.Leases = null;
			Agent.Deleted = false;
			Agent.Capabilities = Capabilities;
			Agent.Version = Version;

			return Agent;
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

			if (!await TryUpdateAsync(Agent, Update))
			{
				return null;
			}

			Agent.SessionId = null;
			Agent.SessionExpiresAt = null;
			Agent.Deleted = bDeleted;
			Agent.Leases = new List<AgentLease>();
			return Agent;
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
			if (!await TryUpdateAsync(Agent, Update))
			{
				return null;
			}

			Agent.Leases = Leases;
			return Agent;
		}

		/// <inheritdoc/>
		public async Task<IAgent?> TryCancelLeaseAsync(IAgent AgentInterface, int LeaseIdx)
		{
			AgentDocument Agent = (AgentDocument)AgentInterface;

			UpdateDefinition<AgentDocument> Update = Builders<AgentDocument>.Update.Set(x => x.Leases![LeaseIdx].State, LeaseState.Cancelled);
			if (!await TryUpdateAsync(Agent, Update))
			{
				return null;
			}

			Agent.Leases![LeaseIdx].State = LeaseState.Cancelled;
			return Agent;
		}
	}
}
