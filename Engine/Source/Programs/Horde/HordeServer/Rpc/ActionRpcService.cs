using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using HordeCommon.Rpc;
using HordeServer.Tasks.Impl;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Rpc
{
	class ActionRpcService : ActionRpc.ActionRpcBase
	{
		ActionTaskSource ActionTaskSource;

		public ActionRpcService(ActionTaskSource ActionTaskSource)
		{
			this.ActionTaskSource = ActionTaskSource;
		}

		/// <inheritdoc/>
		public override Task<Empty> PostActionResult(PostActionResultRequest Request, ServerCallContext Context)
		{
			IActionExecuteOperation? Operation;
			if (ActionTaskSource.TryGetOperation(Request.LeaseId.ToObjectId(), out Operation))
			{
				Operation.TrySetResult(Request.Result);
			}
			return Task.FromResult<Empty>(new Empty());
		}
	}
}
