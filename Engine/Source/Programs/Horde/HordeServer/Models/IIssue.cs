using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Bson.Serialization.Serializers;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Runtime.CompilerServices;

using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;
using TemplateRefId = HordeServer.Utilities.StringId<HordeServer.Models.TemplateRef>;

namespace HordeServer.Models
{
	/// <summary>
	/// The severity of an issue
	/// </summary>
	public enum IssueSeverity
	{
		/// <summary>
		/// Unspecified severity
		/// </summary>
		Unspecified,

		/// <summary>
		/// This error represents a warning
		/// </summary>
		Warning,

		/// <summary>
		/// This issue represents an error
		/// </summary>
		Error,
	}

	/// <summary>
	/// Stores information about a build health issue
	/// </summary>
	public interface IIssue
	{
		/// <summary>
		/// The unique object id
		/// </summary>
		public int Id { get; }

		/// <summary>
		/// Summary for this issue
		/// </summary>
		public string Summary { get; }

		/// <summary>
		/// Summary set by users
		/// </summary>
		public string? UserSummary { get; }

		/// <summary>
		/// Fingerprint for this issue
		/// </summary>
		public IIssueFingerprint Fingerprint { get; }

		/// <summary>
		/// Severity of this issue
		/// </summary>
		public IssueSeverity Severity { get; }

		/// <summary>
		/// Current owner of this issue
		/// </summary>
		public string? Owner { get; }

		/// <summary>
		/// User id of the owner
		/// </summary>
		public ObjectId? OwnerId { get; }

		/// <summary>
		/// User that nominated the current owner
		/// </summary>
		public string? NominatedBy { get; }

		/// <summary>
		/// User id of the person that nominated the owner
		/// </summary>
		public ObjectId? NominatedById { get; }

		/// <summary>
		/// Time at which the issue was created
		/// </summary>
		public DateTime CreatedAt { get; }

		/// <summary>
		/// Time that the current owner was nominated
		/// </summary>
		public DateTime? NominatedAt { get; }

		/// <summary>
		/// Time at which the issue was acknowledged
		/// </summary>
		public DateTime? AcknowledgedAt { get; }

		/// <summary>
		/// Time at which the issue was resolved
		/// </summary>
		public DateTime? ResolvedAt { get; }

		/// <summary>
		/// Fix changelist for this issue
		/// </summary>
		public int? FixChange { get; }

		/// <summary>
		/// Whether all suspects should be notified about this issue
		/// </summary>
		public bool NotifySuspects { get; }

		/// <summary>
		/// Suspects for this issue
		/// </summary>
		public IReadOnlyList<IIssueSuspect> Suspects { get; }

		/// <summary>
		/// Update index for this instance
		/// </summary>
		public int UpdateIndex { get; }
	}

	/// <summary>
	/// Suspect for an issue
	/// </summary>
	public interface IIssueSuspect
	{
		/// <summary>
		/// Author of the change
		/// </summary>
		string Author { get; }

		/// <summary>
		/// The user id of the change's author
		/// </summary>
		ObjectId AuthorId { get; }

		/// <summary>
		/// The change suspected of causing this issue (in the origin stream)
		/// </summary>
		int Change { get; }

		/// <summary>
		/// Time at which the author declined the issue
		/// </summary>
		DateTime? DeclinedAt { get; }
	}
}