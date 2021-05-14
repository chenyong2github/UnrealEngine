// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
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
using System.Linq;
using System.Linq.Expressions;
using System.Security.Claims;
using System.Threading.Tasks;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Collections.Impl
{
	class IssueCollection : IIssueCollection
	{
		class UpdateTransaction
		{
			class Statics<TDocument>
			{
				public static IBsonSerializer<TDocument> Serializer { get; } = BsonSerializer.LookupSerializer<TDocument>();
			}

			[BsonRequired]
			public string Filter { get; set; }

			[BsonRequired]
			public string Update { get; set; }

			private UpdateTransaction()
			{
				this.Filter = null!;
				this.Update = null!;
			}

			public UpdateTransaction(BsonDocument Filter, BsonDocument Update)
			{
				this.Filter = Filter.ToJson();
				this.Update = Update.ToJson();
			}

			public static UpdateTransaction Create<TDocument>(Expression<Func<TDocument, bool>> FilterFunc, UpdateDefinition<TDocument> Update)
			{
				return Create((FilterDefinition<TDocument>)FilterFunc, Update);
			}

			public static UpdateTransaction Create<TDocument>(FilterDefinition<TDocument> Filter, UpdateDefinition<TDocument> Update)
			{
				IBsonSerializer<TDocument> Serializer = Statics<TDocument>.Serializer;

				BsonDocument FilterDocument = Filter.Render(Serializer, BsonSerializer.SerializerRegistry);
				BsonDocument UpdateDocument = (BsonDocument)Update.Render(Serializer, BsonSerializer.SerializerRegistry);

				return new UpdateTransaction(FilterDocument, UpdateDocument);
			}
		}

		[SingletonDocument("5e4c226440ce25fa3207a9af")]
		class IssueLedger : SingletonBase
		{
			public int NextId { get; set; }

			[BsonIgnoreIfNull]
			public Issue? InsertIssue { get; set; }

			[BsonIgnoreIfNull]
			public IssueSpan? InsertIssueSpan { get; set; }

			[BsonIgnoreIfNull]
			public List<IssueSuspect>? InsertIssueSuspects { get; set; }

			[BsonIgnoreIfNull]
			public UpdateTransaction? UpdateIssue { get; set; }

			[BsonIgnoreIfNull]
			public UpdateTransaction? UpdateIssueSpan { get; set; }

			public bool HasPendingTransaction => InsertIssue != null || InsertIssueSpan != null || InsertIssueSuspects != null || UpdateIssue != null || UpdateIssueSpan != null;
		}

		class IssueSequenceToken : IIssueSequenceToken
		{
			IssueCollection Collection;
			public IssueLedger Ledger { get; private set; }

			public IssueSequenceToken(IssueCollection Collection, IssueLedger Ledger)
			{
				this.Collection = Collection;
				this.Ledger = Ledger;
			}

			public async Task ResetAsync()
			{
				Ledger = await Collection.GetLedgerAsync();
			}

			public async Task FlushAsync()
			{
				Ledger = await Collection.FlushLedgerAsync(Ledger);
			}
		}

		class Issue : IIssue
		{
			[BsonId]
			public int Id { get; set; }

			public string Summary { get; set; }

			[BsonIgnoreIfNull]
			public string? UserSummary { get; set; }

			[BsonRequired]
			public IssueFingerprint Fingerprint { get; set; }
			public IssueSeverity Severity { get; set; }

			[BsonIgnoreIfNull]
			public string? Owner { get; set; }

			[BsonIgnoreIfNull]
			public ObjectId? OwnerId { get; set; }

			[BsonIgnoreIfNull]
			public string? NominatedBy { get; set; }

			[BsonIgnoreIfNull]
			public ObjectId? NominatedById { get; set; }

			public DateTime CreatedAt { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? NominatedAt { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? AcknowledgedAt { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? ResolvedAt { get; set; }

			[BsonIgnoreIfNull]
			public ObjectId? ResolvedById { get; set; }

			[BsonIgnoreIfNull]
			public DateTime? VerifiedAt { get; set; }

			public DateTime LastSeenAt { get; set; }

			[BsonIgnoreIfNull]
			public int? FixChange { get; set; }

			public List<IssueStream> Streams { get; set; } = new List<IssueStream>();

			public int MinSuspectChange { get; set; }
			public int MaxSuspectChange { get; set; }

			public bool NotifySuspects { get; set; }

			[BsonRequired]
			public List<IssueSuspect> Suspects { get; set; }

			public int UpdateIndex { get; set; }

			IIssueFingerprint IIssue.Fingerprint => Fingerprint;
			ObjectId? IIssue.OwnerId => OwnerId ?? GetDefaultOwner()?.AuthorId;
			IReadOnlyList<IIssueStream> IIssue.Streams => Streams;
			DateTime IIssue.LastSeenAt => (LastSeenAt == default) ? DateTime.UtcNow : LastSeenAt;

			[BsonConstructor]
			private Issue()
			{
				Summary = String.Empty;
				Fingerprint = null!;
				Suspects = null!;
			}

			public Issue(int Id, string Summary, IIssueSpan Span)
			{
				this.Id = Id;
				this.Summary = Summary;
				this.CreatedAt = DateTime.UtcNow;
				this.LastSeenAt = DateTime.UtcNow;
				this.Fingerprint = new IssueFingerprint(Span.Fingerprint);
				this.Severity = Span.Severity;
				this.NotifySuspects = Span.NotifySuspects;
				this.ResolvedAt = Span.NextSuccess?.StepTime;
				this.VerifiedAt = this.ResolvedAt;
				this.Suspects = Span.Suspects.ConvertAll(x => new IssueSuspect(Id, x.AuthorId, x.OriginatingChange ?? x.Change, null, this.ResolvedAt));

				if (Suspects.Count > 0)
				{
					MinSuspectChange = Suspects.Min(x => x.Change);
					MaxSuspectChange = Suspects.Max(x => x.Change);
				}
			}

			IssueSuspect? GetDefaultOwner()
			{
				if (Suspects.Count > 0)
				{
					ObjectId PossibleOwner = Suspects[0].AuthorId;
					if (Suspects.All(x => x.AuthorId == PossibleOwner) && Suspects.Any(x => x.DeclinedAt == null))
					{
						return Suspects[0];
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
			public ObjectId AuthorId { get; set; }
			public int Change { get; set; }
			public DateTime? DeclinedAt { get; set; }
			public DateTime? ResolvedAt { get; set; } // Degenerate

			private IssueSuspect()
			{
			}

			public IssueSuspect(int IssueId, IIssueSpanSuspect Suspect)
				: this(IssueId, Suspect.AuthorId, Suspect.OriginatingChange ?? Suspect.Change, null, null)
			{
			}

			public IssueSuspect(int IssueId, ObjectId AuthorId, int Change, DateTime? DeclinedAt, DateTime? ResolvedAt)
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
			public IssueSeverity Severity { get; set; }
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

			public bool NotifySuspects { get; set; }
			public List<IssueSpanSuspect> Suspects { get; set; }
			public int? IssueId { get; set; }
			public bool Modified { get; set; }
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

			public IssueSpan(StreamId StreamId, string StreamName, TemplateRefId TemplateRefId, string NodeName, IIssueFingerprint Fingerprint, NewIssueStepData? LastSuccess, NewIssueStepData Failure, NewIssueStepData? NextSuccess, List<IssueSpanSuspect> Suspects)
			{
				this.Id = ObjectId.GenerateNewId();
				this.StreamId = StreamId;
				this.StreamName = StreamName;
				this.TemplateRefId = TemplateRefId;
				this.NodeName = NodeName;
				this.Severity = Failure.Severity;
				this.Fingerprint = new IssueFingerprint(Fingerprint);
				if (LastSuccess != null)
				{
					this.MinChange = LastSuccess.Change;
					this.LastSuccess = new IssueStep(Id, LastSuccess);
				}
				this.FirstFailure = new IssueStep(Id, Failure);
				this.LastFailure = new IssueStep(Id, Failure);
				if (NextSuccess != null)
				{
					this.MaxChange = NextSuccess.Change;
					this.NextSuccess = new IssueStep(Id, NextSuccess);
				}
				this.NotifySuspects = Failure.NotifySuspects;
				this.Suspects = Suspects;
				this.Modified = true;
			}
		}

		class IssueSpanSuspect : IIssueSpanSuspect
		{
			public int Change { get; set; }
			public string Author { get; set; }
			public ObjectId AuthorId { get; set; }
			public int? OriginatingChange { get; set; }

			[BsonConstructor]
			private IssueSpanSuspect()
			{
				Author = String.Empty;
			}

			public IssueSpanSuspect(int Change, string Author, ObjectId AuthorId, int? OriginatingChange)
			{
				this.Change = Change;
				this.Author = Author;
				this.AuthorId = AuthorId;
				this.OriginatingChange = OriginatingChange;
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
			public ObjectId JobId { get; set; }

			[BsonRequired]
			public SubResourceId BatchId { get; set; }

			[BsonRequired]
			public SubResourceId StepId { get; set; }

			public DateTime StepTime { get; set; }

			public ObjectId? LogId { get; set; }

			public bool NotifySuspects { get; set; }

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
				this.NotifySuspects = StepData.NotifySuspects;
			}
		}

		ISingletonDocument<IssueLedger> LedgerSingleton;
		IMongoCollection<Issue> Issues;
		IMongoCollection<IssueSpan> IssueSpans;
		IMongoCollection<IssueStep> IssueSteps;
		IMongoCollection<IssueSuspect> IssueSuspects;
		ILogger Logger;

		// TODO: Temp upgrade path for switching user references to ids. Remove after resaving all issues.
		IUserCollection UserCollection;
		ConcurrentDictionary<string, ObjectId> UserNameToIdCache = new ConcurrentDictionary<string, ObjectId>(StringComparer.OrdinalIgnoreCase);
		IPerforceService PerforceService;

		public IssueCollection(DatabaseService DatabaseService, IUserCollection UserCollection, IPerforceService PerforceService, ILogger<IssueCollection> Logger)
		{
			this.UserCollection = UserCollection;
			this.PerforceService = PerforceService;
			this.Logger = Logger;

			LedgerSingleton = new SingletonDocument<IssueLedger>(DatabaseService);

			Issues = DatabaseService.GetCollection<Issue>("IssuesV2");
			IssueSpans = DatabaseService.GetCollection<IssueSpan>("IssuesV2.Spans");
			IssueSteps = DatabaseService.GetCollection<IssueStep>("IssuesV2.Steps");
			IssueSuspects = DatabaseService.GetCollection<IssueSuspect>("IssuesV2.Suspects");
			
			if (!DatabaseService.ReadOnlyMode)
			{
				Issues.Indexes.CreateOne(new CreateIndexModel<Issue>(Builders<Issue>.IndexKeys.Ascending(x => x.ResolvedAt)));
				Issues.Indexes.CreateOne(new CreateIndexModel<Issue>(Builders<Issue>.IndexKeys.Ascending(x => x.VerifiedAt)));

				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.Modified)));
				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.IssueId)));
				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.StreamId).Ascending(x => x.MinChange).Ascending(x => x.MaxChange)));
				IssueSpans.Indexes.CreateOne(new CreateIndexModel<IssueSpan>(Builders<IssueSpan>.IndexKeys.Ascending(x => x.StreamId).Ascending(x => x.TemplateRefId).Ascending(x => x.NodeName).Ascending(x => x.MinChange).Ascending(x => x.MaxChange), new CreateIndexOptions { Name = "StreamChanges" }));
				
				IssueSteps.Indexes.CreateOne(new CreateIndexModel<IssueStep>(Builders<IssueStep>.IndexKeys.Ascending(x => x.SpanId)));
				IssueSteps.Indexes.CreateOne(new CreateIndexModel<IssueStep>(Builders<IssueStep>.IndexKeys.Ascending(x => x.JobId).Ascending(x => x.BatchId).Ascending(x => x.StepId)));

				IssueSuspects.Indexes.CreateOne(new CreateIndexModel<IssueSuspect>(Builders<IssueSuspect>.IndexKeys.Ascending(x => x.AuthorId).Ascending(x => x.ResolvedAt)));
				IssueSuspects.Indexes.CreateOne(new CreateIndexModel<IssueSuspect>(Builders<IssueSuspect>.IndexKeys.Ascending(x => x.IssueId).Ascending(x => x.Change), new CreateIndexOptions { Unique = true }));
			}
		}

		async Task<IIssue?> ApplyTransactionAsync(IIssue Issue, TransactionBuilder<Issue> Transaction)
		{
			Issue IssueDocument = (Issue)Issue;

			int PrevUpdateIndex = IssueDocument.UpdateIndex;
			Transaction.Set(x => x.UpdateIndex, PrevUpdateIndex + 1);

			UpdateResult Result = await Issues.UpdateOneAsync(x => x.Id == IssueDocument.Id && x.UpdateIndex == PrevUpdateIndex, Transaction.ToUpdateDefinition());
			if (Result.ModifiedCount == 0)
			{
				return null;
			}

			Transaction.ApplyTo(IssueDocument);
			return IssueDocument;
		}

		async Task<IIssueSpan?> ApplyTransactionAsync(IIssueSpan IssueSpan, TransactionBuilder<IssueSpan> Transaction)
		{
			IssueSpan IssueSpanDocument = (IssueSpan)IssueSpan;

			int PrevUpdateIndex = IssueSpanDocument.UpdateIndex;
			Transaction.Set(x => x.UpdateIndex, PrevUpdateIndex + 1);

			UpdateResult Result = await IssueSpans.UpdateOneAsync(x => x.Id == IssueSpanDocument.Id && x.UpdateIndex == PrevUpdateIndex, Transaction.ToUpdateDefinition());
			if (Result.ModifiedCount > 0)
			{
				Transaction.ApplyTo(IssueSpanDocument);
				return IssueSpanDocument;
			}
			return null;
		}

		/// <inheritdoc/>
		public async Task<IIssueSequenceToken> GetSequenceTokenAsync()
		{
			IssueLedger Ledger = await GetLedgerAsync();
			return new IssueSequenceToken(this, Ledger);
		}

		async Task<IssueLedger> GetLedgerAsync()
		{
			IssueLedger Ledger = await LedgerSingleton.GetAsync();
			Ledger = await FlushLedgerAsync(Ledger);
			return Ledger;
		}

		async Task<IssueLedger> FlushLedgerAsync(IssueLedger Ledger)
		{
			while (Ledger.HasPendingTransaction)
			{
				if (Ledger.InsertIssue != null)
				{
					await Issues.InsertOneIgnoreDuplicatesAsync(Ledger.InsertIssue);
					Ledger.InsertIssue = null;
				}
				if (Ledger.InsertIssueSpan != null)
				{
					await IssueSpans.InsertOneIgnoreDuplicatesAsync(Ledger.InsertIssueSpan);
					Ledger.InsertIssueSpan = null;
				}
				if (Ledger.InsertIssueSuspects != null)
				{
					await IssueSuspects.InsertManyIgnoreDuplicatesAsync(Ledger.InsertIssueSuspects);
					Ledger.InsertIssueSuspects = null;
				}
				if (Ledger.UpdateIssue != null)
				{
					await Issues.UpdateOneAsync(Ledger.UpdateIssue.Filter, Ledger.UpdateIssue.Update);
					Ledger.UpdateIssue = null;
				}
				if (Ledger.UpdateIssueSpan != null)
				{
					await IssueSpans.UpdateOneAsync(Ledger.UpdateIssueSpan.Filter, Ledger.UpdateIssueSpan.Update);
					Ledger.UpdateIssueSpan = null;
				}

				if (await LedgerSingleton.TryUpdateAsync(Ledger))
				{
					break;
				}

				Ledger = await LedgerSingleton.GetAsync();
			}
			return Ledger;
		}

		#region Issues

		/// <inheritdoc/>
		public async Task<int?> ReserveUniqueIdAsync(IIssueSequenceToken Token)
		{
			IssueSequenceToken TokenValue = ((IssueSequenceToken)Token);
			int NewIssueId = ++TokenValue.Ledger.NextId;

			if (!await LedgerSingleton.TryUpdateAsync(TokenValue.Ledger))
			{
				return null;
			}

			return NewIssueId;
		}

		/// <inheritdoc/>
		public async Task<IIssue?> AddIssueAsync(IIssueSequenceToken Token, string Summary, IIssueSpan Span)
		{
			IssueSequenceToken TokenValue = ((IssueSequenceToken)Token);
			Issue NewIssue = new Issue(++TokenValue.Ledger.NextId, Summary, Span);
			TokenValue.Ledger.InsertIssue = NewIssue;
			TokenValue.Ledger.InsertIssueSuspects = NewIssue.Suspects;
			TokenValue.Ledger.UpdateIssueSpan = UpdateTransaction.Create<IssueSpan>(x => x.Id == Span.Id, Builders<IssueSpan>.Update.Set(x => x.IssueId, NewIssue.Id));

			if (!await LedgerSingleton.TryUpdateAsync(TokenValue.Ledger))
			{
				return null;
			}

			await TokenValue.FlushAsync();
			return NewIssue;
		}

		/// <inheritdoc/>
		public async Task<IIssue?> GetIssueAsync(int IssueId)
		{
			Issue Issue = await Issues.Find(x => x.Id == IssueId).FirstOrDefaultAsync();
			await UpgradeIssueAsync(Issue);
			return Issue;
		}

		/// <inheritdoc/>
		public Task<List<IIssueSuspect>> GetSuspectsAsync(IIssue Issue)
		{
			Issue IssueDocument = (Issue)Issue;
			return Task.FromResult(IssueDocument.Suspects.ConvertAll<IIssueSuspect>(x => x));
		}

		class ProjectedIssueId
		{
			public int? _id { get; set; }
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, ObjectId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, int? Index = null, int? Count = null)
		{
			List<Issue> Results = await FilterIssuesByUserIdAsync(Ids, UserId, StreamId, MinChange, MaxChange, Resolved ?? false, Index ?? 0, Count);
			return await UpgradeIssuesAsync(Results);
		}

		async Task<List<Issue>> FilterIssuesByUserIdAsync(IEnumerable<int>? Ids, ObjectId? UserId, StreamId? StreamId, int? MinChange, int? MaxChange, bool? Resolved, int Index, int? Count)
		{
			if (UserId == null)
			{
				return await FilterIssuesByStreamIdAsync(Ids, StreamId, MinChange, MaxChange, Resolved, Index, Count);
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
					return await PaginatedJoinAsync(Cursor, (NextIds, NextIndex, NextCount) => FilterIssuesByStreamIdAsync(NextIds, StreamId, MinChange, MaxChange, null, NextIndex, NextCount), Index, Count);
				}
			}
		}

		async Task<List<Issue>> FilterIssuesByStreamIdAsync(IEnumerable<int>? Ids, StreamId? StreamId, int? MinChange, int? MaxChange, bool? Resolved, int Index, int? Count)
		{
			if (StreamId == null)
			{
				return await FilterIssuesByOtherFieldsAsync(Ids, MinChange, MaxChange, Resolved, Index, Count);
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
					return await PaginatedJoinAsync(Cursor, (NextIds, NextIndex, NextCount) => FilterIssuesByOtherFieldsAsync(NextIds, null, null, null, NextIndex, NextCount), Index, Count);
				}
			}
		}

		async Task<List<Issue>> FilterIssuesByOtherFieldsAsync(IEnumerable<int>? Ids, int? MinChange, int? MaxChange, bool? Resolved, int Index, int? Count)
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
		public async Task<List<IIssue>> FindIssuesForSuspectsAsync(List<int> Changes)
		{
			List<IIssue> Results = new List<IIssue>();
			if (Changes.Count > 0)
			{
				int MinChange = Changes.Min();
				int MaxChange = Changes.Max();

				List<Issue> BaseResults = await Issues.Find(x => x.MinSuspectChange <= MaxChange && x.MaxSuspectChange >= MinChange).ToListAsync();
				await UpgradeIssuesAsync(BaseResults);
				Results.AddRange(BaseResults.Where(x => x.Suspects.Any(y => Changes.Contains(y.Change))));
			}
			return Results;
		}

		async Task<ObjectId> FindOrAddUserIdAsync(string UserName)
		{
			ObjectId UserId;
			if (!UserNameToIdCache.TryGetValue(UserName, out UserId))
			{
				PerforceUserInfo? Info = await PerforceService.GetUserInfoAsync(UserName);
				if (Info == null)
				{
					Logger.LogWarning("No user '{UserName}' found", UserName);
				}

				IUser User = await UserCollection.FindOrAddUserByLoginAsync(UserName, Info?.Name, Info?.Email);
				UserId = User.Id;

				UserNameToIdCache.TryAdd(UserName, UserId);
			}
			return UserId;
		}

		async Task<IIssue> UpgradeIssueAsync(Issue Issue)
		{
			bool bModified = false;
			if (Issue.Owner != null && Issue.OwnerId == null)
			{
				Issue.OwnerId = await FindOrAddUserIdAsync(Issue.Owner);
				bModified = true;
			}
			if (Issue.NominatedBy != null && Issue.NominatedById == null)
			{
				Issue.NominatedById = await FindOrAddUserIdAsync(Issue.NominatedBy);
				bModified = true;
			}

			if (bModified)
			{
				await Issues.ReplaceOneAsync(x => x.Id == Issue.Id && x.UpdateIndex == Issue.UpdateIndex, Issue);
			}

			return Issue;
		}

		async Task<List<IIssue>> UpgradeIssuesAsync(List<Issue> Issues)
		{
			List<IIssue> Results = new List<IIssue>();
			foreach(Issue Issue in Issues)
			{
				Results.Add(await UpgradeIssueAsync(Issue));
			}
			return Results;
		}

		async Task<IIssueSpan> UpgradeIssueSpanAsync(IssueSpan IssueSpan)
		{
			bool bModified = false;
			foreach (IssueSpanSuspect Suspect in IssueSpan.Suspects)
			{
				if (Suspect.AuthorId == ObjectId.Empty)
				{
					Suspect.AuthorId = await FindOrAddUserIdAsync(Suspect.Author);
					bModified = true;
				}
			}

			if (bModified)
			{
				await IssueSpans.ReplaceOneAsync(x => x.Id == IssueSpan.Id && x.UpdateIndex == IssueSpan.UpdateIndex, IssueSpan);
			}

			return IssueSpan;
		}

		async Task<List<IIssueSpan>> UpgradeIssueSpansAsync(List<IssueSpan> IssueSpans)
		{
			List<IIssueSpan> Results = new List<IIssueSpan>();
			foreach (IssueSpan IssueSpan in IssueSpans)
			{
				Results.Add(await UpgradeIssueSpanAsync(IssueSpan));
			}
			return Results;
		}

		async Task<List<IssueSpanSuspect>> CreateSpanSuspectsAsync(List<NewIssueSpanSuspectData> Suspects)
		{
			List<IssueSpanSuspect> NewSuspects = new List<IssueSpanSuspect>();
			foreach (NewIssueSpanSuspectData Suspect in Suspects)
			{
				ObjectId AuthorId = await FindOrAddUserIdAsync(Suspect.Author);
				NewSuspects.Add(new IssueSpanSuspect(Suspect.Change, Suspect.Author, AuthorId, Suspect.OriginatingChange));
			}
			return NewSuspects;
		}

		/// <inheritdoc/>
		public async Task<IIssue?> UpdateIssueAsync(IIssue Issue, IssueSeverity? NewSeverity = null, string? NewSummary = null, string? NewUserSummary = null, ObjectId? NewOwnerId = null, ObjectId? NewNominatedById = null, bool? NewAcknowledged = null, ObjectId? NewDeclinedById = null, int? NewFixChange = null, Dictionary<StreamId, bool>? NewFixStreamIds = null, ObjectId? NewResolvedById = null, DateTime? NewLastSeenAt = null, bool? NewNotifySuspects = null)
		{
			Issue IssueDocument = (Issue)Issue;

			DateTime UtcNow = DateTime.UtcNow;

			TransactionBuilder<Issue> Transaction = new TransactionBuilder<Issue>();
			if (NewSeverity != null)
			{
				Transaction.Set(x => x.Severity, NewSeverity.Value);
			}
			if (NewSummary != null)
			{
				Transaction.Set(x => x.Summary, NewSummary);
			}
			if (NewUserSummary != null)
			{
				if (NewUserSummary.Length == 0)
				{
					Transaction.Unset(x => x.UserSummary!);
				}
				else
				{
					Transaction.Set(x => x.UserSummary, NewUserSummary);
				}
			}
			if (NewOwnerId != null)
			{
				if (NewOwnerId.Value == ObjectId.Empty)
				{
					Transaction.Unset(x => x.Owner!);
					Transaction.Unset(x => x.OwnerId!);
					Transaction.Unset(x => x.NominatedAt!);
					Transaction.Unset(x => x.NominatedBy!);
					Transaction.Unset(x => x.NominatedById!);
				}
				else
				{
					IUser? NewOwner = await UserCollection.GetCachedUserAsync(NewOwnerId.Value);
					Transaction.Set(x => x.Owner!, NewOwner?.Login);
					Transaction.Set(x => x.OwnerId!, NewOwnerId.Value);

					Transaction.Set(x => x.NominatedAt, DateTime.UtcNow);
					if (NewNominatedById == null)
					{
						Transaction.Unset(x => x.NominatedBy!);
						Transaction.Unset(x => x.NominatedById!);
					}
					else
					{
						IUser? NewNominatedBy = await UserCollection.GetCachedUserAsync(NewNominatedById.Value);
						Transaction.Set(x => x.NominatedBy, NewNominatedBy?.Login);
						Transaction.Set(x => x.NominatedById, NewNominatedById.Value);
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
						Transaction.Set(x => x.AcknowledgedAt, UtcNow);
					}
				}
				else
				{
					if (IssueDocument.AcknowledgedAt != null)
					{
						Transaction.Unset(x => x.AcknowledgedAt!);
					}
				}
			}
			if (NewDeclinedById != null)
			{
				for (int SuspectIdx = 0; SuspectIdx < IssueDocument.Suspects.Count; SuspectIdx++)
				{
					IssueSuspect Suspect = IssueDocument.Suspects[SuspectIdx];
					if (Suspect.AuthorId == NewDeclinedById.Value && Suspect.DeclinedAt == null)
					{
						int SuspectIdxCopy = SuspectIdx;
						Transaction.Set(x => x.Suspects[SuspectIdxCopy].DeclinedAt, UtcNow);
					}
				}
			}
			if (NewFixChange != null)
			{
				if (NewFixChange == 0)
				{
					Transaction.Unset(x => x.FixChange!);
				}
				else
				{
					Transaction.Set(x => x.FixChange, NewFixChange);
				}
			}
			if (NewFixStreamIds != null)
			{
				List<IssueStream> NewStreams = new List<IssueStream>();
				foreach (IssueStream Stream in Issue.Streams)
				{
					IssueStream NewStream = new IssueStream(Stream);
					if (NewFixStreamIds.TryGetValue(Stream.StreamId, out bool bContainsFix))
					{
						NewStream.ContainsFix = bContainsFix;
					}
					else
					{
						NewStream.ContainsFix = null;
					}
				}
				Transaction.Set(x => x.Streams, NewStreams);
			}
			if (NewResolvedById != null)
			{
				if (NewResolvedById.Value != ObjectId.Empty)
				{
					if (IssueDocument.ResolvedAt == null || IssueDocument.ResolvedById != NewResolvedById)
					{
						Transaction.Set(x => x.ResolvedAt, UtcNow);
						Transaction.Set(x => x.ResolvedById, NewResolvedById.Value);
					}
				}
				else
				{
					if (IssueDocument.ResolvedAt != null)
					{
						Transaction.Unset(x => x.ResolvedAt!);
					}
					if (IssueDocument.ResolvedById != null)
					{
						Transaction.Unset(x => x.ResolvedById!);
					}
				}
			}
			if (NewLastSeenAt != null)
			{
				Transaction.Set(x => x.LastSeenAt, NewLastSeenAt.Value);
			}
			if (NewNotifySuspects != null)
			{
				Transaction.Set(x => x.NotifySuspects, NewNotifySuspects.Value);
			}

			IIssue? NewIssue = await ApplyTransactionAsync(Issue, Transaction);
			if (NewIssue != null)
			{
				if (NewResolvedById != null)
				{
					await UpdateIssueDerivedDataAsync(Issue);
				}
			}
			return NewIssue;
		}

		public async Task<IIssue?> UpdateIssueDerivedDataAsync(IIssue Issue)
		{
			for (; ; )
			{
				Issue IssueDocument = (Issue)Issue;

				// Find all the spans that are attached to the issue
				List<IssueSpan> Spans = await IssueSpans.Find(x => x.IssueId == Issue.Id).ToListAsync();

				// Find the list of suspects for this issue. If the issue has an assigned owner, the other suspects can be eliminated.
				List<IssueSuspect> NewSuspects = new List<IssueSuspect>();
				if (Spans.Count > 0)
				{
					HashSet<int> SuspectChanges = new HashSet<int>(Spans[0].Suspects.Where(x => Issue.OwnerId == null || x.AuthorId == Issue.OwnerId).Select(x => x.OriginatingChange ?? x.Change));
					for (int SpanIdx = 1; SpanIdx < Spans.Count; SpanIdx++)
					{
						SuspectChanges.IntersectWith(Spans[SpanIdx].Suspects.Select(x => x.OriginatingChange ?? x.Change));
					}
					NewSuspects = Spans[0].Suspects.Where(x => SuspectChanges.Contains(x.OriginatingChange ?? x.Change)).Select(x => new IssueSuspect(Issue.Id, x)).ToList();
				}
				if (Issue.OwnerId != null)
				{
					NewSuspects.RemoveAll(x => x.AuthorId != Issue.OwnerId);
					if (NewSuspects.Count == 0)
					{
						NewSuspects.Add(new IssueSuspect(Issue.Id, Issue.OwnerId.Value, 0, null, null));
					}
				}

				// Find the current list of suspects
				List<IssueSuspect> CurSuspects = await IssueSuspects.Find(x => x.IssueId == Issue.Id).ToListAsync();

				HashSet<(ObjectId, int)> CurSuspectKeys = new HashSet<(ObjectId, int)>(CurSuspects.Select(x => (x.AuthorId, x.Change)));
				List<IssueSuspect> CreateSuspects = NewSuspects.Where(x => !CurSuspectKeys.Contains((x.AuthorId, x.Change))).ToList();

				HashSet<(ObjectId, int)> NewSuspectKeys = new HashSet<(ObjectId, int)>(NewSuspects.Select(x => (x.AuthorId, x.Change)));
				List<IssueSuspect> DeleteSuspects = CurSuspects.Where(x => !NewSuspectKeys.Contains((x.AuthorId, x.Change))).ToList();

				// Apply the suspect changes
				if (CreateSuspects.Count > 0)
				{
					await IssueSuspects.InsertManyIgnoreDuplicatesAsync(CreateSuspects);
					CurSuspects.AddRange(CreateSuspects);
				}
				if (DeleteSuspects.Count > 0)
				{
					await IssueSuspects.DeleteManyAsync(Builders<IssueSuspect>.Filter.In(x => x.Id, DeleteSuspects.Select(y => y.Id)));
					CurSuspects.RemoveAll(x => !NewSuspectKeys.Contains((x.AuthorId, x.Change)));
				}

				// Calculate the last time that this issue was seen
				DateTime NewLastSeenAt = Issue.CreatedAt;
				foreach (IIssueSpan Span in Spans)
				{
					if (Span.LastFailure.StepTime > NewLastSeenAt)
					{
						NewLastSeenAt = Span.LastFailure.StepTime;
					}
				}

				// Calculate the time at which the issue is verified fixed
				DateTime? VerifiedAt = null;
				foreach (IIssueSpan Span in Spans)
				{
					if(Span.NextSuccess == null)
					{
						break;
					}
					else if(VerifiedAt == null || Span.NextSuccess.StepTime < VerifiedAt.Value)
					{
						VerifiedAt = Span.NextSuccess.StepTime;
					}
				}
				if (Spans.Count == 0)
				{
					VerifiedAt = Issue.VerifiedAt ?? DateTime.UtcNow;
				}

				// Update the spans with the correct resolved time
				DateTime? ResolvedAt = Issue.ResolvedAt ?? VerifiedAt;

				List<ObjectId> UpdateSpanIds = Spans.Where(x => x.ResolvedAt != ResolvedAt).Select(x => x.Id).ToList();
				if (UpdateSpanIds.Count > 0)
				{
					await IssueSpans.UpdateManyAsync(Builders<IssueSpan>.Filter.In(x => x.Id, UpdateSpanIds), Builders<IssueSpan>.Update.Set(x => x.ResolvedAt, ResolvedAt));
				}

				List<ObjectId> UpdateSuspectIds = CurSuspects.Where(x => x.ResolvedAt != ResolvedAt).Select(x => x.Id).ToList();
				if (UpdateSuspectIds.Count > 0)
				{
					await IssueSuspects.UpdateManyAsync(Builders<IssueSuspect>.Filter.In(x => x.Id, UpdateSuspectIds), Builders<IssueSuspect>.Update.Set(x => x.ResolvedAt, ResolvedAt));
				}

				// Build a list of streams
				List<IssueStream> Streams = new List<IssueStream>();
				foreach (IGrouping<StreamId, IIssueSpan> Group in Spans.GroupBy(x => x.StreamId))
				{
					IssueStream? Stream = IssueDocument.Streams.FirstOrDefault(x => x.StreamId == Group.Key);
					if (Stream == null)
					{
						Stream = new IssueStream { StreamId = Group.Key };
					}
					else
					{
						Stream = new IssueStream(Stream);
					}
					Streams.Add(Stream);
				}

				// Update the issue document itself
				TransactionBuilder<Issue> Transaction = new TransactionBuilder<Issue>();
				Transaction.Set(x => x.Severity, Spans.Any(x => x.Severity == IssueSeverity.Error) ? IssueSeverity.Error : IssueSeverity.Warning);
				Transaction.Set(x => x.NotifySuspects, Spans.Any(x => x.NotifySuspects));
				Transaction.Set(x => x.LastSeenAt, NewLastSeenAt);
				Transaction.Set(x => x.ResolvedAt, ResolvedAt);
				Transaction.Set(x => x.VerifiedAt, VerifiedAt);
				Transaction.Set(x => x.Suspects, CurSuspects);
				Transaction.Set(x => x.Streams, Streams);
				Transaction.Set(x => x.MinSuspectChange, (CurSuspects.Count > 0) ? CurSuspects.Min(x => x.Change) : 0);
				Transaction.Set(x => x.MaxSuspectChange, (CurSuspects.Count > 0) ? CurSuspects.Max(x => x.Change) : 0);

				// Apply the transaction
				IIssue? NewIssue = await ApplyTransactionAsync(Issue, Transaction);
				if(NewIssue != null)
				{
					return NewIssue;
				}

				// Update the issue
				NewIssue = await GetIssueAsync(Issue.Id);
				if(NewIssue == null)
				{
					return null;
				}
				Issue = NewIssue;
			}
		}

		#endregion

		#region Spans

		/// <inheritdoc/>
		public async Task<IIssueSpan?> AddSpanAsync(IIssueSequenceToken Token, IStream Stream, TemplateRefId TemplateRefId, string NodeName, NewIssueFingerprint Fingerprint, NewIssueStepData? LastSuccess, NewIssueStepData Failure, NewIssueStepData? NextSuccess, List<NewIssueSpanSuspectData> Suspects)
		{
			List<IssueSpanSuspect> NewSuspects = await CreateSpanSuspectsAsync(Suspects);
			IssueSpan NewSpan = new IssueSpan(Stream.Id, Stream.Name, TemplateRefId, NodeName, Fingerprint, LastSuccess, Failure, NextSuccess, NewSuspects);

			IssueLedger Ledger = ((IssueSequenceToken)Token).Ledger;
			Ledger.InsertIssueSpan = NewSpan;

			if (!await LedgerSingleton.TryUpdateAsync(Ledger))
			{
				return null;
			}

			await FlushLedgerAsync(Ledger);
			await AddStepAsync(NewSpan.Id, Failure);
			return NewSpan;
		}

		/// <inheritdoc/>
		public async Task<IIssueSpan?> GetSpanAsync(ObjectId SpanId)
		{
			return await IssueSpans.Find(Builders<IssueSpan>.Filter.Eq(x => x.Id, SpanId)).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IIssueSpan?> UpdateSpanAsync(IIssueSpan Span, IssueSeverity? NewSeverity = null, NewIssueStepData? NewLastSuccess = null, NewIssueStepData? NewFailure = null, NewIssueStepData? NewNextSuccess = null, List<NewIssueSpanSuspectData>? NewSuspects = null, bool? NewModified = null)
		{
			TransactionBuilder<IssueSpan> Transaction = new TransactionBuilder<IssueSpan>();
			if (NewSeverity != null)
			{
				Transaction.Set(x => x.Severity, NewSeverity.Value);
			}
			if (NewLastSuccess != null)
			{
				Transaction.Set(x => x.MinChange, NewLastSuccess.Change);
				Transaction.Set(x => x.LastSuccess, new IssueStep(Span.Id, NewLastSuccess));
			}
			if (NewFailure != null)
			{
				if (NewFailure.Change < Span.FirstFailure.Change)
				{
					Transaction.Set(x => x.FirstFailure, new IssueStep(Span.Id, NewFailure));
				}
				if (NewFailure.Change >= Span.LastFailure.Change)
				{
					Transaction.Set(x => x.LastFailure, new IssueStep(Span.Id, NewFailure));
				}
				if (NewFailure.NotifySuspects != Span.NotifySuspects && NewFailure.Change >= Span.LastFailure.Change)
				{
					Transaction.Set(x => x.NotifySuspects, NewFailure.NotifySuspects);
				}
			}
			if (NewNextSuccess != null)
			{
				Transaction.Set(x => x.MaxChange, NewNextSuccess.Change);
				Transaction.Set(x => x.NextSuccess, new IssueStep(Span.Id, NewNextSuccess));
			}
			if (NewSuspects != null)
			{
				Transaction.Set(x => x.Suspects, await CreateSpanSuspectsAsync(NewSuspects));
			}
			if (NewModified != null)
			{
				Transaction.Set(x => x.Modified, NewModified.Value);
			}
			
			IIssueSpan? NewSpan = await ApplyTransactionAsync(Span, Transaction);
			if (NewSpan != null && NewFailure != null)
			{
				await AddStepAsync(NewSpan.Id, NewFailure);
			}
			return NewSpan;
		}

		/// <inheritdoc/>
		public async Task<List<IIssueSpan>> FindSpansAsync(int IssueId)
		{
			return await UpgradeIssueSpansAsync(await IssueSpans.Find(x => x.IssueId == IssueId).ToListAsync());
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

		/// <inheritdoc/>
		public async Task<bool> AttachSpanToIssueAsync(IIssueSequenceToken Token, IIssueSpan Span, IIssue Issue, IssueSeverity? NewSeverity = null, NewIssueFingerprint? NewFingerprint = null, string? NewSummary = null)
		{
			List<UpdateDefinition<Issue>> IssueUpdates = new List<UpdateDefinition<Issue>>();
			if (NewSeverity != null)
			{
				IssueUpdates.Add(Builders<Issue>.Update.Set(x => x.Severity, NewSeverity.Value));
			}
			if (NewFingerprint != null)
			{
				IssueUpdates.Add(Builders<Issue>.Update.Set(x => x.Fingerprint, new IssueFingerprint(NewFingerprint)));
			}
			if (NewSummary != null)
			{
				IssueUpdates.Add(Builders<Issue>.Update.Set(x => x.Summary, NewSummary));
			}
			if (Span.LastFailure.StepTime > Issue.LastSeenAt)
			{
				IssueUpdates.Add(Builders<Issue>.Update.Set(x => x.LastSeenAt, Span.LastFailure.StepTime));
			}

			IssueSequenceToken TokenValue = ((IssueSequenceToken)Token);
			if (IssueUpdates.Count > 0)
			{
				TokenValue.Ledger.UpdateIssue = UpdateTransaction.Create(Builders<Issue>.Filter.Eq(x => x.Id, Issue.Id), Builders<Issue>.Update.Combine(IssueUpdates));
			}
			TokenValue.Ledger.UpdateIssueSpan = UpdateTransaction.Create(Builders<IssueSpan>.Filter.Eq(x => x.Id, Span.Id), Builders<IssueSpan>.Update.Set(x => x.IssueId, Issue.Id));

			if (!await LedgerSingleton.TryUpdateAsync(TokenValue.Ledger))
			{
				return false;
			}

			await TokenValue.FlushAsync();
			return true;
		}

		/// <inheritdoc/>
		public async Task<IIssueSpan?> FindModifiedSpanAsync()
		{
			IssueSpan? IssueSpan = await IssueSpans.Find(x => x.Modified).FirstOrDefaultAsync();
			return IssueSpan;
		}

		#endregion

		#region Steps

		/// <inheritdoc/>
		async Task AddStepAsync(ObjectId SpanId, NewIssueStepData NewStep)
		{
			await IssueSteps.InsertOneIgnoreDuplicatesAsync(new IssueStep(SpanId, NewStep));
		}

		/// <inheritdoc/>
		public Task<List<IIssueStep>> FindStepsAsync(IEnumerable<ObjectId> SpanIds)
		{
			FilterDefinition<IssueStep> Filter = Builders<IssueStep>.Filter.In(x => x.SpanId, SpanIds);
			return IssueSteps.Find(Filter).ToListAsync<IssueStep, IIssueStep>();
		}

		/// <inheritdoc/>
		public Task<List<IIssueStep>> FindStepsAsync(ObjectId JobId, SubResourceId? BatchId, SubResourceId? StepId)
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
	}
}
