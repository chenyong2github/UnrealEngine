// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Tasks
{
	/// <summary>
	/// Handler for a certain lease type
	/// </summary>
	public interface ITaskSource
	{
		/// <summary>
		/// Descriptor for this lease type
		/// </summary>
		MessageDescriptor Descriptor { get; }

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
		Task CancelLeaseAsync(IAgent Agent, ObjectId LeaseId, Any Payload);

		/// <summary>
		/// Notification that a lease has been started
		/// </summary>
		/// <param name="Agent">The agent executing the lease</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">Payload for the lease</param>
		Task OnLeaseStartedAsync(IAgent Agent, ObjectId LeaseId, Any Payload);

		/// <summary>
		/// Notification that a task has completed
		/// </summary>
		/// <param name="Agent">The agent that was allocated to the lease</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">The lease payload</param>
		/// <param name="Outcome">Outcome of the lease</param>
		/// <param name="Output">Output from the task</param>
		Task OnLeaseFinishedAsync(IAgent Agent, ObjectId LeaseId, Any Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output);
	}
	
	/// <summary>
	/// Base implementation of <see cref="ITaskSource"/>
	/// </summary>
	/// <typeparam name="TMessage"></typeparam>
	public abstract class TaskSourceBase<TMessage> : ITaskSource where TMessage : IMessage, new()
	{
		static TMessage Message = new TMessage();

		/// <inheritdoc/>
		public MessageDescriptor Descriptor => Message.Descriptor;

		/// <inheritdoc/>
		public abstract Task<AgentLease?> AssignLeaseAsync(IAgent Agent, CancellationToken CancellationToken);

		/// <inheritdoc/>
		public virtual Task CancelLeaseAsync(IAgent Agent, ObjectId LeaseId, Any Payload) => Task.CompletedTask;

		/// <inheritdoc/>
		public virtual Task OnLeaseStartedAsync(IAgent Agent, ObjectId LeaseId, Any Payload) => Task.CompletedTask;

		/// <inheritdoc/>
		public virtual Task OnLeaseFinishedAsync(IAgent Agent, ObjectId LeaseId, Any Payload, LeaseOutcome Outcome, ReadOnlyMemory<byte> Output) => Task.CompletedTask;
	}
}
