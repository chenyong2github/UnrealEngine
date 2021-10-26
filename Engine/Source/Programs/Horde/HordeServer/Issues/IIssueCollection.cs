// Copyright Epic Games, Inc. All Rights Reserved.

using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MessagePack.Formatters;
using Microsoft.AspNetCore.Connections.Features;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Linq.Expressions;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Transactions;

namespace HordeServer.Collections
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Fingerprint for an issue
	/// </summary>
	public class NewIssueFingerprint : IIssueFingerprint, IEquatable<IIssueFingerprint>
	{
		/// <inheritdoc/>
		public string Type { get; }

		/// <inheritdoc/>
		public CaseInsensitiveStringSet Keys { get; set; }

		/// <inheritdoc/>
		public CaseInsensitiveStringSet? RejectKeys { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">The type of issue</param>
		/// <param name="Keys">Keys which uniquely identify this issue</param>
		/// <param name="RejectKeys">Keys which should not match with this issue</param>
		public NewIssueFingerprint(string Type, IEnumerable<string> Keys, IEnumerable<string>? RejectKeys)
		{
			this.Type = Type;
			this.Keys = new CaseInsensitiveStringSet(Keys);

			if (RejectKeys != null && RejectKeys.Any())
			{
				this.RejectKeys = new CaseInsensitiveStringSet(RejectKeys);
			}
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Other">The fingerprint to copy from</param>
		public NewIssueFingerprint(IIssueFingerprint Other)
			: this(Other.Type, Other.Keys, Other.RejectKeys)
		{
		}

		/// <summary>
		/// Merges another fingerprint into this one
		/// </summary>
		/// <param name="Other">The other fingerprint to merge with</param>
		public NewIssueFingerprint MergeWith(IIssueFingerprint Other)
		{
			NewIssueFingerprint Result = new NewIssueFingerprint(this);

			Result.Keys.UnionWith(Other.Keys);
			if (Other.RejectKeys != null)
			{
				if (Result.RejectKeys == null)
				{
					Result.RejectKeys = new CaseInsensitiveStringSet();
				}
				Result.RejectKeys.UnionWith(Other.RejectKeys);
			}

			return Result;
		}

		/// <inheritdoc/>
		public override bool Equals(object? Other) => Equals(Other as IIssueFingerprint);

		/// <inheritdoc/>
		public bool Equals(IIssueFingerprint? OtherFingerprint)
		{
			if(OtherFingerprint == null || !Type.Equals(OtherFingerprint.Type, StringComparison.Ordinal) || !Keys.SetEquals(OtherFingerprint.Keys))
			{
				return false;
			}

			if (RejectKeys == null)
			{
				if (OtherFingerprint.RejectKeys != null && OtherFingerprint.RejectKeys.Count > 0)
				{
					return false;
				}
			}
			else
			{
				if (OtherFingerprint.RejectKeys == null || !RejectKeys.SetEquals(OtherFingerprint.RejectKeys))
				{
					return false;
				}
			}

			return true;
		}

		/// <summary>
		/// Gets a hashcode for this fingerprint
		/// </summary>
		/// <returns>The hashcode value</returns>
		public override int GetHashCode()
		{
			int Result = StringComparer.Ordinal.GetHashCode(Type);
			foreach (string Key in Keys)
			{
				Result = HashCode.Combine(Result, Key);
			}
			if (RejectKeys != null)
			{
				foreach (string RejectKey in RejectKeys)
				{
					Result = HashCode.Combine(Result, RejectKey);
				}
			}
			return Result;
		}

		/// <summary>
		/// Merges two fingerprints togetherthis issue fingerprint with another
		/// </summary>
		/// <param name="A">The first fingerprint</param>
		/// <param name="B">The second fingerprint to merge with</param>
		/// <returns>Merged fingerprint</returns>
		public static NewIssueFingerprint Merge(IIssueFingerprint A, IIssueFingerprint B)
		{
			NewIssueFingerprint NewFingerprint = new NewIssueFingerprint(A);
			NewFingerprint.Keys.UnionWith(B.Keys);

			if (B.RejectKeys != null)
			{
				if (NewFingerprint.RejectKeys == null)
				{
					NewFingerprint.RejectKeys = new CaseInsensitiveStringSet();
				}
				NewFingerprint.RejectKeys.UnionWith(B.RejectKeys);
			}

			return NewFingerprint;
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return $"{Type}: \"{String.Join("\", \"", Keys)}\"";
		}
	}

	/// <summary>
	/// Information about a new span
	/// </summary>
	public class NewIssueSpanData
	{
		/// <inheritdoc cref="IIssueSpan.StreamId"/>
		public StreamId StreamId { get; set; }

		/// <inheritdoc cref="IIssueSpan.StreamName"/>
		public string StreamName { get; set; }

		/// <inheritdoc cref="IIssueSpan.TemplateRefId"/>
		public TemplateRefId TemplateRefId { get; set; }

		/// <inheritdoc cref="IIssueSpan.NodeName"/>
		public string NodeName { get; set; }

		/// <inheritdoc cref="IIssueSpan.Fingerprint"/>
		public NewIssueFingerprint Fingerprint { get; set; }

		/// <inheritdoc cref="IIssueSpan.LastSuccess"/>
		public NewIssueStepData? LastSuccess { get; set; }

		/// <inheritdoc cref="IIssueSpan.FirstFailure"/>
		public NewIssueStepData FirstFailure { get; set; }

		/// <inheritdoc cref="IIssueSpan.NextSuccess"/>
		public NewIssueStepData? NextSuccess { get; set; }

		/// <inheritdoc cref="IIssueSpan.Suspects"/>
		public List<NewIssueSpanSuspectData> Suspects { get; set; } = new List<NewIssueSpanSuspectData>();

		/// <summary>
		/// Constructor
		/// </summary>
		public NewIssueSpanData(StreamId StreamId, string StreamName, TemplateRefId TemplateRefId, string NodeName, NewIssueFingerprint Fingerprint, NewIssueStepData FirstFailure)
		{
			this.StreamId = StreamId;
			this.StreamName = StreamName;
			this.TemplateRefId = TemplateRefId;
			this.NodeName = NodeName;
			this.Fingerprint = Fingerprint;
			this.FirstFailure = FirstFailure;
		}
	}

	/// <summary>
	/// Identifies a particular changelist and job
	/// </summary>
	public class NewIssueStepData
	{
		/// <inheritdoc cref="IIssueStep.Change"/>
		public int Change { get; set; }

		/// <inheritdoc cref="IIssueStep.Severity"/>
		public IssueSeverity Severity { get; set; }

		/// <inheritdoc cref="IIssueStep.JobName"/>
		public string JobName { get; set; }

		/// <inheritdoc cref="IIssueStep.JobId"/>
		public JobId JobId { get; set; }

		/// <inheritdoc cref="IIssueStep.BatchId"/>
		public SubResourceId BatchId { get; set; }

		/// <inheritdoc cref="IIssueStep.StepId"/>
		public SubResourceId StepId { get; set; }

		/// <inheritdoc cref="IIssueStep.StepTime"/>
		public DateTime StepTime { get; set; }

		/// <inheritdoc cref="IIssueStep.LogId"/>
		public LogId? LogId { get; set; }

		/// <inheritdoc cref="IIssueStep.PromoteByDefault"/>
		public bool PromoteByDefault { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Change">The changelist number for this job</param>
		/// <param name="Severity">Severity of the issue in this step</param>
		/// <param name="JobName">The job name</param>
		/// <param name="JobId">The unique job id</param>
		/// <param name="BatchId">The batch id</param>
		/// <param name="StepId">The step id</param>
		/// <param name="StepTime">Time that the step started</param>
		/// <param name="LogId">Unique id of the log file for this step</param>
		/// <param name="Promoted">Whether this step is promoted</param>
		public NewIssueStepData(int Change, IssueSeverity Severity, string JobName, JobId JobId, SubResourceId BatchId, SubResourceId StepId, DateTime StepTime, LogId? LogId, bool Promoted)
		{
			this.Change = Change;
			this.Severity = Severity;
			this.JobName = JobName;
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.StepId = StepId;
			this.StepTime = StepTime;
			this.LogId = LogId;
			this.PromoteByDefault = Promoted;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Job">The job being built</param>
		/// <param name="Batch">Batch of the job for the step</param>
		/// <param name="Step">The step being built</param>
		/// <param name="Severity">Severity of the issue in this step</param>
		/// <param name="Promoted">Whether this step is promoted</param>
		public NewIssueStepData(IJob Job, IJobStepBatch Batch, IJobStep Step, IssueSeverity Severity, bool Promoted)
			: this(Job.Change, Severity, Job.Name, Job.Id, Batch.Id, Step.Id, Step.StartTimeUtc ?? default, Step.LogId, Promoted)
		{
		}

		/// <summary>
		/// Constructor for sentinel steps
		/// </summary>
		/// <param name="JobStepRef">The jobstep reference</param>
		public NewIssueStepData(IJobStepRef JobStepRef)
			: this(JobStepRef.Change, IssueSeverity.Unspecified, JobStepRef.JobName, JobStepRef.Id.JobId, JobStepRef.Id.BatchId, JobStepRef.Id.StepId, JobStepRef.StartTimeUtc, JobStepRef.LogId, false)
		{
		}
	}

	/// <summary>
	/// Information about a suspect changelist that may have caused an issue
	/// </summary>
	public class NewIssueSuspectData
	{
		/// <inheritdoc cref="IIssueSuspect.AuthorId"/>
		public UserId AuthorId { get; set; }

		/// <inheritdoc cref="IIssueSuspect.Change"/>
		public int Change { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AuthorId">Author of the change</param>
		/// <param name="Change">The changelist number</param>
		public NewIssueSuspectData(UserId AuthorId, int Change)
		{
			this.AuthorId = AuthorId;
			this.Change = Change;
		}
	}

	/// <summary>
	/// Information about a suspect changelist that may have caused an issue
	/// </summary>
	public class NewIssueSpanSuspectData
	{
		/// <inheritdoc cref="IIssueSpanSuspect.Change"/>
		public int Change { get; set; }

		/// <inheritdoc cref="IIssueSpanSuspect.AuthorId"/>
		public UserId AuthorId { get; set; }

		/// <inheritdoc cref="IIssueSpanSuspect.OriginatingChange"/>
		public int? OriginatingChange { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Change">The changelist number</param>
		/// <param name="AuthorId">Author of the change</param>
		public NewIssueSpanSuspectData(int Change, UserId AuthorId)
		{
			this.Change = Change;
			this.AuthorId = AuthorId;
		}
	}

	/// <summary>
	/// Information about a stream containing an issue
	/// </summary>
	public class NewIssueStream : IIssueStream
	{
		/// <inheritdoc cref="IIssueStream.StreamId"/>
		public StreamId StreamId { get; set; }

		/// <inheritdoc cref="IIssueStream.ContainsFix"/>
		public bool? ContainsFix { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StreamId"></param>
		public NewIssueStream(StreamId StreamId)
		{
			this.StreamId = StreamId;
		}
	}

	/// <summary>
	/// Interface for an issue collection
	/// </summary>
	public interface IIssueCollection
	{
		/// <summary>
		/// Attempts to enters a critical section over the issue collection. Used for performing operations that require coordination of several documents (eg. attaching spans to issues).
		/// </summary>
		/// <returns>The disposable critical section</returns>
		Task<IAsyncDisposable> EnterCriticalSectionAsync();

		#region Issues

		/// <summary>
		/// Creates a new issue
		/// </summary>
		/// <param name="Summary">Summary text for the issue</param>
		/// <returns>The new issue instance</returns>
		Task<IIssue> AddIssueAsync(string Summary);

		/// <summary>
		/// Retrieves and issue by id
		/// </summary>
		/// <param name="IssueId">Unique id of the issue</param>
		/// <returns>The issue matching the given id, or null</returns>
		Task<IIssue?> GetIssueAsync(int IssueId);

		/// <summary>
		/// Finds the suspects for an issue
		/// </summary>
		/// <param name="Issue">The issue to retrieve suspects for</param>
		/// <returns>List of suspects</returns>
		Task<List<IIssueSuspect>> FindSuspectsAsync(IIssue Issue);

		/// <summary>
		/// Searches for open issues
		/// </summary>
		/// <param name="Ids">Set of issue ids to find</param>
		/// <param name="UserId">The user to find issues for</param>
		/// <param name="StreamId">The stream affected by the issue</param>
		/// <param name="MinChange">Minimum changelist affected by the issue</param>
		/// <param name="MaxChange">Maximum changelist affected by the issue</param>
		/// <param name="Resolved">Include issues that are now resolved</param>
		/// <param name="Promoted">Include only promoted issues</param>
		/// <param name="Index">Index within the results to return</param>
		/// <param name="Count">Number of results</param>
		/// <returns>List of streams open in the given stream at the given changelist</returns>
		Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, UserId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, bool? Promoted = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Searches for open issues
		/// </summary>
		/// <param name="Changes">List of suspect changes</param>
		/// <returns>List of issues that are affected by the given changes</returns>
		Task<List<IIssue>> FindIssuesForChangesAsync(List<int> Changes);

		/// <summary>
		/// Try to update the state of an issue
		/// </summary>
		/// <param name="Issue">The issue to update</param>
		/// <param name="NewSeverity">New severity for the issue</param>
		/// <param name="NewSummary">New summary for the issue</param>
		/// <param name="NewUserSummary">New user summary for the issue</param>
		/// <param name="NewDescription">New description for the issue</param>
		/// <param name="NewPromoted">New promoted state of the issue</param>
		/// <param name="NewOwnerId">New owner of the issue</param>
		/// <param name="NewNominatedById">Person that nominated the new owner</param>
		/// <param name="NewAcknowledged">Whether the issue has been acknowledged</param>
		/// <param name="NewDeclinedById">Name of a user that has declined the issue</param>
		/// <param name="NewFixChange">Fix changelist for the issue. Pass 0 to clear the fix changelist, -1 for systemic issue.</param>
		/// <param name="NewResolvedById">User that resolved the issue (may be ObjectId.Empty to clear)</param>
		/// <param name="NewExcludeSpanIds">List of span ids to exclude from this issue</param>
		/// <param name="NewLastSeenAt"></param>
		/// <returns>True if the issue was updated</returns>
		Task<IIssue?> TryUpdateIssueAsync(IIssue Issue, IssueSeverity? NewSeverity = null, string? NewSummary = null, string? NewUserSummary = null, string? NewDescription = null, bool? NewPromoted = null, UserId? NewOwnerId = null, UserId? NewNominatedById = null, bool? NewAcknowledged = null, UserId? NewDeclinedById = null, int? NewFixChange = null, UserId? NewResolvedById = null, List<ObjectId>? NewExcludeSpanIds = null, DateTime? NewLastSeenAt = null);

		/// <summary>
		/// Updates derived data for an issue (ie. data computed from the spans attached to it). Also clears the issue's 'modified' state.
		/// </summary>
		/// <param name="Issue">Issue to update</param>
		/// <param name="NewSummary">New summary for the issue</param>
		/// <param name="NewSeverity">New severity for the issue</param>
		/// <param name="NewFingerprints">New fingerprints for the issue</param>
		/// <param name="NewStreams">New streams for the issue</param>
		/// <param name="NewSuspects">New suspects for the issue</param>
		/// <param name="NewResolvedAt">Time for the last resolved change</param>
		/// <param name="NewVerifiedAt">Time that the issue was resolved</param>
		/// <param name="NewLastSeenAt">Last time the issue was seen</param>
		/// <returns>Updated issue, or null if the issue is modified in the interim</returns>
		Task<IIssue?> TryUpdateIssueDerivedDataAsync(IIssue Issue, string NewSummary, IssueSeverity NewSeverity, List<NewIssueFingerprint> NewFingerprints, List<NewIssueStream> NewStreams, List<NewIssueSuspectData> NewSuspects, DateTime? NewResolvedAt, DateTime? NewVerifiedAt, DateTime NewLastSeenAt);

		#endregion

		#region Spans

		/// <summary>
		/// Creates a new span from the given failure
		/// </summary>
		/// <param name="IssueId">The issue that the span belongs to</param>
		/// <param name="NewSpan">Information about the new span</param>
		/// <returns>New span, or null if the sequence token is not valid</returns>
		Task<IIssueSpan> AddSpanAsync(int IssueId, NewIssueSpanData NewSpan);

		/// <summary>
		/// Gets a particular span
		/// </summary>
		/// <param name="SpanId">Unique id of the span</param>
		/// <returns>New span, or null if the sequence token is not valid</returns>
		Task<IIssueSpan?> GetSpanAsync(ObjectId SpanId);

		/// <summary>
		/// Updates the given span. Note that data in the span's issue may be derived from this, and the issue should be updated afterwards.
		/// </summary>
		/// <param name="Span">Span to update</param>
		/// <param name="NewLastSuccess">New last successful step</param>
		/// <param name="NewFailure">New failed step</param>
		/// <param name="NewNextSuccess">New next successful step</param>
		/// <param name="NewSuspects">New suspects for the span</param>
		/// <param name="NewIssueId">The new issue id for this span</param>
		/// <returns>The updated span, or null on failure</returns>
		Task<IIssueSpan?> TryUpdateSpanAsync(IIssueSpan Span, NewIssueStepData? NewLastSuccess = null, NewIssueStepData? NewFailure = null, NewIssueStepData? NewNextSuccess = null, List<NewIssueSpanSuspectData>? NewSuspects = null, int? NewIssueId = null);

		/// <summary>
		/// Gets all the spans for a particular issue
		/// </summary>
		/// <param name="IssueId">Issue id</param>
		/// <returns>List of spans</returns>
		Task<List<IIssueSpan>> FindSpansAsync(int IssueId);

		/// <summary>
		/// Retrieves multiple spans
		/// </summary>
		/// <param name="SpanIds">The span ids</param>
		/// <returns>List of spans</returns>
		Task<List<IIssueSpan>> FindSpansAsync(IEnumerable<ObjectId> SpanIds);

		/// <summary>
		/// Finds the open issues for a given stream
		/// </summary>
		/// <param name="StreamId">The stream id</param>
		/// <param name="TemplateId">The template id</param>
		/// <param name="Name">Name of the node</param>
		/// <param name="Change">Changelist number to query</param>
		/// <returns>List of open issues</returns>
		Task<List<IIssueSpan>> FindOpenSpansAsync(StreamId StreamId, TemplateRefId TemplateId, string Name, int Change);

		#endregion

		#region Steps

		/// <summary>
		/// Adds a new step
		/// </summary>
		/// <param name="SpanId">Initial span for the step</param>
		/// <param name="NewStep">Information about the new step</param>
		/// <returns>New step object</returns>
		Task<IIssueStep> AddStepAsync(ObjectId SpanId, NewIssueStepData NewStep);

		/// <summary>
		/// Find steps for the given spans
		/// </summary>
		/// <param name="SpanIds">Span ids</param>
		/// <returns>List of steps</returns>
		Task<List<IIssueStep>> FindStepsAsync(IEnumerable<ObjectId> SpanIds);

		/// <summary>
		/// Find steps for the given spans
		/// </summary>
		/// <param name="JobId">The job id</param>
		/// <param name="BatchId">The batch id</param>
		/// <param name="StepId">The step id</param>
		/// <returns>List of steps</returns>
		Task<List<IIssueStep>> FindStepsAsync(JobId JobId, SubResourceId? BatchId, SubResourceId? StepId);

		#endregion

		/// <summary>
		/// Gets the logger for a particular issue
		/// </summary>
		/// <param name="IssueId">The issue id</param>
		/// <returns>Logger for this issue</returns>
		IAuditLogChannel<int> GetLogger(int IssueId);
	}
}
