# Copyright Epic Games, Inc. All Rights Reserved.

import switchboard.switchboard_widgets as sb_widgets
from switchboard.switchboard_logging import LOGGER

from PySide2 import QtCore, QtGui, QtWidgets


class DeviceListWidget(QtWidgets.QListWidget):
    signal_register_device_widget = QtCore.Signal(object)
    signal_remove_device = QtCore.Signal(object)

    def __init__(self, name, parent=None):
        super().__init__(parent)

        self.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.setFocusPolicy(QtCore.Qt.NoFocus)
        #self.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
        self.setSelectionMode(QtWidgets.QAbstractItemView.MultiSelection)

        self._device_widgets = {}
        self._header_by_category_name = {}
        self.setMinimumSize(self.minimumSize().width(), 5)

    # Override all mouse events so that selection can be used for qss styling
    def mousePressEvent(self, e):
        pass

    def mouseReleaseEvent(self, e):
        pass

    def mouseDoubleClickEvent(self, e):
        pass

    def mouseMoveEvent(self, e):
        pass

    def contextMenuEvent(self, event):
        device_context_menu = QtWidgets.QMenu(self)
        device_context_menu.addAction("Remove Device", lambda: self.ask_to_confirm_device_removal(event.pos()))
        device_context_menu.exec_(event.globalPos())
        event.accept()

    def ask_to_confirm_device_removal(self, pos):
        item = self.itemAt(pos)
        item_widget = self.itemWidget(item)

        confirmation = QtWidgets.QMessageBox.question(self, "Device Removal Confirmation", "Are you sure you want to delete this device?")
        if confirmation == QtWidgets.QMessageBox.Yes:
            self.signal_remove_device.emit(item_widget.device_hash)

    def on_device_removed(self, device_hash, *args):
        self._device_widgets.pop(device_hash)

        for i in range(self.count()):
            item = self.item(i)
            widget = self.itemWidget(item)
            if widget.device_hash == device_hash:
                self.takeItem(self.row(item))
                break

    def add_header(self, label_name, changelist=True):
        device_item = QtWidgets.QListWidgetItem()
        device_item.setSizeHint(QtCore.QSize(self.parent().width(), sb_widgets.DEVICE_HEADER_LIST_WIDGET_HEIGHT))
        self.addItem(device_item)
        self.setItemWidget(device_item, DeviceWidgetHeader(label_name, changelist=changelist))

        return device_item

    def add_device_widget(self, device):
        category = device.category_name
        if category not in self._header_by_category_name.keys():
            self._header_by_category_name[category] = self.add_header(category + " Devices")

        device_item = QtWidgets.QListWidgetItem()
        device_item.setSizeHint(QtCore.QSize(self.parent().width(), sb_widgets.DEVICE_LIST_WIDGET_HEIGHT))
        self.addItem(device_item)
        self.setItemWidget(device_item, device.widget)

        # Keep a dict for quick lookup
        self._device_widgets[device.device_hash] = device.widget

        self.signal_register_device_widget.emit(device.widget)
        # Force an update to put it in the correct position
        self.update_status(device, device.status)

        return device.widget

    def clear_widgets(self):
        self.clear()
        self._device_widgets = {}
        self._header_by_category_name = {}

    def widget_item_by_device(self, device):
        for i in range(self.count()):
            item = self.item(i)
            widget = self.itemWidget(item)

            if not widget:
                continue

            if widget.device_hash == device.device_hash:
                return item
        return None

    def device_widget_by_hash(self, device_hash):
        if device_hash not in self._device_widgets:
            return None

        return self._device_widgets[device_hash]

    def device_widgets(self):
        return self._device_widgets.values()

    def _get_new_index(self, device):
        widget_item = self.widget_item_by_device(device)
        current_row = self.row(widget_item)

        # create a list of all rows that contain a header, add the row of the item to it and do a sort.
        # this way it's easy to figure out in-between which categories the item currently is.
        header_rows = []
        for _, header in self._header_by_category_name.items():
            header_rows.append(self.row(header))
        header_rows.append(current_row)
        header_rows.sort()
        current_header_row = header_rows[header_rows.index(current_row)-1]

        try:
            new_device_header = self._header_by_category_name[device.category_name]
        except KeyError:
            # if the category does not exist, create it and try again.
            self._header_by_category_name[device.category_name] = self.add_header(device.category_name + " Devices")
            return self._get_new_index(device)

        new_header_row = self.row(new_device_header)

        if new_header_row is current_header_row:
            return None

        try:
            # try to find the next header, so the device is moved to the row just _before_ that header.
            # should that not work because the new header is already the last one we'll get an IndexError,
            # in which case we move the item to the end of the list.
            new_item_row = header_rows[header_rows.index(new_header_row)+1]
        except IndexError:
            new_item_row = self.count()

        return new_item_row

    def update_status(self, device, previous_status):
        widget_item = self.widget_item_by_device(device)
        device_widget = self.itemWidget(widget_item)

        # Tell the widget to update
        device_widget.update_status(device.status, previous_status)

        new_index = self._get_new_index(device)
        # Move the item if needed
        if new_index:
            widget_item_clone = widget_item.clone()
            self.insertItem(new_index, widget_item_clone)
            self.setItemWidget(widget_item_clone, device_widget)
            self.takeItem(self.row(widget_item))


class DeviceWidgetHeader(QtWidgets.QWidget):
    def __init__(self, name, changelist=True, parent=None):
        super().__init__(parent)

        self.ip_address_label = None
        self.device_hash = 0 # When list widget is looking for devices it checks the device_has

        self.layout = QtWidgets.QHBoxLayout()
        self.layout.setContentsMargins(0, 6, 30, 6)
        self.setLayout(self.layout)

        def __label(label_text):
            label = QtWidgets.QLabel()
            label.setText(label_text)
            label.setAlignment(QtCore.Qt.AlignCenter)
            label.setStyleSheet("font-weight: bold")
            return label

        self.name_label = __label(name)
        self.ip_address_label = __label('IP Address')
        self.changelist_label = __label('Changelist') if changelist else None
        self.controls_label = __label('Controls')

        self.layout.addWidget(self.name_label)
        self.layout.addWidget(self.ip_address_label)

        spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)
        self.layout.addItem(spacer)

        if self.changelist_label:
            self.layout.addWidget(self.changelist_label)
            spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)
            self.layout.addItem(spacer)

        self.layout.addWidget(self.controls_label)

        self.name_label.setMinimumSize(QtCore.QSize(235, 0))
        self.ip_address_label.setMinimumSize(QtCore.QSize(100, 0))

    def showEvent(self, event):
        super().showEvent(event)

        self.find_ip_address_label()

    def find_ip_address_label(self):
        labels = self.findChildren(QtWidgets.QLabel)
        for label in labels:
            if label.text() == 'IP Address':
                self.ip_address_label = label
                return

    def resizeEvent(self, event):
        super().resizeEvent(event)

        if not self.ip_address_label:
            return

        width = event.size().width()

        if width < sb_widgets.DEVICE_WIDGET_HIDE_IP_ADDRESS_WIDTH:
            self.ip_address_label.hide()
        else:
            self.ip_address_label.show()

    def paintEvent(self, event):
        opt = QtWidgets.QStyleOption()
        opt.initFrom(self)
        painter = QtGui.QPainter(self)
        self.style().drawPrimitive(QtWidgets.QStyle.PE_Widget, opt, painter, self)
