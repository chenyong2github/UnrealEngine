// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Net;
using System.Security.Claims;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Parameters to create a new schedule
	/// </summary>
	public class CreateSchedulePatternRequest
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
		/// Constructs a model object from this request
		/// </summary>
		/// <returns>Model object</returns>
		public SchedulePattern ToModel()
		{
			return new SchedulePattern(DaysOfWeek?.ConvertAll(x => Enum.Parse<DayOfWeek>(x)), MinTime, MaxTime, Interval);
		}
	}

	/// <summary>
	/// Gate allowing a schedule to trigger.
	/// </summary>
	public class CreateScheduleGateRequest
	{
		/// <summary>
		/// The template containing the dependency
		/// </summary>
		[Required]
		public string TemplateId { get; set; } = String.Empty;

		/// <summary>
		/// Target to wait for
		/// </summary>
		[Required]
		public string Target { get; set; } = String.Empty;

		/// <summary>
		/// Constructs a model object
		/// </summary>
		/// <returns>New model object.</returns>
		public ScheduleGate ToModel()
		{
			return new ScheduleGate(new TemplateRefId(TemplateId), Target);
		}
	}

	/// <summary>
	/// Parameters to create a new schedule
	/// </summary>
	public class CreateScheduleRequest
	{
		/// <summary>
		/// Roles to impersonate for this schedule
		/// </summary>
		public List<CreateAclClaimRequest>? Claims { get; set; }

		/// <summary>
		/// Whether the schedule should be enabled
		/// </summary>
		public bool Enabled { get; set; }

		/// <summary>
		/// Maximum number of builds that can be active at once
		/// </summary>
		public int MaxActive { get; set; }

		/// <summary>
		/// Maximum number of changes the schedule can fall behind head revision. If greater than zero, builds will be triggered for every submitted changelist until the backlog is this size.
		/// </summary>
		public int MaxChanges { get; set; }

		/// <summary>
		/// Whether the build requires a change to be submitted
		/// </summary>
		public bool RequireSubmittedChange { get; set; } = true;

		/// <summary>
		/// Gate allowing the schedule to trigger
		/// </summary>
		public CreateScheduleGateRequest? Gate { get; set; }

		/// <summary>
		/// The types of changes to run for
		/// </summary>
		public List<ChangeContentFlags>? Filter { get; set; }

		/// <summary>
		/// Files that should cause the job to trigger
		/// </summary>
		public List<string>? Files { get; set; }

		/// <summary>
		/// Parameters for the template
		/// </summary>
		public Dictionary<string, string> TemplateParameters { get; set; } = new Dictionary<string, string>();

		/// <summary>
		/// New patterns for the schedule
		/// </summary>
		public List<CreateSchedulePatternRequest> Patterns { get; set; } = new List<CreateSchedulePatternRequest>();

		/// <summary>
		/// Constructs a model object
		/// </summary>
		/// <returns>New model object</returns>
		public Schedule ToModel()
		{
			return new Schedule(Enabled, MaxActive, MaxChanges, RequireSubmittedChange, Gate?.ToModel(), Filter, Files, TemplateParameters, Patterns.ConvertAll(x => x.ToModel()));
		}
	}

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
		/// <param name="Pattern">The pattern to construct from</param>
		public GetSchedulePatternResponse(SchedulePattern Pattern)
		{
			this.DaysOfWeek = Pattern.DaysOfWeek?.ConvertAll(x => x.ToString());
			this.MinTime = Pattern.MinTime;
			this.MaxTime = Pattern.MaxTime;
			this.Interval = Pattern.Interval;
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
		/// <param name="Gate">Gate to construct from</param>
		public GetScheduleGateResponse(ScheduleGate Gate)
		{
			TemplateId = Gate.TemplateRefId.ToString();
			Target = Gate.Target;
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
		/// <param name="Schedule">The schedule to construct from</param>
		public GetScheduleResponse(Schedule Schedule)
		{
			this.Enabled = Schedule.Enabled;
			this.MaxActive = Schedule.MaxActive;
			this.MaxChanges = Schedule.MaxChanges;
			this.RequireSubmittedChange = Schedule.RequireSubmittedChange;
			if (Schedule.Gate != null)
			{
				this.Gate = new GetScheduleGateResponse(Schedule.Gate);
			}
			this.Filter = Schedule.Filter;
			this.TemplateParameters = Schedule.TemplateParameters;
			this.Patterns = Schedule.Patterns.ConvertAll(x => new GetSchedulePatternResponse(x));
			this.LastTriggerChange = Schedule.LastTriggerChange;
			this.LastTriggerTime = Schedule.LastTriggerTime;
			this.ActiveJobs = Schedule.ActiveJobs.ConvertAll(x => x.ToString());
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
		/// <param name="Times">List of trigger times</param>
		public GetScheduleForecastResponse(List<DateTime> Times)
		{
			this.Times = Times;
		}
	}
}
