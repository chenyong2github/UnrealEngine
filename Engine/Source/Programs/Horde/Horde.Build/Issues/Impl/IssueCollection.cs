// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Redis.Utility;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Linq.Expressions;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	class IssueCollection : IIssueCollection
	{
		[SingletonDocument("5e4c226440ce25fa3207a9af")]
		class IssueLedger : SingletonBase
		{
			public int NextId { get; set; }
		}

		class Issue : IIssue
		{
			[BsonId]
			public int Id { get; set; }

			public string Summary { get; set; }

			[BsonIgnoreIfNull]
			public string? UserSummary { get; set; }

			[BsonIgnoreIfNull]
			public string? Description { get; set; }

			[BsonIgnoreIfNull]
			public IssueFingerprint? Fingerprint { get; set; }
			public List<IssueFingerprint>? Fingerprints { get; set; }

			public IssueSeverity Severity { get; set; }

			public bool? Promoted { get; set; }

			[BsonIgnoreIfNull]
			public UserId? OwnerId { get; set; }

			[BsonIgnoreIfNull]
			public UserId? DefaultOwnerId { get; set; }

			[BsonIgnoreIfNull]
			public UserId? NominatedById { get; set; }

			public DateTime CreatedAt { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? NominatedAt { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? AcknowledgedAt { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? ResolvedAt { get; set; }

			[BsonIgnoreIfNull]
			public UserId? ResolvedById { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? VerifiedAt { get; set; }

			public DateTime LastSeenAt { get; set; }

			[BsonIgnoreIfNull]
			public int? FixChange { get; set; }

			public List<IssueStream> Streams { get; set; } = new List<IssueStream>();

			public int MinSuspectChange { get; set; }
			public int MaxSuspectChange { get; set; }

			[BsonElement("NotifySuspects"), BsonIgnoreIfDefault(false)]
			public bool NotifySuspects_DEPRECATED { get; set; }

			[BsonElement("Suspects"), BsonIgnoreIfNull]
			public List<IssueSuspect>? Suspects_DEPRECATED { get; set; }

			[BsonIgnoreIfNull]
			public List<ObjectId>? ExcludeSpans { get; set; }

			public int UpdateIndex { get; set; }

			bool IIssue.Promoted => Promoted ?? NotifySuspects_DEPRECATED;
			IReadOnlyList<IIssueFingerprint> IIssue.Fingerprints => Fingerprints ?? ((Fingerprint == null)? new List<IssueFingerprint>() : new List<IssueFingerprint> { Fingerprint });
			UserId? IIssue.OwnerId => OwnerId ?? DefaultOwnerId ?? GetDefaultOwnerId();
			IReadOnlyList<IIssueStream> IIssue.Streams => Streams;
			DateTime IIssue.LastSeenAt => (LastSeenAt == default) ? DateTime.UtcNow : LastSeenAt;

			[BsonConstructor]
			private Issue()
			{
				Summary = String.Empty;
				Fingerprint = null!;
			}

			public Issue(int Id, string Summary)
			{
				this.Id = Id;
				this.Summary = Summary;
				this.CreatedAt = DateTime.UtcNow;
				this.LastSeenAt = DateTime.UtcNow;
			}

			UserId? GetDefaultOwnerId()
			{
				if (Suspects_DEPRECATED != null && Suspects_DEPRECATED.Count > 0)
				{
					UserId PossibleOwner = Suspects_DEPRECATED[0].AuthorId;
					if (Suspects_DEPRECATED.All(x => x.AuthorId == PossibleOwner) && Suspects_DEPRECATED.Any(x => x.DeclinedAt == null))
					{
						return Suspects_DEPRECATED[0].AuthorId;
					}
				}
				return null;
			}
		}

		class IssueStream : IIssueStream
		{
			public StreamId StreamId { get; set; }
			public bool? ContainsFix { get; set; }

			public IssueStream()
			{
			}

			public IssueStream(IIssueStream Other)
			{
				this.StreamId = Other.StreamId;
				this.ContainsFix = Other.ContainsFix;
			}
		}

		class IssueSuspect : IIssueSuspect
		{
			public ObjectId Id { get; set; }
			public int IssueId { get; set; }
			public UserId AuthorId { get; set; }
			public int Change { get; set; }
			public DateTime? DeclinedAt { get; set; }
			public DateTime? ResolvedAt { get; set; } // Degenerate

			private IssueSuspect()
			{
			}

			public IssueSuspect(int IssueId, NewIssueSuspectData NewSuspect, DateTime? ResolvedAt)
			{
				this.Id = ObjectId.GenerateNewId();
				this.IssueId = IssueId;
				this.AuthorId = NewSuspect.AuthorId;
				this.Change = NewSuspect.Change;
				this.ResolvedAt = ResolvedAt;
			}

			public IssueSuspect(int IssueId, IIssueSpanSuspect Suspect)
				: this(IssueId, Suspect.AuthorId, Suspect.OriginatingChange ?? Suspect.Change, null, null)
			{
			}

			public IssueSuspect(int IssueId, UserId AuthorId, int Change, DateTime? DeclinedAt, DateTime? ResolvedAt)
			{
				this.Id = ObjectId.GenerateNewId();
				this.IssueId = IssueId;
				this.AuthorId = AuthorId;
				this.Change = Change;
				this.DeclinedAt = DeclinedAt;
				this.ResolvedAt = ResolvedAt;
			}
		}

		class IssueFingerprint : IIssueFingerprint
		{
			public string Type { get; set; }
			public CaseInsensitiveStringSet Keys { get; set; }
			public CaseInsensitiveStringSet? RejectKeys { get; set; }

			[BsonConstructor]
			private IssueFingerprint()
			{
				Type = String.Empty;
				Keys = new CaseInsensitiveStringSet();
			}

			public IssueFingerprint(IIssueFingerprint Fingerprint)
			{
				this.Type = Fingerprint.Type;
				this.Keys = Fingerprint.Keys;
				this.RejectKeys = Fingerprint.RejectKeys;
			}
		}

		class IssueSpan : IIssueSpan
		{
			public ObjectId Id { get; set; }

			[BsonRequired]
			public StreamId StreamId { get; set; }

			[BsonRequired]
			public string StreamName { get; set; }

			[BsonRequired]
			public TemplateRefId TemplateRefId { get; set; }

			[BsonRequired]
			public string NodeName { get; set; }
			public DateTime? ResolvedAt { get; set; } // Propagated from the owning issue

			[BsonRequired]
			public IssueFingerprint Fingerprint { get; set; }

			public int MinChange { get; set; }
			public int MaxChange { get; set; } = int.MaxValue;

			public IssueStep? LastSuccess { get; set; }

			[BsonRequired]
			public IssueStep FirstFailure { get; set; }

			[BsonRequired]
			public IssueStep LastFailure { get; set; }

			public IssueStep? NextSuccess { get; set; }

			public bool? PromoteByDefault;

			[BsonElement("NotifySuspects"), BsonIgnoreIfDefault(false)]
			public bool NotifySuspects_DEPRECATED { get; set; }

			bool IIssueSpan.PromoteByDefault => PromoteByDefault ?? NotifySuspects_DEPRECATED;

			public List<IssueSpanSuspect> Suspects { get; set; }
			public int IssueId { get; set; }
			public int UpdateIndex { get; set; }

			IIssueStep? IIssueSpan.LastSuccess => LastSuccess;
			IIssueStep IIssueSpan.FirstFailure => FirstFailure;
			IIssueStep IIssueSpan.LastFailure => LastFailure;
			IIssueStep? IIssueSpan.NextSuccess => NextSuccess;
			IReadOnlyList<IIssueSpanSuspect> IIssueSpan.Suspects => Suspects;
			IIssueFingerprint IIssueSpan.Fingerprint => Fingerprint;

			private IssueSpan()
			{
				this.StreamName = null!;
				this.NodeName = null!;
				this.Fingerprint = null!;
				this.FirstFailure = null!;
				this.LastFailure = null!;
				this.Suspects = new List<IssueSpanSuspect>();
			}

			public IssueSpan(int IssueId, NewIssueSpanData NewSpan)
			{
				this.Id = ObjectId.GenerateNewId();
				this.StreamId = NewSpan.StreamId;
				this.StreamName = NewSpan.StreamName;
				this.TemplateRefId = NewSpan.TemplateRefId;
				this.NodeName = NewSpan.NodeName;
				this.Fingerprint = new IssueFingerprint(NewSpan.Fingerprint);
				if (NewSpan.LastSuccess != null)
				{
					this.MinChange = NewSpan.LastSuccess.Change;
					this.LastSuccess = new IssueStep(Id, NewSpan.LastSuccess);
				}
				this.FirstFailure = new IssueStep(Id, NewSpan.FirstFailure);
				this.LastFailure = new IssueStep(Id, NewSpan.FirstFailure);
				if (NewSpan.NextSuccess != null)
				{
					this.MaxChange = NewSpan.NextSuccess.Change;
					this.NextSuccess = new IssueStep(Id, NewSpan.NextSuccess);
				}
				this.PromoteByDefault = NewSpan.FirstFailure.PromoteByDefault;
				this.Suspects = NewSpan.Suspects.ConvertAll(x => new IssueSpanSuspect(x));
				this.IssueId = IssueId;
			}
		}

		class IssueSpanSuspect : IIssueSpanSuspect
		{
			public int Change { get; set; }
			public UserId AuthorId { get; set; }
			public int? OriginatingChange { get; set; }

			[BsonConstructor]
			private IssueSpanSuspect()
			{
			}

			public IssueSpanSuspect(NewIssueSpanSuspectData NewSuspectData)
			{
				this.Change = NewSuspectData.Change;
				this.AuthorId = NewSuspectData.AuthorId;
				this.OriginatingChange = NewSuspectData.OriginatingChange;
			}
		}

		class IssueStep : IIssueStep
		{
			public ObjectId Id { get; set; }
			public ObjectId SpanId { get; set; }

			public int Change { get; set; }
			public IssueSeverity Severity { get; set; }

			[BsonRequired]
			public string JobName { get; set; }

			[BsonRequired]
			public JobId JobId { get; set; }

			[BsonRequired]
			public SubResourceId BatchId { get; set; }

			[BsonRequired]
			public SubResourceId StepId { get; set; }

			public DateTime StepTime { get; set; }

			public LogId? LogId { get; set; }

			public bool? PromoteByDefault;

			[BsonElement("NotifySuspects"), BsonIgnoreIfDefault(false)]
			public bool NotifySuspects_DEPRECATED { get; set; }

			bool IIssueStep.PromoteByDefault => PromoteByDefault ?? NotifySuspects_DEPRECATED;

			[BsonConstructor]
			private IssueStep()
			{
				this.JobName = null!;
			}

			public IssueStep(ObjectId SpanId, NewIssueStepData StepData)
			{
				this.Id = ObjectId.GenerateNewId();
				this.SpanId = SpanId;
				this.Change = StepData.Change;
				this.Severity = StepData.Severity;
				this.JobName = StepData.JobName;
				this.JobId = StepData.JobId;
				this.BatchId = StepData.BatchId;
				this.StepId = StepData.StepId;
				this.StepTime = StepData.StepTime;
				this.LogId = StepData.LogId;
				this.PromoteByDefault = StepData.PromoteByDefault;
			}
		}

		RedisService RedisService;
		ISingletonDocument<IssueLedger> LedgerSingleton;
		IMongoCollection<Issue> Issues;
		IMongoCollection<IssueSpan> IssueSpans;
		IMongoCollection<IssueStep> IssueSteps;
		IMongoCollection<IssueSuspect> IssueSuspects;
		IAuditLog<int> AuditLog;
		ILogger Logger;

		public IssueCollection(DatabaseService DatabaseService, RedisService RedisService, IAuditLogFactory<int> AuditLogFactory, ILogger<IssueCollection> Logger)
		{
			this.RedisService = RedisService;
			this.Logger = Logger;

			LedgerSingleton = new SingletonDocument<IssueLedger>(DatabaseService);

			Issues = DatabaseService.GetCollection<Issue>("IssuesV2");
			IssueSpans = DatabaseService.GetCollection<IssueSpan>("IssuesV2.Spans");
			IssueSteps = DatabaseService.GetCollection<IssueStep>("IssuesV2.Steps");
			IssueSuspects = DatabaseService.GetCollection<IssueSuspect>("IssuesV2.Suspects");
			AuditLog = AuditLogFactory.Create("IssuesV2.History", "IssueId");

			if (!DatabaseService.ReadOnlyMode)
			{
				Issues.Indexes.CreateOne(new CreateIndexModel<Issue>(Builders<Issue>.IndexKeys.Ascending(x => x.ResolvedAt)));
				Issues.Indexes.CreateOne(new CreateIndexModel<Issue>(Builders<Issue>.IndexKeys.Ascending(x => x.VerifiedAt)));

				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.IssueId)));
				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.StreamId).Ascending(x => x.MinChange).Ascending(x => x.MaxChange)));
				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.StreamId).Ascending(x => x.TemplateRefId).Ascending(x => x.NodeName).Ascending(x => x.MinChange).Ascending(x => x.MaxChange), new CreateIndexOptions { Name = "StreamChanges" }));
				
				IssueSteps.Indexes.CreateOne(new CreateIndexModel<IssueStep>(Builders<IssueStep>.IndexKeys.Ascending(x => x.SpanId)));
				IssueSteps.Indexes.CreateOne(new CreateIndexModel<IssueStep>(Builders<IssueStep>.IndexKeys.Ascending(x => x.JobId).Ascending(x => x.BatchId).Ascending(x => x.StepId)));

				IssueSuspects.Indexes.CreateOne(new CreateIndexModel<IssueSuspect>(Builders<IssueSuspect>.IndexKeys.Ascending(x => x.Change)));
				IssueSuspects.Indexes.CreateOne(new CreateIndexModel<IssueSuspect>(Builders<IssueSuspect>.IndexKeys.Ascending(x => x.AuthorId).Ascending(x => x.ResolvedAt)));
				IssueSuspects.Indexes.CreateOne(new CreateIndexModel<IssueSuspect>(Builders<IssueSuspect>.IndexKeys.Ascending(x => x.IssueId).Ascending(x => x.Change), new CreateIndexOptions { Unique = true }));
			}
		}

		/// <inheritdoc/>
		public async Task<IAsyncDisposable> EnterCriticalSectionAsync()
		{
			Stopwatch Timer = Stopwatch.StartNew();
			TimeSpan NextNotifyTime = TimeSpan.FromSeconds(2.0);

			RedisLock Lock = new RedisLock(RedisService.Database, "issues/lock");
			while (!await Lock.AcquireAsync(TimeSpan.FromMinutes(1)))
			{
				if (Timer.Elapsed > NextNotifyTime)
				{
					Logger.LogWarning("Waiting on lock over issue collection for {TimeSpan}", Timer.Elapsed);
					NextNotifyTime *= 2;
				}
				await Task.Delay(TimeSpan.FromMilliseconds(100));
			}
			return Lock;
		}

		async Task<Issue?> TryUpdateIssueAsync(IIssue Issue, UpdateDefinition<Issue> Update)
		{
			Issue IssueDocument = (Issue)Issue;

			int PrevUpdateIndex = IssueDocument.UpdateIndex;
			Update = Update.Set(x => x.UpdateIndex, PrevUpdateIndex + 1);

			FindOneAndUpdateOptions<Issue, Issue> Options = new FindOneAndUpdateOptions<Issue, Issue> { ReturnDocument = ReturnDocument.After };
			return await Issues.FindOneAndUpdateAsync<Issue>(x => x.Id == IssueDocument.Id && x.UpdateIndex == PrevUpdateIndex, Update, Options);
		}

		async Task<IssueSpan?> TryUpdateSpanAsync(IIssueSpan IssueSpan, UpdateDefinition<IssueSpan> Update)
		{
			IssueSpan IssueSpanDocument = (IssueSpan)IssueSpan;

			int PrevUpdateIndex = IssueSpanDocument.UpdateIndex;
			Update = Update.Set(x => x.UpdateIndex, PrevUpdateIndex + 1);

			FindOneAndUpdateOptions<IssueSpan, IssueSpan> Options = new FindOneAndUpdateOptions<IssueSpan, IssueSpan> { ReturnDocument = ReturnDocument.After };
			return await IssueSpans.FindOneAndUpdateAsync<IssueSpan>(x => x.Id == IssueSpanDocument.Id && x.UpdateIndex == PrevUpdateIndex, Update, Options);
		}

		#region Issues

		/// <inheritdoc/>
		public async Task<IIssue> AddIssueAsync(string Summary)
		{
			IssueLedger Ledger = await LedgerSingleton.UpdateAsync(x => x.NextId++);

			Issue NewIssue = new Issue(Ledger.NextId, Summary);
			await Issues.InsertOneAsync(NewIssue);

			ILogger IssueLogger = GetLogger(NewIssue.Id);
			IssueLogger.LogInformation("Created issue {IssueId}", NewIssue.Id);

			return NewIssue;
		}

		static void LogIssueChanges(ILogger IssueLogger, Issue OldIssue, Issue NewIssue)
		{
			if (NewIssue.Severity != OldIssue.Severity)
			{
				IssueLogger.LogInformation("Changed severity to {Severity}", NewIssue.Severity);
			}
			if (NewIssue.Summary != OldIssue.Summary)
			{
				IssueLogger.LogInformation("Changed summary to \"{Summary}\"", NewIssue.Summary);
			}
			if (NewIssue.Description != OldIssue.Description)
			{
				IssueLogger.LogInformation("Description set to {Value}", NewIssue.Description);
			}
			if (((IIssue)NewIssue).Promoted != ((IIssue)OldIssue).Promoted)
			{
				IssueLogger.LogInformation("Promoted set to {Value}", ((IIssue)NewIssue).Promoted);
			}
			if (NewIssue.OwnerId != OldIssue.OwnerId)
			{
				if (NewIssue.NominatedById != null)
				{
					IssueLogger.LogInformation("User {UserId} was nominated by {NominatedByUserId}", NewIssue.OwnerId, NewIssue.NominatedById);
				}
				else
				{
					IssueLogger.LogInformation("User {UserId} was nominated by default", NewIssue.OwnerId);
				}
			}
			if (NewIssue.AcknowledgedAt != OldIssue.AcknowledgedAt)
			{
				if (NewIssue.AcknowledgedAt == null)
				{
					IssueLogger.LogInformation("Issue was un-acknowledged by {UserId}", OldIssue.OwnerId);
				}
				else
				{
					IssueLogger.LogInformation("Issue was acknowledged by {UserId}", OldIssue.OwnerId);
				}
			}
			if (NewIssue.FixChange != OldIssue.FixChange)
			{
				if (NewIssue.FixChange == 0)
				{
					IssueLogger.LogInformation("Issue was marked as not fixed");
				}
				else
				{
					IssueLogger.LogInformation("Issue was marked as fixed in {Change}", NewIssue.FixChange);
				}
			}
			if (NewIssue.ResolvedById != OldIssue.ResolvedById)
			{
				if (NewIssue.ResolvedById == null)
				{
					IssueLogger.LogInformation("Marking as unresolved");
				}
				else
				{
					IssueLogger.LogInformation("Resolved by {UserId}", NewIssue.ResolvedById);
				}
			}

			HashSet<StreamId> OldFixStreams = new HashSet<StreamId>(OldIssue.Streams.Where(x => x.ContainsFix ?? false).Select(x => x.StreamId));
			HashSet<StreamId> NewFixStreams = new HashSet<StreamId>(NewIssue.Streams.Where(x => x.ContainsFix ?? false).Select(x => x.StreamId));
			foreach (StreamId StreamId in NewFixStreams.Where(x => !OldFixStreams.Contains(x)))
			{
				IssueLogger.LogInformation("Marking stream {StreamId} as fixed", StreamId);
			}
			foreach (StreamId StreamId in OldFixStreams.Where(x => !NewFixStreams.Contains(x)))
			{
				IssueLogger.LogInformation("Marking stream {StreamId} as not fixed", StreamId);
			}
		}

		static void LogIssueSuspectChanges(ILogger IssueLogger, List<IssueSuspect> OldIssueSuspects, List<IssueSuspect> NewIssueSuspects)
		{
			HashSet<(UserId, int)> OldSuspects = new HashSet<(UserId, int)>(OldIssueSuspects.Select(x => (x.AuthorId, x.Change)));
			HashSet<(UserId, int)> NewSuspects = new HashSet<(UserId, int)>(NewIssueSuspects.Select(x => (x.AuthorId, x.Change)));
			foreach ((UserId UserId, int Change) in NewSuspects.Where(x => !OldSuspects.Contains(x)))
			{
				IssueLogger.LogInformation("Added suspect {UserId} for change {Change}", UserId, Change);
			}
			foreach ((UserId UserId, int Change) in OldSuspects.Where(x => !NewSuspects.Contains(x)))
			{
				IssueLogger.LogInformation("Removed suspect {UserId} for change {Change}", UserId, Change);
			}

			HashSet<UserId> OldDeclinedBy = new HashSet<UserId>(OldIssueSuspects.Where(x => x.DeclinedAt != null).Select(x => x.AuthorId));
			HashSet<UserId> NewDeclinedBy = new HashSet<UserId>(NewIssueSuspects.Where(x => x.DeclinedAt != null).Select(x => x.AuthorId));
			foreach (UserId AddDeclinedBy in NewDeclinedBy.Where(x => !OldDeclinedBy.Contains(x)))
			{
				IssueLogger.LogInformation("Declined by {UserId}", AddDeclinedBy);
			}
			foreach (UserId RemoveDeclinedBy in OldDeclinedBy.Where(x => !NewDeclinedBy.Contains(x)))
			{
				IssueLogger.LogInformation("Un-declined by {UserId}", RemoveDeclinedBy);
			}
		}

		static void LogSpanInfo(ILogger IssueLogger, IIssueSpan Span)
		{
			IssueLogger.LogInformation("Added span {SpanId} in {StreamId}", Span.Id, Span.StreamId);
			IssueLogger.LogInformation("Span {SpanId} first failure: CL {Change} ({JobId})", Span.Id, Span.FirstFailure.Change, Span.FirstFailure.JobId);
			if (Span.LastSuccess != null)
			{
				IssueLogger.LogInformation("Span {SpanId} last success: CL {Change} ({JobId})", Span.Id, Span.LastSuccess.Change, Span.LastSuccess.JobId);
			}
			if (Span.NextSuccess != null)
			{
				IssueLogger.LogInformation("Span {SpanId} next success: CL {Change} ({JobId})", Span.Id, Span.NextSuccess.Change, Span.NextSuccess.JobId);
			}
		}

		/// <inheritdoc/>
		public async Task<IIssue?> GetIssueAsync(int IssueId)
		{
			Issue Issue = await Issues.Find(x => x.Id == IssueId).FirstOrDefaultAsync();
			return Issue;
		}

		/// <inheritdoc/>
		public Task<List<IIssueSuspect>> FindSuspectsAsync(IIssue Issue)
		{
			return IssueSuspects.Find(x => x.IssueId == Issue.Id).ToListAsync<IssueSuspect, IIssueSuspect>();
		}

		class ProjectedIssueId
		{
			public int? _id { get; set; }
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, UserId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, bool? Promoted = null, int? Index = null, int? Count = null)
		{
			List<Issue> Results = await FilterIssuesByUserIdAsync(Ids, UserId, StreamId, MinChange, MaxChange, Resolved ?? false, Promoted, Index ?? 0, Count);
			return Results.ConvertAll<IIssue>(x => x);
		}

		async Task<List<Issue>> FilterIssuesByUserIdAsync(IEnumerable<int>? Ids, UserId? UserId, StreamId? StreamId, int? MinChange, int? MaxChange, bool? Resolved, bool? Promoted, int Index, int? Count)
		{
			if (UserId == null)
			{
				return await FilterIssuesByStreamIdAsync(Ids, StreamId, MinChange, MaxChange, Resolved, Promoted, Index, Count);
			}
			else
			{
				FilterDefinition<IssueSuspect> Filter = Builders<IssueSuspect>.Filter.Eq(x => x.AuthorId, UserId);
				if (Ids != null)
				{
					Filter &= Builders<IssueSuspect>.Filter.In(x => x.IssueId, Ids);
				}
				if (Resolved != null)
				{
					if (Resolved.Value)
					{
						Filter &= Builders<IssueSuspect>.Filter.Ne(x => x.ResolvedAt, null);
					}
					else
					{
						Filter &= Builders<IssueSuspect>.Filter.Eq(x => x.ResolvedAt, null);
					}
				}

				using (IAsyncCursor<ProjectedIssueId> Cursor = await IssueSuspects.Aggregate().Match(Filter).Group(x => x.IssueId, x => new ProjectedIssueId { _id = x.Key }).SortByDescending(x => x._id).ToCursorAsync())
				{
					return await PaginatedJoinAsync(Cursor, (NextIds, NextIndex, NextCount) => FilterIssuesByStreamIdAsync(NextIds, StreamId, MinChange, MaxChange, null, Promoted, NextIndex, NextCount), Index, Count);
				}
			}
		}

		async Task<List<Issue>> FilterIssuesByStreamIdAsync(IEnumerable<int>? Ids, StreamId? StreamId, int? MinChange, int? MaxChange, bool? Resolved, bool? Promoted, int Index, int? Count)
		{
			if (StreamId == null)
			{
				return await FilterIssuesByOtherFieldsAsync(Ids, MinChange, MaxChange, Resolved, Promoted, Index, Count);
			}
			else
			{
				FilterDefinition<IssueSpan> Filter = Builders<IssueSpan>.Filter.Eq(x => x.StreamId, StreamId.Value);
				if (Ids != null)
				{
					Filter &= Builders<IssueSpan>.Filter.In(x => x.IssueId, Ids.Select<int, int?>(x => x));
				}
				else
				{
					Filter &= Builders<IssueSpan>.Filter.Exists(x => x.IssueId);
				}

				if (MinChange != null)
				{
					Filter &= Builders<IssueSpan>.Filter.Not(Builders<IssueSpan>.Filter.Lt(x => x.MaxChange, MinChange.Value));
				}
				if (MaxChange != null)
				{
					Filter &= Builders<IssueSpan>.Filter.Not(Builders<IssueSpan>.Filter.Gt(x => x.MinChange, MaxChange.Value));
				}

				if (Resolved != null)
				{
					if (Resolved.Value)
					{
						Filter &= Builders<IssueSpan>.Filter.Ne(x => x.ResolvedAt, null);
					}
					else
					{
						Filter &= Builders<IssueSpan>.Filter.Eq(x => x.ResolvedAt, null);
					}
				}

				using (IAsyncCursor<ProjectedIssueId> Cursor = await IssueSpans.Aggregate().Match(Filter).Group(x => x.IssueId, x => new ProjectedIssueId { _id = x.Key }).SortByDescending(x => x._id).ToCursorAsync())
				{
					List<Issue> Results = await PaginatedJoinAsync(Cursor, (NextIds, NextIndex, NextCount) => FilterIssuesByOtherFieldsAsync(NextIds, null, null, null, Promoted, NextIndex, NextCount), Index, Count);
					if (Resolved != null)
					{
						for (int Idx = Results.Count - 1; Idx >= 0; Idx--)
						{
							Issue Issue = Results[Idx];
							if ((Issue.ResolvedAt != null) != Resolved.Value)
							{
								Logger.LogWarning("Issue {IssueId} has resolved state out of sync with spans", Issue.Id);
								Results.RemoveAt(Idx);
							}
						}
					}
					return Results;
				}
			}
		}

		async Task<List<Issue>> FilterIssuesByOtherFieldsAsync(IEnumerable<int>? Ids, int? MinChange, int? MaxChange, bool? Resolved, bool? Promoted, int Index, int? Count)
		{
			FilterDefinition<Issue> Filter = FilterDefinition<Issue>.Empty;
			if (Ids != null)
			{
				Filter &= Builders<Issue>.Filter.In(x => x.Id, Ids);
			}
			if (Resolved != null)
			{
				if (Resolved.Value)
				{
					Filter &= Builders<Issue>.Filter.Ne(x => x.ResolvedAt, null);
				}
				else
				{
					Filter &= Builders<Issue>.Filter.Eq(x => x.ResolvedAt, null);
				}
			}
			if (MinChange != null)
			{
				Filter &= Builders<Issue>.Filter.Not(Builders<Issue>.Filter.Lt(x => x.MaxSuspectChange, MinChange.Value));
			}
			if (MaxChange != null)
			{
				Filter &= Builders<Issue>.Filter.Not(Builders<Issue>.Filter.Gt(x => x.MinSuspectChange, MaxChange.Value));
			}
			if (Promoted != null)
			{
				if (Promoted.Value)
				{
					Filter &= Builders<Issue>.Filter.Ne(x => x.Promoted, false) & Builders<Issue>.Filter.Ne(x => x.NotifySuspects_DEPRECATED, false); // Note: may not exist on older issues.
				}
				else
				{
					Filter &= Builders<Issue>.Filter.Or(Builders<Issue>.Filter.Eq(x => x.Promoted, false), Builders<Issue>.Filter.Eq(x => x.NotifySuspects_DEPRECATED, false));
				}
			}
			return await Issues.Find(Filter).SortByDescending(x => x.Id).Range(Index, Count).ToListAsync();
		}

		/// <summary>
		/// Performs a client-side join of a filtered set of issues against another query
		/// </summary>
		/// <param name="Cursor"></param>
		/// <param name="NextStageFunc"></param>
		/// <param name="Index"></param>
		/// <param name="Count"></param>
		/// <returns></returns>
		static async Task<List<Issue>> PaginatedJoinAsync(IAsyncCursor<ProjectedIssueId> Cursor, Func<IEnumerable<int>, int, int?, Task<List<Issue>>> NextStageFunc, int Index, int? Count)
		{
			if (Count == null)
			{
				List<ProjectedIssueId> IssueIds = await Cursor.ToListAsync();
				return await NextStageFunc(IssueIds.Where(x => x._id != null).Select(x => x._id!.Value), Index, null);
			}
			else
			{
				List<Issue> Results = new List<Issue>();
				while (await Cursor.MoveNextAsync() && Results.Count < Count.Value)
				{
					List<Issue> NextResults = await NextStageFunc(Cursor.Current.Where(x => x._id != null).Select(x => x._id!.Value), 0, Count.Value - Results.Count);
					int RemoveCount = Math.Min(Index, NextResults.Count);
					NextResults.RemoveRange(0, RemoveCount);
					Index -= RemoveCount;
					Results.AddRange(NextResults);
				}
				return Results;
			}
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesForChangesAsync(List<int> Changes)
		{
			List<int> IssueIds = await (await IssueSuspects.DistinctAsync(x => x.IssueId, Builders<IssueSuspect>.Filter.In(x => x.Change, Changes))).ToListAsync();
			return await Issues.Find(Builders<Issue>.Filter.In(x => x.Id, IssueIds)).ToListAsync<Issue, IIssue>();
		}

		/// <inheritdoc/>
		public async Task<IIssue?> TryUpdateIssueAsync(IIssue Issue, IssueSeverity? NewSeverity = null, string? NewSummary = null, string? NewUserSummary = null, string? NewDescription = null, bool? NewPromoted = null, UserId? NewOwnerId = null, UserId? NewNominatedById = null, bool? NewAcknowledged = null, UserId? NewDeclinedById = null, int? NewFixChange = null, UserId? NewResolvedById = null, List<ObjectId>? NewExcludeSpanIds = null, DateTime? NewLastSeenAt = null)
		{
			Issue IssueDocument = (Issue)Issue;

			DateTime UtcNow = DateTime.UtcNow;

			List<UpdateDefinition<Issue>> Updates = new List<UpdateDefinition<Issue>>();
			if (NewSeverity != null)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Severity, NewSeverity.Value));
			}
			if (NewSummary != null)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Summary, NewSummary));
			}
			if (NewUserSummary != null)
			{
				if (NewUserSummary.Length == 0)
				{
					Updates.Add(Builders<Issue>.Update.Unset(x => x.UserSummary!));
				}
				else
				{
					Updates.Add(Builders<Issue>.Update.Set(x => x.UserSummary, NewUserSummary));
				}
			}
			if (NewDescription != null)
			{
				if (NewDescription.Length == 0)
				{
					Updates.Add(Builders<Issue>.Update.Unset(x => x.Description));
				}
				else
				{
					Updates.Add(Builders<Issue>.Update.Set(x => x.Description, NewDescription));
				}
			}
			if (NewPromoted != null)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Promoted, NewPromoted.Value));
			}
			if (NewOwnerId != null)
			{
				if (NewOwnerId.Value == UserId.Empty)
				{
					Updates.Add(Builders<Issue>.Update.Unset(x => x.OwnerId!));
					Updates.Add(Builders<Issue>.Update.Unset(x => x.NominatedAt!));
					Updates.Add(Builders<Issue>.Update.Unset(x => x.NominatedById!));
				}
				else
				{
					Updates.Add(Builders<Issue>.Update.Set(x => x.OwnerId!, NewOwnerId.Value));

					Updates.Add(Builders<Issue>.Update.Set(x => x.NominatedAt, DateTime.UtcNow));
					if (NewNominatedById == null)
					{
						Updates.Add(Builders<Issue>.Update.Unset(x => x.NominatedById!));
					}
					else
					{
						Updates.Add(Builders<Issue>.Update.Set(x => x.NominatedById, NewNominatedById.Value));
					}
					NewAcknowledged = NewAcknowledged ?? false;
				}
			}
			if (NewAcknowledged != null)
			{
				if (NewAcknowledged.Value)
				{
					if (IssueDocument.AcknowledgedAt == null)
					{
						Updates.Add(Builders<Issue>.Update.Set(x => x.AcknowledgedAt, UtcNow));
					}
				}
				else
				{
					if (IssueDocument.AcknowledgedAt != null)
					{
						Updates.Add(Builders<Issue>.Update.Unset(x => x.AcknowledgedAt!));
					}
				}
			}
			if (NewFixChange != null)
			{
				if (NewFixChange == 0)
				{
					Updates.Add(Builders<Issue>.Update.Unset(x => x.FixChange!));
				}
				else
				{
					Updates.Add(Builders<Issue>.Update.Set(x => x.FixChange, NewFixChange));
				}
			}
			if (NewResolvedById != null)
			{
				if (NewResolvedById.Value != UserId.Empty)
				{
					if (IssueDocument.ResolvedAt == null || IssueDocument.ResolvedById != NewResolvedById)
					{
						Updates.Add(Builders<Issue>.Update.Set(x => x.ResolvedAt, UtcNow));
						Updates.Add(Builders<Issue>.Update.Set(x => x.ResolvedById, NewResolvedById.Value));
					}
				}
				else
				{
					if (IssueDocument.ResolvedAt != null)
					{
						Updates.Add(Builders<Issue>.Update.Unset(x => x.ResolvedAt!));
					}
					if (IssueDocument.ResolvedById != null)
					{
						Updates.Add(Builders<Issue>.Update.Unset(x => x.ResolvedById!));
					}
				}
			}
			if (NewExcludeSpanIds != null)
			{
				List<ObjectId> NewCombinedExcludeSpanIds = NewExcludeSpanIds;
				if (Issue.ExcludeSpans != null)
				{
					NewCombinedExcludeSpanIds = NewCombinedExcludeSpanIds.Union(Issue.ExcludeSpans).ToList();
				}
				Updates.Add(Builders<Issue>.Update.Set(x => x.ExcludeSpans, NewCombinedExcludeSpanIds));
			}
			if (NewLastSeenAt != null)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.LastSeenAt, NewLastSeenAt.Value));
			}

			if (NewDeclinedById != null)
			{
				await IssueSuspects.UpdateManyAsync(x => x.IssueId == Issue.Id && x.AuthorId == NewDeclinedById.Value, Builders<IssueSuspect>.Update.Set(x => x.DeclinedAt, DateTime.UtcNow));
			}
			if (Updates.Count == 0)
			{
				return IssueDocument;
			}

			Issue? NewIssue = await TryUpdateIssueAsync(Issue, Builders<Issue>.Update.Combine(Updates));
			if(NewIssue == null)
			{
				return null;
			}

			ILogger IssueLogger = GetLogger(Issue.Id);
			LogIssueChanges(IssueLogger, IssueDocument, NewIssue);
			return NewIssue;
		}

		/// <inheritdoc/>
		public async Task<IIssue?> TryUpdateIssueDerivedDataAsync(IIssue Issue, string NewSummary, IssueSeverity NewSeverity, List<NewIssueFingerprint> NewFingerprints, List<NewIssueStream> NewStreams, List<NewIssueSuspectData> NewSuspects, DateTime? NewResolvedAt, DateTime? NewVerifiedAt, DateTime NewLastSeenAt)
		{
			Issue IssueImpl = (Issue)Issue;

			// Update all the suspects for this issue
			List<IssueSuspect> OldSuspectImpls = await IssueSuspects.Find(x => x.IssueId == Issue.Id).ToListAsync();
			List<IssueSuspect> NewSuspectImpls = await UpdateIssueSuspectsAsync(Issue.Id, OldSuspectImpls, NewSuspects, NewResolvedAt);

			// Find the default owner
			UserId? NewDefaultOwnerId = null;
			if (NewSuspectImpls.Count > 0)
			{
				UserId PossibleOwnerId = NewSuspectImpls[0].AuthorId;
				if (NewSuspectImpls.All(x => x.AuthorId == PossibleOwnerId) && NewSuspectImpls.Any(x => x.DeclinedAt == null))
				{
					NewDefaultOwnerId = PossibleOwnerId;
				}
			}

			// Update all the spans attached to this issue
			await IssueSpans.UpdateManyAsync(x => x.IssueId == Issue.Id && x.ResolvedAt != NewResolvedAt, Builders<IssueSpan>.Update.Set(x => x.ResolvedAt, NewResolvedAt));

			// Get the range of suspect changes
			int NewMinSuspectChange = (NewSuspects.Count > 0) ? NewSuspects.Min(x => x.Change) : 0;
			int NewMaxSuspectChange = (NewSuspects.Count > 0) ? NewSuspects.Min(x => x.Change) : 0;

			// Perform the actual update with this data
			List<UpdateDefinition<Issue>> Updates = new List<UpdateDefinition<Issue>>();
			if (!String.Equals(Issue.Summary, NewSummary, StringComparison.Ordinal))
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Summary, NewSummary));
			}
			if (Issue.Severity != NewSeverity)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Severity, NewSeverity));
			}
			if (Issue.Fingerprints.Count != NewFingerprints.Count || !NewFingerprints.Zip(Issue.Fingerprints).All(x => x.First.Equals(x.Second)))
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Fingerprints, NewFingerprints.Select(x => new IssueFingerprint(x))));
			}
			if (Issue.Streams.Count != NewStreams.Count || !NewStreams.Zip(Issue.Streams).All(x => x.First.StreamId == x.Second.StreamId && x.First.ContainsFix == x.Second.ContainsFix))
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.Streams, NewStreams.Select(x => new IssueStream(x))));
			}
			if (IssueImpl.MinSuspectChange != NewMinSuspectChange)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.MinSuspectChange, NewMinSuspectChange));
			}
			if (IssueImpl.MaxSuspectChange != NewMaxSuspectChange)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.MaxSuspectChange, NewMaxSuspectChange));
			}
			if (IssueImpl.DefaultOwnerId != NewDefaultOwnerId)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.DefaultOwnerId, NewDefaultOwnerId));
			}
			if (Issue.ResolvedAt != NewResolvedAt)
			{
				Updates.Add(Builders<Issue>.Update.SetOrUnsetNull(x => x.ResolvedAt, NewResolvedAt));
			}
			if (NewResolvedAt == null && Issue.ResolvedById != null)
			{
				Updates.Add(Builders<Issue>.Update.Unset(x => x.ResolvedById));
			}
			if (Issue.VerifiedAt != NewVerifiedAt)
			{
				Updates.Add(Builders<Issue>.Update.SetOrUnsetNull(x => x.VerifiedAt, NewVerifiedAt));
			}
			if (Issue.LastSeenAt != NewLastSeenAt)
			{
				Updates.Add(Builders<Issue>.Update.Set(x => x.LastSeenAt, NewLastSeenAt));
			}

			Issue? NewIssue = await TryUpdateIssueAsync(Issue, Builders<Issue>.Update.Combine(Updates));
			if(NewIssue != null)
			{
				ILogger IssueLogger = GetLogger(Issue.Id);
				LogIssueChanges(GetLogger(Issue.Id), IssueImpl, NewIssue);
				LogIssueSuspectChanges(IssueLogger, OldSuspectImpls, NewSuspectImpls);
				return NewIssue;
			}
			return null;
		}

		async Task<List<IssueSuspect>> UpdateIssueSuspectsAsync(int IssueId, List<IssueSuspect> OldSuspectImpls, List<NewIssueSuspectData> NewSuspects, DateTime? ResolvedAt)
		{
			List<IssueSuspect> NewSuspectImpls = new List<IssueSuspect>(OldSuspectImpls);

			// Find the current list of suspects
			HashSet<(UserId, int)> CurSuspectKeys = new HashSet<(UserId, int)>(OldSuspectImpls.Select(x => (x.AuthorId, x.Change)));
			List<IssueSuspect> CreateSuspects = NewSuspects.Where(x => !CurSuspectKeys.Contains((x.AuthorId, x.Change))).Select(x => new IssueSuspect(IssueId, x, ResolvedAt)).ToList();

			HashSet<(UserId, int)> NewSuspectKeys = new HashSet<(UserId, int)>(NewSuspects.Select(x => (x.AuthorId, x.Change)));
			List<IssueSuspect> DeleteSuspects = OldSuspectImpls.Where(x => !NewSuspectKeys.Contains((x.AuthorId, x.Change))).ToList();

			// Apply the suspect changes
			if (CreateSuspects.Count > 0)
			{
				await IssueSuspects.InsertManyIgnoreDuplicatesAsync(CreateSuspects);
				NewSuspectImpls.AddRange(CreateSuspects);
			}
			if (DeleteSuspects.Count > 0)
			{
				await IssueSuspects.DeleteManyAsync(Builders<IssueSuspect>.Filter.In(x => x.Id, DeleteSuspects.Select(y => y.Id)));
				NewSuspectImpls.RemoveAll(x => !NewSuspectKeys.Contains((x.AuthorId, x.Change)));
			}

			// Make sure all the remaining suspects have the correct resolved time
			if (NewSuspectImpls.Any(x => x.ResolvedAt != ResolvedAt))
			{
				await IssueSuspects.UpdateManyAsync(Builders<IssueSuspect>.Filter.Eq(x => x.IssueId, IssueId), Builders<IssueSuspect>.Update.Set(x => x.ResolvedAt, ResolvedAt));
			}
			return NewSuspectImpls;
		}

		#endregion

		#region Spans

		/// <inheritdoc/>
		public async Task<IIssueSpan> AddSpanAsync(int IssueId, NewIssueSpanData NewSpan)
		{
			IssueSpan Span = new IssueSpan(IssueId, NewSpan);
			await IssueSpans.InsertOneAsync(Span);
			return Span;
		}

		/// <inheritdoc/>
		public async Task<IIssueSpan?> GetSpanAsync(ObjectId SpanId)
		{
			return await IssueSpans.Find(Builders<IssueSpan>.Filter.Eq(x => x.Id, SpanId)).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IIssueSpan?> TryUpdateSpanAsync(IIssueSpan Span, NewIssueStepData? NewLastSuccess = null, NewIssueStepData? NewFailure = null, NewIssueStepData? NewNextSuccess = null, List<NewIssueSpanSuspectData>? NewSuspects = null, int? NewIssueId = null)
		{
			List<UpdateDefinition<IssueSpan>> Updates = new List<UpdateDefinition<IssueSpan>>();
			if (NewLastSuccess != null)
			{
				Updates.Add(Builders<IssueSpan>.Update.Set(x => x.MinChange, NewLastSuccess.Change));
				Updates.Add(Builders<IssueSpan>.Update.Set(x => x.LastSuccess, new IssueStep(Span.Id, NewLastSuccess)));
			}
			if (NewFailure != null)
			{
				if (NewFailure.Change < Span.FirstFailure.Change)
				{
					Updates.Add(Builders<IssueSpan>.Update.Set(x => x.FirstFailure, new IssueStep(Span.Id, NewFailure)));
				}
				if (NewFailure.Change >= Span.LastFailure.Change)
				{
					Updates.Add(Builders<IssueSpan>.Update.Set(x => x.LastFailure, new IssueStep(Span.Id, NewFailure)));
				}
				if (NewFailure.PromoteByDefault != Span.PromoteByDefault && NewFailure.Change >= Span.LastFailure.Change)
				{
					Updates.Add(Builders<IssueSpan>.Update.Set(x => x.PromoteByDefault, NewFailure.PromoteByDefault));
				}
			}
			if (NewNextSuccess != null)
			{
				Updates.Add(Builders<IssueSpan>.Update.Set(x => x.MaxChange, NewNextSuccess.Change));
				Updates.Add(Builders<IssueSpan>.Update.Set(x => x.NextSuccess, new IssueStep(Span.Id, NewNextSuccess)));
			}
			if (NewSuspects != null)
			{
				Updates.Add(Builders<IssueSpan>.Update.Set(x => x.Suspects, NewSuspects.ConvertAll(x => new IssueSpanSuspect(x))));
			}
			if (NewIssueId != null)
			{
				Updates.Add(Builders<IssueSpan>.Update.Set(x => x.IssueId, NewIssueId.Value));
			}

			if (Updates.Count == 0)
			{
				return Span;
			}

			IssueSpan? NewSpan = await TryUpdateSpanAsync(Span, Builders<IssueSpan>.Update.Combine(Updates));
			if (NewSpan != null)
			{
				ILogger Logger = GetLogger(NewSpan.IssueId);
				if (NewLastSuccess != null)
				{
					Logger.LogInformation("Set last success for span {SpanId} to job {JobId} at CL {Change}", NewSpan.Id, NewLastSuccess.JobId, NewLastSuccess.Change);
				}
				if (NewNextSuccess != null)
				{
					Logger.LogInformation("Set next success for span {SpanId} to job {JobId} at CL {Change}", NewSpan.Id, NewNextSuccess.JobId, NewNextSuccess.Change);
				}
				if (NewFailure != null)
				{
					Logger.LogInformation("Added failure for span {SpanId} in job {JobId} at CL {Change}", NewSpan.Id, NewFailure.JobId, NewFailure.Change);
				}
			}
			return NewSpan;
		}

		/// <inheritdoc/>
		public async Task<List<IIssueSpan>> FindSpansAsync(int IssueId)
		{
			return await IssueSpans.Find(x => x.IssueId == IssueId).ToListAsync<IssueSpan, IIssueSpan>();
		}

		/// <inheritdoc/>
		public Task<List<IIssueSpan>> FindSpansAsync(IEnumerable<ObjectId> SpanIds)
		{
			return IssueSpans.Find(Builders<IssueSpan>.Filter.In(x => x.Id, SpanIds)).ToListAsync<IssueSpan, IIssueSpan>();
		}

		/// <inheritdoc/>
		public async Task<List<IIssueSpan>> FindOpenSpansAsync(StreamId StreamId, TemplateRefId TemplateId, string NodeName, int Change)
		{
			List<IssueSpan> Spans = await IssueSpans.Find(x => x.StreamId == StreamId && x.TemplateRefId == TemplateId && x.NodeName == NodeName && Change >= x.MinChange && Change <= x.MaxChange).ToListAsync();
			return Spans.ConvertAll<IIssueSpan>(x => x);
		}

		#endregion

		#region Steps

		/// <inheritdoc/>
		public async Task<IIssueStep> AddStepAsync(ObjectId SpanId, NewIssueStepData NewStep)
		{
			IssueStep Step = new IssueStep(SpanId, NewStep);
			await IssueSteps.InsertOneAsync(Step);
			return Step;
		}

		/// <inheritdoc/>
		public Task<List<IIssueStep>> FindStepsAsync(IEnumerable<ObjectId> SpanIds)
		{
			FilterDefinition<IssueStep> Filter = Builders<IssueStep>.Filter.In(x => x.SpanId, SpanIds);
			return IssueSteps.Find(Filter).ToListAsync<IssueStep, IIssueStep>();
		}

		/// <inheritdoc/>
		public Task<List<IIssueStep>> FindStepsAsync(JobId JobId, SubResourceId? BatchId, SubResourceId? StepId)
		{
			FilterDefinition<IssueStep> Filter = Builders<IssueStep>.Filter.Eq(x => x.JobId, JobId);
			if (BatchId != null)
			{
				Filter &= Builders<IssueStep>.Filter.Eq(x => x.BatchId, BatchId.Value);
			}
			if (StepId != null)
			{
				Filter &= Builders<IssueStep>.Filter.Eq(x => x.StepId, StepId.Value);
			}
			return IssueSteps.Find(Filter).ToListAsync<IssueStep, IIssueStep>();
		}

		#endregion

		/// <inheritdoc/>
		public IAuditLogChannel<int> GetLogger(int IssueId)
		{
			return AuditLog[IssueId];
		}
	}
}
