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

namespace HordeServer.Services
{
	using JobId = ObjectId<IJob>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Manipulates schedule instances
	/// </summary>
	public class ScheduleService : ElectedBackgroundService
	{
		/// <summary>
		/// The downtime service instance
		/// </summary>
		IDowntimeService DowntimeService;

		/// <summary>
		/// Collection of graph documents
		/// </summary>
		IGraphCollection Graphs;

		/// <summary>
		/// The perforce service singleton
		/// </summary>
		IPerforceService Perforce;

		/// <summary>
		/// Collection of job documents
		/// </summary>
		IJobCollection JobCollection;

		/// <summary>
		/// The job service
		/// </summary>
		JobService JobService;

		/// <summary>
		/// The stream service
		/// </summary>
		StreamService StreamService;

		/// <summary>
		/// The template service
		/// </summary>
		ITemplateCollection TemplateCollection;

		/// <summary>
		/// The timezone to use for schedules
		/// </summary>
		TimeZoneInfo TimeZone;

		/// <summary>
		/// The clock instance
		/// </summary>
		IClock Clock;

		/// <summary>
		/// The logging instance
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">Database service instance</param>
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
		public ScheduleService(DatabaseService DatabaseService, IGraphCollection Graphs, IDowntimeService DowntimeService, IPerforceService Perforce, IJobCollection JobCollection, JobService JobService, StreamService StreamService, ITemplateCollection TemplateCollection, IClock Clock, IOptionsMonitor<ServerSettings> Settings, ILogger<ScheduleService> Logger)
			: base(DatabaseService, new ObjectId("6035593e6d721d80fb9efa5c"), Logger)
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
		}

		/// <summary>
		/// Execute any scheduled tasks
		/// </summary>
		/// <param name="StoppingToken">Cancellation token for stopping the service</param>
		/// <returns>Async task</returns>
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime NextTickTime = DateTime.UtcNow.AddMinutes(1.0);
			if (!DowntimeService.IsDowntimeActive)
			{
				Logger.LogInformation("Updating scheduled triggers...");
				Stopwatch Stopwatch = Stopwatch.StartNew();
				using (IScope Scope = GlobalTracer.Instance.BuildSpan("ScheduleService Tick").StartActive())
				{
					NextTickTime = await UpdateSchedulesAsync();
				}
				Stopwatch.Stop();
				Logger.LogInformation("Scheduling triggers took {ElapsedTime} ms", Stopwatch.ElapsedMilliseconds);
			}
			return NextTickTime;
		}

		/// <summary>
		/// Update all the schedules
		/// </summary>
		/// <returns>Next time that checkes need to be updated</returns>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		async Task<DateTime> UpdateSchedulesAsync()
		{
			// Find the next time at which each schedule should run
			DateTimeOffset Now = Clock.UtcNow;
			DateTimeOffset NextUpdateTime = Now.AddMinutes(1.0);

			// Find all the enabled schedules
			List<IStream> Streams = await StreamService.GetStreamsAsync();
			foreach (IStream Stream in Streams)
			{
				if (Stream.IsPaused(Clock.UtcNow))
				{
					Logger.LogDebug("Skipping schedule update for stream {StreamId}. It has been paused until {PausedUntil} with comment '{PauseComment}'.", Stream.Id, Stream.PausedUntil, Stream.PauseComment);
					continue;
				}
				
				foreach ((TemplateRefId TemplateRefId, TemplateRef TemplateRef) in Stream.Templates)
				{
					Schedule? Schedule = TemplateRef.Schedule;
					if (Schedule != null && Schedule.Enabled)
					{
						using IDisposable Scope = Logger.BeginScope("Updating schedule {StreamId}/{TemplateRefId}", Stream.Id, TemplateRefId);

						DateTimeOffset? NextTriggerTime = await UpdateScheduleAsync(Stream, TemplateRefId, TemplateRef, Schedule, Now);
						if (NextTriggerTime.HasValue && NextTriggerTime.Value < NextUpdateTime)
						{
							NextUpdateTime = NextTriggerTime.Value;
						}
					}
				}
			}

			return NextUpdateTime.UtcDateTime;
		}

		/// <summary>
		/// Updates a schedule
		/// </summary>
		/// <returns>Next time to trigger the schedule</returns>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		private async Task<DateTimeOffset?> UpdateScheduleAsync(IStream Stream, TemplateRefId TemplateRefId, TemplateRef TemplateRef, Schedule Schedule, DateTimeOffset Now)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("ScheduleService.UpdateScheduleAsync").StartActive();
			Scope.Span.SetTag("Stream.Id", Stream.Id);
			Scope.Span.SetTag("Stream.Name", Stream.Name);
			
			// Check if we can run the trigger
			DateTimeOffset? NextTriggerTime = Schedule.GetNextTriggerTime(TimeZone);
			if (!NextTriggerTime.HasValue)
			{
				Logger.LogInformation("Schedule {StreamId}/{TemplateRefId} is disabled", Stream.Id, TemplateRefId);
				return NextTriggerTime;
			}
			if (NextTriggerTime.Value > Now)
			{
				Logger.LogDebug("Schedule {StreamId}/{TemplateRefId} will next trigger at {NextTriggerTime}", Stream.Id, TemplateRefId, NextTriggerTime.Value);
				return NextTriggerTime;
			}
			Logger.LogInformation("Schedule {StreamId}/{TemplateRefId} is ready to trigger (last: {Change} at {Time})", Stream.Id, TemplateRefId, Schedule.LastTriggerChange, Schedule.LastTriggerTime);

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
			if (RemoveJobIds.Count > 0)
			{
				await StreamService.UpdateScheduleTriggerAsync(Stream, TemplateRefId, Now, null, new List<JobId>(), RemoveJobIds);
			}

			// Trigger this schedule
			try
			{
				await TriggerAsync(Stream, TemplateRefId, TemplateRef, Schedule, Schedule.ActiveJobs.Count - RemoveJobIds.Count, Now);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Failed to start schedule {StreamId}/{TemplateRefId}", Stream.Id, TemplateRefId);
			}

			// Return the next update time
			return Schedule.GetNextTriggerTime(Now, TimeZone);
		}

		/// <summary>
		/// Trigger a schedule to run
		/// </summary>
		/// <param name="Stream">Stream for the schedule</param>
		/// <param name="TemplateRefId"></param>
		/// <param name="TemplateRef"></param>
		/// <param name="Schedule"></param>
		/// <param name="NumActiveJobs"></param>
		/// <param name="Now"></param>
		/// <returns>Async task</returns>
		private async Task TriggerAsync(IStream Stream, TemplateRefId TemplateRefId, TemplateRef TemplateRef, Schedule Schedule, int NumActiveJobs, DateTimeOffset Now)
		{
			using IScope Scope = GlobalTracer.Instance.BuildSpan("ScheduleService.TriggerAsync").StartActive();
			Scope.Span.SetTag("Stream.Id", Stream.Id);
			Scope.Span.SetTag("Stream.Name", Stream.Name);
			Scope.Span.SetTag("TemplateRefId", TemplateRefId);
			
			// Check we're not already at the maximum number of allowed jobs
			if (Schedule.MaxActive != 0 && NumActiveJobs >= Schedule.MaxActive)
			{
				Logger.LogInformation("Skipping trigger of {StreamId} template {TemplateRefId} - already have maximum number of jobs running ({NumJobs})", Stream.Id, TemplateRefId, Schedule.MaxActive);
				foreach (JobId JobId in Schedule.ActiveJobs)
				{
					Logger.LogInformation("Active job for {StreamId} template {TemplateRefId}: {JobId}", Stream.Id, TemplateRefId, JobId);
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
					(Change, CodeChange) = await GetNextChangeForGateAsync(Stream.Id, TemplateRefId, Schedule.Gate, MinChangeNumber, MaxChangeNumber);
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
				Logger.LogInformation("Skipping trigger of {StreamName} template {TemplateRefId} - no candidate changes after CL {LastTriggerChange}", Stream.Id, TemplateRefId, Schedule.LastTriggerChange);
				return;
			}

			// Get the matching template
			ITemplate? Template = await TemplateCollection.GetAsync(TemplateRef.Hash);
			if (Template == null)
			{
				Logger.LogWarning("Unable to find template '{TemplateHash}' for '{TemplateRefId}'", TemplateRef.Hash, TemplateRefId);
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
				IJob NewJob = await JobService.CreateJobAsync(null, Stream, TemplateRefId, Template.Id, Graph, Template.Name, Change, CodeChange, null, null, null, Template.Priority, null, null, TemplateRef.ChainedJobs, TemplateRef.ShowUgsBadges, TemplateRef.ShowUgsAlerts, TemplateRef.NotificationChannel, TemplateRef.NotificationChannelFilter, DefaultArguments);
				Logger.LogInformation("Started new job for {StreamName} template {TemplateName} at CL {Change} (Code CL {CodeChange}): {JobId}", Stream.Id, TemplateRef.Name, Change, CodeChange, NewJob.Id);
				await StreamService.UpdateScheduleTriggerAsync(Stream, TemplateRefId, Now, Change, new List<JobId> { NewJob.Id }, new List<JobId>());
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
			if (FilterFlags != null && FilterFlags.Value != 0)
			{
				ChangeDetails Details = await Perforce.GetChangeDetailsAsync(ClusterName, StreamName, Change, null);
				if ((Details.GetContentFlags() & FilterFlags.Value) == 0)
				{
					Logger.LogDebug("Not building change {Change} ({ChangeFlags}) due to filter flags ({FilterFlags})", Change, Details.GetContentFlags().ToString(), FilterFlags.Value.ToString());
					return false;
				}
			}
			if (FileFilter != null)
			{
				ChangeDetails Details = await Perforce.GetChangeDetailsAsync(ClusterName, StreamName, Change, null);
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
						Logger.LogInformation("Skipping trigger of {StreamName} template {TemplateRefId} - last {OtherTemplateRefId} job ({JobId}) ended with errors", StreamId, TemplateRefId, Gate.TemplateRefId, Job.Id);
					}
				}

				MaxChange = Job.Change - 1;
			}
		}
	}
}
