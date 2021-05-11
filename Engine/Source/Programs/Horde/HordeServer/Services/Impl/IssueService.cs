// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
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
using Datadog.Trace;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;
using HordeServer.IssueHandlers.Impl;
using HordeServer.Notifications;

namespace HordeServer.Services.Impl
{
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
		HashSet<ObjectId> NotifyUsers;

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
				NotifyUsers = new HashSet<ObjectId>(Suspects.Select(x => x.AuthorId));
			}
			else
			{
				NotifyUsers = new HashSet<ObjectId> { Issue.OwnerId.Value };
			}
		}

		/// <summary>
		/// Determines whether the given user should be notified about the given issue
		/// </summary>
		/// <returns>True if the user should be notified for this change</returns>
		public bool ShowNotifications()
		{
			return ShowDesktopAlerts && Issue.Fingerprint.Type != DefaultIssueHandler.Type;
		}

		/// <summary>
		/// Determines if the issue is relevant to the given user
		/// </summary>
		/// <param name="UserId">The user to query</param>
		/// <returns>True if the issue is relevant to the given user</returns>
		public bool IncludeForUser(ObjectId UserId)
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

		/// <summary>
		/// Collection of job documents
		/// </summary>
		IJobCollection Jobs;

		/// <summary>
		/// Collection of job step ref documents
		/// </summary>
		IJobStepRefCollection JobStepRefs;

		/// <summary>
		/// Collection of issue documents
		/// </summary>
		IIssueCollection IssueCollection;

		/// <summary>
		/// Collection of stream documents
		/// </summary>
		IStreamCollection Streams;

		/// <summary>
		/// Collection of user documents
		/// </summary>
		IUserCollection UserCollection;

		/// <summary>
		/// Collection of commit documents
		/// </summary>
		IPerforceService Perforce;

		/// <summary>
		/// The log file service instance
		/// </summary>
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
		/// <param name="DatabaseService"></param>
		/// <param name="IssueCollection">Collection of issue documents</param>
		/// <param name="Jobs">Collection of job documents</param>
		/// <param name="JobStepRefs">Collection of jobstepref documents</param>
		/// <param name="Streams">Collection of stream documents</param>
		/// <param name="UserCollection"></param>
		/// <param name="Perforce">The perforce service instance</param>
		/// <param name="LogFileService">Collection of event documents</param>
		/// <param name="Logger">The logger instance</param>
		public IssueService(DatabaseService DatabaseService, IIssueCollection IssueCollection, IJobCollection Jobs, IJobStepRefCollection JobStepRefs, IStreamCollection Streams, IUserCollection UserCollection, IPerforceService Perforce, ILogFileService LogFileService, ILogger<IssueService> Logger)
			: base(DatabaseService, ObjectId.Parse("609542152fb0794700a6c3df"), Logger)
		{
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

			List<IStream> CachedStreams = await Streams.FindAllAsync();
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
					await IssueCollection.UpdateIssueAsync(OpenIssue, NewResolvedById: IIssue.ResolvedByTimeoutId);
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
			List<IIssueSuspect> Suspects = await IssueCollection.GetSuspectsAsync(Issue);

			List<IUser> SuspectUsers = new List<IUser>();
			foreach (ObjectId SuspectUserId in Suspects.Select(x => x.AuthorId).Distinct())
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
			return IssueCollection.GetSuspectsAsync(Issue);
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, ObjectId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, int? Index = null, int? Count = null)
		{
			return await IssueCollection.FindIssuesAsync(Ids, UserId, StreamId, MinChange, MaxChange, Resolved, Index, Count);
		}

		/// <inheritdoc/>
		public async Task<List<IIssue>> FindIssuesForJobAsync(int[]? Ids, IJob Job, IGraph Graph, SubResourceId? StepId = null, SubResourceId? BatchId = null, int? LabelIdx = null, ObjectId? UserId = null, bool? Resolved = null, int? Index = null, int? Count = null)
		{
			List<IIssueStep> Steps = await IssueCollection.FindStepsAsync(Job.Id, BatchId, StepId);
			List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(Steps.Select(x => x.SpanId));

			if (LabelIdx != null)
			{
				HashSet<string> NodeNames = new HashSet<string>(Job.GetNodesForLabel(Graph, LabelIdx.Value).Select(x => Graph.GetNode(x).Name));
				Spans.RemoveAll(x => !NodeNames.Contains(x.NodeName));
			}

			List<int> IssueIds = new List<int>();
			foreach (IIssueSpan Span in Spans)
			{
				if (Span.IssueId != null)
				{
					IssueIds.Add(Span.IssueId.Value);
				}
			}

			if(IssueIds.Count == 0)
			{
				return new List<IIssue>();
			}

			return await FindIssuesAsync(Ids: IssueIds, UserId: UserId, Resolved: Resolved, Index: Index, Count: Count);
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
		public async Task<bool> UpdateIssueAsync(int Id, string? UserSummary = null, ObjectId? OwnerId = null, ObjectId? NominatedById = null, bool? Acknowledged = null, ObjectId? DeclinedById = null, int? FixChange = null, ObjectId? ResolvedById = null)
		{
			Dictionary<StreamId, bool>? NewFixStreamIds = null;
			for (; ; )
			{
				IIssue? Issue = await IssueCollection.GetIssueAsync(Id);
				if(Issue == null)
				{
					return false;
				}

				if (FixChange != null && FixChange.Value > 0)
				{
					NewFixStreamIds = await GetFixStreamsAsync(Issue.Id, FixChange.Value, NewFixStreamIds ?? ((FixChange == Issue.FixChange)? Issue.GetFixStreamIds() : null));
				}

				Issue = await IssueCollection.UpdateIssueAsync(Issue, NewUserSummary: UserSummary, NewOwnerId: OwnerId ?? ResolvedById, NewNominatedById: NominatedById, NewAcknowledged: Acknowledged, NewDeclinedById: DeclinedById, NewFixChange: FixChange, NewFixStreamIds: NewFixStreamIds, NewResolvedById: ResolvedById);

				if(Issue != null)
				{
					OnIssueUpdated?.Invoke(Issue);
					return true;
				}
			}
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
			using Scope Scope = Tracer.Instance.StartActive("UpdateCompleteStep");
			Scope.Span.SetTag("JobId", Job.Id.ToString());
			Scope.Span.SetTag("BatchId", BatchId.ToString());
			Scope.Span.SetTag("StepId", StepId.ToString());

			IStream? Stream = await Streams.GetAsync(Job.StreamId);
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

			// Figure out if we want notifications for this step
			bool bNotifySuspects = Job.ShowUgsAlerts;
			if(bNotifySuspects)
			{
				string? StreamId = Job.StreamId.ToString();
				if (StreamId != null && StreamId.StartsWith("fortnite-", StringComparison.Ordinal))
				{
					bNotifySuspects = false;
				}
			}

			// Gets the events for this step grouped by fingerprint
			HashSet<NewEventGroup> EventGroups = await GetEventGroupsForStepAsync(Job, Batch, Step, Node);

			// Try to update all the events. We may need to restart this due to optimistic transactions, so keep track of any existing spans we do not need to check against.
			HashSet<ObjectId> CheckedSpans = new HashSet<ObjectId>();
			for(; ;)
			{
				// Get a token for transactional operations on the issue collections
				IIssueSequenceToken Token = await IssueCollection.GetSequenceTokenAsync();

				// Get the spans that are currently open
				List<IIssueSpan> OpenSpans = await IssueCollection.FindOpenSpansAsync(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);
				Logger.LogDebug("{NumSpans} spans are open in {StreamId} at CL {Change} for template {TemplateId}, node {Node}", OpenSpans.Count, Job.StreamId, Job.Change, Job.TemplateId, Node.Name);

				// Add the events to existing issues, and create new issues for everything else
				if (EventGroups.Count > 0)
				{
					if (!await AddEventsToExistingSpans(Job, Batch, Step, EventGroups, OpenSpans, CheckedSpans, bNotifySuspects))
					{
						continue;
					}
					if (!await AddEventsToNewSpans(Token, Stream, Job, Batch, Step, Node, OpenSpans, EventGroups, bNotifySuspects))
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

			// Update all the unassigned spans
			await AttachSpansToIssues();
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
		/// <param name="NotifySuspects"></param>
		/// <returns>True if the adding completed</returns>
		async Task<bool> AddEventsToExistingSpans(IJob Job, IJobStepBatch Batch, IJobStep Step, HashSet<NewEventGroup> NewEventGroups, List<IIssueSpan> OpenSpans, HashSet<ObjectId> CheckedSpanIds, bool NotifySuspects)
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
						// Get the severity of these events
						IssueSeverity Severity = MatchEventGroups.SelectMany(x => x.Events).Any(x => x.Event.Severity == EventSeverity.Error) ? IssueSeverity.Error : IssueSeverity.Warning;

						// Get the new severity
						IssueSeverity? NewSeverity = null;
						if (OpenSpan.Severity == IssueSeverity.Warning && Severity == IssueSeverity.Error)
						{
							NewSeverity = IssueSeverity.Error;
						}

						// Get the new step data
						NewIssueStepData NewFailure = new NewIssueStepData(Job, Batch, Step, Severity, NotifySuspects);

						// Figure out whether we need to update the issue
						bool bMarkAsModified = NewSeverity != null;
						if (OpenSpan.IssueId != null)
						{
							IIssue? Issue = await IssueCollection.GetIssueAsync(OpenSpan.IssueId.Value);
							if (Issue != null)
							{
								bMarkAsModified |= Issue.ResolvedAt != null;
							}
						}

						// Update the span
						IIssueSpan? NewSpan = await IssueCollection.UpdateSpanAsync(OpenSpan, NewSeverity: NewSeverity, NewFailure: NewFailure, NewModified: bMarkAsModified);
						if (NewSpan == null)
						{
							return false;
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
		/// <param name="Token">Token for the update</param>
		/// <param name="Stream">The stream containing the job</param>
		/// <param name="Job">The job instance</param>
		/// <param name="Batch">The job batch</param>
		/// <param name="Step">The job step</param>
		/// <param name="Node">Node run in the step</param>
		/// <param name="OpenSpans">List of open spans. New issues will be added to this list.</param>
		/// <param name="NewEventGroups">Set of remaining events</param>
		/// <param name="NotifySuspects"></param>
		/// <returns>True if all events were added</returns>
		async Task<bool> AddEventsToNewSpans(IIssueSequenceToken Token, IStream Stream, IJob Job, IJobStepBatch Batch, IJobStep Step, INode Node, List<IIssueSpan> OpenSpans, HashSet<NewEventGroup> NewEventGroups, bool NotifySuspects)
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
				NewIssueStepData StepData = new NewIssueStepData(Job, Batch, Step, GetIssueSeverity(EventGroup.Events), NotifySuspects);

				// Create the new span
				IIssueSpan? NewSpan = await AddEventToNewSpanAsync(Token, Stream, Job, Node, EventGroup.Fingerprint, StepData);
				if(NewSpan == null)
				{
					return false;
				}
				OpenSpans.Add(NewSpan);

				// Update the log events
				Logger.LogDebug("Created new span {SpanId} from event group {Group}", NewSpan.Id, EventGroup.Digest.ToString());
				await LogFileService.AddSpanToEventsAsync(EventGroup.Events.Select(x => x.Event), NewSpan.Id);

				// Remove the events from the remaining list of events to match
				NewEventGroups.ExceptWith(SourceEventGroups);
			}
			return true;
		}

		/// <summary>
		/// Adds an event to a new span
		/// </summary>
		/// <param name="Token">The sequence token to insert a new span</param>
		/// <param name="Stream">Stream containing the job</param>
		/// <param name="Job">Job that spawned the event</param>
		/// <param name="Node">Node that was running</param>
		/// <param name="Fingerprint">Fingerprint for the event</param>
		/// <param name="NewFailure">Information about the step that failed</param>
		/// <returns>The new span, or null if the insert failed</returns>
		async Task<IIssueSpan?> AddEventToNewSpanAsync(IIssueSequenceToken Token, IStream Stream, IJob Job, INode Node, NewIssueFingerprint Fingerprint, NewIssueStepData NewFailure)
		{
			// Get the previous job in this span
			IJobStepRef? PrevJob = await JobStepRefs.GetPrevStepForNodeAsync(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);

			NewIssueStepData? LastSuccess = null;
			if (PrevJob != null)
			{
				LastSuccess = new NewIssueStepData(PrevJob);
			}

			// Get the next successful job in this stream
			IJobStepRef? NextJob = await JobStepRefs.GetNextStepForNodeAsync(Job.StreamId, Job.TemplateId, Node.Name, Job.Change);

			NewIssueStepData? NextSuccess = null;
			if (NextJob != null)
			{
				NextSuccess = new NewIssueStepData(NextJob);
			}

			// Find all the suspects for this span
			List<NewIssueSpanSuspectData> Suspects = new List<NewIssueSpanSuspectData>();
			if (LastSuccess != null)
			{
				Suspects = await FindSuspectsForSpanAsync(Stream, Fingerprint, LastSuccess.Change + 1, NewFailure.Change);
			}

			// Add the span
			return await IssueCollection.AddSpanAsync(Token, Stream, Job.TemplateId, Node.Name, Fingerprint, LastSuccess, NewFailure, NextSuccess, Suspects);
		}

		/// <summary>
		/// Find all the spans which are not currently attached to an issue, and try to attach them
		/// </summary>
		/// <returns></returns>
		async Task AttachSpansToIssues()
		{
			IIssueSequenceToken Token = await IssueCollection.GetSequenceTokenAsync();
			for (; ; )
			{
				// Find an unassigned span
				IIssueSpan? Span = await IssueCollection.FindModifiedSpanAsync();
				if (Span == null)
				{
					break;
				}

				// Try to find an issue for it
				IIssue? Issue;
				if (Span.IssueId == null)
				{
					Issue = await FindIssueForSpanAsync(Span);
					if (Issue == null)
					{
						Issue = await IssueCollection.AddIssueAsync(Token, GetSummary(Span.Fingerprint, Span.Severity), Span);
						if (Issue == null)
						{
							await Token.ResetAsync();
							continue;
						}
					}
					else
					{
						NewIssueFingerprint NewFingerprint = NewIssueFingerprint.Merge(Issue.Fingerprint, Span.Fingerprint);
						IssueSeverity NewSeverity = (Issue.Severity > Span.Severity) ? Issue.Severity : Span.Severity;
						string NewSummary = GetSummary(NewFingerprint, NewSeverity);

						if (!await IssueCollection.AttachSpanToIssueAsync(Token, Span, Issue, NewSeverity, NewFingerprint, NewSummary))
						{
							await Token.ResetAsync();
							continue;
						}
					}
				}
				else
				{
					// Update the issue that it belongs to
					Issue = await GetIssueAsync(Span.IssueId.Value);
					if (Issue != null)
					{
						Issue = await UpdateIssueDerivedDataAsync(Issue);
						if (Issue == null)
						{
							continue;
						}
					}
				}

				// Perform any shared issue updates
				if (Issue != null)
				{
					Issue = await UpdateResolvedStateAsync(Issue, Span);
					if(Issue == null)
					{
						continue;
					}
					OnIssueUpdated?.Invoke(Issue);
				}

				// Clear the modified flag
				await IssueCollection.UpdateSpanAsync(Span, NewModified: false);
			}
		}

		async Task<IIssue?> UpdateResolvedStateAsync(IIssue? Issue, IIssueSpan Span)
		{
			// Update the fix streams
			if (Issue != null && Issue.FixChange != null && Issue.FixChange.Value > 0)
			{
				Dictionary<StreamId, bool>? FixStreamIds = await GetFixStreamsAsync(Issue.Id, Issue.FixChange.Value, Issue.GetFixStreamIds());
				if (FixStreamIds != null)
				{
					Issue = await IssueCollection.UpdateIssueAsync(Issue, NewFixStreamIds: FixStreamIds);
				}
			}

			// Update the resolved flag
			if (Issue != null && Issue.ResolvedAt != null && ShouldMarkFixFailed(Issue, Span))
			{
				Issue = await IssueCollection.UpdateIssueAsync(Issue, NewResolvedById: ObjectId.Empty);
			}

			return Issue;
		}

		/// <summary>
		/// Determine if an issue should be marked as fix failed
		/// </summary>
		/// <param name="Issue"></param>
		/// <param name="Span"></param>
		/// <returns></returns>
		static bool ShouldMarkFixFailed(IIssue Issue, IIssueSpan Span)
		{
			if (Issue.ResolvedAt != null && Span.NextSuccess == null)
			{
				if (Issue.FixChange != null && (Issue.Streams.FirstOrDefault(x => x.StreamId == Span.StreamId)?.ContainsFix ?? false))
				{
					if (Span.LastFailure.Change > Issue.FixChange.Value)
					{
						return true;
					}
				}
				else
				{
					if (Span.LastFailure.StepTime > Issue.ResolvedAt.Value + TimeSpan.FromHours(24.0))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Finds the streams affected by an issue
		/// </summary>
		/// <param name="IssueId"></param>
		/// <param name="FixChange"></param>
		/// <param name="PrevFixStreamIds"></param>
		/// <returns></returns>
		async Task<Dictionary<StreamId, bool>?> GetFixStreamsAsync(int IssueId, int FixChange, IReadOnlyDictionary<StreamId, bool>? PrevFixStreamIds = null)
		{
			List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(IssueId);
			if(Spans.Count == 0)
			{
				return null;
			}

			Dictionary<StreamId, bool> NextFixStreamIds = new Dictionary<StreamId, bool>();
			foreach (IIssueSpan Span in Spans)
			{
				if (!NextFixStreamIds.ContainsKey(Span.StreamId))
				{
					bool ContainsFix;
					if (PrevFixStreamIds == null || !PrevFixStreamIds.TryGetValue(Span.StreamId, out ContainsFix))
					{
						List<ChangeSummary> Changes = await Perforce.GetChangesAsync(Span.StreamName, FixChange, FixChange, 1, null);
						ContainsFix = Changes.Count > 0;
					}
					NextFixStreamIds[Span.StreamId] = ContainsFix;
				}
			}
			return NextFixStreamIds;
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
				List<ChangeDetails> Changes = await PerforceServiceExtensions.GetChangeDetailsAsync(Perforce, Stream.Name, MinChange, MaxChange, MaxChanges, null);
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
							NewIssueSpanSuspectData Suspect = new NewIssueSpanSuspectData(SuspectChange.Details.Number, SuspectChange.Details.Author);
							Suspect.OriginatingChange = ParseRobomergeSource(SuspectChange.Details.Description);

							string? RobomergeOwner = ParseRobomergeOwner(SuspectChange.Details.Description);
							if(!String.IsNullOrEmpty(RobomergeOwner))
							{
								Suspect.Author = RobomergeOwner;
							}

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
		async Task<IIssue?> FindIssueForSpanAsync(IIssueSpan Span)
		{
			List<IIssue> ExistingIssues;
			if (Span.LastSuccess == null)
			{
				ExistingIssues = await IssueCollection.FindIssuesAsync(StreamId: Span.StreamId, MinChange: Span.FirstFailure.Change, MaxChange: Span.FirstFailure.Change, Resolved: false);
			}
			else
			{
				List<int> SourceChanges = Span.Suspects.ConvertAll(x => x.OriginatingChange ?? x.Change);
				ExistingIssues = await IssueCollection.FindIssuesForSuspectsAsync(SourceChanges);
			}
			return ExistingIssues.FirstOrDefault(x => x.Fingerprint.IsMatch(Span.Fingerprint));
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Issue"></param>
		/// <returns></returns>
		async Task<IIssue?> UpdateIssueDerivedDataAsync(IIssue Issue)
		{
			IIssue? NewIssue = await IssueCollection.UpdateIssueDerivedDataAsync(Issue);
			if (NewIssue != null)
			{
				string NewSummary = GetSummary(Issue.Fingerprint, Issue.Severity);
				if (!NewSummary.Equals(Issue.Summary, StringComparison.Ordinal))
				{
					NewIssue = await IssueCollection.UpdateIssueAsync(NewIssue, NewSummary: NewSummary);
				}
			}
			return NewIssue;
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

					if (await IssueCollection.UpdateSpanAsync(Span, NewLastSuccess: NewLastSuccess, NewSuspects: NewSuspects, NewModified: true) == null)
					{
						return false;
					}

					Logger.LogInformation("Set last success for issue {IssueId}, template {TemplateId}, node {Node} as job {JobId}, cl {Change}", Span.IssueId, Job.TemplateId, Span.NodeName, Job.Id, Job.Change);
				}
				else if (Job.Change > Span.LastFailure.Change && (Span.NextSuccess == null || Job.Change < Span.NextSuccess.Change))
				{
					NewIssueStepData NewNextSucccess = new NewIssueStepData(Job.Change, IssueSeverity.Unspecified, Job.Name, Job.Id, Batch.Id, Step.Id, Step.StartTimeUtc ?? default, Step.LogId, false);
					if (await IssueCollection.UpdateSpanAsync(Span, NewNextSuccess: NewNextSucccess, NewModified: true) == null)
					{
						return false;
					}
					Logger.LogInformation("Set next success for issue {IssueId}, template {TemplateId}, node {Node} as job {JobId}, cl {Change}", Span.IssueId, Job.TemplateId, Span.NodeName, Job.Id, Job.Change);
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
			// #ROBOMERGE-SOURCE: CL 13232051 in //Fortnite/Release-12.60/... via CL 13232062 via CL 13242953
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
		public async Task<List<ILogEvent>> FindEventsForIssueAsync(int IssueId, ObjectId[]? LogIds, int Index, int Count)
		{
			List<IIssueSpan> Spans = await IssueCollection.FindSpansAsync(IssueId);
			return await LogFileService.FindEventsForSpansAsync(Spans.Select(x => x.Id), LogIds, Index, Count);
		}
	}
}
