// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Reflection;
using Google.Protobuf.WellKnownTypes;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Tasks
{
	/// <summary>
	/// Information about a new task
	/// </summary>
	public class NewLeaseInfo
	{
		/// <summary>
		/// Lease to execute the task
		/// </summary>
		public AgentLease Lease { get; }

		/// <summary>
		/// Callback for connection to the agent being lost
		/// </summary>
		public Action? OnConnectionLost { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Lease"></param>
		/// <param name="OnConnectionLost"></param>
		public NewLeaseInfo(AgentLease Lease, Action? OnConnectionLost = null)
		{
			this.Lease = Lease;
			this.OnConnectionLost = OnConnectionLost;
		}
	}

	/// <summary>
	/// Interface for a an object that waits for a task
	/// </summary>
	public interface ITaskListener : IAsyncDisposable
	{
		/// <summary>
		/// Task returning a lease for the agent. If a null lease is returned, subsequent task sources will not be checked for leases.
		/// </summary>
		Task<NewLeaseInfo?> LeaseTask { get; }

		/// <summary>
		/// Whether this lease has been accepted. An lease which has not been accepted will be returned to the pool when the subscription is disposed.
		/// </summary>
		bool Accepted { get; set; }
	}

	class TaskSubscription : ITaskListener
	{
		public Task<NewLeaseInfo?> LeaseTask { get; }

		public bool Accepted { get; set; }

		public TaskSubscription(Task<NewLeaseInfo?> LeaseTask)
		{
			this.LeaseTask = LeaseTask;
		}

		public static TaskSubscription FromResult(AgentLease? Lease)
		{
			if (Lease == null)
			{
				return new TaskSubscription(Task.FromResult<NewLeaseInfo?>(null));
			}
			else
			{
				return new TaskSubscription(Task.FromResult<NewLeaseInfo?>(new NewLeaseInfo(Lease)));
			}
		}

		public ValueTask DisposeAsync()
		{
			return default;
		}
	}

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
		/// Subscribes an agent to execute leases for this source. The lifetime of the returned subscription object indicates the time that the agent is volunteering for work.
		/// </summary>
		/// <param name="Agent">The agent state</param>
		/// <returns>The subscription object</returns>
		Task<ITaskListener?> SubscribeAsync(IAgent Agent);

		/// <summary>
		/// Force a lease as complete
		/// </summary>
		/// <param name="Agent">The agent that was allocated to the lease</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">The lease payload</param>
		/// <returns>Async task</returns>
		Task AbortTaskAsync(IAgent Agent, ObjectId LeaseId, Any Payload);
	}

	/// <summary>
	/// Strongly typed base class for implementation of ITaskHandler
	/// </summary>
	/// <typeparam name="T">Type of task message</typeparam>
	public abstract class TaskSourceBase<T> : ITaskSource where T : IMessage<T>, new()
	{
		/// <inheritdoc/>
		public MessageDescriptor Descriptor { get; } = new T().Descriptor;

		/// <inheritdoc/>
		public abstract Task<ITaskListener?> SubscribeAsync(IAgent Agent);

		/// <inheritdoc/>
		public Task AbortTaskAsync(IAgent Agent, ObjectId LeaseId, Any Payload)
		{
			return AbortTaskAsync(Agent, LeaseId, Payload.Unpack<T>());
		}

		/// <summary>
		/// Cancels the given task
		/// </summary>
		/// <param name="Agent">Agent executing the task</param>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="Payload">The task type</param>
		/// <returns>Async task</returns>
		protected virtual Task AbortTaskAsync(IAgent Agent, ObjectId LeaseId, T Payload)
		{
			return Task.CompletedTask;
		}
	}
}
