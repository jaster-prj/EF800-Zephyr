import serial
import random


def get_measurement():
    measurement_str = ""
    ctrl = random.choice(["033", "048"])
    match ctrl:
        case "033":
            hz = random.randrange(0, 360000, 1)/1000
            v = random.randrange(0,90000,1)/1000
            measurement_str = ctrl + " " + str(hz) + "; " + str(v) + ";\r\n"
            return measurement_str
        case "048":
            dist = random.randrange(0, 100000, 1)/1000
            hz = random.randrange(0, 360000, 1)/1000
            v = random.randrange(0,90000,1)/1000
            measurement_str = ctrl + " dummy " + str(dist) + "; " + str(hz) + "; " + str(v) + ";\r\n"
            return measurement_str
    return measurement_str

port = serial.Serial(port="/dev/ttyUSB0", baudrate=1200, bytesize=7, parity=serial.PARITY_ODD, stopbits=2.0);

measurement = False

while True:
    res = port.readline()
    cmd = res.decode("utf-8")
    print(cmd, end='')
    match cmd:
        case "S11\r\n":
            port.write("A\r\n".encode())
            continue
        case "S12\r\n":
            port.write("A\r\n".encode())
            continue
        case "S14\r\n":
            port.write("A\r\n".encode())
            measurement = True
            continue
        case "B\r\n":
            if measurement:
                port.write(get_measurement().encode())
            continue
        case "Q\r\n":
            port.write("A\r\n".encode())
            measurement = False
            continue



