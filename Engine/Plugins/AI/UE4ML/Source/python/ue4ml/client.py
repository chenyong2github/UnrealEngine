# Copyright Epic Games, Inc. All Rights Reserved.
 
import msgpackrpc
from msgpackrpc.error import TransportError
from . import logger
from .utils import *
import threading


class Client(msgpackrpc.Client):
    __port_range_start = DEFAULT_PORT
    __port_range_span = 128
    __next_available_port_offset = 0
    __lock = threading.Lock()
    
    def __init__(self, server_address=LOCALHOST, server_port=DEFAULT_PORT, timeout=20, **kwargs):
        
        if server_port is None:
            server_port = Client.next_available_port(server_address)
        
        address = msgpackrpc.Address(server_address, server_port)
        super().__init__(address, timeout=timeout, **kwargs)
        self._restart = lambda: self.__init__(server_address, server_port, **kwargs)

    def wait(self):
        done = False
        while not done:
            try:
                done = self.call('ping')
            except TransportError:
                self._restart()

    def _add_function(self, function_name):
        self.__dict__[function_name] = lambda *args: self.call(function_name, *args)

    def add_functions(self):
        self.wait()
        function_list = self.call('list_functions')
        for fname in map(lambda x: x.decode('utf-8'), function_list):
            self._add_function(fname)            
        logger.debug('Funtions bound: {}'.format(function_list))

    @staticmethod
    def next_available_port(server_address):
        Client.__lock.acquire()
        port = Client.__port_range_start + Client.__next_available_port_offset
        while not is_port_available(server_address, port):
            Client.__next_available_port_offset = (Client.__next_available_port_offset + 1) % Client.__port_range_span
            port = Client.__port_range_start + Client.__next_available_port_offset

        Client.__next_available_port_offset = (Client.__next_available_port_offset + 1) % Client.__port_range_span
        Client.__lock.release()
        return port 

    @staticmethod
    def set_port_range(port_min, port_max):
        range = (min(port_min, port_max), max(port_min, port_max))
        Client.__port_range_start = range[0]
        Client.__port_range_span = range[1] - range[0] + 1

    @classmethod
    def connect(cls):
        """ Returns an instance of the default client"""
        return cls()
