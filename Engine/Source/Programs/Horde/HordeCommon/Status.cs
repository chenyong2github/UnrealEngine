using System;
using System.Collections.Generic;
using System.Text;

namespace Google.Rpc
{
	public partial class Status
	{
		public Status(Grpc.Core.StatusCode Code, string Message)
		{
			this.Code = (int)Code;
			this.Message = Message;
		}

		public static implicit operator Grpc.Core.Status(Status Other)
		{
			return new Grpc.Core.Status((Grpc.Core.StatusCode)Other.Code, Other.Message);
		}

		public static implicit operator Status(Grpc.Core.Status Other)
		{
			return new Status((Grpc.Core.StatusCode)Other.StatusCode, Other.Detail);
		}
	}
}
