// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using HordeServer.Utilities.BlockKit;
using HordeServer.Utilities.Slack.BlockKit;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.Linq;
using System.Net.Http;
using System.Net.WebSockets;
using System.Security.Claims;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Notifications.Impl
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Maintains a connection to Slack, in order to receive socket-mode notifications of user interactions
	/// </summary>
	public class SlackNotificationSink : BackgroundService, INotificationSink, IAvatarService
	{
		const string PostMessageUrl = "https://slack.com/api/chat.postMessage";
		const string UpdateMessageUrl = "https://slack.com/api/chat.update";

		class SocketResponse
		{
			[JsonPropertyName("ok")]
			public bool Ok { get; set; }

			[JsonPropertyName("url")]
			public Uri? Url { get; set; }
		}

		class EventMessage
		{
			[JsonPropertyName("envelope_id")]
			public string? EnvelopeId { get; set; }

			[JsonPropertyName("type")]
			public string? Type { get; set; }

			[JsonPropertyName("payload")]
			public EventPayload? Payload { get; set; }
		}

		class EventPayload
		{
			[JsonPropertyName("type")]
			public string? Type { get; set; }

			[JsonPropertyName("user")]
			public UserInfo? User { get; set; }

			[JsonPropertyName("response_url")]
			public string? ResponseUrl { get; set; }

			[JsonPropertyName("actions")]
			public List<ActionInfo> Actions { get; set; } = new List<ActionInfo>();
		}

		class UserResponse
		{
			[JsonPropertyName("ok")]
			public bool Ok { get; set; }

			[JsonPropertyName("user")]
			public UserInfo? User { get; set; }
		}

		class UserInfo
		{
			[JsonPropertyName("id")]
			public string? UserName { get; set; }

			[JsonPropertyName("profile")]
			public UserProfile? Profile { get; set; }
		}

		class UserProfile
		{
			[JsonPropertyName("is_custom_image")]
			public bool IsCustomImage { get; set; }

			[JsonPropertyName("image_24")]
			public string? Image24 { get; set; }

			[JsonPropertyName("image_32")]
			public string? Image32 { get; set; }

			[JsonPropertyName("image_48")]
			public string? Image48 { get; set; }

			[JsonPropertyName("image_72")]
			public string? Image72 { get; set; }
		}

		class ActionInfo
		{
			[JsonPropertyName("value")]
			public string? Value { get; set; }
		}

		class MessageStateDocument
		{
			[BsonId]
			public ObjectId Id { get; set; }

			[BsonElement("uid")]
			public string Recipient { get; set; } = String.Empty;

			[BsonElement("usr")]
			public UserId? UserId { get; set; }

			[BsonElement("eid")]
			public string EventId { get; set; } = String.Empty;

			[BsonElement("ch")]
			public string Channel { get; set; } = String.Empty;

			[BsonElement("ts")]
			public string Ts { get; set; } = String.Empty;

			[BsonElement("dig")]
			public string Digest { get; set; } = String.Empty;
		}

		class SlackUser : IAvatar
		{
			public const int CurrentVersion = 2;
			
			public UserId Id { get; set; }

			[BsonElement("u")]
			public string? SlackUserId { get; set; }

			[BsonElement("i24")]
			public string? Image24 { get; set; }

			[BsonElement("i32")]
			public string? Image32 { get; set; }
			
			[BsonElement("i48")]
			public string? Image48 { get; set; }

			[BsonElement("i72")]
			public string? Image72 { get; set; }

			[BsonElement("t")]
			public DateTime Time { get; set; }

			[BsonElement("v")]
			public int Version { get; set; }

			private SlackUser()
			{
			}

			public SlackUser(UserId Id, UserInfo? Info)
			{
				this.Id = Id;
				this.SlackUserId = Info?.UserName;
				if (Info != null && Info.Profile != null && Info.Profile.IsCustomImage)
				{
					this.Image24 = Info.Profile.Image24;
					this.Image32 = Info.Profile.Image32;
					this.Image48 = Info.Profile.Image48;
					this.Image72 = Info.Profile.Image72;
				}
				this.Time = DateTime.UtcNow;
				this.Version = CurrentVersion;
			}
		}

		IIssueService IssueService;
		IUserCollection UserCollection;
		ILogFileService LogFileService;
		StreamService StreamService;
		ServerSettings Settings;
		IMongoCollection<MessageStateDocument> MessageStates;
		IMongoCollection<SlackUser> SlackUsers;
		HashSet<string>? AllowUsers;
		ILogger Logger;

		/// <summary>
		/// Map of email address to Slack user ID.
		/// </summary>
		private MemoryCache UserCache = new MemoryCache(new MemoryCacheOptions());

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="IssueService"></param>
		/// <param name="UserCollection">The user collection</param>
		/// <param name="LogFileService"></param>
		/// <param name="StreamService"></param>
		/// <param name="Settings">The current configuration settings</param>
		/// <param name="Logger">Logging device</param>
		public SlackNotificationSink(DatabaseService DatabaseService, IIssueService IssueService, IUserCollection UserCollection, ILogFileService LogFileService, StreamService StreamService, IOptions<ServerSettings> Settings, ILogger<SlackNotificationSink> Logger)
		{
			this.IssueService = IssueService;
			this.UserCollection = UserCollection;
			this.LogFileService = LogFileService;
			this.StreamService = StreamService;
			this.Settings = Settings.Value;
			this.MessageStates = DatabaseService.Database.GetCollection<MessageStateDocument>("Slack");
			this.SlackUsers = DatabaseService.Database.GetCollection<SlackUser>("Slack.UsersV2");
			this.Logger = Logger;

			if (!String.IsNullOrEmpty(Settings.Value.SlackUsers))
			{
				AllowUsers = new HashSet<string>(Settings.Value.SlackUsers.Split(','), StringComparer.OrdinalIgnoreCase);
			}
		}

		/// <inheritdoc/>
		public override void Dispose()
		{
			base.Dispose();

			UserCache.Dispose();

			GC.SuppressFinalize(this);
		}

		#region Avatars

		/// <inheritdoc/>
		public async Task<IAvatar?> GetAvatarAsync(IUser User)
		{
			return await GetSlackUser(User);
		}

		#endregion

		#region Message state 

		async Task<(MessageStateDocument, bool)> AddOrUpdateMessageStateAsync(string Recipient, string EventId, UserId? UserId, string Digest)
		{
			ObjectId NewId = ObjectId.GenerateNewId();

			FilterDefinition<MessageStateDocument> Filter = Builders<MessageStateDocument>.Filter.Eq(x => x.Recipient, Recipient) & Builders<MessageStateDocument>.Filter.Eq(x => x.EventId, EventId);
			UpdateDefinition<MessageStateDocument> Update = Builders<MessageStateDocument>.Update.SetOnInsert(x => x.Id, NewId).Set(x => x.UserId, UserId).Set(x => x.Digest, Digest);

			MessageStateDocument State = await MessageStates.FindOneAndUpdateAsync(Filter, Update, new FindOneAndUpdateOptions<MessageStateDocument> { IsUpsert = true, ReturnDocument = ReturnDocument.After });
			return (State, State.Id == NewId);
		}

		async Task SetMessageTimestampAsync(ObjectId MessageId, string Channel, string Ts)
		{
			FilterDefinition<MessageStateDocument> Filter = Builders<MessageStateDocument>.Filter.Eq(x => x.Id, MessageId);
			UpdateDefinition<MessageStateDocument> Update = Builders<MessageStateDocument>.Update.Set(x => x.Channel, Channel).Set(x => x.Ts, Ts);
			await MessageStates.FindOneAndUpdateAsync(Filter, Update);
		}

		#endregion

		#region Job Complete

		/// <inheritdoc/>
		public async Task NotifyJobCompleteAsync(IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome)
		{
			if (Job.NotificationChannel != null)
			{
				await SendJobCompleteNotificationToChannelAsync(Job.NotificationChannel, Job.NotificationChannelFilter, JobStream, Job, Graph, Outcome);
			}
			if (JobStream.NotificationChannel != null)
			{
				await SendJobCompleteNotificationToChannelAsync(JobStream.NotificationChannel, JobStream.NotificationChannelFilter, JobStream, Job, Graph, Outcome);
			}
		}

		Task SendJobCompleteNotificationToChannelAsync(string NotificationChannel, string? NotificationFilter, IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome)
		{
			if (NotificationFilter != null)
			{
				List<LabelOutcome> Outcomes = new List<LabelOutcome>();
				foreach (string FilterOption in NotificationFilter.Split('|'))
				{
					LabelOutcome Result;
					if (Enum.TryParse(FilterOption, out Result))
					{
						Outcomes.Add(Result);
					}
					else
					{
						Logger.LogWarning("Invalid filter option {Option} specified in job filter {NotificationChannelFilter} in job {JobId} or stream {StreamId}", FilterOption, NotificationFilter, Job.Id, Job.StreamId);
					}
				}
				if (!Outcomes.Contains(Outcome))
				{
					return Task.CompletedTask;
				}
			}
			return SendJobCompleteMessageAsync(NotificationChannel, JobStream, Job, Graph);
		}

		/// <inheritdoc/>
		public async Task NotifyJobCompleteAsync(IUser SlackUser, IStream JobStream, IJob Job, IGraph Graph, LabelOutcome Outcome)
		{
			string? SlackUserId = await GetSlackUserId(SlackUser);
			if (SlackUserId != null)
			{
				await SendJobCompleteMessageAsync(SlackUserId, JobStream, Job, Graph);
			}
		}

		private Task SendJobCompleteMessageAsync(string Recipient, IStream Stream, IJob Job, IGraph Graph)
		{
			JobStepOutcome JobOutcome = Job.Batches.SelectMany(x => x.Steps).Min(x => x.Outcome);
			Logger.LogInformation("Sending Slack notification for job {JobId} outcome {Outcome} to {SlackUser}", Job.Id, JobOutcome, Recipient);

			Uri JobLink = new Uri($"{Settings.DashboardUrl}job/{Job.Id}");

			Color OutcomeColor = JobOutcome == JobStepOutcome.Failure ? BlockKitAttachmentColors.Error : JobOutcome == JobStepOutcome.Warnings ? BlockKitAttachmentColors.Warning : BlockKitAttachmentColors.Success;

			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.FallbackText = $"{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - {JobOutcome}";
			Attachment.Color = OutcomeColor;
			Attachment.Blocks.Add(new SectionBlock($"*<{JobLink}|{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name}>*"));
			if (JobOutcome == JobStepOutcome.Success)
			{
				Attachment.Blocks.Add(new SectionBlock($"*Job Succeeded*"));
			}
			else
			{
				List<string> FailedStepStrings = new List<string>();
				List<string> WarningStepStrings = new List<string>();

				IReadOnlyDictionary<NodeRef, IJobStep> NodeToStep = Job.GetStepForNodeMap();
				foreach ((NodeRef NodeRef, IJobStep Step) in NodeToStep)
				{
					INode StepNode = Graph.GetNode(NodeRef);
					string StepName = $"<{JobLink}?step={Step.Id}|{StepNode.Name}>";
					if (Step.Outcome == JobStepOutcome.Failure)
					{
						FailedStepStrings.Add(StepName);
					}
					else if (Step.Outcome == JobStepOutcome.Warnings)
					{
						WarningStepStrings.Add(StepName);
					}
				}

				if (FailedStepStrings.Any())
				{
					string Msg = $"*Errors*\n{string.Join(", ", FailedStepStrings)}";
					Attachment.Blocks.Add(new SectionBlock(Msg.Substring(0, Math.Min(Msg.Length, 3000))));
				}
				else if (WarningStepStrings.Any())
				{
					string Msg = $"*Warnings*\n{string.Join(", ", WarningStepStrings)}";
					Attachment.Blocks.Add(new SectionBlock(Msg.Substring(0, Math.Min(Msg.Length, 3000))));
				}
			}

			if (Job.AutoSubmit)
			{
				Attachment.Blocks.Add(new DividerBlock());
				if (Job.AutoSubmitChange != null)
				{
					Attachment.Blocks.Add(new SectionBlock($"Shelved files were submitted in CL {Job.AutoSubmitChange}."));
				}
				else
				{
					Attachment.Color = BlockKitAttachmentColors.Warning;

					string AutoSubmitMessage = String.Empty;
					if (!String.IsNullOrEmpty(Job.AutoSubmitMessage))
					{
						AutoSubmitMessage = $"\n\n```{Job.AutoSubmitMessage}```";
					}

					Attachment.Blocks.Add(new SectionBlock($"Files in CL {Job.PreflightChange} were *not submitted*. Please resolve the following issues and submit manually.{Job.AutoSubmitMessage}"));
				}
			}

			return SendMessageAsync(Recipient, Attachments: new[] { Attachment });
		}

		#endregion

		#region Job step complete

		/// <inheritdoc/>
		public async Task NotifyJobStepCompleteAsync(IUser SlackUser, IStream JobStream, IJob Job, IJobStepBatch Batch, IJobStep Step, INode Node, List<ILogEventData> JobStepEventData)
		{
			Logger.LogInformation("Sending Slack notification for job {JobId}, batch {BatchId}, step {StepId}, outcome {Outcome} to {SlackUser} ({UserId})", Job.Id, Batch.Id, Step.Id, Step.Outcome, SlackUser.Name, SlackUser.Id);

			string? SlackUserId = await GetSlackUserId(SlackUser);
			if (SlackUserId != null)
			{
				await SendJobStepCompleteMessageAsync(SlackUserId, JobStream, Job, Step, Node, JobStepEventData);
			}
		}

		/// <summary>
		/// Creates a Slack message about a completed step job.
		/// </summary>
		/// <param name="Recipient"></param>
		/// <param name="Stream"></param>
		/// <param name="Job">The job that contains the step that completed.</param>
		/// <param name="Step">The job step that completed.</param>
		/// <param name="Node">The node for the job step.</param>
		/// <param name="Events">Any events that occurred during the job step.</param>
		private Task SendJobStepCompleteMessageAsync(string Recipient, IStream Stream, IJob Job, IJobStep Step, INode Node, List<ILogEventData> Events)
		{
			Uri JobStepLink = new Uri($"{Settings.DashboardUrl}job/{Job.Id}?step={Step.Id}");
			Uri JobStepLogLink = new Uri($"{Settings.DashboardUrl}log/{Step.LogId}");

			Color OutcomeColor = Step.Outcome == JobStepOutcome.Failure ? BlockKitAttachmentColors.Error : Step.Outcome == JobStepOutcome.Warnings ? BlockKitAttachmentColors.Warning : BlockKitAttachmentColors.Success;

			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.FallbackText = $"{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - {Node.Name} - {Step.Outcome}";
			Attachment.Color = OutcomeColor;
			Attachment.Blocks.Add(new SectionBlock($"*<{JobStepLink}|{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - {Node.Name}>*"));
			if (Step.Outcome == JobStepOutcome.Success)
			{
				Attachment.Blocks.Add(new SectionBlock($"*Job Step Succeeded*"));
			}
			else
			{
				List<ILogEventData> Errors = Events.Where(x => x.Severity == EventSeverity.Error).ToList();
				List<ILogEventData> Warnings = Events.Where(x => x.Severity == EventSeverity.Warning).ToList();
				List<string> EventStrings = new List<string>();
				if (Errors.Any())
				{
					string ErrorSummary = Errors.Count > MaxJobStepEvents ? $"*Errors (First {MaxJobStepEvents} shown)*" : $"*Errors*";
					EventStrings.Add(ErrorSummary);
					foreach (ILogEventData Error in Errors.Take(MaxJobStepEvents))
					{
						EventStrings.Add($"```{Error.Message}```");
					}
				}
				else if (Warnings.Any())
				{
					string WarningSummary = Warnings.Count > MaxJobStepEvents ? $"*Warnings (First {MaxJobStepEvents} shown)*" : $"*Warnings*";
					EventStrings.Add(WarningSummary);
					foreach (ILogEventData Warning in Warnings.Take(MaxJobStepEvents))
					{
						EventStrings.Add($"```{Warning.Message}```");
					}
				}

				Attachment.Blocks.Add(new SectionBlock(string.Join("\n", EventStrings)));
				Attachment.Blocks.Add(new SectionBlock($"<{JobStepLogLink}|View Job Step Log>"));
			}

			return SendMessageAsync(Recipient, Attachments: new[] { Attachment });
		}

		#endregion

		#region Label complete

		/// <inheritdoc/>
		public async Task NotifyLabelCompleteAsync(IUser User, IJob Job, IStream Stream, ILabel Label, int LabelIdx, LabelOutcome Outcome, List<(string, JobStepOutcome, Uri)> StepData)
		{
			Logger.LogInformation("Sending Slack notification for job {JobId} outcome {Outcome} to {Name} ({UserId})", Job.Id, Outcome, User.Name, User.Id);

			string? SlackUserId = await GetSlackUserId(User);
			if (SlackUserId != null)
			{
				await SendLabelUpdateMessageAsync(SlackUserId, Stream, Job, Label, LabelIdx, Outcome, StepData);
			}
		}

		Task SendLabelUpdateMessageAsync(string Recipient, IStream Stream, IJob Job, ILabel Label, int LabelIdx, LabelOutcome Outcome, List<(string, JobStepOutcome, Uri)> JobStepData)
		{
			Uri LabelLink = new Uri($"{Settings.DashboardUrl}job/{Job.Id}?label={LabelIdx}");

			Color OutcomeColor = Outcome == LabelOutcome.Failure ? BlockKitAttachmentColors.Error : Outcome == LabelOutcome.Warnings ? BlockKitAttachmentColors.Warning : BlockKitAttachmentColors.Success;

			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.FallbackText = $"{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - Label {Label.DashboardName} - {Outcome}";
			Attachment.Color = OutcomeColor;
			Attachment.Blocks.Add(new SectionBlock($"*<{LabelLink}|{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - Label {Label.DashboardName}>*"));
			if (Outcome == LabelOutcome.Success)
			{
				Attachment.Blocks.Add(new SectionBlock($"*Label Succeeded*"));
			}
			else
			{
				List<string> FailedStepStrings = new List<string>();
				List<string> WarningStepStrings = new List<string>();
				foreach ((string Name, JobStepOutcome StepOutcome, Uri Link) JobStep in JobStepData)
				{
					string StepString = $"<{JobStep.Link}|{JobStep.Name}>";
					if (JobStep.StepOutcome == JobStepOutcome.Failure)
					{
						FailedStepStrings.Add(StepString);
					}
					else if (JobStep.StepOutcome == JobStepOutcome.Warnings)
					{
						WarningStepStrings.Add(StepString);
					}
				}

				if (FailedStepStrings.Any())
				{
					Attachment.Blocks.Add(new SectionBlock($"*Errors*\n{string.Join(", ", FailedStepStrings)}"));
				}
				else if (WarningStepStrings.Any())
				{
					Attachment.Blocks.Add(new SectionBlock($"*Warnings*\n{string.Join(", ", WarningStepStrings)}"));
				}
			}

			return SendMessageAsync(Recipient, Attachments: new[] { Attachment });
		}

		#endregion

		#region Issues

		/// <inheritdoc/>
		public async Task NotifyIssueUpdatedAsync(IIssue Issue)
		{
			IIssueDetails Details = await IssueService.GetIssueDetailsAsync(Issue);

			HashSet<UserId> UserIds = new HashSet<UserId>();
			if (Issue.Promoted)
			{
				UserIds.UnionWith(Details.Suspects.Select(x => x.AuthorId));
			}
			if (Issue.OwnerId.HasValue)
			{
				UserIds.Add(Issue.OwnerId.Value);
			}

			HashSet<string> Channels = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

			List<MessageStateDocument> ExistingMessages = await MessageStates.Find(x => x.EventId == GetIssueEventId(Issue)).ToListAsync();
			foreach (MessageStateDocument ExistingMessage in ExistingMessages)
			{
				if (ExistingMessage.UserId != null)
				{
					UserIds.Add(ExistingMessage.UserId.Value);
				}
				else
				{
					Channels.Add(ExistingMessage.Recipient);
				}
			}

			if (Issue.OwnerId == null && (Details.Suspects.Count == 0 || Details.Suspects.All(x => x.DeclinedAt != null) || Issue.CreatedAt < DateTime.UtcNow - TimeSpan.FromHours(1.0)))
			{
				foreach (IIssueSpan Span in Details.Spans)
				{
					IStream? Stream = await StreamService.GetCachedStream(Span.StreamId);
					if (Stream != null)
					{
						TemplateRef? TemplateRef;
						if (Stream.Templates.TryGetValue(Span.TemplateRefId, out TemplateRef) && TemplateRef.TriageChannel != null)
						{
							Channels.Add(TemplateRef.TriageChannel);
						}
						else if (Stream.TriageChannel != null)
						{
							Channels.Add(Stream.TriageChannel);
						}
					}
				}
			}

			if (UserIds.Count > 0)
			{
				foreach (UserId UserId in UserIds)
				{
					IUser? User = await UserCollection.GetUserAsync(UserId);
					if (User == null)
					{
						Logger.LogWarning("Unable to find user {UserId}", UserId);
					}
					else
					{
						await NotifyIssueUpdatedAsync(User, Issue, Details);
					}
				}
			}

			if (Channels.Count > 0)
			{
				foreach (string Channel in Channels)
				{
					await SendIssueMessageAsync(Channel, Issue, Details, null);
				}
			}
		}

		async Task NotifyIssueUpdatedAsync(IUser User, IIssue Issue, IIssueDetails Details)
		{
			IUserSettings UserSettings = await UserCollection.GetSettingsAsync(User.Id);
			if (!UserSettings.EnableIssueNotifications)
			{
				Logger.LogInformation("Issue notifications are disabled for user {UserId} ({UserName})", User.Id, User.Name);
				return;
			}

			string? SlackUserId = await GetSlackUserId(User);
			if (SlackUserId == null)
			{
				return;
			}

			await SendIssueMessageAsync(SlackUserId, Issue, Details, User.Id);
		}

		async Task<string> GetSampleText(IIssueDetails Details)
		{
			StringBuilder SampleText = new StringBuilder();
			if (Details.Spans.Count > 0)
			{
				IIssueSpan Span = Details.Spans[0];
				if (Span.FirstFailure.LogId != null)
				{
					ILogFile? LogFile = await LogFileService.GetCachedLogFileAsync(Span.FirstFailure.LogId.Value);
					if (LogFile != null)
					{
						List<ILogEvent> Events = await LogFileService.FindEventsForSpansAsync(Details.Spans.Select(x => x.Id).Take(1), null, 0, 1);
						foreach (ILogEvent Event in Events)
						{
							ILogEventData Data = await LogFileService.GetEventDataAsync(LogFile, Event.LineIndex, Event.LineCount);
							if (SampleText.Length > 0)
							{
								SampleText.Append("\n\n");
							}
							SampleText.Append(CultureInfo.InvariantCulture, $"```{Data.Message}```");
						}
					}
				}
			}
			return SampleText.ToString();
		}

		async Task SendIssueMessageAsync(string Recipient, IIssue Issue, IIssueDetails Details, UserId? UserId)
		{
			using IDisposable Scope = Logger.BeginScope("SendIssueMessageAsync (User: {SlackUser}, Issue: {IssueId})", Recipient, Issue.Id);

			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.Color = Color.Red;

			Uri IssueUrl = Settings.DashboardUrl;
			if (Details.Steps.Count > 0)
			{
				IIssueStep FirstStep = Details.Steps[0];
				IssueUrl = new Uri(IssueUrl, $"job/{FirstStep.JobId}?step={FirstStep.StepId}&issue={Issue.Id}");
			}
			Attachment.Blocks.Add(new SectionBlock($"*<{IssueUrl}|Issue #{Issue.Id}: {Issue.Summary}>*"));

			string StreamList = StringUtils.FormatList(Details.Spans.Select(x => $"*{x.StreamName}*").Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(x => x, StringComparer.OrdinalIgnoreCase));
			Attachment.Blocks.Add(new SectionBlock($"Occurring in {StreamList}"));

			if (Issue.FixChange != null)
			{
				IIssueStep? FixFailedStep = Issue.FindFixFailedStep(Details.Spans);

				string Text;
				if (Issue.FixChange.Value < 0)
				{
					Text = ":tick: Marked as a systemic issue.";
				}
				else if (FixFailedStep != null)
				{
					Uri FixFailedUrl = new Uri(Settings.DashboardUrl, $"job/{FixFailedStep.JobId}?step={FixFailedStep.StepId}&issue={Issue.Id}");
					Text = $":cross: Marked fixed in *CL {Issue.FixChange.Value}*, but seen again at *<{FixFailedUrl}|CL {FixFailedStep.Change}>*";
				}
				else
				{
					Text = $":tick: Marked fixed in *CL {Issue.FixChange.Value}*.";
				}
				Attachment.Blocks.Add(new SectionBlock(Text));
			}
			else if (UserId != null && Issue.OwnerId == UserId)
			{
				if (Issue.AcknowledgedAt.HasValue)
				{
					Attachment.Blocks.Add(new SectionBlock($":+1: Acknowledged at {FormatSlackTime(Issue.AcknowledgedAt.Value)}"));
				}
				else
				{
					if (Issue.NominatedById != null)
					{
						IUser? NominatedByUser = await UserCollection.GetUserAsync(Issue.NominatedById.Value);
						if (NominatedByUser != null)
						{
							string? NominatedBySlackUserId = await GetSlackUserId(NominatedByUser);
							string Mention = (NominatedBySlackUserId != null) ? $"<@{NominatedBySlackUserId}>" : NominatedByUser.Login ?? $"User {NominatedByUser.Id}";
							string Text = $"You were nominated to fix this issue by {Mention} at {FormatSlackTime(Issue.NominatedAt ?? DateTime.UtcNow)}";
							Attachment.Blocks.Add(new SectionBlock(Text));
						}
					}
					else
					{
						List<int> Changes = Details.Suspects.Where(x => x.AuthorId == UserId).Select(x => x.Change).OrderBy(x => x).ToList();
						if (Changes.Count > 0)
						{
							string Text = $"Horde has determined that {StringUtils.FormatList(Changes.Select(x => $"CL {x}"), "or")} is the most likely cause for this issue.";
							Attachment.Blocks.Add(new SectionBlock(Text));
						}
					}

					ActionsBlock Actions = new ActionsBlock();
					Actions.AddButton("Acknowledge", Value: $"issue_{Issue.Id}_ack_{UserId}", Style: ActionButton.ButtonStyle.Primary);
					Actions.AddButton("Not Me", Value: $"issue_{Issue.Id}_decline_{UserId}", Style: ActionButton.ButtonStyle.Danger);
					Attachment.Blocks.Add(Actions);
				}
			}
			else if (Issue.OwnerId != null)
			{
				string OwnerMention = await FormatMentionAsync(Issue.OwnerId.Value);
				if (Issue.AcknowledgedAt.HasValue)
				{
					Attachment.Blocks.Add(new SectionBlock($":+1: Acknowledged by {OwnerMention} at {FormatSlackTime(Issue.AcknowledgedAt.Value)}"));
				}
				else if (Issue.NominatedById == null)
				{
					Attachment.Blocks.Add(new SectionBlock($"Assigned to {OwnerMention}"));
				}
				else if (Issue.NominatedById == UserId)
				{
					Attachment.Blocks.Add(new SectionBlock($"You nominated {OwnerMention} to fix this issue."));
				}
				else
				{
					Attachment.Blocks.Add(new SectionBlock($"{OwnerMention} was nominated to fix this issue by {await FormatMentionAsync(Issue.NominatedById.Value)}"));
				}
			}
			else if (UserId != null)
			{
				IIssueSuspect? Suspect = Details.Suspects.FirstOrDefault(x => x.AuthorId == UserId);
				if (Suspect != null)
				{
					if (Suspect.DeclinedAt != null)
					{
						Attachment.Blocks.Add(new SectionBlock($":downvote: Declined at {FormatSlackTime(Suspect.DeclinedAt.Value)}"));
					}
					else
					{
						Attachment.Blocks.Add(new SectionBlock("Please check if any of your recently submitted changes have caused this issue."));

						ActionsBlock Actions = new ActionsBlock();
						Actions.AddButton("Will Fix", Value: $"issue_{Issue.Id}_accept_{UserId}", Style: ActionButton.ButtonStyle.Primary);
						Actions.AddButton("Not Me", Value: $"issue_{Issue.Id}_decline_{UserId}", Style: ActionButton.ButtonStyle.Danger);
						Attachment.Blocks.Add(Actions);
					}
				}
			}
			else if (Details.Suspects.Count > 0)
			{
				List<string> DeclinedLines = new List<string>();
				foreach (IIssueSuspect Suspect in Details.Suspects)
				{
					if (Suspect.DeclinedAt == null)
					{
						DeclinedLines.Add($":heavy_minus_sign: Ignored by {await FormatNameAsync(Suspect.AuthorId)} (CL {Suspect.Change})");
					}
					else
					{
						DeclinedLines.Add($":downvote: Declined by {await FormatNameAsync(Suspect.AuthorId)} at {FormatSlackTime(Suspect.DeclinedAt.Value)} (CL {Suspect.Change})");
					}
				}
				Attachment.Blocks.Add(new SectionBlock(String.Join("\n", DeclinedLines)));
			}

			await SendOrUpdateMessageAsync(Recipient, GetIssueEventId(Issue), UserId, Attachments: new[] { Attachment });
		}

		static string GetIssueEventId(IIssue Issue)
		{
			return $"issue_{Issue.Id}";
		}

		async Task<string> FormatNameAsync(UserId UserId)
		{
			IUser? User = await UserCollection.GetUserAsync(UserId);
			if (User == null)
			{
				return $"User {UserId}";
			}
			return User.Name;
		}

		async Task<string> FormatMentionAsync(UserId UserId)
		{
			IUser? User = await UserCollection.GetUserAsync(UserId);
			if (User == null)
			{
				return $"User {UserId}";
			}

			string? SlackUserId = await GetSlackUserId(User);
			if (SlackUserId == null)
			{
				return User.Login;
			}

			return $"<@{SlackUserId}>";
		}

		async Task HandleIssueResponseAsync(EventPayload Payload, ActionInfo Action)
		{
			string? UserName = Payload.User?.UserName;
			if(UserName == null)
			{
				Logger.LogWarning("No user for message payload: {Payload}", Payload);
				return;
			}
			if (Payload.ResponseUrl == null)
			{
				Logger.LogWarning("No response url for payload: {Payload}", Payload);
				return;
			}

			Match Match = Regex.Match(Action.Value ?? String.Empty, @"^issue_(\d+)_([a-zA-Z]+)_([a-fA-F0-9]{24})$");
			if (!Match.Success)
			{
				Logger.LogWarning("Could not match format of button action: {Action}", Action.Value);
				return;
			}

			int IssueId = int.Parse(Match.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture);
			string Verb = Match.Groups[2].Value;
			UserId UserId = Match.Groups[3].Value.ToObjectId<IUser>();
			Logger.LogInformation("Issue {IssueId}: {Action} from {SlackUser} ({UserId})", IssueId, Verb, UserName, UserId);

			if (String.Equals(Verb, "ack", StringComparison.Ordinal))
			{
				await IssueService.UpdateIssueAsync(IssueId, Acknowledged: true);
			}
			else if (String.Equals(Verb, "accept", StringComparison.Ordinal))
			{
				await IssueService.UpdateIssueAsync(IssueId, OwnerId: UserId, Acknowledged: true);
			}
			else if (String.Equals(Verb, "decline", StringComparison.Ordinal))
			{
				await IssueService.UpdateIssueAsync(IssueId, DeclinedById: UserId);
			}

			IIssue? NewIssue = await IssueService.GetIssueAsync(IssueId);
			if (NewIssue != null)
			{
				IUser? User = await UserCollection.GetUserAsync(UserId);
				if (User != null)
				{
					string? Recipient = await GetSlackUserId(User);
					if (Recipient != null)
					{
						IIssueDetails Details = await IssueService.GetIssueDetailsAsync(NewIssue);
						await SendIssueMessageAsync(Recipient, NewIssue, Details, UserId);
					}
				}
			}
		}

		static string FormatSlackTime(DateTimeOffset Time)
		{
			return $"<!date^{Time.ToUnixTimeSeconds()}^{{time}}|{Time}>";
		}

		#endregion

		#region Stream updates

		/// <inheritdoc/>
		public async Task NotifyConfigUpdateFailureAsync(string ErrorMessage, string FileName, int? Change = null, IUser? Author = null, string? Description = null)
		{
			Logger.LogInformation("Sending config update failure notification for {FileName} (change: {Change}, author: {UserId})", FileName, Change ?? -1, Author?.Id ?? UserId.Empty);

			string? SlackUserId = null;
			if (Author != null)
			{
				SlackUserId = await GetSlackUserId(Author);
				if (SlackUserId == null)
				{
					Logger.LogWarning("Unable to identify Slack user id for {UserId}", Author.Id);
				}
				else
				{
					Logger.LogInformation("Mappsed user {UserId} to Slack user {SlackUserId}", Author.Id, SlackUserId);
				}
			}

			if (SlackUserId != null)
			{
				await SendConfigUpdateFailureMessageAsync(SlackUserId, ErrorMessage, FileName, Change, SlackUserId, Description);
			}
			if (Settings.UpdateStreamsNotificationChannel != null)
			{
				await SendConfigUpdateFailureMessageAsync($"#{Settings.UpdateStreamsNotificationChannel}", ErrorMessage, FileName, Change, SlackUserId, Description);
			}
		}

		private Task SendConfigUpdateFailureMessageAsync(string Recipient, string ErrorMessage, string FileName, int? Change = null, string? Author = null, string? Description = null)
		{
			Color OutcomeColor = BlockKitAttachmentColors.Error;
			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.Color = OutcomeColor;
			Attachment.FallbackText = $"Update Failure: {FileName}";

			Attachment.Blocks.Add(new HeaderBlock($"Config Update Failure :rip:", false, true));

			Attachment.Blocks.Add(new SectionBlock($"Horde was unable to update {FileName}"));
			Attachment.Blocks.Add(new SectionBlock($"```{ErrorMessage}```"));
			if (Change != null)
			{
				if (Author != null)
				{
					Attachment.Blocks.Add(new SectionBlock($"Possibly due to CL: {Change.Value} by <@{Author}>"));
				}
				else
				{
					Attachment.Blocks.Add(new SectionBlock($"Possibly due to CL: {Change.Value} - (Could not determine author from P4 user)"));
				}
				if (Description != null)
				{
					Attachment.Blocks.Add(new SectionBlock($"```{Description}```"));
				}
			}

			return SendMessageAsync(Recipient, Attachments: new[] { Attachment });
		}

		#endregion

		#region Stream update (file)

		/// <inheritdoc/>
		public async Task NotifyStreamUpdateFailedAsync(FileSummary File)
		{
			Logger.LogDebug("Sending stream update failure notification for {File}", File.DepotPath);
			if (Settings.UpdateStreamsNotificationChannel != null)
			{
				await SendStreamUpdateFailureMessage($"#{Settings.UpdateStreamsNotificationChannel}", File);
			}
		}

		/// <summary>
		/// Creates a stream update failure message in relation to a file
		/// </summary>
		/// <param name="Recipient"></param>
		/// <param name="File">The file</param>
		/// <returns></returns>
		Task SendStreamUpdateFailureMessage(string Recipient, FileSummary File)
		{
			Color OutcomeColor = BlockKitAttachmentColors.Error;
			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.Color = OutcomeColor;
			Attachment.FallbackText = $"{File.DepotPath} - Update Failure";

			Attachment.Blocks.Add(new HeaderBlock($"Stream Update Failure :rip:", false, true));

			Attachment.Blocks.Add(new SectionBlock($"<!here> Horde was unable to update {File.DepotPath}"));
			Attachment.Blocks.Add(new SectionBlock($"```{File.Error}```"));

			return SendMessageAsync(Recipient, Attachments: new[] { Attachment });
		}

		#endregion

		#region Device notifications

		/// <inheritdoc/>
		public async Task NotifyDeviceServiceAsync(string Message, IDevice? Device = null, IDevicePool? Pool = null, IStream? Stream = null, IJob? Job = null, IJobStep? Step = null, INode? Node = null)
		{
			Logger.LogDebug("Sending device service failure notification for {DeviceName} in pool {PoolName}", Device?.Name, Pool?.Name);
			if (Settings.DeviceServiceNotificationChannel != null)
			{
				await SendDeviceServiceMessage($"#{Settings.DeviceServiceNotificationChannel}", Message, Device, Pool, Stream, Job, Step, Node);
			}
		}

		/// <summary>
		/// Creates a Slack message about a completed step job.
		/// </summary>
		/// <param name="Recipient"></param>
        /// <param name="Message"></param>
        /// <param name="Device"></param>
        /// <param name="Pool"></param>
		/// <param name="Stream"></param>
		/// <param name="Job">The job that contains the step that completed.</param>
		/// <param name="Step">The job step that completed.</param>
		/// <param name="Node">The node for the job step.</param>
		private Task SendDeviceServiceMessage(string Recipient, string Message, IDevice? Device = null, IDevicePool? Pool = null, IStream? Stream = null, IJob? Job = null, IJobStep? Step = null, INode? Node = null)
		{
			BlockKitAttachment Attachment = new BlockKitAttachment();

			// truncate message to avoid slack error on message length
			if (Message.Length > 150)
			{
				Message = Message.Substring(0, 146) + "...";
			}
            
            Attachment.FallbackText = $"{Message}";

			if (Device != null && Pool != null)
			{
                Attachment.FallbackText += $" - Device: {Device.Name} Pool: {Pool.Name}";
            }

			Attachment.Blocks.Add(new HeaderBlock($"{Message}", false, false));

			if (Stream != null && Job != null && Step != null && Node != null)
			{
				Uri JobStepLink = new Uri($"{Settings.DashboardUrl}job/{Job.Id}?step={Step.Id}");
				Uri JobStepLogLink = new Uri($"{Settings.DashboardUrl}log/{Step.LogId}");
				
				Attachment.FallbackText += $" - {Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - {Node.Name}";				
				Attachment.Blocks.Add(new SectionBlock($"*<{JobStepLink}|{Stream.Name} - {GetJobChangeText(Job)} - {Job.Name} - {Node.Name}>*"));
				Attachment.Blocks.Add(new SectionBlock($"<{JobStepLogLink}|View Job Step Log>"));
			}
			else
			{				
				Attachment.FallbackText += " - No job information (Gauntlet might need to be updated in stream)";				
				Attachment.Blocks.Add(new SectionBlock("*No job information (Gauntlet might need to be updated in stream)*"));
			}

			return SendMessageAsync(Recipient, Attachments: new[] { Attachment });
		}
		
		#endregion


		const int MaxJobStepEvents = 5;

		static string GetJobChangeText(IJob Job)
		{
			if (Job.PreflightChange == 0)
			{
				return $"CL {Job.Change}";
			}
			else
			{
				return $"Preflight CL {Job.PreflightChange} against CL {Job.Change}";
			}
		}

		static bool ShouldUpdateUser(SlackUser? Document)
		{
			if(Document == null || Document.Version < SlackUser.CurrentVersion)
			{
				return true;
			}

			TimeSpan ExpiryTime;
			if (Document.SlackUserId == null)
			{
				ExpiryTime = TimeSpan.FromMinutes(10.0);
			}
			else
			{
				ExpiryTime = TimeSpan.FromDays(1.0);
			}
			return Document.Time + ExpiryTime < DateTime.UtcNow;
		}

		private async Task<string?> GetSlackUserId(IUser User)
		{
			return (await GetSlackUser(User))?.SlackUserId;
		}

		private async Task<SlackUser?> GetSlackUser(IUser User)
		{
			string? Email = User.Email;
			if (Email == null)
			{
				Logger.LogWarning("Unable to find Slack user id for {UserId} ({Name}): No email address in user profile", User.Id, User.Name);
				return null;
			}

			SlackUser? UserDocument;
			if (!UserCache.TryGetValue(Email, out UserDocument))
			{
				UserDocument = await SlackUsers.Find(x => x.Id == User.Id).FirstOrDefaultAsync();
				if (UserDocument == null || ShouldUpdateUser(UserDocument))
				{
					UserInfo? UserInfo = await GetSlackUserInfoByEmail(Email);
					if (UserDocument == null || UserInfo != null)
					{
						UserDocument = new SlackUser(User.Id, UserInfo);
						await SlackUsers.ReplaceOneAsync(x => x.Id == User.Id, UserDocument, new ReplaceOptions { IsUpsert = true });
					}
				}
				using (ICacheEntry Entry = UserCache.CreateEntry(Email))
				{
					Entry.SlidingExpiration = TimeSpan.FromMinutes(10.0);
					Entry.Value = UserDocument;
				}
			}

			return UserDocument;
		}

		private async Task<UserInfo?> GetSlackUserInfoByEmail(string Email)
		{
			using HttpClient Client = new HttpClient();

			using HttpRequestMessage GetUserIdRequest = new HttpRequestMessage(HttpMethod.Post, $"https://slack.com/api/users.lookupByEmail?email={Email}");
			GetUserIdRequest.Headers.Add("Authorization", $"Bearer {Settings.SlackToken ?? ""}");

			HttpResponseMessage ResponseMessage = await Client.SendAsync(GetUserIdRequest);
			byte[] ResponseData = await ResponseMessage.Content.ReadAsByteArrayAsync();

			UserResponse UserResponse = JsonSerializer.Deserialize<UserResponse>(ResponseData)!;
			if(!UserResponse.Ok || UserResponse.User == null)
			{
				Logger.LogWarning("Unable to find Slack user id for {Email}: {Response}", Email, Encoding.UTF8.GetString(ResponseData));
				return null;
			}

			return UserResponse.User;
		}

		class SlackResponse
		{
			[JsonPropertyName("ok")]
			public bool Ok { get; set; }

			[JsonPropertyName("error")]
			public string? Error { get; set; }
		}

		class PostMessageResponse : SlackResponse
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("ts")]
			public string? Ts { get; set; }
		}

		private async Task SendMessageAsync(string Recipient, string? Text = null, BlockBase[]? Blocks = null, BlockKitAttachment[]? Attachments = null)
		{
			if (AllowUsers != null && !AllowUsers.Contains(Recipient))
			{
				Logger.LogDebug("Suppressing message to {Recipient}: {Text}", Recipient, Text);
				return;
			}

			BlockKitMessage Message = new BlockKitMessage();
			Message.Channel = Recipient;
			Message.Text = Text;
			if (Blocks != null)
			{
				Message.Blocks.AddRange(Blocks);
			}
			if (Attachments != null)
			{
				Message.Attachments.AddRange(Attachments);
			}

			await SendRequestAsync<PostMessageResponse>(PostMessageUrl, Message);
		}

		private async Task SendOrUpdateMessageAsync(string Recipient, string EventId, UserId? UserId, string? Text = null, BlockBase[]? Blocks = null, BlockKitAttachment[]? Attachments = null)
		{
			if (AllowUsers != null && !AllowUsers.Contains(Recipient))
			{
				Logger.LogDebug("Suppressing message to {Recipient}: {Text}", Recipient, Text);
				return;
			}

			BlockKitMessage Message = new BlockKitMessage();
			Message.Text = Text;
			if (Blocks != null)
			{
				Message.Blocks.AddRange(Blocks);
			}
			if (Attachments != null)
			{
				Message.Attachments.AddRange(Attachments);
			}

			string RequestDigest = ContentHash.MD5(JsonSerializer.Serialize(Message, new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull })).ToString();

			(MessageStateDocument State, bool IsNew) = await AddOrUpdateMessageStateAsync(Recipient, EventId, UserId, RequestDigest);
			if (IsNew)
			{
				Logger.LogInformation("Sending new slack message to {SlackUser} (msg: {MessageId})", Recipient, State.Id);
				Message.Channel = Recipient;
				Message.Ts = null;

				PostMessageResponse Response = await SendRequestAsync<PostMessageResponse>(PostMessageUrl, Message);
				if (String.IsNullOrEmpty(Response.Ts) || String.IsNullOrEmpty(Response.Channel))
				{
					Logger.LogWarning("Missing 'ts' or 'channel' field on slack response");
				}
				await SetMessageTimestampAsync(State.Id, Response.Channel ?? String.Empty, Response.Ts ?? String.Empty);
			}
			else if (!String.IsNullOrEmpty(State.Ts))
			{
				Logger.LogInformation("Updating existing slack message {MessageId} for user {SlackUser} ({Channel}, {MessageTs})", State.Id, Recipient, State.Channel, State.Ts);
				Message.Channel = State.Channel;
				Message.Ts = State.Ts;
				await SendRequestAsync<PostMessageResponse>(UpdateMessageUrl, Message);
			}
		}

		private async Task<TResponse> SendRequestAsync<TResponse>(string RequestUrl, object Request) where TResponse : SlackResponse
		{
			using (HttpClient Client = new HttpClient())
			{
				using (HttpRequestMessage SendMessageRequest = new HttpRequestMessage(HttpMethod.Post, RequestUrl))
				{
					string RequestJson = JsonSerializer.Serialize(Request, new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull });
					using (StringContent MessageContent = new StringContent(RequestJson, Encoding.UTF8, "application/json"))
					{
						SendMessageRequest.Content = MessageContent;
						SendMessageRequest.Headers.Add("Authorization", $"Bearer {Settings.SlackToken ?? ""}");

						HttpResponseMessage Response = await Client.SendAsync(SendMessageRequest);
						byte[] ResponseBytes = await Response.Content.ReadAsByteArrayAsync();

						TResponse ResponseObject = JsonSerializer.Deserialize<TResponse>(ResponseBytes)!;
						if(!ResponseObject.Ok)
						{
							Logger.LogError("Failed to send Slack message ({Error}). Request: {Request}. Response: {Response}", ResponseObject.Error, RequestJson, Encoding.UTF8.GetString(ResponseBytes));
						}

						return ResponseObject;
					}
				}
			}
		}

		/// <inheritdoc/>
		protected async override Task ExecuteAsync(CancellationToken StoppingToken)
		{
			if (!String.IsNullOrEmpty(Settings.SlackSocketToken))
			{
				while (!StoppingToken.IsCancellationRequested)
				{
					try
					{
						Uri? WebSocketUrl = await GetWebSocketUrlAsync(StoppingToken);
						if (WebSocketUrl == null)
						{
							await Task.Delay(TimeSpan.FromSeconds(5.0), StoppingToken);
							continue;
						}
						await HandleSocketAsync(WebSocketUrl, StoppingToken);
					}
					catch (OperationCanceledException)
					{
						break;
					}
					catch (Exception Ex)
					{
						Logger.LogError(Ex, "Exception while updating Slack socket");
						await Task.Delay(TimeSpan.FromSeconds(5.0), StoppingToken);
					}
				}
			}
		}

		/// <summary>
		/// Get the url for opening a websocket to Slack
		/// </summary>
		/// <param name="StoppingToken"></param>
		/// <returns></returns>
		private async Task<Uri?> GetWebSocketUrlAsync(CancellationToken StoppingToken)
		{
			using HttpClient Client = new HttpClient();
			Client.DefaultRequestHeaders.Add("Authorization", $"Bearer {Settings.SlackSocketToken}");

			using FormUrlEncodedContent Content = new FormUrlEncodedContent(Array.Empty<KeyValuePair<string?, string?>>());
			HttpResponseMessage Response = await Client.PostAsync(new Uri("https://slack.com/api/apps.connections.open"), Content, StoppingToken);

			byte[] ResponseData = await Response.Content.ReadAsByteArrayAsync();

			SocketResponse SocketResponse = JsonSerializer.Deserialize<SocketResponse>(ResponseData)!;
			if (!SocketResponse.Ok)
			{
				Logger.LogWarning("Unable to get websocket url: {Response}", Encoding.UTF8.GetString(ResponseData));
				return null;
			}

			return SocketResponse.Url;
		}

		/// <summary>
		/// Handle the lifetime of a websocket connection
		/// </summary>
		/// <param name="SocketUrl"></param>
		/// <param name="StoppingToken"></param>
		/// <returns></returns>
		private async Task HandleSocketAsync(Uri SocketUrl, CancellationToken StoppingToken)
		{
			using ClientWebSocket Socket = new ClientWebSocket();
			await Socket.ConnectAsync(SocketUrl, StoppingToken);

			byte[] Buffer = new byte[2048];
			while (!StoppingToken.IsCancellationRequested)
			{
				// Read the next message
				int Length = 0;
				for (; ; )
				{
					if (Length == Buffer.Length)
					{
						Array.Resize(ref Buffer, Buffer.Length + 2048);
					}

					WebSocketReceiveResult Result = await Socket.ReceiveAsync(new ArraySegment<byte>(Buffer, Length, Buffer.Length - Length), StoppingToken);
					if (Result.MessageType == WebSocketMessageType.Close)
					{
						return;
					}
					Length += Result.Count;

					if (Result.EndOfMessage)
					{
						break;
					}
				}

				// Get the message data
				Logger.LogInformation("Slack event: {Message}", Encoding.UTF8.GetString(Buffer, 0, Length));
				EventMessage EventMessage = JsonSerializer.Deserialize<EventMessage>(Buffer.AsSpan(0, Length))!;

				// Acknowledge the message
				if (EventMessage.EnvelopeId != null)
				{
					object Response = new { EventMessage.EnvelopeId };
					await Socket.SendAsync(JsonSerializer.SerializeToUtf8Bytes(Response), WebSocketMessageType.Text, true, StoppingToken);
				}

				// Handle the message type
				if (EventMessage.Type != null)
				{
					string Type = EventMessage.Type;
					if (Type.Equals("disconnect", StringComparison.Ordinal))
					{
						break;
					}
					else if (Type.Equals("interactive", StringComparison.Ordinal))
					{
						await HandleInteractionMessage(EventMessage);
					}
					else
					{
						Logger.LogDebug("Unhandled event type ({Type})", Type);
					}
				}
			}
		}

		/// <summary>
		/// Handle a button being clicked
		/// </summary>
		/// <param name="Message">The event message</param>
		/// <returns></returns>
		private async Task HandleInteractionMessage(EventMessage Message)
		{
			if (Message.Payload != null && Message.Payload.User != null && Message.Payload.User.UserName != null && String.Equals(Message.Payload.Type, "block_actions", StringComparison.Ordinal))
			{
				foreach (ActionInfo Action in Message.Payload.Actions)
				{
					if (Action.Value != null)
					{
						if (Action.Value.StartsWith("issue_", StringComparison.Ordinal))
						{
							await HandleIssueResponseAsync(Message.Payload, Action);
						}
					}
				}
			}
		}
	}
}
