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
using Datadog.Trace;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Collections
{
	/// <summary>
	/// Fingerprint for an issue
	/// </summary>
	public class NewIssueFingerprint : IIssueFingerprint
	{
		/// <summary>
		/// The type of issue
		/// </summary>
		public string Type { get; }

		/// <summary>
		/// List of keys which identify this issue.
		/// </summary>
		public CaseInsensitiveStringSet Keys { get; set; }

		/// <summary>
		/// Set of keys which should trigger a negative match
		/// </summary>
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
		public override bool Equals(object? Other)
		{
			NewIssueFingerprint? OtherFingerprint = Other as NewIssueFingerprint;
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
			int Result = string.GetHashCode(Type, StringComparison.Ordinal);
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
	/// Identifies a particular changelist and job
	/// </summary>
	public class NewIssueStepData
	{
		/// <summary>
		/// The changelist number
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Severity of the issue
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Name of the job
		/// </summary>
		public string JobName { get; set; }

		/// <summary>
		/// The unique job id
		/// </summary>
		public ObjectId JobId { get; set; }

		/// <summary>
		/// Batch id for the step
		/// </summary>
		public SubResourceId BatchId { get; set; }

		/// <summary>
		/// Id of the step
		/// </summary>
		public SubResourceId StepId { get; set; }

		/// <summary>
		/// Time that the step started
		/// </summary>
		public DateTime StepTime { get; set; }

		/// <summary>
		/// The log id for this step
		/// </summary>
		public ObjectId? LogId { get; set; }

		/// <summary>
		/// Whether to notify suspects for this step
		/// </summary>
		public bool NotifySuspects { get; set; }

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
		/// <param name="NotifySuspects">Whether to notify suspects for this step</param>
		public NewIssueStepData(int Change, IssueSeverity Severity, string JobName, ObjectId JobId, SubResourceId BatchId, SubResourceId StepId, DateTime StepTime, ObjectId? LogId, bool NotifySuspects)
		{
			this.Change = Change;
			this.Severity = Severity;
			this.JobName = JobName;
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.StepId = StepId;
			this.StepTime = StepTime;
			this.LogId = LogId;
			this.NotifySuspects = NotifySuspects;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Job">The job being built</param>
		/// <param name="Batch">Batch of the job for the step</param>
		/// <param name="Step">The step being built</param>
		/// <param name="Severity">Severity of the issue in this step</param>
		/// <param name="NotifySuspects">Whether to notify suspects for this step failing</param>
		public NewIssueStepData(IJob Job, IJobStepBatch Batch, IJobStep Step, IssueSeverity Severity, bool NotifySuspects)
			: this(Job.Change, Severity, Job.Name, Job.Id, Batch.Id, Step.Id, Step.StartTimeUtc ?? default, Step.LogId, NotifySuspects)
		{
		}

		/// <summary>
		/// Constructor
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
		/// <summary>
		/// Author of the changelist
		/// </summary>
		public ObjectId AuthorId { get; set; }

		/// <summary>
		/// The submitted changelist
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// The time that the user declined causing this issue
		/// </summary>
		public DateTime? DeclinedAt { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AuthorId">Author of the change</param>
		/// <param name="Change">The changelist number</param>
		/// <param name="DeclinedAt">The time that the user declined this issue</param>
		public NewIssueSuspectData(ObjectId AuthorId, int Change, DateTime? DeclinedAt)
		{
			this.AuthorId = AuthorId;
			this.Change = Change;
			this.DeclinedAt = DeclinedAt;
		}
	}

	/// <summary>
	/// Information about a suspect changelist that may have caused an issue
	/// </summary>
	public class NewIssueSpanSuspectData
	{
		/// <summary>
		/// The submitted changelist
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Author of the changelist
		/// </summary>
		public string Author { get; set; }

		/// <summary>
		/// The original changelist number, if merged from another branch. For changes merged between several branches, this is the originally submitted change.
		/// </summary>
		public int? OriginatingChange { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Change">The changelist number</param>
		/// <param name="Author">Author of the change</param>
		public NewIssueSpanSuspectData(int Change, string Author)
		{
			this.Change = Change;
			this.Author = Author;
		}
	}

	/// <summary>
	/// Interface for a token used to indicate a position in the transaction queue. Transactions are opportunistic, and will fail if the token is longer valid at the time that it is used.
	/// </summary>
	[SuppressMessage("Design", "CA1040:Avoid empty interfaces")]
	public interface IIssueSequenceToken
	{
		/// <summary>
		/// Reset the token to the last operation
		/// </summary>
		/// <returns>Async task</returns>
		public Task ResetAsync();
	}

	/// <summary>
	/// Interface for an issue collection
	/// </summary>
	public interface IIssueCollection
	{
		/// <summary>
		/// Gets a token used to sequence operations to the issue collection
		/// </summary>
		/// <returns>Sequence token instance</returns>
		Task<IIssueSequenceToken> GetSequenceTokenAsync();

		/// <summary>
		/// Assign a unique issue id. For compatibility with the legacy issue system.
		/// </summary>
		/// <param name="Token">The current sequence token</param>
		/// <returns>Sequence token</returns>
		Task<int?> ReserveUniqueIdAsync(IIssueSequenceToken Token);

		#region Issues

		/// <summary>
		/// Creates a new issue
		/// </summary>
		/// <param name="Token">Token for the transaction</param>
		/// <param name="Summary">Summary text for the issue</param>
		/// <param name="Span">The initial span for the issue</param>
		/// <returns>The new issue instance</returns>
		Task<IIssue?> AddIssueAsync(IIssueSequenceToken Token, string Summary, IIssueSpan Span);

		/// <summary>
		/// Retrieves and issue by id
		/// </summary>
		/// <param name="IssueId">Unique id of the issue</param>
		/// <returns>The issue matching the given id, or null</returns>
		Task<IIssue?> GetIssueAsync(int IssueId);

		/// <summary>
		/// Gets the suspects for an issue
		/// </summary>
		/// <param name="Issue">The issue to retrieve suspects for</param>
		/// <returns>List of suspects</returns>
		Task<List<IIssueSuspect>> GetSuspectsAsync(IIssue Issue);

		/// <summary>
		/// Searches for open issues
		/// </summary>
		/// <param name="Ids">Set of issue ids to find</param>
		/// <param name="UserId">The user to find issues for</param>
		/// <param name="StreamId">The stream affected by the issue</param>
		/// <param name="MinChange">Minimum changelist affected by the issue</param>
		/// <param name="MaxChange">Maximum changelist affected by the issue</param>
		/// <param name="Resolved">Include issues that are now resolved</param>
		/// <param name="Index">Index within the results to return</param>
		/// <param name="Count">Number of results</param>
		/// <returns>List of streams open in the given stream at the given changelist</returns>
		Task<List<IIssue>> FindIssuesAsync(IEnumerable<int>? Ids = null, ObjectId? UserId = null, StreamId? StreamId = null, int? MinChange = null, int? MaxChange = null, bool? Resolved = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Searches for open issues
		/// </summary>
		/// <param name="Changes">List of suspect changes</param>
		/// <returns>List of issues that are affected by the given changes</returns>
		Task<List<IIssue>> FindIssuesForSuspectsAsync(List<int> Changes);

		/// <summary>
		/// Updates the state of an issue
		/// </summary>
		/// <param name="Issue">The issue to update</param>
		/// <param name="NewSeverity">New severity for the issue</param>
		/// <param name="NewSummary">New summary for the issue</param>
		/// <param name="NewUserSummary">New user summary for the issue</param>
		/// <param name="NewOwnerId">New owner of the issue</param>
		/// <param name="NewNominatedById">Person that nominated the new owner</param>
		/// <param name="NewAcknowledged">Whether the issue has been acknowledged</param>
		/// <param name="NewDeclinedById">Name of a user that has declined the issue</param>
		/// <param name="NewFixChange">Fix changelist for the issue. Pass 0 to clear the fix changelist, -1 for systemic issue.</param>
		/// <param name="NewFixStreamIds">List of stream ids with a bool indicating whether the fix changelist is in this stream</param>
		/// <param name="NewResolvedById">User that resolved the issue (may be ObjectId.Empty to clear)</param>
		/// <param name="NewLastSeenAt"></param>
		/// <param name="NewNotifySuspects">Whether all suspects should be notified about this issue</param>
		/// <returns>True if the issue was updated</returns>
		Task<IIssue?> UpdateIssueAsync(IIssue Issue, IssueSeverity? NewSeverity = null, string? NewSummary = null, string? NewUserSummary = null, ObjectId? NewOwnerId = null, ObjectId? NewNominatedById = null, bool? NewAcknowledged = null, ObjectId? NewDeclinedById = null, int? NewFixChange = null, Dictionary<StreamId, bool>? NewFixStreamIds = null, ObjectId? NewResolvedById = null, DateTime? NewLastSeenAt = null, bool? NewNotifySuspects = null);

		/// <summary>
		/// Updates the list of suspects for an issue, and optionally sets the resolved state
		/// </summary>
		/// <param name="Issue">The issue to update</param>
		/// <returns>True if the issue was updated</returns>
		Task<IIssue?> UpdateIssueDerivedDataAsync(IIssue Issue);

		#endregion

		#region Spans

		/// <summary>
		/// Creates a new span from the given failure
		/// </summary>
		/// <param name="Token">Token to sequence the addition of new spans</param>
		/// <param name="Stream">The stream containing the span</param>
		/// <param name="TemplateRefId">The template being executed</param>
		/// <param name="NodeName">Name of the step</param>
		/// <param name="Fingerprint">Fingerprint for this issue</param>
		/// <param name="LastSuccess">Last time the build succeeded before the span</param>
		/// <param name="Failure">List of failing steps</param>
		/// <param name="NextSuccess">First time the build succeeded after the span</param>
		/// <param name="Suspects">Suspects for this span</param>
		/// <returns>New span, or null if the sequence token is not valid</returns>
		Task<IIssueSpan?> AddSpanAsync(IIssueSequenceToken Token, IStream Stream, TemplateRefId TemplateRefId, string NodeName, NewIssueFingerprint Fingerprint, NewIssueStepData? LastSuccess, NewIssueStepData Failure, NewIssueStepData? NextSuccess, List<NewIssueSpanSuspectData> Suspects);

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
		/// <param name="NewSeverity">New severity for the span</param>
		/// <param name="NewLastSuccess">New last successful step</param>
		/// <param name="NewFailure">New failed step</param>
		/// <param name="NewNextSuccess">New next successful step</param>
		/// <param name="NewSuspects">New suspects for the span</param>
		/// <param name="NewModified">New modified flag for the span</param>
		/// <returns>The updated span, or null on failure</returns>
		Task<IIssueSpan?> UpdateSpanAsync(IIssueSpan Span, IssueSeverity? NewSeverity = null, NewIssueStepData? NewLastSuccess = null, NewIssueStepData? NewFailure = null, NewIssueStepData? NewNextSuccess = null, List<NewIssueSpanSuspectData>? NewSuspects = null, bool? NewModified = null);

		/// <summary>
		/// Updates the given span
		/// </summary>
		/// <param name="Token">The token</param>
		/// <param name="Span">The span to update</param>
		/// <param name="Issue">The issue to attach to</param>
		/// <param name="NewSeverity">New severity for the span</param>
		/// <param name="NewFingerprint">New fingerprint for the span</param>
		/// <param name="NewSummary">New summary for the issue</param>
		/// <returns>The updated span, or null on failure</returns>
		Task<bool> AttachSpanToIssueAsync(IIssueSequenceToken Token, IIssueSpan Span, IIssue Issue, IssueSeverity? NewSeverity = null, NewIssueFingerprint? NewFingerprint = null, string? NewSummary = null);

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

		/// <summary>
		/// Finds a span marked as modified
		/// </summary>
		/// <returns></returns>
		Task<IIssueSpan?> FindModifiedSpanAsync();

		#endregion

		#region Steps
			
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
		Task<List<IIssueStep>> FindStepsAsync(ObjectId JobId, SubResourceId? BatchId, SubResourceId? StepId);

		#endregion
	}
}
