# Copyright Epic Games, Inc. All Rights Reserved.

from PySide2 import QtWidgets, QtCore
from PySide2.QtWidgets import QHBoxLayout, QVBoxLayout, QTableView, QSizePolicy, QHeaderView, QPushButton, QSpacerItem
from PySide2.QtCore import Qt
 
class nDisplayMonitorUI(QtWidgets.QWidget):

    def __init__(self, parent, monitor):
        QtWidgets.QWidget.__init__(self, parent)

        # create button row
        #
        self.labelConsoleExec = QtWidgets.QLabel("Console:")

        self.cmbConsoleExec = QtWidgets.QComboBox()
        self.cmbConsoleExec.setEditable(True)
        self.cmbConsoleExec.lineEdit().returnPressed.connect(lambda: monitor.do_console_exec(self.cmbConsoleExec.lineEdit().text()))
        size = QSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)
        size.setHorizontalStretch(3)
        self.cmbConsoleExec.setSizePolicy(size)
        self.cmbConsoleExec.setMinimumWidth(150)

        self.btnConsoleExec = QPushButton("Exec")
        self.btnConsoleExec.setToolTip("Issues a console command via nDisplay cluster event")
        self.btnConsoleExec.clicked.connect(lambda: monitor.do_console_exec(self.cmbConsoleExec.lineEdit().text()))

        monitor.console_exec_issued.connect(lambda: self.cmbConsoleExec.lineEdit().clear())

        self.btnFixExeFlags = QPushButton("Fix ExeFlags")
        self.btnFixExeFlags.setToolTip("Disables fullscreen optimizations on the executable.")
        self.btnFixExeFlags.clicked.connect(monitor.btnFixExeFlags_clicked)

        self.btnSoftKill = QPushButton("Soft Kill")
        self.btnSoftKill.setToolTip(
            "Sends a message to the master node to terminate the session.\n"
            "This is preferable to the normal kill button because it ensures the nodes exit properly")
        self.btnSoftKill.clicked.connect(monitor.btnSoftKill_clicked)

        # arrange them in a horizontal layout
        layout_buttons = QHBoxLayout()
        layout_buttons.addWidget(self.labelConsoleExec)
        layout_buttons.addWidget(self.cmbConsoleExec)
        layout_buttons.addWidget(self.btnConsoleExec)
        layout_buttons.addStretch(1)
        layout_buttons.addWidget(self.btnFixExeFlags)
        layout_buttons.addWidget(self.btnSoftKill)

        # create table
        #
        self.tableview = QTableView()
        self.tableview.setModel(monitor) # the monitor is the model of this tableview.

        size = QSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)
        size.setHorizontalStretch(1)
        self.tableview.setSizePolicy(size)

        # configure resize modes on headers
        self.tableview.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self.tableview.horizontalHeader().setStretchLastSection(False)
        self.tableview.verticalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self.tableview.verticalHeader().setVisible(False)

        # create layout
        #
        layout = QVBoxLayout()

        layout.addLayout(layout_buttons)
        layout.addWidget(self.tableview)

        self.setLayout(layout)
