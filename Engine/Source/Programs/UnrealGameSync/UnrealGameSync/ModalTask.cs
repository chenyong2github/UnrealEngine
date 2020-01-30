// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	interface IModalTask
	{
		bool Run(out string ErrorMessage);
	}

	public enum ModalTaskResult
	{
		Succeeded,
		Failed,
		Aborted,
	}

	static class ModalTask
	{
		public static ModalTaskResult Execute(IWin32Window Owner, IModalTask Task, string InTitle, string InMessage, out string ErrorMessage)
		{
			ModalTaskWindow Window = new ModalTaskWindow(Task, InTitle, InMessage, (Owner == null)? FormStartPosition.CenterScreen : FormStartPosition.CenterParent);
			Window.Complete += () => Window.Close();
			Window.ShowDialog(Owner);
			ErrorMessage = Window.ErrorMessage;
			return Window.Result;
		}

		public static bool ExecuteAndShowError(IWin32Window Owner, IModalTask Task, string InTitle, string InMessage)
		{
			string ErrorMessage;
			ModalTaskResult Result = Execute(Owner, Task, InTitle, InMessage, out ErrorMessage);
			if(Result != ModalTaskResult.Succeeded)
			{
				if (!String.IsNullOrEmpty(ErrorMessage))
				{
					MessageBox.Show(ErrorMessage);
				}
				return false;
			}
			return true;
		}
	}
}
