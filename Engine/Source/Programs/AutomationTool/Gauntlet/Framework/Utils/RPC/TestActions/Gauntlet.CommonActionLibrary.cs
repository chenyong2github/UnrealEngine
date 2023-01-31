// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Newtonsoft.Json;


namespace Gauntlet
{
	/// <summary>
	/// Base actions that are used to determine things like process availability, getting RPC endpoints, WaitForMessage, etc.
	/// When an error occurs that should fail and stop the test, throwing an exception will automatically complete the test and set its result to failed.
	/// 
	/// These classes contain extension methods for RpcTargets based on the Role they have. When creating new actions, placing them in the correct
	/// class will make the test writing experience much cleaner as it will auto-filter based on the RpcTarget's role. The current categories are
	/// AnyRoleActions, EditorOnlyActions, ServerOnlyActions and ClientOnlyActions. If you have an action that can be called by more than 1 role 
	/// but less than all, place it in AnyRoleActions and comment which processes it works on.

	/// For those not familiar with Extension Methods, they are a way to quickly add functionality to a class without modifying it or deriving from it.
	/// By passing in (this <Type> ParamName) as the first argument of a public static method in a public static class, the method becomes available 
	/// to any object of that class as if it were defined in the class itself.
	/// For more info on Extension methods: https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/extension-methods
	/// </summary>
	public static partial class CommonActionLibrary
	{

		public static void PressApplicationCloseButton(this RpcTarget RpcTarget)
		{
			try
			{
				string ResponseString = RpcTarget.CallRpc(RpcEndpointRegistry.PressApplicationCloseButton);
				if (string.IsNullOrEmpty(ResponseString))
				{
					throw new TestException(RpcTarget.LogError("No HTTP Response From Process."));
				}

				GauntletHttpResponse Response = JsonConvert.DeserializeObject<GauntletHttpResponse>(ResponseString);

				if (Response == null || !Response.Success || !string.IsNullOrEmpty(Response.Reason))
				{
					string ErrorMsg = "Press Application Close Button RPC failed due to error reported from Process!";

					if (!string.IsNullOrEmpty(Response.Reason))
					{
						ErrorMsg = string.Format("{0} Reason: {1}", ErrorMsg, Response.Reason);
					}

					throw new TestException(RpcTarget.LogError(ErrorMsg));
				}

				RpcTarget.LogInfo("****Application Closed Successfully!****");

			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				throw new TestException(RpcTarget.LogError("Press Application CloseButton RPC failed due to an exception. Message: {0}, Stacktrace: {0}", Ex.Message, Ex.StackTrace));

			}
		}

		/// <summary>
		/// Executes a cheat via the Cheat Manager. You can see its C++ implementation in:
		/// UFortAutomationRpcManager::HttpExecuteConsoleCommand
		/// </summary>
		/// <param name="InRpcTarget"></param>
		/// <param name="InCheatName">The name of the cheat to execute</param>
		/// <param name="InCheatParams">Optional: If the cheat requires input parameters, they will be sent to the
		/// process formatted exactly as they are passed in.</param>
		public static void ExecuteCheatCommand(this RpcTarget InRpcTarget, string InCheatName, string InCheatParams = "")
		{
			try
			{
				Dictionary<string, object> Args = new Dictionary<string, object>();
				Args.Add("Command", string.Format("{0},{1}", InCheatName, InCheatParams));

				string ResponseString = InRpcTarget.CallRpc(RpcEndpointRegistry.ExecuteCheatCommand, Args);
				if (string.IsNullOrEmpty(ResponseString))
				{
					throw new TestException(InRpcTarget.LogError("No HTTP Response From Process."));
				}

				GauntletHttpResponse Response = JsonConvert.DeserializeObject<GauntletHttpResponse>(ResponseString);

				if (Response == null)
				{
					throw new TestException(InRpcTarget.LogError("Execute Cheat Command RPC Failed! Response is Null."));
				}

				if (!Response.Success)
				{
					throw new TestException(InRpcTarget.LogError("Execute Cheat Command RPC Failed! Process responded with error! Response data: Success:{0} Reason:{1}.", Response.Success.ToString(), Response.Reason));
				}

				InRpcTarget.LogInfo(string.Format("Cheat was executed successfully!", Response.Reason));

			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				throw new TestException(InRpcTarget.LogError("CloseEditor RPC failed due to an exception. Message: {0}, Stacktrace: {0}", Ex.Message, Ex.StackTrace));
			}
		}
	}
}
