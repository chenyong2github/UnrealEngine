// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Linq.Expressions;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	using JobId = ObjectId<IJob>;
	using LeaseId = ObjectId<ILease>;
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Base class for Horde controllers
	/// </summary>
	public abstract class HordeControllerBase : ControllerBase
	{
		/// <summary>
		/// Create a response indicating a problem with the request
		/// </summary>
		/// <param name="Message">Standard structured-logging format string</param>
		/// <param name="Args">Arguments for the format string</param>
		[NonAction]
		protected ActionResult BadRequest(string Message, params object[] Args)
		{
			return BadRequest(LogEvent.Create(LogLevel.Error, Message, Args));
		}

		/// <summary>
		/// Create a response indicating a problem with the request
		/// </summary>
		/// <param name="EventId">Id identifying the error</param>
		/// <param name="Message">Standard structured-logging format string</param>
		/// <param name="Args">Arguments for the format string</param>
		[NonAction]
		protected ActionResult BadRequest(EventId EventId, string Message, params object[] Args)
		{
			return BadRequest(LogEvent.Create(LogLevel.Error, EventId, Message, Args));
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(AclAction Action)
		{
			return Forbid("User does not have {Action} permission", Action);
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action and object
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(AclAction Action, AgentId AgentId)
		{
			return Forbid(Action, "agent {AgentId}", AgentId);
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action and object
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(AclAction Action, JobId JobId)
		{
			return Forbid(Action, "job {JobId}", JobId);
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action and object
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(AclAction Action, ProjectId ProjectId)
		{
			return Forbid(Action, "project {ProjectId}", ProjectId);
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action and object
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(AclAction Action, StreamId StreamId)
		{
			return Forbid(Action, "stream {StreamId}", StreamId);
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action and object
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(AclAction Action, TemplateRefId TemplateId)
		{
			return Forbid(Action, "template {TemplateId}", TemplateId);
		}

		/// <summary>
		/// Returns a 403 (forbidden) response with the given action and object
		/// </summary>
		[NonAction]
		private ActionResult Forbid(AclAction Action, string ObjectMessage, object Object)
		{
			return Forbid($"User does not have {{Action}} permission for {ObjectMessage}", Action, Object);
		}

		/// <summary>
		/// Returns a 403 response with the given log event
		/// </summary>
		[NonAction]
		protected ActionResult Forbid(string Message, params object[] Args)
		{
			return StatusCode(StatusCodes.Status403Forbidden, LogEvent.Create(LogLevel.Error, Message, Args));
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(AgentId AgentId)
		{
			return NotFound("Agent {AgentId} not found", AgentId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(AgentId AgentId, LeaseId LeaseId)
		{
			return NotFound("Lease {LeaseId} not found for agent {AgentId}", LeaseId, AgentId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(JobId JobId)
		{
			return NotFound("Job {JobId} not found", JobId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(JobId JobId, int GroupIdx)
		{
			return NotFound("Group {GroupIdx} on job {JobId} not found", GroupIdx, JobId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(JobId JobId, int GroupIdx, int NodeIdx)
		{
			return NotFound("Node {NodeIdx} not found on job {JobId} group {GroupIdx}", NodeIdx, JobId, GroupIdx);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(JobId JobId, SubResourceId BatchId)
		{
			return NotFound("Batch {BatchId} not found on job {JobId}", BatchId, JobId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(JobId JobId, SubResourceId BatchId, SubResourceId StepId)
		{
			return NotFound("Step {StepId} not found on job {JobId} batch {BatchId}", StepId, JobId, BatchId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(StreamId StreamId)
		{
			return NotFound("Stream {StreamId} not found", StreamId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(StreamId StreamId, TemplateRefId TemplateId)
		{
			return NotFound("Template {TemplateId} not found on stream {StreamId}", TemplateId, StreamId);
		}

		/// <summary>
		/// Returns a 404 response for the given object
		/// </summary>
		[NonAction]
		protected ActionResult NotFound(string Message, params object[] Args)
		{
			return NotFound(LogEvent.Create(LogLevel.Error, Message, Args));
		}
	}
}
