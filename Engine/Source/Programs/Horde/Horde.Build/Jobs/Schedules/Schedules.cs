// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using Horde.Build.Acls;
using Horde.Build.Perforce;
using Horde.Build.Streams;
using Horde.Build.Utilities;

namespace Horde.Build.Jobs.Schedules
{
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Information about a schedule pattern
	/// </summary>
	public class GetSchedulePatternResponse
	{
		/// <summary>
		/// Days of the week to run this schedule on. If null, the schedule will run every day.
		/// </summary>
		public List<string>? DaysOfWeek { get; set; }

		/// <summary>
		/// Time during the day for the first schedule to trigger. Measured in minutes from midnight.
		/// </summary>
		public int MinTime { get; set; }

		/// <summary>
		/// Time during the day for the last schedule to trigger. Measured in minutes from midnight.
		/// </summary>
		public int? MaxTime { get; set; }

		/// <summary>
		/// Interval between each schedule triggering
		/// </summary>
		public int? Interval { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="pattern">The pattern to construct from</param>
		public GetSchedulePatternResponse(SchedulePattern pattern)
		{
			DaysOfWeek = pattern.DaysOfWeek?.ConvertAll(x => x.ToString());
			MinTime = pattern.MinTime;
			MaxTime = pattern.MaxTime;
			Interval = pattern.Interval;
		}
	}

	/// <summary>
	/// Gate allowing a schedule to trigger.
	/// </summary>
	public class GetScheduleGateResponse
	{
		/// <summary>
		/// The template containing the dependency
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Target to wait for
		/// </summary>
		public string Target { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="gate">Gate to construct from</param>
		public GetScheduleGateResponse(ScheduleGate gate)
		{
			TemplateId = gate.TemplateRefId.ToString();
			Target = gate.Target;
		}
	}

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
		public GetScheduleGateResponse? Gate { get; set; }

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
		public List<GetSchedulePatternResponse> Patterns { get; set; }

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
		public List<string> ActiveJobs { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="schedule">The schedule to construct from</param>
		public GetScheduleResponse(Schedule schedule)
		{
			Enabled = schedule.Enabled;
			MaxActive = schedule.MaxActive;
			MaxChanges = schedule.MaxChanges;
			RequireSubmittedChange = schedule.RequireSubmittedChange;
			if (schedule.Gate != null)
			{
				Gate = new GetScheduleGateResponse(schedule.Gate);
			}
			Filter = schedule.Filter;
			TemplateParameters = schedule.TemplateParameters;
			Patterns = schedule.Patterns.ConvertAll(x => new GetSchedulePatternResponse(x));
			LastTriggerChange = schedule.LastTriggerChange;
			LastTriggerTime = schedule.GetLastTriggerTimeUtc();
			ActiveJobs = schedule.ActiveJobs.ConvertAll(x => x.ToString());
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
