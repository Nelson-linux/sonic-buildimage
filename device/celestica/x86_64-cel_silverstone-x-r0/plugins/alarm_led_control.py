#!/usr/bin/python

try:
    import signal
    import threading
    import time
    from datetime import datetime

    from sonic_py_common import daemon_base, logger
    from sonic_py_common.task_base import ProcessTaskBase
    from sonic_platform.thermal import Thermal
    from sonic_platform.psu import Psu
    from sonic_platform.fan import Fan
    from sonic_platform.helper import APIHelper
    from misc_sensors import MiscSensors


except ImportError as e:
    raise ImportError(repr(e) + " - required module not found")

try:
    from swsscommon import swsscommon
except ImportError as e:
    from tests import mock_swsscommon as swsscommon

SYSLOG_IDENTIFIER = 'ledctld'
NOT_AVAILABLE = 'N/A'

# Baseboard CPLD file
BASECPLD_SETREG_FILE = "/sys/devices/platform/sys_cpld/setreg"
# Alarm control register in baseboard CPLD
ALAEM_LED_CR_REG = "0xA163"
ALAEM_LED_1HZ = "1"
ALAEM_LED_4HZ = "2"
ALAEM_LED_GREEN = "0x10"
ALAEM_LED_AMBER = "0x20"

# COMe power button control register in baseboard CPLD
COMe_PWRCR_REG="0xA124"
# COMe power off value in baseboard CPLD
COMe_PWRCR_OFF="0"

NUM_THERMAL = 20
NUM_PSU = 2
NUM_MISC_SENSORS_PWR = 75
PSU_NUM_FAN = 2
NUM_FAN_TRAY = 7
NUM_FAN_PER_TRAY = 2

# utility functions

# try get information from platform API and return a default value if caught NotImplementedError
def try_get(callback, default=NOT_AVAILABLE):
    """
    Handy function to invoke the callback and catch NotImplementedError
    :param callback: Callback to be invoked
    :param default: Default return value if exception occur
    :return: Default return value if exception occur else return value of the callback
    """
    try:
        ret = callback()
        if ret is None:
            ret = default
    except NotImplementedError:
        ret = default

    return ret

def alarm_led_blink(hz):
    _api_helper = APIHelper()
    _api_helper.run_command("echo " + ALAEM_LED_CR_REG + " " + hz + " > " + BASECPLD_SETREG_FILE)

def __get_air_flow():
    _api_helper = APIHelper()
    fan_air_flow = _api_helper.read_txt_file("/sys/bus/i2c/devices/i2c-9/9-000d/fan1_direction")
    return 'B2F' if int(fan_air_flow) == 0 else "F2B"

#
# TemperatureUpdater  ======================================================================
#
class TemperatureUpdater(logger.Logger):

    def __init__(self, log_identifier, _thermal_list):
        super(TemperatureUpdater, self).__init__(log_identifier)
        # TODO: Refactor to eliminate the need for this Logger instance
        self.logger = logger.Logger(SYSLOG_IDENTIFIER)
        self._thermal_list = _thermal_list

    def update(self):
        # normal warning; critical warning; non-recoverable warning
        warning = [False, False, False]
        self.log_debug("Start temperature updating")
        for index, thermal in enumerate(self._thermal_list):
            try:
                warning = self._refresh_temperature_status(thermal, index)
                if any(warning):
                    return warning
            except Exception as e:
                self.log_warning('Failed to update thermal status - {}'.format(repr(e)))

        self.log_debug("End temperature updating")

        return warning

    def _refresh_temperature_status(self, thermal, index):
        _api_helper = APIHelper()
        import os
        warning = [True, True, False]
        name = try_get(thermal.get_name, 'Thermal {}'.format(index + 1))

        high_threshold = NOT_AVAILABLE
        low_threshold = NOT_AVAILABLE
        high_critical_threshold = NOT_AVAILABLE
        low_critical_threshold = NOT_AVAILABLE
        temperature = try_get(thermal.get_temperature)
        if temperature != NOT_AVAILABLE and temperature != 0:
            high_threshold = try_get(thermal.get_high_threshold)
            low_threshold = try_get(thermal.get_low_threshold)
            high_critical_threshold = try_get(thermal.get_high_critical_threshold)
            low_critical_threshold = try_get(thermal.get_low_critical_threshold)
        else:
            self.log_warning('Cant get temperature %s value\n' % name)
            return warning

        if high_critical_threshold != NOT_AVAILABLE and temperature > int(high_critical_threshold):
            if name == "TEMP_SW_Internal":
                try:
                    # if TEMP_SW_Internal is higher than critical threshold, perform SW shutdown
                    warning = [True, True, True]
                    self.log_error('Temperature %s: %d higher than critical threshold %d\n' % (name, temperature, high_critical_threshold))
                    self.log_error('SW shutdown due to temperature %s: %d higher than critical threshold %d\n' % (name, temperature, high_critical_threshold))
                    # /var/log/syslog report has delays, to avoid missing log before SW shutdown, use below cmd to collect error again immediately
                    command = "echo \"{} ERROR Temperature {}: {} higher than critical threshold {}\" >> /var/log/messages".format(datetime.now(), name, str(temperature), str(high_critical_threshold))
                    _api_helper.run_command(command)
                    command = "echo \"{} ERROR SW shutdown due to temperature {}: {} higher than critical threshold {}\" >> /var/log/messages".format(datetime.now(), name, str(temperature), str(high_critical_threshold))
                    _api_helper.run_command(command)
                    # collect = os.popen("cat /var/log/syslog | tail -n 10 | grep shutdown").read()
                    # print(collect)
                    # if "TEMP_SW_Internal" in collect:
                        # _api_helper.run_command("echo " + COMe_PWRCR_REG + " " + COMe_PWRCR_OFF + " > " + BASECPLD_SETREG_FILE)
                    _api_helper.run_command("echo " + COMe_PWRCR_REG + " " + COMe_PWRCR_OFF + " > " + BASECPLD_SETREG_FILE)

                except Exception as E:
                    print("error cause:%s" % E)
            else:
                self.log_warning('Temperature %s: %d higher than critical threshold %d\n' % (name, temperature, high_critical_threshold))
                warning = [True, True, False]
        elif high_threshold != NOT_AVAILABLE and temperature > int(high_threshold):
            if name == "TEMP_SW_Internal":
                self.log_warning('Temperature %s: %d higher than threshold %d\n' % (name, temperature, high_threshold))
                warning = [True, True, False]
                # /var/log/syslog report has delays, to avoid missing log before SW shutdown, use below cmd to collect error again immediately
                command = "echo \"{} WARNING Temperature {}: {} higher than threshold {}\" >> /var/log/messages".format(datetime.now(), name, str(temperature), str(high_threshold))
                _api_helper.run_command(command)
            else:
                self.log_notice('Temperature %s: %d higher than threshold %d\n' % (name, temperature, high_threshold))
                warning = [True, False, False]
        elif low_critical_threshold != NOT_AVAILABLE and temperature < int(low_critical_threshold):
            self.log_warning('Temperature %s: %d lower than critical threshold %d\n' % (name, temperature, low_critical_threshold))
            warning = [False, True, False]
        elif low_threshold != NOT_AVAILABLE and temperature < int(low_threshold):
            self.log_notice('Temperature %s: %d lower than threshold %d\n' % (name, temperature, low_threshold))
            warning = [True, False, False]
        else:
            warning = [False, False, False]

        return warning


#
# MiscSensorsUpdater  ======================================================================
#
class MiscSensorsUpdater(logger.Logger):

    def __init__(self, log_identifier, _miscsensors_list):
        super(MiscSensorsUpdater, self).__init__(log_identifier)
        # TODO: Refactor to eliminate the need for this Logger instance
        self.logger = logger.Logger(SYSLOG_IDENTIFIER)
        self._miscsensors_list = _miscsensors_list

    def update(self):
         # normal warning; critical warning; non-recoverable warning
        warning = [False, False, False]
        self.log_debug("Start miscellaneous updating")
        for index, miscsensors in enumerate(self._miscsensors_list):
            try:
                warning = self._refresh_miscsensors_status(miscsensors, index)
                if any(warning):
                    return warning
            except Exception as e:
                self.log_warning('Failed to update miscsensors status - {}'.format(repr(e)))

        self.log_debug("End miscsensors or power updating")

        return warning

    def _refresh_miscsensors_status(self, miscsensors, index):
        warning = [True, True, False]
        name = try_get(miscsensors.get_name, 'miscsensors {}'.format(index + 1))

        high_threshold = NOT_AVAILABLE
        low_threshold = NOT_AVAILABLE
        high_critical_threshold = NOT_AVAILABLE
        low_critical_threshold = NOT_AVAILABLE
        value = try_get(miscsensors.get_miscsensors_value)
        if value != NOT_AVAILABLE and value != 0:
            high_threshold = try_get(miscsensors.get_high_threshold)
            low_threshold = try_get(miscsensors.get_low_threshold)
            high_critical_threshold = try_get(miscsensors.get_high_critical_threshold)
            low_critical_threshold = try_get(miscsensors.get_low_critical_threshold)
        else:
            self.log_warning('Cant get sensor %s value\n' % name)
            return warning

        if high_critical_threshold != NOT_AVAILABLE and value > int(high_critical_threshold):
            if name == "PSU1_VOut" or name == "PSU2_VOut":
                self.log_error('sensor %s: %d higher than critical threshold %d\n' % (name, value, high_critical_threshold))
                warning = [True, True, True]
            else:
                self.log_warning('sensor %s: %d higher than critical threshold %d\n' % (name, value, high_critical_threshold))
                warning = [False, True, False]
        elif high_threshold != NOT_AVAILABLE and value > int(high_threshold):
            if name == "PSU1_VOut" or name == "PSU2_VOut":
                self.log_warning('sensor %s: %d higher than threshold %d\n' % (name, value, high_threshold))
                warning = [True, True, False]
            else:
                self.log_notice('sensor %s: %d higher than threshold %d\n' % (name, value, high_threshold))
                warning = [True, False, False]
        elif low_critical_threshold != NOT_AVAILABLE and value < int(low_critical_threshold):
            self.log_warning('sensor %s: %d lower than critical threshold %d\n' % (name, value, low_critical_threshold))
            warning = [False, True, False]
        elif low_threshold != NOT_AVAILABLE and value < int(low_threshold):
            self.log_notice('sensor %s: %d lower than threshold %d\n' % (name, value, low_threshold))
            warning = [True, False, False]
        else:
            warning = [False, False, False]

        return warning

#
# PsuUpdater  ======================================================================
#
class PsuUpdater(logger.Logger):

    def __init__(self, log_identifier, _psu_list):

        super(PsuUpdater, self).__init__(log_identifier)
        # TODO: Refactor to eliminate the need for this Logger instance
        self.logger = logger.Logger(SYSLOG_IDENTIFIER)
        self._psu_list = _psu_list

    def update(self):
        warning = False
        self.log_debug("Start PSU updating")
        for index, psu in enumerate(self._psu_list):
            try:
                warning = self._refresh_psu_status(psu, index)
                if warning is True:
                    return True
            except Exception as e:
                self.log_warning('Failed to update psu status - {}'.format(repr(e)))

        self.log_debug("End PSU updating")

        return False

    def _refresh_psu_status(self, psu, index):
        name = try_get(psu.get_name, 'psu {}'.format(index + 1))

        presence = try_get(psu.get_presence)
        if presence is True:
            powergood_status = try_get(psu.get_powergood_status)
        else:
            self.log_warning("%s is not present\n" % name)
            return True

        if powergood_status is False:
            self.log_warning('%s status is not OK\n' % name)
            return True
        else:
            return False

            #
# FanUpdater  ======================================================================
#
class FanUpdater(logger.Logger):

    def __init__(self, log_identifier, _fan_list):

        super(FanUpdater, self).__init__(log_identifier)
        # TODO: Refactor to eliminate the need for this Logger instance
        self.logger = logger.Logger(SYSLOG_IDENTIFIER)
        self._fan_list = _fan_list

    def update(self):
        warning = False
        self.log_debug("Start fan updating")
        for index, fan in enumerate(self._fan_list):
            try:
                warning = self._refresh_fan_status(fan, index)
                if warning is True:
                    return True
            except Exception as e:
                self.log_warning('Failed to update fan status - {}'.format(repr(e)))

        self.log_debug("End fan updating")

        return False

    def _refresh_fan_status(self, fan, index):
        name = try_get(fan.get_name, 'fan {}'.format(index + 1))

        presence = try_get(fan.get_presence)
        if presence is True:
            status = try_get(fan.get_status)
        else:
            self.log_warning("%s is not present\n" % name)
            return True

        if status is False:
            self.log_warning('%s status is not OK\n' % name)
            return True
        else:
            return False

#
# Main =========================================================================
#
def main():
    _miscsensors_list = []
    _psu_list = []
    _thermal_list = []
    _fan_list = []

    # initialize thermal object
    airflow = __get_air_flow()
    for index in range(0, NUM_THERMAL):
        _thermal =  Thermal(index, airflow)
        _thermal_list.append(_thermal)

    # initialize PSU object
    for index in range(0, NUM_PSU):
        _psu = Psu(index)
        _psu_list.append(_psu)

    # initialize miscellaneous object
    for index in range(0, NUM_MISC_SENSORS_PWR):
        _miscsensors = MiscSensors(index)
        _miscsensors_list.append(_miscsensors)

    # initialize fan object
    for fant_index in range(0 , NUM_FAN_TRAY):
        for fan_index in range(0, NUM_FAN_PER_TRAY):
            fan = Fan(fant_index, fan_index)
            _fan_list.append(fan)
    for fan_index in range(0, PSU_NUM_FAN):
        fan = Fan(fan_index, 0, is_psu_fan=True, psu_index=fan_index+1)
        _fan_list.append(fan)

    # chassis = sonic_platform.platform.Platform().get_chassis()
    temperature_warning = [False, False, False]
    miscsensors_warning = [False, False, False]
    psu_warning = False
    fan_warning = False

    while True:
        Temperature = TemperatureUpdater(SYSLOG_IDENTIFIER, _thermal_list)
        temperature_warning = Temperature.update()

        PSU = PsuUpdater(SYSLOG_IDENTIFIER, _psu_list)
        psu_warning = PSU.update()

        FAN = FanUpdater(SYSLOG_IDENTIFIER, _fan_list)
        fan_warning = FAN.update()

        Miscsensors = MiscSensorsUpdater(SYSLOG_IDENTIFIER, _miscsensors_list)
        miscsensors_warning = Miscsensors.update()
        # print(temperature_warning)
        # print(miscsensors_warning)
        # print(psu_warning)
        # print(fan_warning)
        if any(temperature_warning) or any(miscsensors_warning) or psu_warning or fan_warning:
            if temperature_warning[2] is True or miscsensors_warning[2] is True:
                alarm_led_blink(ALAEM_LED_AMBER)
                # print("AMBER")
            elif temperature_warning[1] is True or miscsensors_warning[1] is True or psu_warning or fan_warning:
                alarm_led_blink(ALAEM_LED_4HZ)
                # print("4Hz")
            else:
                alarm_led_blink(ALAEM_LED_1HZ)
                # print("1Hz")
        else:
            alarm_led_blink(ALAEM_LED_GREEN)
        time.sleep(2)


if __name__ == '__main__':
    main()
