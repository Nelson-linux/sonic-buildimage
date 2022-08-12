#!/usr/bin/env python

import os.path
import subprocess
import sys
import re

try:
    from sonic_psu.psu_base import PsuBase
except ImportError as e:
    raise ImportError (str(e) + "- required module not found")


class PsuUtil(PsuBase):
    """Platform-specific PSUutil class"""

    def __init__(self):
        self.path = "/sys/devices/platform/sys_cpld/getreg"
        self.reg = "0xA160"
        PsuBase.__init__(self)

    def run_command(self, command):
        proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE)
        (out, err) = proc.communicate()

        if proc.returncode != 0:
            sys.exit(proc.returncode)
    
        return out

        
    def get_num_psus(self):
        """
        Retrieves the number of PSUs available on the device
        :return: An integer, the number of PSUs available on the device
        """
        return 2

    def get_psu_status(self, index):
        """
        Retrieves the oprational status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is operating properly, False if PSU is faulty
        """
        if index is None:
            return False
        try:
            with open(self.path, "w") as fd:
                fd.write(self.reg)
        except Exception as e:
            return None
        try:
            with open(self.path, "r") as fd:
                status_byte = fd.read()
        except Exception as e:
            return None
        status_byte.replace('0x', '')
        if index == 1:
            failure_detected = (int(status_byte, 16) >> 1) & 1
            input_lost = (int(status_byte, 16) >> 3) & 1
        else:
            failure_detected = (int(status_byte, 16) >> 0) & 1
            input_lost = (int(status_byte, 16) >> 2) & 1
        if failure_detected == 0 or input_lost:
            return False
        else:
            return True

    def get_psu_presence(self, index):
        """
        Retrieves the presence status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is plugged, False if not
        """
        if index is None:
            return False
        try:
            with open(self.path, "w") as fd:
                fd.write(self.reg)
        except Exception as e:
            return None
        try:
            with open(self.path, "r") as fd:
                status_byte = fd.read()
        except Exception as e:
            return None
        status_byte.replace('0x', '')
        if status_byte is None:
            return False
        if index == 1:
            presence = (int(status_byte, 16) >> 3) & 1
        else:
            presence = (int(status_byte, 16) >> 2) & 1

        if presence:
            return False
        else:
            return True

