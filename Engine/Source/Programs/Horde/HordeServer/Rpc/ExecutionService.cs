// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using EpicGames.Core;
using Google.LongRunning;
using Google.Protobuf;
using Grpc.Core;
using HordeServer.Storage;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Text;
using Google.Protobuf.WellKnownTypes;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using HordeServer.Storage.Primitives;
using HordeServer.Utility;
using HordeCommon.Rpc.Tasks;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;

using Action = Build.Bazel.Remote.Execution.V2.Action;
using Directory = Build.Bazel.Remote.Execution.V2.Directory;
using Status = Google.Rpc.Status;

namespace HordeServer.Rpc
{
	class ExecutionService : Execution.ExecutionBase
	{
		ActionTaskSource ActionTaskSource;

		public ExecutionService(ActionTaskSource ActionTaskSource)
		{
			this.ActionTaskSource = ActionTaskSource;
		}

		public override async Task Execute(ExecuteRequest Request, IServerStreamWriter<Operation> ResponseStream, ServerCallContext Context)
		{
			// Try to create the operation
			IActionExecuteOperation? Operation = ActionTaskSource.Execute(Request);
			if (Operation == null)
			{
				throw new RpcException(new Grpc.Core.Status(StatusCode.ResourceExhausted, "No agent available to execute request"));
			}

			// Return all the status updates
			await foreach (Operation Status in Operation.ReadStatusUpdatesAsync())
			{
				await ResponseStream.WriteAsync(Status);
			}
		}

		public override async Task WaitExecution(WaitExecutionRequest Request, IServerStreamWriter<Operation> ResponseStream, ServerCallContext Context)
		{
			// Get the operation
			IActionExecuteOperation? Operation;
			if (!ActionTaskSource.TryGetOperation(Request.Name.ToObjectId(), out Operation))
			{
				throw new RpcException(new Grpc.Core.Status(StatusCode.NotFound, "No agent available to execute request"));
			}

			// Stream all the status updates
			await foreach (Operation Status in Operation.ReadStatusUpdatesAsync())
			{
				await ResponseStream.WriteAsync(Status);
			}
		}
	}
}
