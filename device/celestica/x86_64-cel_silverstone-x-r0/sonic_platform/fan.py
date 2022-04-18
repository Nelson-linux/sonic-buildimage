#!/usr/bin/env python
# -*- coding: utf-8 -*-

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the fan status which are available in the platform
#
#############################################################################
import math
import os
try:
    from sonic_platform_base.fan_base import FanBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

FAN_NAME_LIST = ["FAN-1F", "FAN-1R", "FAN-2F", "FAN-2R",
                 "FAN-3F", "FAN-3R", "FAN-4F", "FAN-4R",
                 "FAN-5F", "FAN-5R", "FAN-6F", "FAN-6R",
                 "FAN-7F", "FAN-7R"]
# Bus Path Config
Fan_Speed = "/sys/bus/i2c/devices/i2c-9/9-000d/fan{}_{}_speed_rpm"
Psu_Fan_Speed = "/sys/bus/i2c/devices/i2c-7/7-00{}/hwmon/{}/fan1_input"
Fan_Direction = "/sys/bus/i2c/devices/i2c-9/9-000d/fan{}_direction"
Fan_Target_Speed = "/sys/bus/i2c/devices/i2c-9/9-000d/pwm{}"
Fan_Set_Speed = "/sys/bus/i2c/devices/i2c-9/9-000d/pwm{}"
Fan_Set_Led = "/sys/bus/i2c/devices/i2c-9/9-000d/fan{}_led"
Fan_Get_Led = "/sys/bus/i2c/devices/i2c-9/9-000d/fan{}_led"
Fan_Presence = "/sys/bus/i2c/devices/i2c-9/9-000d/fan{}_present"

# define fan max(min) speed
MAX_OUTLET = 30200  # F2B EXHAUST
MAX_INLET = 32000  # B2F INTAKE

# Allowable error range between pwm and actual value
SPEED_TOLERANCE = 10

# Fan led value
FAN_LED_OFF_VALUE = "3"
FAN_LED_AMBER_VALUE = "2"
FAN_LED_GREEN_VALUE = "1"

# PSU type
psu_intake_list = ["SAC1500D12RA", "TDPS1500AB6C", "TDPS1500AB7C"]
psu_exhaust_list = ["TDPS1500AB6A", "SAC1500D12AA", "TDPS1500AB6B", "TDPS1500AB6D", "TDPS1500AB6E"]


class Fan(FanBase):
    """Platform-specific Fan class"""

    def __init__(self, fan_tray_index, fan_index=0, is_psu_fan=False, psu_index=0):
        self.fan_index = fan_index
        self.fan_tray_index = fan_tray_index
        self.is_psu_fan = is_psu_fan
        if self.is_psu_fan:
            self.psu_index = psu_index
        self._api_helper = APIHelper()
        self.index = self.fan_tray_index * 2 + self.fan_index

    def get_direction(self):
        """
        Retrieves the direction of fan
        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        direction = self.FAN_DIRECTION_EXHAUST
        if not self.is_psu_fan:
            fan_tray_index_ = self.fan_tray_index + 1
            direction_value = self._api_helper.read_txt_file(Fan_Direction.format(fan_tray_index_))
            if int(direction_value) == 0:
                direction = self.FAN_DIRECTION_INTAKE
        else:

            tdps = os.popen("i2cdump -y -f -r 0x2c-0x37 7 0x5%s b "
                            "| awk '{print substr($0, length($0)-15)}' | sed -n 2p"
                            % self.fan_tray_index).read().strip()
            number = os.popen("i2cdump -y -f -r 0x2c-0x37 7 0x5%s b "
                              "| awk '{print substr($0, length($0)-15)}' | sed -n 3p"
                              % self.fan_tray_index).read().strip()
            psu_type = tdps + number
            if psu_type in psu_intake_list:
                direction = self.FAN_DIRECTION_INTAKE
            elif psu_type in psu_exhaust_list:
                direction = self.FAN_DIRECTION_EXHAUST
            else:
                # NOT Support
                direction = "NA"
        return direction

    def get_speed(self):
        """
        Retrieves the speed of fan as a percentage of full speed
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)
        Note:
            M = 150
            Max F2B = 12600 RPM
            Max B2F = 10300 RPM
        """
        speed = 0
        max_rpm = MAX_INLET if self.fan_index % 2 == 0 else MAX_OUTLET
        if not self.is_psu_fan:
            fan_tray_index_ = self.fan_tray_index + 1
            fan_r_f = "rear" if self.fan_index % 2 == 0 else "front"
            rpm_speed = self._api_helper.read_txt_file(Fan_Speed.format(fan_tray_index_, fan_r_f))
            if rpm_speed:
                rpm_speed = int(rpm_speed)
                speed = int(rpm_speed * 100 / max_rpm)
            else:
                rpm_speed = 0
        else:
            if self.psu_index == 1:
                psu_fan_bus = "58"
            else:
                psu_fan_bus = "59"
            bus_path = os.popen("ls /sys/bus/i2c/devices/i2c-7/7-00%s/hwmon" % psu_fan_bus).read().strip()
            rpm_speed = self._api_helper.read_txt_file(Psu_Fan_Speed.format(psu_fan_bus, bus_path))
            if rpm_speed:
                rpm_speed = int(rpm_speed)
                speed = int(rpm_speed * 100 / max_rpm)
            else:
                rpm_speed = 0

        return speed if speed <= 100 else rpm_speed

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 255 (full speed)
        Note:
            speed_pc = pwm_target*100/255
        """
        if not self.is_psu_fan:
            pwm = self._api_helper.read_txt_file(Fan_Target_Speed.format(self.fan_tray_index+1))
            target = math.ceil(float(pwm) * 100 / 255) if pwm else "N/A"

        else:
            # PSU Fan Not Support
            target = "N/A"
        return target

    @staticmethod
    def get_speed_tolerance():
        """
        Retrieves the speed tolerance of the fan
        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        return SPEED_TOLERANCE

    def set_speed(self, speed):
        """
        Sets the fan speed
        Args:
            speed: An integer, the percentage of full fan speed to set fan to,
                   in the range 0 (off) to 255 (full speed)
        Returns:
            A boolean, True if speed is set successfully, False if not
        # """
        if int(speed) > 255:
            print("Error:Set the fan speed value should be between 0 and 255")
            return False
        if not self.is_psu_fan:
            try:
                with open(Fan_Set_Speed.format(self.fan_tray_index + 1), "w") as f:
                    f.write(speed)
                return True
            except Exception as E:
                print("Error: Set fan speed has error, cause '%s'" % E)
                return False

    def set_status_led(self, color):
        """
        Sets the state of the fan module status LED
        Args:
            color: A string representing the color with which to set the
                   fan module status LED
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        if not self.is_psu_fan:
            led_value = {
                self.STATUS_LED_COLOR_GREEN: FAN_LED_GREEN_VALUE,
                self.STATUS_LED_COLOR_AMBER: FAN_LED_AMBER_VALUE,
                self.STATUS_LED_COLOR_OFF: FAN_LED_OFF_VALUE
            }.get(color)
            try:
                with open(Fan_Set_Led.format(self.fan_tray_index+1), "w") as f:
                    f.write(led_value)
            except Exception as E:
                print("Error: Set fan%s led fail! cause '%s'" % (self.fan_tray_index+1, E))
                return False
        else:
            print("Error, Couldn't set PSU led status")
            return False
        return True

    def get_status_led(self):
        """
        Gets the state of the fan status LED
        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        Note:
            STATUS_LED_COLOR_GREEN = "green"
            STATUS_LED_COLOR_RED = "red"
            STATUS_LED_COLOR_OFF = "off"
        """
        if not self.is_psu_fan:
            with open(Fan_Get_Led.format(self.fan_tray_index+1), "r") as f:
                color = f.read().strip()
            status_led = {
                "off": self.STATUS_LED_COLOR_OFF,
                "green": self.STATUS_LED_COLOR_GREEN,
                "amber": self.STATUS_LED_COLOR_AMBER,
            }.get(color, self.STATUS_LED_COLOR_OFF)

        else:
            status_led = "NA"
            print("Error: Not Support Get PSU LED Status")
        return status_led

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """

        if not self.is_psu_fan:
            fan_name = FAN_NAME_LIST[self.fan_tray_index * 2 + self.fan_index]
        else:
            fan_name = "PSU-{} FAN-1".format(self.psu_index)
        return fan_name

    def get_presence(self):
        """
        Retrieves the presence of the FAN
        Returns:
            bool: True if FAN is present, False if not
        """
        presence = False
        if not self.is_psu_fan:
            with open(Fan_Presence.format(self.fan_tray_index + 1), "r") as f:
                presence_value = int(f.read().strip())
            if presence_value == 0:
                presence = True
        else:
            if self.psu_index == 1:
                psu_fan_bus = "58"
            else:
                psu_fan_bus = "59"
            bus_path = os.popen("ls /sys/bus/i2c/devices/i2c-7/7-00%s/hwmon" % psu_fan_bus).read().strip()
            with open(Psu_Fan_Speed.format(psu_fan_bus, bus_path), "r") as f:
                rpm_speed = int(f.read().strip())
            if int(rpm_speed) != 0:
                presence = True
        return presence

    @staticmethod
    def get_model():
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        # Not Support
        model = "Unknown"
        return model

    @staticmethod
    def get_serial():
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        # Not Support
        serial = "Unknown"
        return serial

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """

        return self.get_presence() and self.get_speed() > 0
