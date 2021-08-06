// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	public partial class ClobberWindow : Form
	{
		Dictionary<string, bool> FilesToClobber;

		public ClobberWindow(Dictionary<string, bool> InFilesToClobber, HashSet<string> InUncontrolledFiles)
		{
			bool bUncontrolledChangeFound = false;

			InitializeComponent();

			FilesToClobber = InFilesToClobber;

			foreach(KeyValuePair<string, bool> FileToClobber in FilesToClobber)
			{
				ListViewItem Item = new ListViewItem(Path.GetFileName(FileToClobber.Key));
				Item.Tag = FileToClobber.Key;
				Item.Checked = FileToClobber.Value;
				Item.SubItems.Add(Path.GetDirectoryName(FileToClobber.Key));
				FileList.Items.Add(Item);

				if (InUncontrolledFiles.Contains(FileToClobber.Key.Replace("\\", "/")))
				{
					bUncontrolledChangeFound = true;
					Item.ForeColor = Color.Red;
				}
			}

			if (bUncontrolledChangeFound)
			{
				// Updates the string to inform the user to take special care with Uncontrolled Changes
				this.label1.Text = "The following files are writable in your workspace." + Environment.NewLine +
	"Red files are Uncontrolled Changes and may contain modifications you made on purpose." + Environment.NewLine +
	"Select which files you want to overwrite:";
			}
		}

		private void UncheckAll_Click(object sender, EventArgs e)
		{
			foreach(ListViewItem Item in FileList.Items)
			{
				Item.Checked = false;
			}
		}

		private void ContinueButton_Click(object sender, EventArgs e)
		{
			foreach(ListViewItem Item in FileList.Items)
			{
				FilesToClobber[(string)Item.Tag] = Item.Checked;
			}
		}
	}
}
