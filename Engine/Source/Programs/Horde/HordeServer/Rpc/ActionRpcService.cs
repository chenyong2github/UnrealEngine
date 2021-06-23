// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeCommon.Rpc;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace HordeServer.Rpc
{
	class ActionRpcService : ActionRpc.ActionRpcBase
	{
		ActionTaskSource ActionTaskSource;
		ILogger<ActionRpcService> Logger;

		public ActionRpcService(ActionTaskSource ActionTaskSource, ILogger<ActionRpcService> Logger)
		{
			this.ActionTaskSource = ActionTaskSource;
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public override Task<Empty> PostActionResult(PostActionResultRequest Request, ServerCallContext Context)
		{
			IActionExecuteOperation? Operation;
			if (ActionTaskSource.TryGetOperation(Request.LeaseId.ToObjectId(), out Operation))
			{
				Logger.LogInformation("Setting operation result. Result={Result} Error={Error}. LeaseId={LeaseId} OperationId={OperationId}", Request.Result, Request.Error, Request.LeaseId, Operation.Id);
				if (Request.Error != null)
				{
					Operation.TrySetResult(new ActionExecuteResult(Request.Error));	
				}
				else if (Request.Result != null)
				{
					Operation.TrySetResult(new ActionExecuteResult(Request.Result));	
				}
				else
				{
					Logger.LogError("Both Result and Error are null!");
				}
				
			}
			return Task.FromResult<Empty>(new Empty());
		}
	}
}
