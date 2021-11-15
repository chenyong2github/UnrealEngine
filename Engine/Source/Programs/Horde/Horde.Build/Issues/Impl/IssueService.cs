// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Redis.Utility;
using HordeCommon;
using HordeServer.Collections;
using HordeServer.IssueHandlers;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Data.Common;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using HordeServer.IssueHandlers.Impl;
using HordeServer.Notifications;
using OpenTracing;
using OpenTracing.Util;

namespace HordeServer.Services.Impl
{
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Detailed issue information
	/// </summary>
	class IssueDetails : IIssueDetails
	{
		/// <inheritdoc/>
		public IIssue Issue { get; }

		/// <inheritdoc/>
		public IUser? Owner { get; }

		/// <inheritdoc/>
		public IUser? NominatedBy { get; }

		/// <inheritdoc/>
		public IUser? ResolvedBy { get; }

		/// <inheritdoc/>
		public IReadOnlyList<IIssueSpan> Spans { get; }

		/// <inheritdoc/>
		public IReadOnlyList<IIssueStep> Steps { get; }

		/// <inheritdoc/>
		public IReadOnlyList<IIssueSuspect> Suspects { get; }

		/// <inheritdoc/>
		public IReadOnlyList<IUser> SuspectUsers { get; }

		/// <inheritdoc/>
		bool ShowDesktopAlerts;

		/// <inheritdoc/>
		HashSet<UserId> NotifyUsers;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Issue"></param>
		/// <param name="Owner"></param>
		/// <param name="NominatedBy"></param>
		/// <param name="ResolvedBy"></param>
		/// <param name="Spans">Spans for this issue</param>
		/// <param name="Steps">Steps for this issue</param>
		/// <param name="Suspects"></param>
		/// <param name="SuspectUsers"></param>
		/// <param name="ShowDesktopAlerts"></param>
		public IssueDetails(IIssue Issue, IUser? Owner, IUser? NominatedBy, IUser? ResolvedBy, IReadOnlyList<IIssueSpan> Spans, IReadOnlyList<IIssueStep> Steps, IReadOnlyList<IIssueSuspect> Suspects, IReadOnlyList<IUser> SuspectUsers, bool ShowDesktopAlerts)
		{
			this.Issue = Issue;
			this.Owner = Owner;
			this.NominatedBy = NominatedBy;
			this.ResolvedBy = ResolvedBy;
			this.Spans = Spans;
			this.Steps = Steps;
			this.Suspects = Suspects;
			this.SuspectUsers = SuspectUsers;
			this.ShowDesktopAlerts = ShowDesktopAlerts;

			if (Issue.OwnerId == null)
			{
				NotifyUsers = new HashSet<UserId>(Suspects.Select(x => x.AuthorId));
			}
			else
			{
				NotifyUsers = new HashSet<UserId> { Issue.OwnerId.Value };
			}
		}

		/// <summary>
		/// Determines whether the given user should be notified about the given issue
		/// </summary>
		/// <returns>True if the user should be notified for this change</returns>
		public bool ShowNotifications()
		{
			return ShowDesktopAlerts && Issue.Fingerprints.Any(x => x.Type != DefaultIssueHandler.Type);
		}

		/// <summary>
		/// Determines if the issue is relevant to the given user
		/// </summary>
		/// <param name="UserId">The user to query</param>
		/// <returns>True if the issue is relevant to the given user</returns>
		public bool IncludeForUser(UserId UserId)
		{
			return NotifyUsers.Contains(UserId);
		}
	}

	/// <summary>
	/// Wraps funtionality for manipulating build health issues
	/// </summary>
	public class IssueService : ElectedBackgroundService, IIssueService
	{
		/// <summary>
		/// Maximum number of changes to query from Perforce in one go
		/// </summary>
		const int MaxChanges = 250;

		RedisService RedisService;
		IJobCollection Jobs;
		IJobStepRefCollection JobStepRefs;
		IIssueCollection IssueCollection;
		StreamService Streams;
		IUserCollection UserCollection;
		IPerforceService Perforce;
		ILogFileService LogFileService;

		/// <summary>
		/// 
		/// </summary>
		public event IIssueService.IssueUpdatedEvent? OnIssueUpdated;

		/// <summary>
		/// List of issue matchers
		/// </summary>
		List<IIssueHandler> Matchers = new List<IIssueHandler>();

		/// <summary>
		/// Available issue serializers
		/// </summary>
		Dictionary<string, IIssueHandler> TypeToHandler = new Dictionary<string, IIssueHandler>(StringComparer.Ordinal);

		/// <summary>
		/// Cached list of currently open issues
		/// </summary>
		IReadOnlyList<IIssueDetails> CachedIssues = new List<IIssueDetails>();

		/// <summary>
		/// Cache of templates to show desktop alerts for
		/// </summary>
		Dictionary<StreamId, HashSet<TemplateRefId>> CachedDesktopAlerts = new Dictionary<StreamId, HashSet<TemplateRefId>>();

		/// <summary>
		/// Logger for tracing
		/// </summary>
		ILogger<IssueService> Logger;

		/// <summary>
		/// Accessor for list of cached open issues
		/// </summary>
		public IReadOnlyList<IIssueDetails> CachedOpenIssues => CachedIssues;

		/// <summary>
		/// Constructor
		/// </summary>
		public IssueService(DatabaseService DatabaseService, RedisService RedisService, IIssueCollection IssueCollection, IJobCollection Jobs, IJobStepRefCollection JobStepRefs, StreamService Streams, IUserCollection UserCollection, IPerforceService Perforce, ILogFileService LogFileService, ILogger<IssueService> Logger)
			: base(DatabaseService, ObjectId.Parse("609542152fb0794700a6c3df"), Logger)
		{
			this.RedisService = RedisService;

			Type[] IssueTypes = Assembly.GetExecutingAssembly().GetTypes().Where(x => !x.IsAbstract && typeof(IIssue).IsAssignableFrom(x)).ToArray();
			foreach (Type IssueType in IssueTypes)
			{
				BsonClassMap.LookupClassMap(IssueType);
			}

			// Get all the collections
			this.IssueCollection = IssueCollection;
			this.Jobs = Jobs;
			this.JobStepRefs = JobStepRefs;
			this.Streams = Streams;
			this.UserCollection = UserCollection;
			this.Perforce = Perforce;
			this.LogFileService = LogFileService;
			this.Logger = Logger;

			// Create all the issue factories
			Type[] MatcherTypes = Assembly.GetExecutingAssembly().GetTypes().Where(x => !x.IsAbstract && typeof(IIssueHandler).IsAssignableFrom(x)).ToArray();
			foreach (Type MatcherType in MatcherTypes)
			{
				IIssueHandler? Matcher = (IIssueHandler?)Activator.CreateInstance(MatcherType);
				if (Matcher != null)
				{
					Matchers.Add(Matcher);
				}
			}
			Matchers.SortBy(x => -x.Priority);

			// Build the type name to factory map
			TypeToHandler = Matchers.ToDictionary(x => x.Type, x => x, StringComparer.Ordinal);
		}

		/// <summary>
		/// Periodically update the list of cached open issues
		/// </summary>
		/// <param name="StoppingToken">Token to indicate that the service should stop</param>
		/// <returns>Async task</returns>
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime UtcNow = DateTime.UtcNow;

			Dictionary<StreamId, HashSet<TemplateRefId>> NewCachedDesktopAlerts = new Dictionary<StreamId, HashSet<TemplateRefId>>();

			List<IStream> CachedStreams = await Streams.StreamCollection.FindAllAsync();
			foreach(IStream CachedStream in CachedStreams)
			{
				HashSet<TemplateRefId> Templates = new HashSet<TemplateRefId>(CachedStream.Templates.Where(x => x.Value.ShowUgsAlerts).Select(x => x.Key));
				if(Templates.Count > 0)
				{
					NewCachedDesktopAlerts[CachedStream.Id] = Templates;
				}
			}

			CachedDesktopAlerts = NewCachedDesktopAlerts;

			// Resolve any issues that haven't been seen in a week
			List<IIssue> OpenIssues = await FindIssuesAsync(Resolved: false);
			for (int Idx = 0; Idx < OpenIssues.Count; Idx++)
			{
				IIssue OpenIssue = OpenIssues[Idx];
				if (OpenIssue.LastSeenAt < UtcNow - TimeSpan.FromDays(7.0))
				{
					await IssueCollection.TryUpdateIssueAsync(OpenIssue, NewResolvedById: IIssue.ResolvedByTimeoutId);
					OpenIssues.RemoveAt(Idx--);
				}
			}

			// Cache the details for any issues that are still open
			List<IIssueDetails> NewCachedOpenIssues = new List<IIssueDetails>();
			foreach(IIssue OpenIssue in OpenIssues)
			{
				NewCachedOpenIssues.Add(await GetIssueDetailsAsync(OpenIssue));
			}
			CachedIssues = NewCachedOpenIssues;

			return UtcNow + TimeSpan.FromMinutes(1.0);
		}

		/// <inheritdoc/>
		public async Task<IIssueDetails?> GetCachedIssueDetailsAsync(int IssueId)
		{
			IIssueDetails? CachedIssue = CachedIssues.FirstOrDefault(x => x.Issue.Id == IssueId);
			if (CachedIssue == null)
			{
				IIssue? Issue = await IssueCollection.GetIssueAsync(IssueId);
				if (Issue != null)
				{
					CachedIssue = await GetIssueDetailsAsync(Issue);
				}
			}
			return CachedIssue;
		}

		/// <inheritdoc/>
		public async Task<IIssueDetails> GetIssueDetailsAsync(IIssue Issue)
		{
			IUser? Owner = Issue.OwnerId.HasValue ? await UserCollection.GetCachedUserAsync(Issue.OwnerId.Value) : null;
			IUser? NominatedBy = Issue.NominatedById.HasValue ? await UserCollection.GetCachedUserAsync(Issue.NominatedById.Value) : null;
			IUser? ResolvedBy = (Issue.ResolvedById.HasValue && Issue.ResolvedById != IIssue.ResolvedByTimeoutId && Issue.ResolvedById != IIssue.ResolvedByUnknownId)? await UserCollection.GetCachedUserAsync(Issue.ResolvedById.Value) : null;

			List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(Issue.Id);
			List<IIssueStep> Steps = await IssueCollection.FindStepsAsync(Spans.Select(x => x.Id));
			List<IIssueSuspect> Suspects = await IssueCollection.FindSuspectsAsync(Issue);

			List<IUser> SuspectUsers = new List<IUser>();
			foreach (UserId SuspectUserId in Suspects.Select(x => x.AuthorId).Distinct())
			{
				IUser? SuspectUser = await UserCollection.GetCachedUserAsync(SuspectUserId);
				if (SuspectUser != null)
				{
					SuspectUsers.Add(SuspectUser);
				}
			}

			return new IssueDetails(Issue, Owner, NominatedBy, ResolvedBy, Spans, Steps, Suspects, SuspectUsers, ShowDesktopAlertsForIssue(Issue, Spans));
		}

		/// <inheritdoc/>
		public Task<List<IIssueSpan>> GetIssueSpansAsync(IIssue Issue)
		{
			return IssueCollection.FindSpansAsync(Issue.Id);
		}

		/// <inheritdoc/>
		public Task<List<IIssueStep>> GetIssueStepsAsync(IIssueSpan Span)
		{
			return IssueCollection.FindStepsAsync(new[] { Span.Id });
		}

		/// <inheritdoc/>
		public Task<List<IIssueSuspect>> GetIssueSuspectsAsync(IIssue Issue)
		{
			return IssueCollection.FindSuspectsAsync(Issue);
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, UserId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, bool? Promoted = null, int? Index = null, int? Count = null)
		{
			return await IssueCollection.FindIssuesAsync(Ids, UserId, StreamId, MinChange, MaxChange, Resolved, Promoted, Index, Count);
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesForJobAsync(int[]? Ids, IJob Job, IGraph Graph, SubResourceId? StepId = null, SubResourceId? BatchId = null, int? LabelIdx = null, UserId? UserId = null, bool? Resolved = null, bool? Promoted = null, int? Index = null, int? Count = null)
		{
			List<IIssueStep> Steps = await IssueCollection.FindStepsAsync(Job.Id, BatchId, StepId);
			List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(Steps.Select(x => x.SpanId));

			if (LabelIdx != null)
			{
				HashSet<string> NodeNames = new HashSet<string>(Job.GetNodesForLabel(Graph, LabelIdx.Value).Select(x => Graph.GetNode(x).Name));
				Spans.RemoveAll(x => !NodeNames.Contains(x.NodeName));
			}

			List<int> IssueIds = new List<int>(Spans.Select(x => x.IssueId));
			if(IssueIds.Count == 0)
			{
				return new List<IIssue>();
			}

			return await FindIssuesAsync(Ids: IssueIds, UserId: UserId, Resolved: Resolved, Promoted: Promoted, Index: Index, Count: Count);
		}

		/// <inheritdoc/>
		public bool ShowDesktopAlertsForIssue(IIssue Issue, IReadOnlyList<IIssueSpan> Spans)
		{
			bool bShowDesktopAlerts = false;

			HashSet<(StreamId, TemplateRefId)> CheckedTemplates = new HashSet<(StreamId, TemplateRefId)>();
			foreach(IIssueSpan Span in Spans)
			{
				if (Span.NextSuccess == null && CheckedTemplates.Add((Span.StreamId, Span.TemplateRefId)))
				{
					HashSet<TemplateRefId>? Templates;
					if (CachedDesktopAlerts.TryGetValue(Span.StreamId, out Templates) && Templates.Contains(Span.TemplateRefId))
					{
						bShowDesktopAlerts = true;
						break;
					}
				}
			}

			return bShowDesktopAlerts;
		}

		/// <summary>
		/// Gets an issue with the given id
		/// </summary>
		/// <param name="IssueId"></param>
		/// <returns></returns>
		public Task<IIssue?> GetIssueAsync(int IssueId)
		{
			return IssueCollection.GetIssueAsync(IssueId);
		}

		/// <inheritdoc/>
		public async Task<bool> UpdateIssueAsync(int Id, string? UserSummary = null, string? Description = null, bool? Promoted = null, UserId? OwnerId = null, UserId? NominatedById = null, bool? Acknowledged = null, UserId? DeclinedById = null, int? FixChange = null, UserId? ResolvedById = null, List<ObjectId>? AddSpanIds = null, List<ObjectId>? RemoveSpanIds = null)
		{
			IIssue? Issue;
			for (; ; )
			{
				Issue = await IssueCollection.GetIssueAsync(Id);
				if (Issue == null)
				{
					return false;
				}

				Issue = await IssueCollection.TryUpdateIssueAsync(Issue, NewUserSummary: UserSummary, NewDescription: Description, NewPromoted: Promoted, NewOwnerId: OwnerId ?? ResolvedById, NewNominatedById: NominatedById, NewDeclinedById: DeclinedById, NewAcknowledged: Acknowledged, NewFixChange: FixChange, NewResolvedById: ResolvedById, NewExcludeSpanIds: RemoveSpanIds);
				if (Issue != null)
				{
					break;
				}
			}

			await using IAsyncDisposable Lock = await IssueCollection.EnterCriticalSectionAsync();

			List<IIssue> UpdateIssues = new List<IIssue>();
			UpdateIssues.Add(Issue);

			if (AddSpanIds != null)
			{
				foreach (ObjectId AddSpanId in AddSpanIds)
				{
					IIssueSpan? Span = await IssueCollection.GetSpanAsync(AddSpanId);
					if (Span != null)
					{
						IIssue? OldIssue = await IssueCollection.GetIssueAsync(Span.IssueId);
						UpdateIssues.Add(OldIssue ?? Issue);
						await IssueCollection.TryUpdateSpanAsync(Span, NewIssueId: Issue.Id);
					}
				}
			}

			if (RemoveSpanIds != null)
			{
				foreach (ObjectId RemoveSpanId in RemoveSpanIds)
				{
					IIssueSpan? Span = await IssueCollection.GetSpanAsync(RemoveSpanId);
					if (Span != null)
					{
						IIssue NewIssue = await FindOrAddIssueForSpanAsync(Span);
						UpdateIssues.Add(NewIssue);
						await IssueCollection.TryUpdateSpanAsync(Span, NewIssueId: NewIssue.Id);
					}
				}
			}

			foreach (IIssue UpdateIssue in UpdateIssues.GroupBy(x => x.Id).Select(x => x.First()))
			{
				await UpdateIssueDerivedDataAsync(UpdateIssue);
			}

			return true;
		}

		/// <summary>
		/// Information about a particular log event to be added to a span
		/// </summary>
		class NewEvent
		{
			/// <summary>
			/// The event interface
			/// </summary>
			public ILogEvent Event { get; }

			/// <summary>
			/// Data for the event
			/// </summary>
			public ILogEventData EventData { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Event">The event interface</param>
			/// <param name="EventData">Data for the event</param>
			public NewEvent(ILogEvent Event, ILogEventData EventData)
			{
				this.Event = Event;
				this.EventData = EventData;
			}

			/// <inheritdoc/>
			public override string ToString()
			{
				return $"[{Event.LineIndex}] {EventData.Message}";
			}
		}

		/// <summary>
		/// Information about a new event to be added to a span
		/// </summary>
		class NewEventGroup
		{
			/// <summary>
			/// Digest of the fingerprint, for log tracking
			/// </summary>
			public ContentHash Digest { get; }

			/// <summary>
			/// Fingerprint for the event
			/// </summary>
			public NewIssueFingerprint Fingerprint { get; }

			/// <summary>
			/// Individual log events
			/// </summary>
			public List<NewEvent> Events { get; } = new List<NewEvent>();

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Fingerprint">Fingerprint for the event</param>
			public NewEventGroup(NewIssueFingerprint Fingerprint)
			{
				this.Digest = ContentHash.MD5(Fingerprint.ToString());
				this.Fingerprint = Fingerprint;
			}

			/// <summary>
			/// Merge with another group
			/// </summary>
			/// <param name="OtherGroup">The group to merge with</param>
			/// <returns>A new group combining both groups</returns>
			public NewEventGroup MergeWith(NewEventGroup OtherGroup)
			{
				NewEventGroup NewGroup = new NewEventGroup(Fingerprint.MergeWith(OtherGroup.Fingerprint));
				NewGroup.Events.AddRange(Events);
				NewGroup.Events.AddRange(OtherGroup.Events);
				return NewGroup;
			}
		}

		/// <summary>
		/// Marks a step as complete
		/// </summary>
		/// <param name="Job">The job to update</param>
		/// <param name="Graph">Graph for the job</param>
		/// <param name="BatchId">Unique id of the batch</param>
		/// <param name="StepId">Unique id of the step</param>
		/// <returns>Async task</returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		public async Task UpdateCompleteStep(IJob Job, IGraph Graph, SubResourceId BatchId, SubResourceId StepId)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("UpdateCompleteStep").StartActive();
			Scope.Span.SetTag("JobId", Job.Id.ToString());
			Scope.Span.SetTag("BatchId", BatchId.ToString());
			Scope.Span.SetTag("StepId", StepId.ToString());

			IStream? Stream = await Streams.GetStreamAsync(Job.StreamId);
			if (Stream == null)
			{
				throw new Exception($"Invalid stream id '{Job.StreamId}' on job '{Job.Id}'");
			}

			// Find the batch for this event
			IJobStepBatch? Batch;
			if (!Job.TryGetBatch(BatchId, out Batch))
			{
				throw new ArgumentException($"Invalid batch id {BatchId}");
			}

			// Find the step for this event
			IJobStep? Step;
			if (!Job.TryGetStep(StepId, out Step))
			{
				throw new ArgumentException($"Invalid step id {StepId}");
			}
			Scope.Span.SetTag("LogId", Step.LogId.ToString());

			// Get the node associated with this step
			INode Node = Graph.Groups[Batch.GroupIdx].Nodes[Step.NodeIdx];

			// Gets the events for this step grouped by fingerprint
			HashSet<NewEventGroup> EventGroups = await GetEventGroupsForStepAsync(Job, Batch, Step, Node);

			// Figure out if we want notifications for this step
			bool bPromoteByDefault = Job.ShowUgsAlerts;
			if (bPromoteByDefault)
			{
				if (!Job.TemplateId.ToString().Contains("incremental", StringComparison.Ordinal))
				{
					if (EventGroups.FirstOrDefault(Group => Group.Fingerprint.Type == "Compile" || Group.Fingerprint.Type == "Symbol" || Group.Fingerprint.Type == "Copyright") == null)
					{
						bPromoteByDefault = false;
					}

					string? StreamId = Job.StreamId.ToString();
					if (StreamId != null && StreamId.StartsWith("fortnite-", StringComparison.Ordinal))
					{
						bPromoteByDefault = false;
					}

				}
			}

			// Try to update all the events. We may need to restart this due to optimistic transactions, so keep track of any existing spans we do not need to check against.
			await using(IAsyncDisposable Lock = await IssueCollection.EnterCriticalSectionAsync())
			{
				HashSet<ObjectId> CheckedSpanIds = new HashSet<ObjectId>();
				for (; ; )
				{
					// Get the spans that are currently open
					List<IIssueSpan> OpenSpans = await IssueCollection.FindOpenSpansAsync(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);
					Logger.LogDebug("{NumSpans} spans are open in {StreamId} at CL {Change} for template {TemplateId}, node {Node}", OpenSpans.Count, Job.StreamId, Job.Change, Job.TemplateId, Node.Name);

					// Add the events to existing issues, and create new issues for everything else
					if (EventGroups.Count > 0)
					{
						if (!await AddEventsToExistingSpans(Job, Batch, Step, EventGroups, OpenSpans, CheckedSpanIds, bPromoteByDefault))
						{
							continue;
						}
						if (!await AddEventsToNewSpans(Stream, Job, Batch, Step, Node, OpenSpans, EventGroups, bPromoteByDefault))
						{
							continue;
						}
					}

					// Try to update the sentinels for any other open steps
					if (!await TryUpdateSentinelsAsync(OpenSpans, Stream, Job, Batch, Step))
					{
						continue;
					}

					break;
				}
			}
		}

		/// <summary>
		/// Gets a set of events for the given step 
		/// </summary>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Batch">Unique id of the batch</param>
		/// <param name="Step">Unique id of the step</param>
		/// <param name="Node">The node corresponding to the step</param>
		/// <returns>Set of new events</returns>
		async Task<HashSet<NewEventGroup>> GetEventGroupsForStepAsync(IJob Job, IJobStepBatch Batch, IJobStep Step, INode Node)
		{
			// Make sure the step has a log file
			if (Step.LogId == null)
			{
				throw new ArgumentException($"Step id {Step.Id} does not have any log set");
			}

			// Find the log file for this step
			ILogFile? LogFile = await LogFileService.GetLogFileAsync(Step.LogId.Value);
			if (LogFile == null)
			{
				throw new ArgumentException($"Unable to retrieve log {Step.LogId}");
			}

			// Process all the events for this step
			List<ILogEvent> StepEvents = await LogFileService.FindLogEventsAsync(LogFile);

			// Create a new list of event fingerprints for each one
			Dictionary<NewIssueFingerprint, NewEventGroup> FingerprintToEventGroup = new Dictionary<NewIssueFingerprint, NewEventGroup>();
			foreach (ILogEvent StepEvent in StepEvents)
			{
				// Read the full message for this log event
				ILogEventData StepEventData = await LogFileService.GetEventDataAsync(LogFile, StepEvent.LineIndex, StepEvent.LineCount);

				// Parse a fingerprint from the event
				NewIssueFingerprint? Fingerprint = null;
				foreach (IIssueHandler Matcher in Matchers)
				{
					if (Matcher.TryGetFingerprint(Job, Node, StepEventData, out Fingerprint))
					{
						break;
					}
				}

				// If we matched a fingerprint, add it to the existing set
				if (Fingerprint != null)
				{
					NewEventGroup? EventGroup;
					if (!FingerprintToEventGroup.TryGetValue(Fingerprint, out EventGroup))
					{
						EventGroup = new NewEventGroup(Fingerprint);
						FingerprintToEventGroup.Add(Fingerprint, EventGroup);
					}
					EventGroup.Events.Add(new NewEvent(StepEvent, StepEventData));
				}
			}

			// Print the list of new events
			HashSet<NewEventGroup> EventGroups = new HashSet<NewEventGroup>(FingerprintToEventGroup.Values);

			Logger.LogDebug("UpdateCompleteStep({JobId}, {BatchId}, {StepId}): {NumEvents} events, {NumFingerprints} unique fingerprints", Job.Id, Batch.Id, Step.Id, StepEvents.Count, EventGroups.Count);
			foreach (NewEventGroup EventGroup in EventGroups)
			{
				Logger.LogDebug("Group {Digest}: Type '{FingerprintType}', keys '{FingerprintKeys}'", EventGroup.Digest.ToString(), EventGroup.Fingerprint.Type, String.Join(", ", EventGroup.Fingerprint.Keys));
				foreach (NewEvent Event in EventGroup.Events)
				{
					Logger.LogDebug("Group {Digest}: [{Line}] {Message}", EventGroup.Digest.ToString(), Event.Event.LineIndex, Event.EventData.Message);
				}
			}

			return EventGroups;
		}

		/// <summary>
		/// Adds events to open spans
		/// </summary>
		/// <param name="Job">The job containing the step</param>
		/// <param name="Batch">Unique id of the batch</param>
		/// <param name="Step">Unique id of the step</param>
		/// <param name="NewEventGroups">Set of events to try to add</param>
		/// <param name="OpenSpans">List of open spans</param>
		/// <param name="CheckedSpanIds">Set of span ids that have been checked</param>
		/// <param name="PromoteByDefault"></param>
		/// <returns>True if the adding completed</returns>
		async Task<bool> AddEventsToExistingSpans(IJob Job, IJobStepBatch Batch, IJobStep Step, HashSet<NewEventGroup> NewEventGroups, List<IIssueSpan> OpenSpans, HashSet<ObjectId> CheckedSpanIds, bool PromoteByDefault)
		{
			for(int SpanIdx = 0; SpanIdx < OpenSpans.Count; SpanIdx++)
			{
				IIssueSpan OpenSpan = OpenSpans[SpanIdx];
				if (!CheckedSpanIds.Contains(OpenSpan.Id))
				{
					// Filter out the events which match the span's fingerprint
					List<NewEventGroup> MatchEventGroups = NewEventGroups.Where(x => OpenSpan.Fingerprint.IsMatch(x.Fingerprint)).ToList();
					if (MatchEventGroups.Count > 0)
					{
						// Add the new step data
						NewIssueStepData NewFailure = new NewIssueStepData(Job, Batch, Step, GetIssueSeverity(MatchEventGroups.SelectMany(x => x.Events)), PromoteByDefault);
						await IssueCollection.AddStepAsync(OpenSpan.Id, NewFailure);

						// Update the span if this changes the current range
						IIssueSpan? NewSpan = OpenSpan;
						if (NewFailure.Change <= OpenSpan.FirstFailure.Change || NewFailure.Change >= OpenSpan.LastFailure.Change)
						{
							NewSpan = await IssueCollection.TryUpdateSpanAsync(OpenSpan, NewFailure: NewFailure);
							if (NewSpan == null)
							{
								return false;
							}
							await UpdateIssueDerivedDataAsync(NewSpan.IssueId);
						}

						// Write out all the merged events
						foreach (NewEventGroup EventGroup in MatchEventGroups)
						{
							Logger.LogDebug("Matched fingerprint {Digest} ({NumLogEvents} log events) to span {SpanId}", EventGroup.Digest, EventGroup.Events.Count, NewSpan.Id);
						}

						// Assign all the events to the span
						await LogFileService.AddSpanToEventsAsync(MatchEventGroups.SelectMany(x => x.Events.Select(x => x.Event)), NewSpan.Id);

						// Remove the matches from the set of events
						NewEventGroups.ExceptWith(MatchEventGroups);
					}

					// Add the span id to the list of checked spans, so we don't have to check it again
					CheckedSpanIds.Add(OpenSpan.Id);
				}
			}
			return true;
		}

		/// <summary>
		/// Gets an issue severity from its events
		/// </summary>
		/// <param name="Events">Set of event severity values</param>
		/// <returns>Severity of this issue</returns>
		static IssueSeverity GetIssueSeverity(IEnumerable<NewEvent> Events)
		{
			IssueSeverity Severity;
			if (Events.Any(x => x.Event.Severity == EventSeverity.Error))
			{
				Severity = IssueSeverity.Error;
			}
			else if (Events.Any(x => x.Event.Severity == EventSeverity.Warning))
			{
				Severity = IssueSeverity.Warning;
			}
			else
			{
				Severity = IssueSeverity.Unspecified;
			}
			return Severity;
		}

		/// <summary>
		/// Adds new spans for the given events
		/// </summary>
		/// <param name="Stream">The stream containing the job</param>
		/// <param name="Job">The job instance</param>
		/// <param name="Batch">The job batch</param>
		/// <param name="Step">The job step</param>
		/// <param name="Node">Node run in the step</param>
		/// <param name="OpenSpans">List of open spans. New issues will be added to this list.</param>
		/// <param name="NewEventGroups">Set of remaining events</param>
		/// <param name="PromoteByDefault"></param>
		/// <returns>True if all events were added</returns>
		async Task<bool> AddEventsToNewSpans(IStream Stream, IJob Job, IJobStepBatch Batch, IJobStep Step, INode Node, List<IIssueSpan> OpenSpans, HashSet<NewEventGroup> NewEventGroups, bool PromoteByDefault)
		{
			while (NewEventGroups.Count > 0)
			{
				// Keep track of the event groups we merge together
				List<NewEventGroup> SourceEventGroups = new List<NewEventGroup>();
				SourceEventGroups.Add(NewEventGroups.First());

				// Take the first event, and find all other events that match against it
				NewEventGroup EventGroup = SourceEventGroups[0];
				foreach (NewEventGroup OtherEventGroup in NewEventGroups.Skip(1))
				{
					if (OtherEventGroup.Fingerprint.IsMatchForNewSpan(EventGroup.Fingerprint))
					{
						NewEventGroup NewEventGroup = EventGroup.MergeWith(OtherEventGroup);
						Logger.LogDebug("Merging group {Group} with group {OtherGroup} to form {NewGroup}", EventGroup.Digest.ToString(), OtherEventGroup.Digest.ToString(), NewEventGroup.Digest.ToString());
						SourceEventGroups.Add(OtherEventGroup);
						EventGroup = NewEventGroup;
					}
				}

				// Get the step data
				NewIssueStepData StepData = new NewIssueStepData(Job, Batch, Step, GetIssueSeverity(EventGroup.Events), PromoteByDefault);

				// Get the span data
				NewIssueSpanData SpanData = new NewIssueSpanData(Stream.Id, Stream.Name, Job.TemplateId, Node.Name, EventGroup.Fingerprint, StepData);

				IJobStepRef? PrevJob = await JobStepRefs.GetPrevStepForNodeAsync(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);
				if (PrevJob != null)
				{
					SpanData.LastSuccess = new NewIssueStepData(PrevJob);
					SpanData.Suspects = await FindSuspectsForSpanAsync(Stream, SpanData.Fingerprint, SpanData.LastSuccess.Change + 1, SpanData.FirstFailure.Change);
				}

				IJobStepRef? NextJob = await JobStepRefs.GetNextStepForNodeAsync(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);
				if (NextJob != null)
				{
					SpanData.NextSuccess = new NewIssueStepData(NextJob);
				}

				// Add all the new objects
				IIssue NewIssue = await FindOrAddIssueForSpanAsync(SpanData);
				IIssueSpan NewSpan = await IssueCollection.AddSpanAsync(NewIssue.Id, SpanData);
				IIssueStep NewStep = await IssueCollection.AddStepAsync(NewSpan.Id, StepData);
				await UpdateIssueDerivedDataAsync(NewIssue);

				// Update the log events
				Logger.LogDebug("Created new span {SpanId} from event group {Group}", NewSpan.Id, EventGroup.Digest.ToString());
				await LogFileService.AddSpanToEventsAsync(EventGroup.Events.Select(x => x.Event), NewSpan.Id);

				// Remove the events from the remaining list of events to match
				NewEventGroups.ExceptWith(SourceEventGroups);
				OpenSpans.Add(NewSpan);
			}
			return true;
		}

		async Task<IIssue?> UpdateIssueDerivedDataAsync(int IssueId)
		{
			IIssue? Issue = await IssueCollection.GetIssueAsync(IssueId);
			if(Issue != null)
			{
				Issue = await UpdateIssueDerivedDataAsync(Issue);
			}
			return Issue;
		}

		async Task<IIssue?> UpdateIssueDerivedDataAsync(IIssue Issue)
		{
			Dictionary<(StreamId, int), bool> FixChangeCache = new Dictionary<(StreamId, int), bool>();
			for (; ; )
			{
				// Find all the spans that are attached to the issue
				List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(Issue.Id);

				// Update the suspects for this issue
				List<NewIssueSuspectData> NewSuspects = new List<NewIssueSuspectData>();
				if (Spans.Count > 0)
				{
					HashSet<int> SuspectChanges = new HashSet<int>(Spans[0].Suspects.Where(x => Issue.OwnerId == null || x.AuthorId == Issue.OwnerId).Select(x => x.OriginatingChange ?? x.Change));
					for (int SpanIdx = 1; SpanIdx < Spans.Count; SpanIdx++)
					{
						SuspectChanges.IntersectWith(Spans[SpanIdx].Suspects.Select(x => x.OriginatingChange ?? x.Change));
					}
					NewSuspects = Spans[0].Suspects.Where(x => SuspectChanges.Contains(x.OriginatingChange ?? x.Change)).Select(x => new NewIssueSuspectData(x.AuthorId, x.OriginatingChange ?? x.Change)).ToList();
				}
				if (Issue.OwnerId != null)
				{
					NewSuspects.RemoveAll(x => x.AuthorId != Issue.OwnerId);
					if (NewSuspects.Count == 0)
					{
						NewSuspects.Add(new NewIssueSuspectData(Issue.OwnerId.Value, 0));
					}
				}

				// Create the combined fingerprint for this issue
				List<NewIssueFingerprint> NewFingerprints = new List<NewIssueFingerprint>();
				foreach (IIssueSpan Span in Spans)
				{
					NewIssueFingerprint? Fingerprint = NewFingerprints.FirstOrDefault(x => x.Type == Span.Fingerprint.Type);
					if (Fingerprint == null)
					{
						NewFingerprints.Add(new NewIssueFingerprint(Span.Fingerprint));
					}
					else
					{
						Fingerprint.MergeWith(Span.Fingerprint);
					}
				}

				// Find the severity for this issue
				IssueSeverity NewSeverity;
				if (Spans.Count == 0 || Spans.All(x => x.NextSuccess != null))
				{
					NewSeverity = Issue.Severity;
				}
				else
				{
					NewSeverity = Spans.Any(x => x.NextSuccess == null && x.LastFailure.Severity == IssueSeverity.Error) ? IssueSeverity.Error : IssueSeverity.Warning;
				}

				// Find the default summary for this issue
				string NewSummary;
				if (NewFingerprints.Count == 0)
				{
					NewSummary = Issue.Summary;
				}
				else if (NewFingerprints.Count == 1)
				{
					NewSummary = GetSummary(NewFingerprints[0], NewSeverity);
				}
				else
				{
					NewSummary = (NewSeverity == IssueSeverity.Error) ? "Errors in multiple steps" : "Warnings in multiple steps";
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
				DateTime? NewVerifiedAt = null;
				if (Spans.Count == 0)
				{
					NewVerifiedAt = Issue.VerifiedAt ?? DateTime.UtcNow;
				}
				else
				{
					foreach (IIssueSpan Span in Spans)
					{
						if (Span.NextSuccess == null)
						{
							break;
						}
						else if (NewVerifiedAt == null || Span.NextSuccess.StepTime < NewVerifiedAt.Value)
						{
							NewVerifiedAt = Span.NextSuccess.StepTime;
						}
					}
				}

				// Get the new resolved timestamp
				DateTime? NewResolvedAt = NewVerifiedAt;
				if (Issue.ResolvedById != null && Issue.ResolvedAt != null)
				{
					NewResolvedAt = Issue.ResolvedAt;
				}

				// Get the new list of streams
				List<NewIssueStream> NewStreams = Spans.Select(x => x.StreamId).OrderBy(x => x.ToString(), StringComparer.Ordinal).Distinct().Select(x => new NewIssueStream(x)).ToList();
				if (Issue.FixChange != null)
				{
					foreach (NewIssueStream NewStream in NewStreams)
					{
						IIssueStream? Stream = Issue.Streams.FirstOrDefault(x => x.StreamId == NewStream.StreamId);
						if (Stream != null)
						{
							NewStream.ContainsFix = Stream.ContainsFix;
						}
						if (NewStream.ContainsFix == null)
						{
							NewStream.ContainsFix = await ContainsFixChange(NewStream.StreamId, Issue.FixChange.Value, FixChangeCache);
						}
					}
				}

				// If a fix changelist has been specified and it's not valid, clear it out
				if (NewVerifiedAt == null && NewResolvedAt != null)
				{
					if (Spans.Any(x => HasFixFailed(x, Issue.FixChange, NewResolvedAt.Value, NewStreams)))
					{
						NewStreams.ForEach(x => x.ContainsFix = null);
						NewResolvedAt = null;
					}
				}

				// Update the issue
				IIssue? NewIssue = await IssueCollection.TryUpdateIssueDerivedDataAsync(Issue, NewSummary, NewSeverity, NewFingerprints, NewStreams, NewSuspects, NewResolvedAt, NewVerifiedAt, NewLastSeenAt);
				if (NewIssue != null)
				{
					OnIssueUpdated?.Invoke(NewIssue);
					return NewIssue;
				}

				// Fetch the issue and try again
				NewIssue = await IssueCollection.GetIssueAsync(Issue.Id);
				if (NewIssue == null)
				{
					return null;
				}
				Issue = NewIssue;
			}
		}

		/// <summary>
		/// Determine if an issue should be marked as fix failed
		/// </summary>
		static bool HasFixFailed(IIssueSpan Span, int? FixChange, DateTime ResolvedAt, IReadOnlyList<NewIssueStream> Streams)
		{
			if (Span.NextSuccess == null)
			{
				NewIssueStream? Stream = Streams.FirstOrDefault(x => x.StreamId == Span.StreamId);
				if (Stream != null && FixChange != null && (Stream.ContainsFix ?? false))
				{
					if (Span.LastFailure.Change > FixChange.Value)
					{
						return true;
					}
				}
				else
				{
					if (Span.LastFailure.StepTime > ResolvedAt + TimeSpan.FromHours(24.0))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Figure out if a stream contains a fix changelist
		/// </summary>
		async ValueTask<bool> ContainsFixChange(StreamId StreamId, int FixChange, Dictionary<(StreamId, int), bool> CachedContainsFixChange)
		{
			bool bContainsFixChange;
			if (!CachedContainsFixChange.TryGetValue((StreamId, FixChange), out bContainsFixChange))
			{
				IStream? Stream = await Streams.GetCachedStream(StreamId);
				if (Stream != null)
				{
					Logger.LogInformation("Querying fix changelist {FixChange} in {StreamId}", FixChange, StreamId);
					List<ChangeSummary> Changes = await Perforce.GetChangesAsync(Stream.ClusterName, Stream.Name, FixChange, FixChange, 1, null);
					bContainsFixChange = Changes.Count > 0;
				}
				CachedContainsFixChange[(StreamId, FixChange)] = bContainsFixChange;
			}
			return bContainsFixChange;
		}

		/// <summary>
		/// Find suspects that can match a given span
		/// </summary>
		/// <param name="Stream">The stream name</param>
		/// <param name="Fingerprint">The fingerprint for the span</param>
		/// <param name="MinChange">Minimum changelist to consider</param>
		/// <param name="MaxChange">Maximum changelist to consider</param>
		/// <returns>List of suspects</returns>
		async Task<List<NewIssueSpanSuspectData>> FindSuspectsForSpanAsync(IStream Stream, IIssueFingerprint Fingerprint, int MinChange, int MaxChange)
		{
			List<NewIssueSpanSuspectData> Suspects = new List<NewIssueSpanSuspectData>();
			if (TypeToHandler.TryGetValue(Fingerprint.Type, out IIssueHandler? Handler))
			{
				Logger.LogDebug("Querying for changes in {StreamName} between {MinChange} and {MaxChange}", Stream.Name, MinChange, MaxChange);

				// Get the submitted changes before this job
				List<ChangeDetails> Changes = await PerforceServiceExtensions.GetChangeDetailsAsync(Perforce, Stream.ClusterName, Stream.Name, MinChange, MaxChange, MaxChanges, null);
				Logger.LogDebug("Found {NumResults} changes", Changes.Count);

				// Get the handler to rank them
				List<SuspectChange> SuspectChanges = Changes.ConvertAll(x => new SuspectChange(x));
				Handler.RankSuspects(Fingerprint, SuspectChanges);

				// Output the rankings
				if (SuspectChanges.Count > 0)
				{
					int MaxRank = SuspectChanges.Max(x => x.Rank);
					foreach (SuspectChange SuspectChange in SuspectChanges)
					{
						Logger.LogDebug("Suspect CL: {Change}, Author: {Author}, Rank: {Rank}, MaxRank: {MaxRank}", SuspectChange.Details.Number, SuspectChange.Details.Author, SuspectChange.Rank, MaxRank);
						if (SuspectChange.Rank == MaxRank)
						{
							IUser? Owner = SuspectChange.Details.Author;

							string? RoboOwnerName = ParseRobomergeOwner(SuspectChange.Details.Description);
							if (RoboOwnerName != null)
							{
								Owner = await Perforce.FindOrAddUserAsync(Stream.ClusterName, RoboOwnerName);
							}

							NewIssueSpanSuspectData Suspect = new NewIssueSpanSuspectData(SuspectChange.Details.Number, Owner.Id);
							Suspect.OriginatingChange = ParseRobomergeSource(SuspectChange.Details.Description);

							Suspects.Add(Suspect);
						}
					}
				}
			}
			return Suspects;
		}

		/// <summary>
		/// Find an existing issue that may match one of the given suspect changes
		/// </summary>
		/// <param name="Span">The span to find an issue for</param>
		/// <returns>The matching issue</returns>
		async Task<IIssue> FindOrAddIssueForSpanAsync(IIssueSpan Span)
		{
			List<IIssue> ExistingIssues;
			if (Span.LastSuccess == null)
			{
				ExistingIssues = await IssueCollection.FindIssuesAsync(StreamId: Span.StreamId, MinChange: Span.FirstFailure.Change, MaxChange: Span.FirstFailure.Change, Resolved: false);
			}
			else
			{
				ExistingIssues = await IssueCollection.FindIssuesForChangesAsync(Span.Suspects.ConvertAll(x => x.OriginatingChange ?? x.Change));
			}

			IIssue? Issue = ExistingIssues.FirstOrDefault(x => x.Fingerprints.Any(y => y.IsMatch(Span.Fingerprint)));
			if (Issue == null)
			{
				string Summary = GetSummary(Span.Fingerprint, Span.FirstFailure.Severity);
				Issue = await IssueCollection.AddIssueAsync(Summary);
			}
			return Issue;
		}

		/// <summary>
		/// Find an existing issue that may match one of the given suspect changes
		/// </summary>
		/// <param name="Span">The span to find an issue for</param>
		/// <returns>The matching issue</returns>
		async Task<IIssue> FindOrAddIssueForSpanAsync(NewIssueSpanData Span)
		{
			List<IIssue> ExistingIssues;
			if (Span.LastSuccess == null)
			{
				ExistingIssues = await IssueCollection.FindIssuesAsync(StreamId: Span.StreamId, MinChange: Span.FirstFailure.Change, MaxChange: Span.FirstFailure.Change, Resolved: false);
			}
			else
			{
				ExistingIssues = await IssueCollection.FindIssuesForChangesAsync(Span.Suspects.ConvertAll(x => x.OriginatingChange ?? x.Change));
			}

			IIssue? Issue = ExistingIssues.FirstOrDefault(x => x.Fingerprints.Any(y => y.IsMatch(Span.Fingerprint)));
			if (Issue == null)
			{
				string Summary = GetSummary(Span.Fingerprint, Span.FirstFailure.Severity);
				Issue = await IssueCollection.AddIssueAsync(Summary);
			}
			return Issue;
		}

		/// <summary>
		/// Gets the summary text for a particular fingerprint
		/// </summary>
		/// <param name="Fingerprint">The issue fingerprint</param>
		/// <param name="Severity">Severity of the issue</param>
		/// <returns>The summary text</returns>
		string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			if (TypeToHandler.TryGetValue(Fingerprint.Type, out IIssueHandler? Matcher))
			{
				return Matcher.GetSummary(Fingerprint, Severity);
			}
			else
			{
				return $"Unknown issue type '{Fingerprint.Type}";
			}
		}

		/// <summary>
		/// Update the sentinels for the given list of issues
		/// </summary>
		/// <param name="Spans"></param>
		/// <param name="Stream">The stream instance</param>
		/// <param name="Job"></param>
		/// <param name="Batch"></param>
		/// <param name="Step"></param>
		/// <returns></returns>
		async Task<bool> TryUpdateSentinelsAsync(List<IIssueSpan> Spans, IStream Stream, IJob Job, IJobStepBatch Batch, IJobStep Step)
		{
			foreach(IIssueSpan Span in Spans)
			{
				if (Job.Change < Span.FirstFailure.Change && (Span.LastSuccess == null || Job.Change > Span.LastSuccess.Change))
				{
					NewIssueStepData NewLastSuccess = new NewIssueStepData(Job.Change, IssueSeverity.Unspecified, Job.Name, Job.Id, Batch.Id, Step.Id, Step.StartTimeUtc ?? default, Step.LogId, false);
					List<NewIssueSpanSuspectData> NewSuspects = await FindSuspectsForSpanAsync(Stream, Span.Fingerprint, Job.Change + 1, Span.FirstFailure.Change);

					if (await IssueCollection.TryUpdateSpanAsync(Span, NewLastSuccess: NewLastSuccess, NewSuspects: NewSuspects) == null)
					{
						return false;
					}

					Logger.LogInformation("Set last success for issue {IssueId}, template {TemplateId}, node {Node} as job {JobId}, cl {Change}", Span.IssueId, Job.TemplateId, Span.NodeName, Job.Id, Job.Change);
					await UpdateIssueDerivedDataAsync(Span.IssueId);
				}
				else if (Job.Change > Span.LastFailure.Change && (Span.NextSuccess == null || Job.Change < Span.NextSuccess.Change))
				{
					NewIssueStepData NewNextSucccess = new NewIssueStepData(Job.Change, IssueSeverity.Unspecified, Job.Name, Job.Id, Batch.Id, Step.Id, Step.StartTimeUtc ?? default, Step.LogId, false);

					if (await IssueCollection.TryUpdateSpanAsync(Span, NewNextSuccess: NewNextSucccess) == null)
					{
						return false;
					}

					Logger.LogInformation("Set next success for issue {IssueId}, template {TemplateId}, node {Node} as job {JobId}, cl {Change}", Span.IssueId, Job.TemplateId, Span.NodeName, Job.Id, Job.Change);
					await UpdateIssueDerivedDataAsync(Span.IssueId);
				}
			}
			return true;
		}

		/// <summary>
		/// Attempts to parse the Robomerge source from this commit information
		/// </summary>
		/// <param name="Description">Description text to parse</param>
		/// <returns>The parsed source changelist, or null if no #ROBOMERGE-SOURCE tag was present</returns>
		static int? ParseRobomergeSource(string Description)
		{
			// #ROBOMERGE-SOURCE: CL 13232051 in //Fortnite/Release-12.60/... via CL 13232062 via CL 13242953
			Match Match = Regex.Match(Description, @"^#ROBOMERGE-SOURCE: CL (\d+)", RegexOptions.Multiline);
			if (Match.Success)
			{
				return int.Parse(Match.Groups[1].Value, CultureInfo.InvariantCulture);
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Attempts to parse the Robomerge owner from this commit information
		/// </summary>
		/// <param name="Description">Description text to parse</param>
		/// <returns>The Robomerge owner, or null if no #ROBOMERGE-OWNER tag was present</returns>
		static string? ParseRobomergeOwner(string Description)
		{
			// #ROBOMERGE-OWNER: ben.marsh
			Match Match = Regex.Match(Description, @"^#ROBOMERGE-OWNER:\s*([^\s]+)", RegexOptions.Multiline);
			if (Match.Success)
			{
				return Match.Groups[1].Value;
			}
			else
			{
				return null;
			}
		}

		/// <inheritdoc/>
		public async Task<List<ILogEvent>> FindEventsForIssueAsync(int IssueId, LogId[]? LogIds, int Index, int Count)
		{
			List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(IssueId);
			return await LogFileService.FindEventsForSpansAsync(Spans.Select(x => x.Id), LogIds, Index, Count);
		}
	}
}
