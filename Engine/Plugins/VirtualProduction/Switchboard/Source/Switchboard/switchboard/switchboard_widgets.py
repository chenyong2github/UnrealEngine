# Copyright Epic Games, Inc. All Rights Reserved.
from PySide2 import QtCore, QtGui, QtWidgets
import time

DEVICE_LIST_WIDGET_HEIGHT = 54
DEVICE_HEADER_LIST_WIDGET_HEIGHT = 40
DEVICE_WIDGET_HIDE_IP_ADDRESS_WIDTH = 500


class NonScrollableComboBox(QtWidgets.QComboBox):
    def __init__(self, parent=None):
        super().__init__(parent=parent)
        self.installEventFilter(self)

    def eventFilter(self, obj, event):
        if obj == self and event.type() == QtCore.QEvent.Wheel:
            event.ignore()
            return True
        return False


# Each entry has a checkbox, the first element is a lineedit that will show all selected entries
class MultiSelectionComboBox(QtWidgets.QComboBox):
    separator = ' | '
    signal_selection_changed = QtCore.Signal(list)

    def __init__(self, parent=None):
        super().__init__(parent=parent)
        self.model().itemChanged.connect(self.on_stateChanged)
        # an editable combo has a lineedit at the first entry which we can use to show a list of all selected entries
        self.setEditable(True)
        super().addItem("")
        item = self.model().item(0, 0)
        item.setEnabled(False)
        # we only care about the editablilty as a means to get a lineedit, the user isn't allowed to change the text manually
        self.lineEdit().installEventFilter(self)
        self.lineEdit().setReadOnly(True)
        # disallow text selection
        self.lineEdit().selectionChanged.connect(lambda: self.lineEdit().setSelection(0, 0))

        self.popup_is_showing = False
        # the combo calls show/hidePopup internally, to avoid messing up the state we only allow showing/hiding the popup
        # if it happened inside a time intervall that could have reasonably been triggered by a user.
        # this is obviously a workaround but there seems to be no way to get better behavior w/o implementing a full-blown combobox.
        self.last_time_popup_was_triggered = time.time()

    def add_items(self, selected_entries, all_entries):
        for entry in all_entries:
            self.addItem(entry)
            item = self.model().item(self.count()-1, 0)
            item.setFlags(QtCore.Qt.ItemIsUserCheckable | QtCore.Qt.ItemIsEnabled)
            state = QtCore.Qt.Checked if entry in selected_entries else QtCore.Qt.Unchecked
            item.setCheckState(state)

        invalid_entries = [entry for entry in selected_entries if entry not in all_entries]
        for entry in invalid_entries:
            self.addItem(entry)
            item = self.model().item(self.count()-1, 0)
            item.setFlags(QtCore.Qt.ItemIsUserCheckable | QtCore.Qt.ItemIsEnabled)
            state = QtCore.Qt.Checked
            item.setCheckState(state)

            brush = item.foreground()
            brush.setColor(QtCore.Qt.red)
            item.setForeground(brush)

    def eventFilter(self, obj, event):
        if obj == self.lineEdit() and event.type() == QtCore.QEvent.MouseButtonPress:
            # if the lineedit is clicked we want the combo to open/close
            if self.popup_is_showing:
                self.hidePopup()
            else:
                self.showPopup()
            event.accept()
            return True
        elif obj == self and event.type() == QtCore.QEvent.Wheel:
            event.ignore()
            return True
        return False

    def wheelEvent(self, event):
        event.ignore()
        return True

    def showPopup(self):
        now = time.time()
        diff = abs(now - self.last_time_popup_was_triggered)
        if diff > 0.1:
            super().showPopup()
            self.popup_is_showing = True
            self.last_time_popup_was_triggered = now

    def hidePopup(self):
        now = time.time()
        diff = abs(now - self.last_time_popup_was_triggered)
        if diff > 0.1:
            super().hidePopup()
            self.popup_is_showing = False
            self.last_time_popup_was_triggered = now

    def on_stateChanged(self, item):
        self.clearEditText()
        selected_entries = []
        for i in range(self.count()):
            item = self.model().item(i, 0)
            if item.checkState() == QtCore.Qt.Checked:
                selected_entries.append(self.itemText(i))

        if len(selected_entries) > 0:
            self.setEditText(self.separator.join(selected_entries))
        self.signal_selection_changed.emit(selected_entries)


class ControlQPushButton(QtWidgets.QPushButton):
    def __init__(self, parent=None):
        super().__init__(parent)

    def focusInEvent(self, e):
        super().focusInEvent(e)

    def enterEvent(self, event):
        super().enterEvent(event)
        self.setFocus()

    def leaveEvent(self, event):
        super().leaveEvent(event)
        self.clearFocus()

    @classmethod
    def create(self, icon_off, icon_on=None,
                icon_hover_on=None, icon_hover=None,
                icon_disabled_on=None, icon_disabled=None,
                icon_size=None,
                checkable=True, checked=False,
                tool_tip=None, parent=True):
        button = ControlQPushButton()

        button.setProperty("no_background", True)
        button.setStyle(button.style())

        icon = QtGui.QIcon()

        if icon_on:
            pixmap = QtGui.QPixmap(icon_on)
            icon.addPixmap(pixmap, QtGui.QIcon.Normal, QtGui.QIcon.On)

        if icon_hover:
            pixmap = QtGui.QPixmap(icon_hover)
            icon.addPixmap(pixmap, QtGui.QIcon.Active, QtGui.QIcon.Off)

        if icon_hover_on:
            pixmap = QtGui.QPixmap(icon_hover_on)
            icon.addPixmap(pixmap, QtGui.QIcon.Active, QtGui.QIcon.On)

        if icon_disabled:
            pixmap = QtGui.QPixmap(icon_disabled)
            icon.addPixmap(pixmap, QtGui.QIcon.Disabled, QtGui.QIcon.Off)

        if icon_disabled_on:
            pixmap = QtGui.QPixmap(icon_disabled)
            icon.addPixmap(pixmap, QtGui.QIcon.Disabled, QtGui.QIcon.On)

        pixmap = QtGui.QPixmap(icon_off)
        icon.addPixmap(pixmap, QtGui.QIcon.Normal, QtGui.QIcon.Off)

        button.setIcon(icon)

        if not icon_size:
            icon_size = pixmap.rect().size()
        button.setIconSize(icon_size)
        button.setMinimumSize(QtCore.QSize(25, 35))

        if tool_tip:
            button.setToolTip(tool_tip)

        if checkable:
            button.setCheckable(checkable)
            button.setChecked(checked)

        return button


class FramelessQLineEdit(QtWidgets.QLineEdit):
    def __init__(self, parent=None):
        super().__init__(parent)

        if not self.isReadOnly():
            self.setProperty("frameless", True)
            self.setStyle(self.style())

        self.current_text = None
        self.is_valid = True

    def enterEvent(self, event):
        super().enterEvent(event)

        if not self.isReadOnly():
            set_qt_property(self, "frameless", False)

    def leaveEvent(self, event):
        super().leaveEvent(event)

        if self.hasFocus():
            return

        if not self.isReadOnly():
            set_qt_property(self, "frameless", True)

    def focusInEvent(self, e):
        super().focusInEvent(e)

        # Store the current value
        self.current_text = self.text()

        if not self.isReadOnly():
            set_qt_property(self, "frameless", False)

    def focusOutEvent(self, e):
        super().focusOutEvent(e)

        if not self.isReadOnly():
            set_qt_property(self, "frameless", True)

        if not self.is_valid:
            self.setText(self.current_text)
            self.is_valid = True

            set_qt_property(self, "error", False)

    def keyPressEvent(self, e):
        super().keyPressEvent(e)

        if e.key() == QtCore.Qt.Key_Return or e.key() == QtCore.Qt.Key_Enter:
            if self.is_valid:
                self.clearFocus()
        elif e.key() == QtCore.Qt.Key_Escape:
            self.setText(self.current_text)
            self.clearFocus()


def set_qt_property(widget, prop, value):
    widget.setProperty(prop, value)
    widget.setStyle(widget.style())
