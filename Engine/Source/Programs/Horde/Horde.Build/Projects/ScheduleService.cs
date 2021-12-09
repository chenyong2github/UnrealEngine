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
using EpicGames.Redis.Utility;

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
		RedisKey BaseLockKey;
		RedisKey TickLockKey; // Lock to tick the queue
		RedisSortedSet<QueueItem> Queue; // Items to tick, ordered by time
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
			this.BaseLockKey = "scheduler/locks";
			this.TickLockKey = BaseLockKey.Append("/tick");
			this.Queue = new RedisSortedSet<QueueItem>(Redis.Database, "scheduler/queue");
			this.BackgroundTask = new BackgroundTask(RunAsync);
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken CancellationToken)
		{
			await UpdateQueueAsync(Clock.UtcNow);
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
			await UpdateQueueAsync(Clock.UtcNow);

			for(; ;)
			{
				Stopwatch Timer = Stopwatch.StartNew();
				await TickAsync(CancellationToken);

				TimeSpan DelayTime = TimeSpan.FromMinutes(1.0) - Timer.Elapsed;
				if(DelayTime > TimeSpan.Zero)
				{
					await Task.Delay(DelayTime, CancellationToken);
				}
			}
		}

		async Task TickAsync(CancellationToken CancellationToken)
		{
			DateTime UtcNow = Clock.UtcNow;

			// Update the current queue
			await using (RedisLock Lock = new RedisLock(Redis.Database, TickLockKey))
			{
				if (await Lock.AcquireAsync(TimeSpan.FromMinutes(1.0), false))
				{
					await UpdateQueueAsync(UtcNow);
				}
			}

			// Keep updating schedules
			while (!CancellationToken.IsCancellationRequested)
			{
				// Get the item with the lowest score (ie. the one that hasn't been updated in the longest time)
				QueueItem? Item = await PopQueueItemAsync();
				if (Item == null)
				{
					break;
				}

				// Acquire the lock for this schedule and update it
				await using (RedisLock Lock = new RedisLock<QueueItem>(Redis.Database, BaseLockKey, Item))
				{
					if (await Lock.AcquireAsync(TimeSpan.FromMinutes(1.0)))
					{
						try
						{
							await TriggerAsync(Item.StreamId, Item.TemplateId, UtcNow, CancellationToken);
						}
						catch (OperationCanceledException)
						{
							throw;
						}
						catch (Exception Ex)
						{
							Logger.LogError(Ex, "Error while updating schedule for {StreamId}/{TemplateId}", Item.StreamId, Item.TemplateId);
						}
					}
				}
			}
		}

		async Task<QueueItem?> PopQueueItemAsync()
		{
			for (; ; )
			{
				QueueItem[] Items = await Queue.RangeByRankAsync(0, 0);
				if (Items.Length == 0)
				{
					return null;
				}
				if (await Queue.RemoveAsync(Items[0]))
				{
					return Items[0];
				}
			}
		}

		internal async Task ResetAsync()
		{
			await Redis.Database.KeyDeleteAsync(Queue.Key);
			await Redis.Database.KeyDeleteAsync(TickLockKey);
		}

		internal async Task TickForTestingAsync()
		{
			await UpdateQueueAsync(Clock.UtcNow);
			await TickAsync(CancellationToken.None);
		}

		/// <summary>
		/// Get the current set of streams and ensure there's an entry for each item
		/// </summary>
		public async Task UpdateQueueAsync(DateTime UtcNow)
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
							if (UtcNow > NextTriggerTimeUtc.Value)
							{
								double Score = QueueItem.GetScoreFromTime(NextTriggerTimeUtc.Value);
								QueueItems.Add(new SortedSetEntry<QueueItem>(new QueueItem(Stream.Id, TemplateId), Score));

								await StreamService.UpdateScheduleTriggerAsync(Stream, TemplateId, UtcNow);
							}
						}
					}
				}
			}

			await Queue.AddAsync(QueueItems.ToArray());
		}

		/// <summary>
		/// Trigger a schedule to run
		/// </summary>
		/// <param name="StreamId">Stream for the schedule</param>
		/// <param name="TemplateId"></param>
		/// <param name="UtcNow"></param>
		/// <param name="CancellationToken"></param>
		/// <returns>Async task</returns>
		private async Task<bool> TriggerAsync(StreamId StreamId, TemplateRefId TemplateId, DateTime UtcNow, CancellationToken CancellationToken)
		{
			IStream? Stream = await StreamService.GetStreamAsync(StreamId);
			if (Stream == null || !Stream.Templates.TryGetValue(TemplateId, out TemplateRef? TemplateRef))
			{
				return false;
			}

			Schedule? Schedule = TemplateRef.Schedule;
			if (Schedule == null)
			{
				return false;
			}

			using IScope Scope = GlobalTracer.Instance.BuildSpan("ScheduleService.TriggerAsync").StartActive();
			Scope.Span.SetTag("StreamId", Stream.Id);
			Scope.Span.SetTag("TemplateId", TemplateId);

			Stopwatch Stopwatch = Stopwatch.StartNew();
			Logger.LogInformation("Updating schedule for {StreamId} template {TemplateId}", Stream.Id, TemplateId);

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
			if (Stream.IsPaused(UtcNow))
			{
				Logger.LogDebug("Skipping schedule update for stream {StreamId}. It has been paused until {PausedUntil} with comment '{PauseComment}'.", Stream.Id, Stream.PausedUntil, Stream.PauseComment);
				return false;
			}

			// Trigger this schedule
			try
			{
				await TriggerAsync(Stream, TemplateId, TemplateRef, Schedule, Schedule.ActiveJobs.Count - RemoveJobIds.Count, UtcNow, CancellationToken);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Failed to start schedule {StreamId}/{TemplateId}", Stream.Id, TemplateId);
			}

			// Print some timing info
			Stopwatch.Stop();
			Logger.LogInformation("Updated schedule for {StreamId} template {TemplateId} in {TimeSeconds}ms", Stream.Id, TemplateId, (long)Stopwatch.Elapsed.TotalMilliseconds);
			return true;
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
		/// <param name="CancellationToken"></param>
		/// <returns>Async task</returns>
		private async Task TriggerAsync(IStream Stream, TemplateRefId TemplateId, TemplateRef TemplateRef, Schedule Schedule, int NumActiveJobs, DateTime UtcNow, CancellationToken CancellationToken)
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
				CancellationToken.ThrowIfCancellationRequested();

				// Get the next valid change
				int Change, CodeChange;
				if (Schedule.Gate != null)
				{
					(Change, CodeChange) = await GetNextChangeForGateAsync(Stream.Id, TemplateId, Schedule.Gate, MinChangeNumber, MaxChangeNumber, CancellationToken);
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
				CancellationToken.ThrowIfCancellationRequested();
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
		private async Task<(int Change, int CodeChange)> GetNextChangeForGateAsync(StreamId StreamId, TemplateRefId TemplateRefId, ScheduleGate Gate, int? MinChange, int? MaxChange, CancellationToken CancellationToken)
		{
			for (; ; )
			{
				CancellationToken.ThrowIfCancellationRequested();

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
