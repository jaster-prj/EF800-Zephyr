import os, sys, time, serial

f2 = '5'        # f2 grundmenue
f3 = '5'        # f3 datendatei ausgewählt
zwischencode = '02'
zwischennr = 0
zwischensh = 0
while f2 !='0':
    sys.stdout.write(' Hauptmenue-Eingabe: ( "n"Neue Datei / "s"Standpunkt / "m"Messung / "0"ENDE): ')
    f2 = input()
    if f2 == 'n':
        sys.stdout.write('     Neuer Dateiname (ohne Ext.): ')
        dateiname = input()
        dateiname = dateiname+'.dat'
        f3 = '0'
    if f2 == 's':
        if f3 == '0':
            sys.stdout.write(' Eingabe-Standpunkt:\n')
            sys.stdout.write('   Stp: ')
            stp = input()
            stp = stp.upper()
            sys.stdout.write('  Code: ')
            code = input()
            code = code.upper()
            sys.stdout.write('I-Höhe: ')
            ih = input()
            ih = float(ih)
            datzeile = '-'+stp+' '+"%.3f"%ih+' '+code+' \r'
            print(datzeile)
            datendatei = open(dateiname,'a')
            datendatei.write(datzeile)
            datendatei.close()
    if f2 == 'm':
        with serial.Serial(
                    port='COM4',
                    baudrate=1200,
                    bytesize=serial.SEVENBITS,
                    parity=serial.PARITY_ODD,
                    stopbits=serial.STOPBITS_TWO) as ser:
            f1 ='5'                  # f1 Messungsmenue - Steuerung Geraet
            if f3 != '0':
                f1 = '0'
                sys.stdout.write('      Keine Neue Datei geladen!!!   Weiter mit <ENTER>')
                warte = input()
            while f1 !='0':
                sys.stdout.write('  Messung-Eingabe: ( "+"up/ "-"down/ "e"ENTER/ "m"Messungsauslösung/ "0"ENDE): ')
                f1 = input()
                if f1 == '+':
                    asciistring='S12'+chr(13)+chr(10)
                if f1 == '-':
                     asciistring='S11'+chr(13)+chr(10)
                if f1 == 'e':
                    asciistring='S14'+chr(13)+chr(10)
                if f1 == 'm':
                    asciistring='S14'+chr(13)+chr(10)
                if (f1=='+') or (f1=='-')  or (f1=='e')  or (f1=='m'):
                    if f1=='m':
                        zwischennr = zwischennr+1
                        datzeile = '    Neuer Punkt Code: '+zwischencode+' (Halten mit <ENTER> oder Eingabe):'
                        sys.stdout.write(datzeile)
                        eing = input()
                        if len(eing) > 0:
                            zwischencode = eing
                        zwnr = str(zwischennr)
                        datzeile = '    Neuer Punkt Nr: '+zwnr+' (Halten mit <ENTER> oder Eingabe):'
                        sys.stdout.write(datzeile)
                        eing = input()
                        if len(eing) > 0:
                            zwischennr = eing
                            zwischennr = int(zwischennr)
                    bytestring = asciistring.encode(encoding='ascii')
                    ser.write(bytestring)
                    bytestring = ser.readline()
                    if f1=='m':
                        while (f1 == 'm') or (f1 == 'f'):
                            bytestring = ser.readline()
                            if bytestring == b'A\r\n':
                                asciistring='B'+chr(13)+chr(10)
                                bytestring = asciistring.encode(encoding='ascii')
                                print()
                                ser.write(bytestring)
                                bytestring = ser.readline()
                                asciistring = str(bytestring.decode(encoding='ascii'))
                                print()
                                daten=asciistring
                                asciistring='Q'+chr(13)+chr(10)
                                bytestring = asciistring.encode(encoding='ascii')
                                ser.write(bytestring)
                                datenteil = daten.split()
                                k = datenteil[0][1]
                                j = datenteil[0][2]
                                kontroll = k+j
                                if kontroll == '33' :
                                    dist = 0.000
                                    hz = datenteil[1]
                                    hz = hz.rstrip(';')
                                    hz = float(hz)
                                    v = datenteil[2]
                                    v = v.rstrip(';')
                                    v = float(v)
                                    print(zwischennr,' Hz=',"%.4f"%hz,' SD='"%.3f"%dist,' V=',"%.4f"%v)
                                if kontroll == '48' :
                                    dist = datenteil[2]
                                    dist = dist.rstrip(';')
                                    dist = float(dist)
                                    hz = datenteil[3]
                                    hz = hz.rstrip(';')
                                    hz = float(hz)
                                    v = datenteil[4]
                                    v = v.rstrip(';')
                                    v = float(v)
                                    print(zwischennr,' Hz=',"%.4f"%hz,' SD='"%.3f"%dist,' V=',"%.4f"%v,' sh=',"%.3f"%zwischensh)
                                fragesh = '0'
                                while (fragesh != 's')  and (fragesh != 'n') and (fragesh != 'h') and (fragesh != 'f'):
                                    datzeile = 'Eingabe: "h"Stabhoehe/ "s"Speichern/ "n"Nichtspeichern/ "f"Fixmessung :'
                                    sys.stdout.write(datzeile)
                                    fragesh = input()
                                    if fragesh == 'h':
                                        datzeile = ' Neuer Punkt SH: '+"%.3f"%zwischensh+' (Halten mit <ENTER> oder Eingabe):'
                                        sys.stdout.write(datzeile)
                                        eing = input()
                                        if len(eing) > 0:
                                            zwischensh = eing
                                            zwischensh = float(zwischensh)
                                        print()
                                        print(zwischennr,' Hz=',"%.4f"%hz,' SD='"%.3f"%dist,' V=',"%.4f"%v,' sh=',"%.3f"%zwischensh)
                                        print()
                                        fragesh = '0'
                                if fragesh == 'n':
                                    zwischennr = zwischennr-1
                                    f1 = '1'
                                    print()
                                if fragesh == 's':
                                    if kontroll == '48':
                                        datzeile = str(zwischennr)+'  '+"%.4f"%hz+'  '+"%.3f"%dist+'  '+"%.4f"%v+'  '+"%.3f"%zwischensh+'  '+zwischencode+' \r'
                                    if kontroll == '33':
                                        datzeile = str(zwischennr)+'  '+"%.4f"%hz+'  '+"%.3f"%dist+'  '+"%.4f"%v+'  '+"%.3f"%zwischensh+'  '+zwischencode+' \r'
                                    datendatei = open(dateiname,'a')
                                    datendatei.write(datzeile)
                                    datendatei.close()
                                    f1 = '1'
                                    print()
                                if fragesh == 'f':
                                    if kontroll == '48':
                                        datzeile = str(zwischennr)+'  '+"%.4f"%hz+'  '+"%.3f"%dist+'  '+"%.4f"%v+'  '+"%.3f"%zwischensh+'  '+zwischencode+' \r'
                                    if kontroll == '33':
                                        datzeile = str(zwischennr)+'  '+"%.4f"%hz+'  '+"%.3f"%dist+'  '+"%.4f"%v+'  '+"%.3f"%zwischensh+'  '+zwischencode+' \r'
                                    datendatei = open(dateiname,'a')
                                    datendatei.write(datzeile)
                                    datendatei.close()
                                    zwischennr = zwischennr+1
                                    f1 = 'f'
                                    asciistring='S14'+chr(13)+chr(10)
                                    bytestring = asciistring.encode(encoding='ascii')
                                    ser.write(bytestring)
                                    bytestring = ser.readline()
                            else:
                                f1 = '0'
                                zwischennr = zwischennr-1
print('durch')
