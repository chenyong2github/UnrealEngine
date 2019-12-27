// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Windows.Forms;


namespace DatasmithRevitExporter
{
	public class DatasmithRevitExportMessages : Form
	{
		private TextBox MessageBox;

		public DatasmithRevitExportMessages()
		{
			MessageBox = new TextBox();
			MessageBox.Name = "MessageBox";
			MessageBox.AutoSize = true;
			MessageBox.BorderStyle = BorderStyle.FixedSingle;
			MessageBox.Dock = DockStyle.Fill;
			MessageBox.ReadOnly = true;
			MessageBox.Multiline = true;
			MessageBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
			MessageBox.TabIndex = 0;
			MessageBox.WordWrap = true;

			Button OKButton = new Button();
			OKButton.Name = "OKButton";
			OKButton.Text = "OK";
			OKButton.Anchor = ((AnchorStyles)((AnchorStyles.Bottom | AnchorStyles.Right)));;
			OKButton.DialogResult = DialogResult.OK;
			OKButton.Margin = new Padding(3, 3, 3, 3);
			OKButton.Size = new Size(75, 23);
			OKButton.TabIndex = 1;
			OKButton.UseVisualStyleBackColor = true;
			OKButton.Click += new EventHandler(OKButtonClicked);

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 1;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 2;
			DialogLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.TabIndex = 2;

			DialogLayout.Controls.Add(MessageBox, 0, 0);
			DialogLayout.Controls.Add(OKButton,   0, 1);

			Name = "UnrealDatasmithExportMessages";
			Text = "Unreal Datasmith Export - Messages";
			AutoScaleDimensions = new SizeF(12F, 25F);
			AutoScaleMode = AutoScaleMode.Font;
			ClientSize = new Size(1200, 300);
			Padding = new Padding(10);
			ShowIcon = false;
			SizeGripStyle = SizeGripStyle.Show;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);
		}

		private void OKButtonClicked(
			object    InSender,
			EventArgs InEventArgs
		)
		{
			Close();
		}

		public string Messages
		{
			get
			{
				return MessageBox.Text;
			}
			set
			{
				MessageBox.Text = value;
				MessageBox.SelectionStart = 0;
			}
		}
	}
}
