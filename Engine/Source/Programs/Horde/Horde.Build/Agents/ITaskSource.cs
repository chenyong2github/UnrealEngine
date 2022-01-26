// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Tasks
{
	using LeaseId = ObjectId<ILease>;

	/// <summary>
	/// Flags indicating when a task source is valid
	/// </summary>
	[Flags]
	public enum TaskSourceFlags
	{
		/// <summary>
		/// Normal behavior
		/// </summary>
		None = 0,

		/// <summary>
		/// Indicates that tasks from this source can run during downtime
		/// </summary>
		AllowDuringDowntime = 1,

		/// <summary>
		/// Allows this source to schedule tasks when the agent is disabled
		/// </summary>
		AllowWhenDisabled = 2,
	}

	/// <summary>
	/// Handler for a certain lease type
	/// </summary>
	public interface ITaskSource
	{
		/// <summary>
		/// The task type
		/// </summary>
		string Type { get; }

		/// <summary>
		/// Descriptor for this lease type
		/// </summary>
		MessageDescriptor Descriptor { get; }

		/// <summary>
		/// Flags controlling behavior of this source
		/// </summary>
		TaskSourceFlags Flags { get; }

		/// <summary>
		/// Assigns a lease or waits for one to be available
		/// </summary>
		/// <param name="Agent">The agent to assign a lease to</param>
		/// <param name="CancellationToken">Cancellation token for the wait</param>
		/// <returns>New lease object</returns>
		Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken);

		/// <summary>
		/// Cancel a lease that was previously assigned to an agent, allowing it to be assigned out again
		/// </summary>
		/// <param name="Agent">The agent that was assigned the lease</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">Payload for the lease</param>
		/// <returns></returns>
		Task CancelLeaseAsync(IAgent Agent, LeaseId LeaseId, Any Payload);

		/// <summary>
		/// Notification that a lease has been started
		/// </summary>
		/// <param name="Agent">The agent executing the lease</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">Payload for the lease</param>
		/// <param name="Logger">Logger for the agent</param>
		Task OnLeaseStartedAsync(IAgent Agent, LeaseId LeaseId, Any Payload, ILogger Logger);

		/// <summary>
		/// Notification that a task has completed
		/// </summary>
		/// <param name="Agent">The agent that was allocated to the lease</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">The lease payload</param>
		/// <param name="Outcome">Outcome of the lease</param>
		/// <param name="Output">Output from the task</param>
		/// <param name="Logger">Logger for the agent</param>
		Task OnLeaseFinishedAsync(IAgent Agent, LeaseId LeaseId, Any Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output, ILogger Logger);

		/// <summary>
		/// Gets information to include for a lease in a lease info response
		/// </summary>
		/// <param name="Payload">The lease payload</param>
		/// <param name="Details">Properties for the lease</param>
		void GetLeaseDetails(Any Payload, Dictionary<string, string> Details);
	}
	
	/// <summary>
	/// Base implementation of <see cref="ITaskSource"/>
	/// </summary>
	/// <typeparam name="TMessage"></typeparam>
	public abstract class TaskSourceBase<TMessage> : ITaskSource where TMessage : IMessage, new()
	{
		/// <summary>
		/// List of properties that can be output as log messages
		/// </summary>
		protected class PropertyList
		{
			internal StringBuilder FormatString = new StringBuilder();
			internal List<Func<TMessage, object>> Accessors = new List<Func<TMessage, object>>();
			internal List<(string, Func<TMessage, object>)> JsonAccessors = new List<(string, Func<TMessage, object>)>();

			/// <summary>
			/// Adds a new property to the list
			/// </summary>
			/// <param name="Expr">Accessor for the property</param>
			public PropertyList Add(Expression<Func<TMessage, object>> Expr)
			{
				MemberInfo Member = ((MemberExpression)Expr.Body).Member;
				return Add(Member.Name, Expr.Compile());
			}

			/// <summary>
			/// Adds a new property to the list
			/// </summary>
			/// <param name="Name">Name of the property</param>
			/// <param name="Accessor">Accessor for the property</param>
			[System.Diagnostics.CodeAnalysis.SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "<Pending>")]
			public PropertyList Add(string Name, Func<TMessage, object> Accessor)
			{
				FormatString.Append(CultureInfo.InvariantCulture, $", {Name}={{{Name}}}");
				Accessors.Add(Accessor);
				JsonAccessors.Add((Name[0..1].ToLower(CultureInfo.InvariantCulture) + Name[1..], Accessor));
				return this;
			}
		}

		static TMessage Message = new TMessage();

		/// <inheritdoc/>
		public abstract string Type { get; }

		/// <inheritdoc/>
		public abstract TaskSourceFlags Flags { get; }

		/// <inheritdoc/>
		protected virtual PropertyList OnLeaseStartedProperties { get; } = new PropertyList();

		/// <inheritdoc/>
		public MessageDescriptor Descriptor => Message.Descriptor;

		/// <inheritdoc/>
		public abstract Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken);

		/// <inheritdoc/>
		public Task CancelLeaseAsync(IAgent Agent, LeaseId LeaseId, Any Payload) => CancelLeaseAsync(Agent, LeaseId, Payload.Unpack<TMessage>());

		/// <inheritdoc/>
		public Task OnLeaseStartedAsync(IAgent Agent, LeaseId LeaseId, Any Payload, ILogger Logger) => OnLeaseStartedAsync(Agent, LeaseId, Payload.Unpack<TMessage>(), Logger);

		/// <inheritdoc/>
		public Task OnLeaseFinishedAsync(IAgent Agent, LeaseId LeaseId, Any Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output, ILogger Logger) => OnLeaseFinishedAsync(Agent, LeaseId, Payload.Unpack<TMessage>(), Outcome, Output, Logger);

		/// <inheritdoc cref="ITaskSource.CancelLeaseAsync(IAgent, LeaseId, Any)"/>
		public virtual Task CancelLeaseAsync(IAgent Agent, LeaseId LeaseId, TMessage Payload) => Task.CompletedTask;

		/// <inheritdoc cref="ITaskSource.OnLeaseStartedAsync(IAgent, LeaseId, Any, ILogger)"/>
		public virtual Task OnLeaseStartedAsync(IAgent Agent, LeaseId LeaseId, TMessage Payload, ILogger Logger)
		{
			object[] Arguments = new object[2 + OnLeaseStartedProperties.Accessors.Count];
			Arguments[0] = LeaseId;
			Arguments[1] = Type;
			for (int Idx = 0; Idx < OnLeaseStartedProperties.Accessors.Count; Idx++)
			{
				Arguments[Idx + 2] = OnLeaseStartedProperties.Accessors[Idx](Payload);
			}
#pragma warning disable CA2254 // Template should be a static expression
			Logger.LogInformation($"Lease {{LeaseId}} started (Type={{Type}}{OnLeaseStartedProperties.FormatString})", Arguments);
#pragma warning restore CA2254 // Template should be a static expression
			return Task.CompletedTask;
		}

		/// <inheritdoc cref="ITaskSource.OnLeaseFinishedAsync(IAgent, LeaseId, Any, LeaseOutcome, ReadOnlyMemory{byte}, ILogger)"/>
		public virtual Task OnLeaseFinishedAsync(IAgent Agent, LeaseId LeaseId, TMessage Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output, ILogger Logger)
		{
			Logger.LogInformation("Lease {LeaseId} complete", LeaseId);
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public virtual void GetLeaseDetails(Any Payload, Dictionary<string, string> Details)
		{
			Details["type"] = Type;

			if (OnLeaseStartedProperties.Accessors.Count > 0)
			{
				TMessage Message = Payload.Unpack<TMessage>();
				foreach ((string Name, Func<TMessage, object> GetMethod) in OnLeaseStartedProperties.JsonAccessors)
				{
					Details[Name] = GetMethod(Message)?.ToString() ?? String.Empty;
				}
			}
		}
	}
}
