// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Horde.Build.Perforce;
using Horde.Build.Streams;
using Horde.Build.Utilities;

namespace Horde.Build.Jobs.Schedules
{
	using JobId = ObjectId<IJob>;

	/// <summary>
	/// Response describing a schedule
	/// </summary>
	public class GetScheduleResponse
	{
		/// <summary>
		/// Whether the schedule is currently enabled
		/// </summary>
		public bool Enabled { get; set; }

		/// <summary>
		/// Maximum number of scheduled jobs at once
		/// </summary>
		public int MaxActive { get; set; }

		/// <summary>
		/// Maximum number of changes the schedule can fall behind head revision. If greater than zero, builds will be triggered for every submitted changelist until the backlog is this size.
		/// </summary>
		public int MaxChanges { get; set; }

		/// <summary>
		/// Whether the build requires a change to be submitted
		/// </summary>
		public bool RequireSubmittedChange { get; set; }

		/// <summary>
		/// Gate for this schedule to trigger
		/// </summary>
		public ScheduleGateConfig? Gate { get; set; }

		/// <summary>
		/// The types of changes to run for
		/// </summary>
		public List<ChangeContentFlags>? Filter { get; set; }

		/// <summary>
		/// Parameters for the template
		/// </summary>
		public Dictionary<string, string> TemplateParameters { get; set; }

		/// <summary>
		/// New patterns for the schedule
		/// </summary>
		public List<SchedulePatternConfig> Patterns { get; set; }

		/// <summary>
		/// Last changelist number that this was triggered for
		/// </summary>
		public int LastTriggerChange { get; set; }

		/// <summary>
		/// Last time that the schedule was triggered
		/// </summary>
		public DateTimeOffset LastTriggerTime { get; set; }

		/// <summary>
		/// List of active jobs
		/// </summary>
		public List<JobId> ActiveJobs { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="schedule">Schedule to construct from</param>
		public GetScheduleResponse(ITemplateSchedule schedule)
		{
			Enabled = schedule.Config.Enabled;
			MaxActive = schedule.Config.MaxActive;
			MaxChanges = schedule.Config.MaxChanges;
			RequireSubmittedChange = schedule.Config.RequireSubmittedChange;
			Gate = schedule.Config.Gate;
			Filter = schedule.Config.Filter;
			TemplateParameters = schedule.Config.TemplateParameters;
			Patterns = schedule.Config.Patterns;
			LastTriggerChange = schedule.LastTriggerChange;
			LastTriggerTime = schedule.LastTriggerTimeUtc;
			ActiveJobs = new List<JobId>(schedule.ActiveJobs);
		}
	}

	/// <summary>
	/// Response describing when a schedule is expected to trigger
	/// </summary>
	public class GetScheduleForecastResponse
	{
		/// <summary>
		/// Next trigger times
		/// </summary>
		public List<DateTime> Times { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="times">List of trigger times</param>
		public GetScheduleForecastResponse(List<DateTime> times)
		{
			Times = times;
		}
	}
}
