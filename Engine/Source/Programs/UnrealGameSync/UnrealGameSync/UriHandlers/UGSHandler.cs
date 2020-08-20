// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace UnrealGameSync
{

	/// <summary>
	/// UGS handler 
	/// </summary>
	static class UGSHandler
	{

		[UriHandler]
		public static UriResult OpenProject(string Stream, string Project, bool Sync = false)
		{
			// Create the request
			using (MemoryStream InputDataStream = new MemoryStream())
			{
				using (BinaryWriter Writer = new BinaryWriter(InputDataStream))
				{
					Writer.Write(Stream);
					Writer.Write(Project);
				}

				AutomationRequestInput Input = new AutomationRequestInput(Sync ? AutomationRequestType.SyncProject : AutomationRequestType.OpenProject, InputDataStream.GetBuffer());
				return new UriResult() { Success = true, Request = new AutomationRequest(Input) };
			}
		}

		[UriHandler]
		public static UriResult BuildStep(string Project, string Stream, string Step, string Changelist, string Arguments)
		{
			MessageBox.Show(string.Format("Project: {0}\nStream: {1}\nStep: {2}\nChange: {3}\nArguments: {4}", Project, Stream, Step, Changelist, Arguments), "UGS Build Step Handler");

			return new UriResult() { Success = true };
		}

		[UriHandler]
		public static UriResult Execute(string Stream, int Changelist, string Command, string Project = "")
		{
			using (MemoryStream InputDataStream = new MemoryStream())
			{
				using (BinaryWriter Writer = new BinaryWriter(InputDataStream))
				{
					Writer.Write(Stream);
					Writer.Write(Changelist);
					Writer.Write(Command);
					Writer.Write(Project);
				}

				AutomationRequestInput Input = new AutomationRequestInput(AutomationRequestType.ExecCommand, InputDataStream.GetBuffer());
				return new UriResult() { Success = true, Request = new AutomationRequest(Input) };
			}
		}

		[UriHandler]
		public static UriResult OpenIssue(int Id)
		{
			using (MemoryStream InputDataStream = new MemoryStream())
			{
				using (BinaryWriter Writer = new BinaryWriter(InputDataStream))
				{
					Writer.Write(Id);
				}

				AutomationRequestInput Input = new AutomationRequestInput(AutomationRequestType.OpenIssue, InputDataStream.GetBuffer());
				return new UriResult() { Success = true, Request = new AutomationRequest(Input) };
			}
		}
	}
}