import serial
import random

def get_measurement():
    measurement_str = ""
    ctrl = random.choice(["033", "048"])
    match ctrl:
        case "048":
            dist = random.randrange(0, 100000, 1)/1000
            hz = random.randrange(0, 360000, 1)/1000
            v = random.randrange(0,90000,1)/1000
            vertgr = v/10*9
            vgrad = int(vertgr)
            vminuten = int((vertgr-vgrad)*60)
            vsekunden = int(((vertgr-vgrad)*60-vminuten)*60)
            hzgr = hz/10*9
            hzgrad = int(hzgr)
            hzminuten = int((hzgr-hzgrad)*60)
            hzsekunden = int(((hzgr-hzgrad)*60-hzminuten)*60)
            schraeg = int(dist*1000)
            measurement_str = f"{schraeg:07d} {vgrad:03d}{vminuten:02d}{vsekunden:02d} {hzgrad:03d}{hzminuten:02d}{hzsekunden:02d} FF" + "\r\n"
            return measurement_str
    return measurement_str

port = serial.Serial(port="/dev/ttyUSB0", baudrate=9600, bytesize=8, parity=serial.PARITY_NONE, stopbits=1.0);

measurement = False

while True:
    res = port.readline()
    cmd = res.decode("utf-8")
    print(res)
    match cmd:
        case "\x11\r\n":
            port.write(get_measurement().encode())
            continue



