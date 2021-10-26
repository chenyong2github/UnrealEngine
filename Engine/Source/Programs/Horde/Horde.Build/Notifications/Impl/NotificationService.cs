// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Mail;
using System.Security.Claims;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using HordeServer.Utilities.BlockKit;
using HordeServer.Utilities.Slack.BlockKit;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using StatsdClient;
using JsonSerializer = System.Text.Json.JsonSerializer;
using HordeServer.Notifications;
using HordeServer.Services;

namespace HordeServer.Notifications.Impl
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Wraps funtionality for delivering notifications.
	/// </summary>
	public class NotificationService : BackgroundService, INotificationService
	{
		/// <summary>
		/// The available notification sinks
		/// </summary>
		private List<INotificationSink> Sinks;

		/// <summary>
		/// Collection of subscriptions
		/// </summary>
		private readonly ISubscriptionCollection SubscriptionCollection;

		/// <summary>
		/// Collection of notification request documents.
		/// </summary>
		private readonly INotificationTriggerCollection TriggerCollection;

		/// <summary>
		/// Instance of the <see cref="GraphCollection"/>.
		/// </summary>
		private readonly IGraphCollection GraphCollection;

		/// <summary>
		/// 
		/// </summary>
		private readonly IUserCollection UserCollection;

		/// <summary>
		/// Job service instance
		/// </summary>
		private readonly JobService JobService;

		/// <summary>
		/// Instance of the <see cref="StreamService"/>.
		/// </summary>
		private readonly StreamService StreamService;

		/// <summary>
		/// 
		/// </summary>
		private readonly IIssueService IssueService;

		/// <summary>
		/// Instance of the <see cref="LogFileService"/>.
		/// </summary>
		private readonly ILogFileService LogFileService;
		
		/// <summary>
		/// Instance of the <see cref="IDogStatsd"/>.
		/// </summary>
		private readonly IDogStatsd DogStatsd;
		
		/// <summary>
		/// Settings for the application.
		/// </summary>
		private readonly IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// List of asychronous tasks currently executing
		/// </summary>
		private readonly ConcurrentQueue<Task> NewTasks = new ConcurrentQueue<Task>();

		/// <summary>
		/// Set when there are new tasks to wait for
		/// </summary>
		private AsyncEvent NewTaskEvent = new AsyncEvent();

		/// <summary>
		/// Settings for the application.
		/// </summary>
		private readonly ILogger<NotificationService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public NotificationService(IEnumerable<INotificationSink> Sinks, IOptionsMonitor<ServerSettings> Settings, ILogger<NotificationService> Logger, IGraphCollection GraphCollection, ISubscriptionCollection SubscriptionCollection, INotificationTriggerCollection TriggerCollection, IUserCollection UserCollection, JobService JobService, StreamService StreamService, IIssueService IssueService, ILogFileService LogFileService, IDogStatsd DogStatsd)
		{
			this.Sinks = Sinks.ToList();
			this.Settings = Settings;
			this.Logger = Logger;
			this.GraphCollection = GraphCollection;
			this.SubscriptionCollection = SubscriptionCollection;
			this.TriggerCollection = TriggerCollection;
			this.UserCollection = UserCollection;
			this.JobService = JobService;
			this.StreamService = StreamService;
			this.IssueService = IssueService;
			this.LogFileService = LogFileService;
			this.DogStatsd = DogStatsd;

			IssueService.OnIssueUpdated += NotifyIssueUpdated;
			JobService.OnJobStepComplete += NotifyJobStepComplete;
			JobService.OnLabelUpdate += NotifyLabelUpdate;
		}

		/// <inheritdoc/>
		public override void Dispose()
		{
			base.Dispose();

			IssueService.OnIssueUpdated -= NotifyIssueUpdated;
			JobService.OnJobStepComplete -= NotifyJobStepComplete;
			JobService.OnLabelUpdate -= NotifyLabelUpdate;

			GC.SuppressFinalize(this);
		}

		/// <inheritdoc/>
		public async Task<bool> UpdateSubscriptionsAsync(ObjectId TriggerId, ClaimsPrincipal User, bool? Email, bool? Slack)
		{
			UserId? UserId = User.GetUserId();
			if (UserId == null)
			{
				Logger.LogWarning("Unable to find username for principal {User}", User.Identity?.Name);
				return false;
			}

			INotificationTrigger Trigger = await TriggerCollection.FindOrAddAsync(TriggerId);
			await TriggerCollection.UpdateSubscriptionsAsync(Trigger, UserId.Value, Email, Slack);
			return true;
		}

		/// <inheritdoc/>
		public async Task<INotificationSubscription?> GetSubscriptionsAsync(ObjectId TriggerId, ClaimsPrincipal User)
		{
			UserId? UserId = User.GetUserId();
			if (UserId == null)
			{
				return null;
			}

			INotificationTrigger? Trigger = await TriggerCollection.GetAsync(TriggerId);
			if(Trigger == null)
			{
				return null;
			}

			return Trigger.Subscriptions.FirstOrDefault(x => x.UserId == UserId.Value);
		}

		/// <inheritdoc/>
		public void NotifyJobStepComplete(IJob Job, IGraph Graph, SubResourceId BatchId, SubResourceId StepId)
		{
			// Enqueue job step complete notifications if needed
			if (Job.TryGetStep(BatchId, StepId, out IJobStep? Step))
			{
				Logger.LogInformation("Queuing step notifications for {JobId}:{BatchId}:{StepId}", Job.Id, BatchId, StepId);
				EnqueueTask(() => SendJobStepNotificationsAsync(Job, BatchId, StepId));
			}

			// Enqueue job complete notifications if needed
			if (Job.GetState() == JobState.Complete)
			{
				Logger.LogInformation("Queuing job notifications for {JobId}:{BatchId}:{StepId}", Job.Id, BatchId, StepId);
				EnqueueTask(() => SendJobNotificationsAsync(Job, Graph));
				EnqueueTask(() => RecordJobCompleteMetrics(Job));
			}
		}

		/// <inheritdoc/>
		public void NotifyLabelUpdate(IJob Job, IReadOnlyList<(LabelState, LabelOutcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates)
		{
			// If job has any label trigger IDs, send label complete notifications if needed
			for (int Idx = 0; Idx < OldLabelStates.Count && Idx < NewLabelStates.Count; Idx++)
			{
				if (OldLabelStates[Idx] != NewLabelStates[Idx])
				{
					EnqueueTask(() => SendAllLabelNotificationsAsync(Job, OldLabelStates, NewLabelStates));
					break;
				}
			}
		}

		/// <inheritdoc/>
		public void NotifyIssueUpdated(IIssue Issue)
		{
			Logger.LogInformation("Issue {IssueId} updated", Issue.Id);
			EnqueueTasks(Sink => Sink.NotifyIssueUpdatedAsync(Issue));
		}

		/// <inheritdoc/>
		public void NotifyConfigUpdateFailure(string ErrorMessage, string FileName, int? Change = null, IUser? Author = null, string? Description = null)
		{
			EnqueueTasks(Sink => Sink.NotifyConfigUpdateFailureAsync(ErrorMessage, FileName, Change, Author, Description));
		}

		/// <inheritdoc/>
		public void NotifyDeviceService(string Message, IDevice? Device = null, IDevicePool? Pool = null, IStream? Stream = null, IJob? Job = null, IJobStep? Step = null, INode? Node = null)
		{
			EnqueueTasks(Sink => Sink.NotifyDeviceServiceAsync(Message, Device, Pool, Stream, Job, Step, Node));
		}

		/// <summary>
		/// Enqueues an async task
		/// </summary>
		/// <param name="TaskFunc">Function to generate an async task</param>
		void EnqueueTask(Func<Task> TaskFunc)
		{
			NewTasks.Enqueue(Task.Run(TaskFunc));
		}

		/// <summary>
		/// Enqueues an async task
		/// </summary>
		/// <param name="TaskFunc">Function to generate an async task</param>
		void EnqueueTasks(Func<INotificationSink, Task> TaskFunc)
		{
			foreach (INotificationSink Sink in Sinks)
			{
				EnqueueTask(() => TaskFunc(Sink));
			}
		}

		/// <inheritdoc/>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		protected async override Task ExecuteAsync(CancellationToken StoppingToken)
		{
			// This background service just waits for tasks to finish and prints any exception info. The only reason to do this is to
			// ensure we finish processing everything before shutdown.
			using (CancellationTask StoppingTask = new CancellationTask(StoppingToken))
			{
				List<Task> Tasks = new List<Task>();
				Tasks.Add(NewTaskEvent.Task);

				for (; ; )
				{
					// Add any new tasks to be monitored
					Task? NewTask;
					while (NewTasks.TryDequeue(out NewTask))
					{
						Tasks.Add(NewTask);
					}

					// If we don't have any
					if (Tasks.Count == 1)
					{
						await Task.WhenAny(NewTaskEvent.Task, StoppingTask.Task);
						if (StoppingToken.IsCancellationRequested)
						{
							break;
						}
					}
					else
					{
						// Wait for something to finish
						Task Task = await Task.WhenAny(Tasks);
						if (Task == NewTaskEvent.Task)
						{
							NewTaskEvent = new AsyncEvent();
							Tasks[0] = NewTaskEvent.Task;
						}
						else
						{
							try
							{
								await Task;
							}
							catch (Exception Ex)
							{
								Logger.LogError(Ex, "Exception while executing notification");
							}
							Tasks.Remove(Task);
						}
					}
				}
			}
		}

		internal Task ExecuteBackgroundForTest(CancellationToken StoppingToken)
		{
			return ExecuteAsync(StoppingToken);
		}

		/// <summary>
		/// Gets the <see cref="INotificationTrigger"/> for a given trigger ID, if any.
		/// </summary>
		/// <param name="TriggerId"></param>
		/// <param name="bFireTrigger">If true, the trigger is fired and cannot be reused</param>
		/// <returns></returns>
		private async Task<INotificationTrigger?> GetNotificationTrigger(ObjectId? TriggerId, bool bFireTrigger)
		{
			if (TriggerId == null)
			{
				return null;
			}

			INotificationTrigger? Trigger = await TriggerCollection.GetAsync(TriggerId.Value);
			if (Trigger == null)
			{
				return null;
			}

			return bFireTrigger ? await TriggerCollection.FireAsync(Trigger) : Trigger;
		}
	
		private async Task SendJobNotificationsAsync(IJob Job, IGraph Graph)
		{
			using IDisposable Scope = Logger.BeginScope("Sending notifications for job {JobId}", Job.Id);

			Job.GetJobState(Job.GetStepForNodeMap(), out _, out LabelOutcome Outcome);
			JobCompleteEventRecord JobCompleteEvent = new JobCompleteEventRecord(Job.StreamId, Job.TemplateId, Outcome);

			IStream? JobStream = await StreamService.GetStreamAsync(Job.StreamId);
			if (JobStream == null)
			{
				Logger.LogError("Unable to get stream {StreamId}", Job.StreamId);
				return;
			}

			List<IUser> UsersToNotify = await GetUsersToNotify(JobCompleteEvent, Job.NotificationTriggerId, true);
			foreach (IUser UserToNotify in UsersToNotify)
			{
				if(Job.PreflightChange != 0)
				{
					if(UserToNotify.Id != Job.StartedByUserId)
					{
						continue;
					}
				}
				EnqueueTasks(Sink => Sink.NotifyJobCompleteAsync(UserToNotify, JobStream, Job, Graph, Outcome));
			}

			if (Job.PreflightChange == 0)
			{
				EnqueueTasks(Sink => Sink.NotifyJobCompleteAsync(JobStream, Job, Graph, Outcome));
			}

			Logger.LogDebug("Finished sending notifications for job {JobId}", Job.Id);
		}

		private Task RecordJobCompleteMetrics(IJob Job)
		{
			void RecordMetric(string Type, JobStepOutcome Outcome, DateTimeOffset? StartTime, DateTimeOffset? FinishTime)
			{
				string OutcomeStr = Outcome switch
				{
					JobStepOutcome.Unspecified => "unspecified",
					JobStepOutcome.Failure => "failure",
					JobStepOutcome.Warnings => "warnings",
					JobStepOutcome.Success => "success",
					_ => "unspecified"
				};
				
				string[] Tags = {"stream:" + Job.StreamId, "template:" + Job.TemplateId};
				DogStatsd.Increment($"horde.{Type}.{OutcomeStr}.count", 1, tags: Tags);

				if (StartTime == null || FinishTime == null)
				{
					Logger.LogDebug("Completed job or step is missing start or finish time, cannot record duration metric. Job ID={JobId}", Job.Id);
					return;
				}

				TimeSpan Duration = FinishTime.Value - StartTime.Value;
				DogStatsd.Timer($"horde.{Type}.{OutcomeStr}.duration", Duration.TotalSeconds, tags: Tags);
			}

			JobStepOutcome JobOutcome = Job.Batches.SelectMany(x => x.Steps).Min(x => x.Outcome);
			DateTime? StartTime = Job.Batches.Select(x => x.StartTimeUtc).Min();
			DateTime? FinishTime = Job.Batches.Select(x => x.FinishTimeUtc).Max();
			RecordMetric("job", JobOutcome, StartTime, FinishTime);

			// TODO: record metrics for individual steps
			// foreach (IJobStepBatch Batch in Job.Batches)
			// {
			// 	foreach (IJobStep Step in Batch.Steps)
			// 	{
			// 	}
			// }

			return Task.CompletedTask;
		}

		private async Task<List<IUser>> GetUsersToNotify(EventRecord? Event, ObjectId? NotificationTriggerId, bool bFireTrigger)
		{
			List<UserId> UserIds = new List<UserId>();

			// Find the notifications for all steps of this type
			if (Event != null)
			{
				List<ISubscription> Subscriptions = await SubscriptionCollection.FindSubscribersAsync(Event);
				foreach (ISubscription Subscription in Subscriptions)
				{
					if (Subscription.NotificationType == NotificationType.Slack)
					{
						UserIds.Add(Subscription.UserId);
					}
				}
			}

			// Find the notifications for this particular step
			if (NotificationTriggerId != null)
			{
				INotificationTrigger? Trigger = await GetNotificationTrigger(NotificationTriggerId, bFireTrigger);
				if (Trigger != null)
				{
					foreach (INotificationSubscription Subscription in Trigger.Subscriptions)
					{
						if (Subscription.Email)
						{
							// TODO?
						}
						if (Subscription.Slack)
						{
							UserIds.Add(Subscription.UserId);
						}
					}
				}
			}
			return await UserCollection.FindUsersAsync(UserIds);
		}

		private async Task SendJobStepNotificationsAsync(IJob Job, SubResourceId BatchId, SubResourceId StepId)
		{
			using IDisposable Scope = Logger.BeginScope("Sending notifications for step {JobId}:{BatchId}:{StepId}", Job.Id, BatchId, StepId);

			IJobStepBatch? Batch;
			if(!Job.TryGetBatch(BatchId, out Batch))
			{
				Logger.LogError("Unable to find batch {BatchId} in job {JobId}", BatchId, Job.Id);
				return;
			}

			IJobStep? Step;
			if (!Batch.TryGetStep(StepId, out Step))
			{
				Logger.LogError("Unable to find step {StepId} in batch {JobId}:{BatchId}", StepId, Job.Id, BatchId);
				return;
			}

			IGraph JobGraph = await GraphCollection.GetAsync(Job.GraphHash);
			INode Node = JobGraph.GetNode(new NodeRef(Batch.GroupIdx, Step.NodeIdx));

			// Find the notifications for this particular step
			EventRecord EventRecord = new StepCompleteEventRecord(Job.StreamId, Job.TemplateId, Node.Name, Step.Outcome);

			List<IUser> UsersToNotify = await GetUsersToNotify(EventRecord, Step.NotificationTriggerId, true);
			if(UsersToNotify.Count == 0)
			{
				Logger.LogInformation("No users to notify for step {JobId}:{BatchId}:{StepId}", Job.Id, BatchId, StepId);
				return;
			}

			IStream? JobStream = await StreamService.GetStreamAsync(Job.StreamId);
			if (JobStream == null)
			{
				Logger.LogError("Unable to find stream {StreamId}", Job.StreamId);
				return;
			}

			if(Step.LogId == null)
			{
				Logger.LogError("Step does not have a log file");
				return;
			}

			ILogFile? LogFile = await LogFileService.GetLogFileAsync(Step.LogId.Value);
			if(LogFile == null)
			{
				Logger.LogError("Step does not have a log file");
				return;
			}

			List<ILogEvent> JobStepEvents = await LogFileService.FindLogEventsAsync(LogFile);
			List<ILogEventData> JobStepEventData = new List<ILogEventData>();
			foreach (ILogEvent Event in JobStepEvents)
			{
				ILogEventData EventData = await LogFileService.GetEventDataAsync(LogFile, Event.LineIndex, Event.LineCount);
				JobStepEventData.Add(EventData);
			}

			foreach (IUser SlackUser in UsersToNotify)
			{
				if(Job.PreflightChange != 0)
				{
					if(SlackUser.Id != Job.StartedByUserId)
					{
						continue;
					}
				}
				EnqueueTasks(Sink => Sink.NotifyJobStepCompleteAsync(SlackUser, JobStream, Job, Batch, Step, Node, JobStepEventData));
			}
			Logger.LogDebug("Finished sending notifications for step {JobId}:{BatchId}:{StepId}", Job.Id, BatchId, StepId);
		}

		private async Task SendAllLabelNotificationsAsync(IJob Job, IReadOnlyList<(LabelState State, LabelOutcome Outcome)> OldLabelStates, IReadOnlyList<(LabelState, LabelOutcome)> NewLabelStates)
		{
			IStream? Stream = await StreamService.GetStreamAsync(Job.StreamId);
			if (Stream == null)
			{
				Logger.LogError("Unable to find stream {StreamId} for job {JobId}", Job.StreamId, Job.Id);
				return;
			}

			IGraph? Graph = await GraphCollection.GetAsync(Job.GraphHash);
			if (Graph == null)
			{
				Logger.LogError("Unable to find graph {GraphHash} for job {JobId}", Job.GraphHash, Job.Id);
				return;
			}

			IReadOnlyDictionary<NodeRef, IJobStep> StepForNode = Job.GetStepForNodeMap();
			for (int LabelIdx = 0; LabelIdx < Graph.Labels.Count; ++LabelIdx)
			{
				(LabelState State, LabelOutcome Outcome) OldLabel = OldLabelStates[LabelIdx];
				(LabelState State, LabelOutcome Outcome) NewLabel = NewLabelStates[LabelIdx];
				if (OldLabel != NewLabel)
				{
					// If the state transitioned from Unspecified to Running, don't update unless the outcome also changed.
					if (OldLabel.State == LabelState.Unspecified && NewLabel.State == LabelState.Running && OldLabel.Outcome == NewLabel.Outcome)
					{
						continue;
					}

					// If the label isn't complete, don't report on outcome changing to success, this will be reported when the label state becomes complete.
					if (NewLabel.State != LabelState.Complete && NewLabel.Outcome == LabelOutcome.Success)
					{
						return;
					}

					ILabel Label = Graph.Labels[LabelIdx];

					EventRecord? EventId;
					if (String.IsNullOrEmpty(Label.DashboardName))
					{
						EventId = null;
					}
					else
					{
						EventId = new LabelCompleteEventRecord(Job.StreamId, Job.TemplateId, Label.DashboardCategory, Label.DashboardName, NewLabel.Outcome);
					}

					ObjectId? TriggerId;
					if (Job.LabelIdxToTriggerId.TryGetValue(LabelIdx, out ObjectId NewTriggerId))
					{
						TriggerId = NewTriggerId;
					}
					else
					{
						TriggerId = null;
					}

					bool bFireTrigger = NewLabel.State == LabelState.Complete;

					List<IUser> UsersToNotify = await GetUsersToNotify(EventId, TriggerId, bFireTrigger);
					if (UsersToNotify.Count > 0)
					{
						SendLabelUpdateNotifications(Job, Stream, Graph, StepForNode, Graph.Labels[LabelIdx], LabelIdx, NewLabel.Outcome, UsersToNotify);
					}
					else
					{
						Logger.LogDebug("No users to notify for label {DashboardName}/{UgsName} in job {JobId}", Graph.Labels[LabelIdx].DashboardName, Graph.Labels[LabelIdx].UgsName, Job.Id);
					}
				}
			}
		}

		private void SendLabelUpdateNotifications(IJob Job, IStream Stream, IGraph Graph, IReadOnlyDictionary<NodeRef, IJobStep> StepForNode, ILabel Label, int LabelIdx, LabelOutcome Outcome, List<IUser> SlackUsers)
		{
			List<(string, JobStepOutcome, Uri)> StepData = new List<(string, JobStepOutcome, Uri)>();
			if (Outcome != LabelOutcome.Success)
			{
				foreach (NodeRef IncludedNodeRef in Label.IncludedNodes)
				{
					INode IncludedNode = Graph.GetNode(IncludedNodeRef);
					IJobStep IncludedStep = StepForNode[IncludedNodeRef];
					StepData.Add((IncludedNode.Name, IncludedStep.Outcome, new Uri($"{Settings.CurrentValue.DashboardUrl}job/{Job.Id}?step={IncludedStep.Id}")));
				}
			}

			foreach (IUser SlackUser in SlackUsers)
			{
				EnqueueTasks(Sink => Sink.NotifyLabelCompleteAsync(SlackUser, Job, Stream, Label, LabelIdx, Outcome, StepData));
			}

			Logger.LogDebug("Finished sending label notifications for label {DashboardName}/{UgsName} in job {JobId}", Label.DashboardName, Label.UgsName, Job.Id);
		}
	}
}
