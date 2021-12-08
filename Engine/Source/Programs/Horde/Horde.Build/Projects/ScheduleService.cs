// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.S3.Model.Internal.MarshallTransformations;
using EpicGames.Core;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Controllers;
using HordeServer.Models;
using HordeServer.Utilities;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Server.Kestrel.Core.Features;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Driver;
using OpenTracing;
using OpenTracing.Util;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Security.Claims;
using System.Threading;
using System.Threading.Tasks;
using TimeZoneConverter;
using StackExchange.Redis;
using EpicGames.Redis;
using EpicGames.Serialization;
using System.Text.RegularExpressions;

namespace HordeServer.Services
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Manipulates schedule instances
	/// </summary>
	public sealed class ScheduleService : IHostedService, IDisposable
	{
		[RedisConverter(typeof(RedisCbConverter<QueueItem>))]
		class QueueItem
		{
			[CbField("sid")]
			public StreamId StreamId { get; set; }

			[CbField("tid")]
			public TemplateRefId TemplateId { get; set; }

			public QueueItem()
			{
			}

			public QueueItem(StreamId StreamId, TemplateRefId TemplateId)
			{
				this.StreamId = StreamId;
				this.TemplateId = TemplateId;
			}

			public static DateTime GetTimeFromScore(double Score) => DateTime.UnixEpoch + TimeSpan.FromSeconds(Score);
			public static double GetScoreFromTime(DateTime Time) => (Time.ToUniversalTime() - DateTime.UnixEpoch).TotalSeconds;
		}

		IDowntimeService DowntimeService;
		IGraphCollection Graphs;
		IPerforceService Perforce;
		IJobCollection JobCollection;
		JobService JobService;
		StreamService StreamService;
		ITemplateCollection TemplateCollection;
		TimeZoneInfo TimeZone;
		IClock Clock;
		ILogger Logger;
		RedisService Redis;
		RedisSortedSet<QueueItem> Queue;
		BackgroundTask BackgroundTask;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Redis"></param>
		/// <param name="Graphs">Collection of graph documents</param>
		/// <param name="DowntimeService">The downtime service</param>
		/// <param name="Perforce">The Perforce service</param>
		/// <param name="JobCollection">Job collection singleton</param>
		/// <param name="JobService">The job service instance</param>
		/// <param name="StreamService">The stream service instance</param>
		/// <param name="TemplateCollection">The template service instance</param>
		/// <param name="Clock"></param>
		/// <param name="Settings">Settings for the server</param>
		/// <param name="Logger">Logging instance</param>
		public ScheduleService(RedisService Redis, IGraphCollection Graphs, IDowntimeService DowntimeService, IPerforceService Perforce, IJobCollection JobCollection, JobService JobService, StreamService StreamService, ITemplateCollection TemplateCollection, IClock Clock, IOptionsMonitor<ServerSettings> Settings, ILogger<ScheduleService> Logger)
		{
			this.Graphs = Graphs;
			this.DowntimeService = DowntimeService;
			this.Perforce = Perforce;
			this.JobCollection = JobCollection;
			this.JobService = JobService;
			this.StreamService = StreamService;
			this.TemplateCollection = TemplateCollection;
			this.Clock = Clock;

			string? ScheduleTimeZone = Settings.CurrentValue.ScheduleTimeZone;
			this.TimeZone = (ScheduleTimeZone == null) ? TimeZoneInfo.Local : TZConvert.GetTimeZoneInfo(ScheduleTimeZone);

			this.Logger = Logger;

			this.Redis = Redis;
			this.Queue = new RedisSortedSet<QueueItem>(Redis.Database, "scheduler/queue");
			this.BackgroundTask = new BackgroundTask(RunAsync);
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken CancellationToken)
		{
			await RefreshSchedulesAsync(Clock.UtcNow);
			BackgroundTask.Start();
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken CancellationToken)
		{
			await BackgroundTask.StopAsync();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			BackgroundTask?.Dispose();
		}

		async Task RunAsync(CancellationToken CancellationToken)
		{
			await RefreshSchedulesAsync(Clock.UtcNow);

			for(; ;)
			{
				TimeSpan DelayTime = await TickAsync(CancellationToken);
				if (CancellationToken.IsCancellationRequested)
				{
					break;
				}
				await Task.Delay(DelayTime, CancellationToken);
			}
		}

		async Task<TimeSpan> TickAsync(CancellationToken CancellationToken)
		{
			DateTime UtcNow = Clock.UtcNow;

			// Keep updating schedules
			while (!CancellationToken.IsCancellationRequested)
			{
				// Get the item with the lowest score (ie. the one that hasn't been updated in the longest time)
				SortedSetEntry<QueueItem>[] Entries = await Queue.RangeByScoreWithScoresAsync(Take: 1);
				if (Entries.Length == 0)
				{
					await RefreshSchedulesAsync(UtcNow);
					return TimeSpan.FromSeconds(10.0);
				}

				SortedSetEntry<QueueItem> Entry = Entries[0];
				QueueItem Item = Entry.Element;
				DateTime ScheduledTime = QueueItem.GetTimeFromScore(Entry.Score);

				// Check if the first entry is due to run
				if (UtcNow < ScheduledTime)
				{
					TimeSpan Delay = (ScheduledTime - UtcNow).Add(TimeSpan.FromSeconds(1.0));
					return Delay;
				}

				// If the stream id is null, it's a request to update the schedules
				if (Item.StreamId == StreamId.Empty)
				{
					await RefreshSchedulesAsync(UtcNow);
					continue;
				}

				// Get the stream and template
				IStream? Stream = await StreamService.GetStreamAsync(Item.StreamId);
				if (Stream == null || !Stream.Templates.TryGetValue(Item.TemplateId, out TemplateRef? TemplateRef) || TemplateRef.Schedule == null)
				{
					await Queue.RemoveAsync(Item);
					continue;
				}

				// Calculate the next trigger time for this schedule; it may have changed since the queue item was created.
				DateTime? NextUpdateTimeUtc = TemplateRef.Schedule.GetNextTriggerTimeUtc(TimeZone);
				if (NextUpdateTimeUtc == null)
				{
					await Queue.RemoveAsync(Item);
					continue;
				}

				// Check if we're ready to trigger now
				if (UtcNow < NextUpdateTimeUtc)
				{
					await TrySetNextUpdateTime(Entry, NextUpdateTimeUtc.Value);
					continue;
				}

				// Try to prevent any updates being done for 5 minutes, but keep extending that again every minute. This
				// creates a reservation for the stream update that will expire if the pod is terminated.
				TimeSpan ReservationTime = TimeSpan.FromMinutes(5.0);
				if (!await TrySetNextUpdateTime(Entry, Clock.UtcNow + ReservationTime))
				{
					continue;
				}

				// Try to do the update
				try
				{
					Task TriggerTask = Task.Run(() => TriggerAsync(Stream, Item.TemplateId, TemplateRef, UtcNow));
					for (; ; )
					{
						Task DelayTask = Task.Delay(TimeSpan.FromMinutes(1.0));
						if (await Task.WhenAny(DelayTask, TriggerTask) == TriggerTask)
						{
							break;
						}
						await SetNextUpdateTime(Item, Clock.UtcNow + ReservationTime);
					}
					await TriggerTask;
				}
				catch (OperationCanceledException)
				{
					throw;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Error while updating schedule for {StreamId}/{TemplateId}", Item.StreamId, Item.TemplateId);
				}

				// Update the next trigger time
				DateTime? NextTime = TemplateRef.Schedule.GetNextTriggerTimeUtc(UtcNow, TimeZone);
				if (NextTime == null)
				{
					await Queue.RemoveAsync(Item);
				}
				else
				{
					await SetNextUpdateTime(Item, NextTime.Value);
				}
			}
			return TimeSpan.Zero;
		}

		internal async Task ResetAsync()
		{
			await Redis.Database.KeyDeleteAsync(Queue.Key);
		}

		internal async Task TickForTestingAsync()
		{
			await RefreshSchedulesAsync(Clock.UtcNow);
			await TickAsync(CancellationToken.None);
		}

		Task<bool> TrySetNextUpdateTime(SortedSetEntry<QueueItem> Entry, DateTime NextTimeUtc)
		{
			ITransaction Transaction = Redis.Database.CreateTransaction();
			Transaction.AddCondition(Condition.SortedSetEqual(Queue.Key, Entry.ElementValue, Entry.Score));
			_ = Transaction.With(Queue).AddAsync(Entry.Element, QueueItem.GetScoreFromTime(NextTimeUtc));
			return Transaction.ExecuteAsync();
		}

		async Task SetNextUpdateTime(QueueItem Item, DateTime NextTimeUtc)
		{
			await Queue.AddAsync(Item, QueueItem.GetScoreFromTime(NextTimeUtc));
		}

		/// <summary>
		/// Get the current set of streams and ensure there's an entry for each item
		/// </summary>
		public async Task RefreshSchedulesAsync(DateTime UtcNow)
		{
			List<SortedSetEntry<QueueItem>> QueueItems = new List<SortedSetEntry<QueueItem>>();

			List<IStream> Streams = await StreamService.GetStreamsAsync();
			foreach (IStream Stream in Streams)
			{
				foreach ((TemplateRefId TemplateId, TemplateRef TemplateRef) in Stream.Templates)
				{
					if (TemplateRef.Schedule != null)
					{
						DateTime? NextTriggerTimeUtc = TemplateRef.Schedule.GetNextTriggerTimeUtc(TimeZone);
						if (NextTriggerTimeUtc != null)
						{
							double Score = QueueItem.GetScoreFromTime(NextTriggerTimeUtc.Value);
							QueueItems.Add(new SortedSetEntry<QueueItem>(new QueueItem(Stream.Id, TemplateId), Score));
						}
					}
				}
			}
			QueueItems.Add(new SortedSetEntry<QueueItem>(new QueueItem(StreamId.Empty, TemplateRefId.Empty), QueueItem.GetScoreFromTime(UtcNow.AddMinutes(1.0))));

			await Queue.AddAsync(QueueItems.ToArray());
		}

		/// <summary>
		/// Trigger a schedule to run
		/// </summary>
		/// <param name="Stream">Stream for the schedule</param>
		/// <param name="TemplateId"></param>
		/// <param name="TemplateRef"></param>
		/// <param name="UtcNow"></param>
		/// <returns>Async task</returns>
		private async Task TriggerAsync(IStream Stream, TemplateRefId TemplateId, TemplateRef TemplateRef, DateTime UtcNow)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("ScheduleService.TriggerAsync").StartActive();
			Scope.Span.SetTag("StreamId", Stream.Id);
			Scope.Span.SetTag("TemplateId", TemplateId);

			Stopwatch Stopwatch = Stopwatch.StartNew();
			Logger.LogInformation("Updating schedule for {StreamId} template {TemplateId}", Stream.Id, TemplateId);

			// Make sure the schedule is valid
			Schedule? Schedule = TemplateRef.Schedule;
			if(Schedule == null)
			{
				return;
			}

			// Get a list of jobs that we need to remove
			List<JobId> RemoveJobIds = new List<JobId>();
			foreach (JobId ActiveJobId in Schedule.ActiveJobs)
			{
				IJob? Job = await JobService.GetJobAsync(ActiveJobId);
				if (Job == null || Job.Batches.All(x => x.State == JobStepBatchState.Complete))
				{
					Logger.LogInformation("Removing active job {JobId}", ActiveJobId);
					RemoveJobIds.Add(ActiveJobId);
				}
			}
			await StreamService.UpdateScheduleTriggerAsync(Stream, TemplateId, RemoveJobs: RemoveJobIds);

			// If the stream is paused, bail out
			if (Stream.IsPaused(Clock.UtcNow))
			{
				Logger.LogDebug("Skipping schedule update for stream {StreamId}. It has been paused until {PausedUntil} with comment '{PauseComment}'.", Stream.Id, Stream.PausedUntil, Stream.PauseComment);
				return;
			}

			// Trigger this schedule
			try
			{
				await TriggerAsync(Stream, TemplateId, TemplateRef, Schedule, Schedule.ActiveJobs.Count - RemoveJobIds.Count, UtcNow);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Failed to start schedule {StreamId}/{TemplateId}", Stream.Id, TemplateId);
			}

			// Print some timing info
			Stopwatch.Stop();
			Logger.LogInformation("Updated schedule for {StreamId} template {TemplateId} in {TimeSeconds}ms", Stream.Id, TemplateId, (long)Stopwatch.Elapsed.TotalMilliseconds);
		}

		/// <summary>
		/// Trigger a schedule to run
		/// </summary>
		/// <param name="Stream">Stream for the schedule</param>
		/// <param name="TemplateId"></param>
		/// <param name="TemplateRef"></param>
		/// <param name="Schedule"></param>
		/// <param name="NumActiveJobs"></param>
		/// <param name="UtcNow">The current time</param>
		/// <returns>Async task</returns>
		private async Task TriggerAsync(IStream Stream, TemplateRefId TemplateId, TemplateRef TemplateRef, Schedule Schedule, int NumActiveJobs, DateTime UtcNow)
		{
			// Check we're not already at the maximum number of allowed jobs
			if (Schedule.MaxActive != 0 && NumActiveJobs >= Schedule.MaxActive)
			{
				Logger.LogInformation("Skipping trigger of {StreamId} template {TemplateId} - already have maximum number of jobs running ({NumJobs})", Stream.Id, TemplateId, Schedule.MaxActive);
				foreach (JobId JobId in Schedule.ActiveJobs)
				{
					Logger.LogInformation("Active job for {StreamId} template {TemplateId}: {JobId}", Stream.Id, TemplateId, JobId);
				}
				return;
			}

			// Minimum changelist number, inclusive
			int MinChangeNumber = Schedule.LastTriggerChange;
			if (MinChangeNumber > 0 && !Schedule.RequireSubmittedChange)
			{
				MinChangeNumber--;
			}

			// Maximum changelist number, exclusive
			int? MaxChangeNumber = null;

			// Get the maximum number of changes to trigger
			int MaxNewChanges = 1;
			if (Schedule.MaxChanges != 0)
			{
				MaxNewChanges = Schedule.MaxChanges;
			}
			if (Schedule.MaxActive != 0)
			{
				MaxNewChanges = Math.Min(MaxNewChanges, Schedule.MaxActive - NumActiveJobs);
			}

			// Create a timer to limit the amount we look back through P4 history
			Stopwatch Timer = Stopwatch.StartNew();
			ChangeContentFlags? FilterFlags = Schedule.GetFilterFlags();

			// Create a file filter
			FileFilter? FileFilter = null;
			if (Schedule.Files != null)
			{
				FileFilter = new FileFilter(Schedule.Files);
			}

			// Start as many jobs as possible
			List<(int Change, int CodeChange)> TriggerChanges = new List<(int, int)>();
			while (TriggerChanges.Count < MaxNewChanges)
			{
				// Get the next valid change
				int Change, CodeChange;
				if (Schedule.Gate != null)
				{
					(Change, CodeChange) = await GetNextChangeForGateAsync(Stream.Id, TemplateId, Schedule.Gate, MinChangeNumber, MaxChangeNumber);
				}
				else
				{
					(Change, CodeChange) = await GetNextChangeAsync(Stream.ClusterName, Stream.Name, MinChangeNumber, MaxChangeNumber);
				}

				// Quit if we didn't find anything
				if (Change < MinChangeNumber)
				{
					break;
				}
				if (Change == MinChangeNumber && (Schedule.RequireSubmittedChange || TriggerChanges.Count > 0))
				{
					break;
				}

				// Adjust the changelist for the desired filter
				if (await ShouldBuildChangeAsync(Stream.ClusterName, Stream.Name, Change, FilterFlags, FileFilter))
				{
					TriggerChanges.Add((Change, CodeChange));
				}

				// Check we haven't exceeded the time limit
				if (Timer.Elapsed > TimeSpan.FromMinutes(2.0))
				{
					Logger.LogError("Querying for changes to trigger has taken {Time}. Aborting.", Timer.Elapsed);
					break;
				}

				// Start the next build before this change
				if (FilterFlags != null && (FilterFlags.Value & ChangeContentFlags.ContainsContent) == 0)
				{
					MaxChangeNumber = Math.Min(CodeChange, Change - 1);
				}
				else
				{
					MaxChangeNumber = Change - 1;
				}
			}

			// Early out if there's nothing to do
			if (TriggerChanges.Count == 0)
			{
				Logger.LogInformation("Skipping trigger of {StreamName} template {TemplateId} - no candidate changes after CL {LastTriggerChange}", Stream.Id, TemplateId, Schedule.LastTriggerChange);
				return;
			}

			// Get the matching template
			ITemplate? Template = await TemplateCollection.GetAsync(TemplateRef.Hash);
			if (Template == null)
			{
				Logger.LogWarning("Unable to find template '{TemplateHash}' for '{TemplateId}'", TemplateRef.Hash, TemplateId);
				return;
			}

			// Register the graph for it
			IGraph Graph = await Graphs.AddAsync(Template);

			// We may need to submit a new change for any new jobs. This only makes sense if there's one change.
			if (Template.SubmitNewChange != null)
			{
				int NewChange = await Perforce.CreateNewChangeForTemplateAsync(Stream, Template);
				int NewCodeChange = await Perforce.GetCodeChangeAsync(Stream.ClusterName, Stream.Name, NewChange);
				TriggerChanges = new List<(int, int)> { (NewChange, NewCodeChange) };
			}

			// Try to start all the new jobs
			foreach ((int Change, int CodeChange) in TriggerChanges.OrderBy(x => x.Change))
			{
				List<string> DefaultArguments = Template.GetDefaultArguments();
				IJob NewJob = await JobService.CreateJobAsync(null, Stream, TemplateId, Template.Id, Graph, Template.Name, Change, CodeChange, null, null, null, Template.Priority, null, null, TemplateRef.ChainedJobs, TemplateRef.ShowUgsBadges, TemplateRef.ShowUgsAlerts, TemplateRef.NotificationChannel, TemplateRef.NotificationChannelFilter, DefaultArguments);
				Logger.LogInformation("Started new job for {StreamName} template {TemplateId} at CL {Change} (Code CL {CodeChange}): {JobId}", Stream.Id, Template.Id, Change, CodeChange, NewJob.Id);
				await StreamService.UpdateScheduleTriggerAsync(Stream, TemplateId, UtcNow, Change, new List<JobId> { NewJob.Id }, new List<JobId>());
			}
		}

		/// <summary>
		/// Tests whether a schedule should build a particular change, based on its requested change filters
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="StreamName"></param>
		/// <param name="Change"></param>
		/// <param name="FilterFlags"></param>
		/// <param name="FileFilter">Filter for the files to trigger a build</param>
		/// <returns></returns>
		private async Task<bool> ShouldBuildChangeAsync(string ClusterName, string StreamName, int Change, ChangeContentFlags? FilterFlags, FileFilter? FileFilter)
		{
			ChangeDetails Details = await Perforce.GetChangeDetailsAsync(ClusterName, StreamName, Change, null);
			if (Regex.IsMatch(Details.Description, @"^\s*#\s*skipci", RegexOptions.Multiline))
			{
				return false;
			}
			if (FilterFlags != null && FilterFlags.Value != 0)
			{
				if ((Details.GetContentFlags() & FilterFlags.Value) == 0)
				{
					Logger.LogDebug("Not building change {Change} ({ChangeFlags}) due to filter flags ({FilterFlags})", Change, Details.GetContentFlags().ToString(), FilterFlags.Value.ToString());
					return false;
				}
			}
			if (FileFilter != null)
			{
				if (!Details.Files.Any(x => FileFilter.Matches(x.Path)))
				{
					Logger.LogDebug("Not building change {Change} due to file filter", Change);
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Get the next change to build in this stream, between a given range
		/// </summary>
		/// <returns>The new change and code change to build, or 0.</returns>
		private async Task<(int Change, int CodeChange)> GetNextChangeAsync(string ClusterName, string StreamName, int? MinChange, int? MaxChange)
		{
			List<ChangeSummary> Changes = await Perforce.GetChangesAsync(ClusterName, StreamName, MinChange, MaxChange, 1, null);
			if (Changes.Count > 0)
			{
				int Change = Changes[0].Number;
				int CodeChange = await Perforce.GetCodeChangeAsync(ClusterName, StreamName, Change);
				return (Change, CodeChange);
			}
			return default;
		}

		/// <summary>
		/// Gets the next change to build for a schedule on a gate
		/// </summary>
		/// <returns></returns>
		private async Task<(int Change, int CodeChange)> GetNextChangeForGateAsync(StreamId StreamId, TemplateRefId TemplateRefId, ScheduleGate Gate, int? MinChange, int? MaxChange)
		{
			for (; ; )
			{
				List<IJob> Jobs = await JobCollection.FindAsync(StreamId: StreamId, Templates: new[] { Gate.TemplateRefId }, MinChange: MinChange, MaxChange: MaxChange, Count: 1);
				if (Jobs.Count == 0)
				{
					return default;
				}

				IJob Job = Jobs[0];

				IGraph? Graph = await Graphs.GetAsync(Job.GraphHash);
				if (Graph != null)
				{
					(JobStepState, JobStepOutcome)? State = Job.GetTargetState(Graph, Gate.Target);
					if (State != null && State.Value.Item1 == JobStepState.Completed)
					{
						JobStepOutcome Outcome = State.Value.Item2;
						if (Outcome == JobStepOutcome.Success || Outcome == JobStepOutcome.Warnings)
						{
							return (Job.Change, Job.CodeChange);
						}
						Logger.LogInformation("Skipping trigger of {StreamName} template {TemplateId} - last {OtherTemplateRefId} job ({JobId}) ended with errors", StreamId, TemplateRefId, Gate.TemplateRefId, Job.Id);
					}
				}

				MaxChange = Job.Change - 1;
			}
		}
	}
}
