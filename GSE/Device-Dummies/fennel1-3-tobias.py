import os, sys, time, serial

f2 = '5'        # f2 grundmenue
f3 = '5'        # f3 datendatei ausgewählt
zwischencode = '02'
zwischennr = 0
zwischensh = 0
print('=================================================================')
print('                Messungsprogramm für GEO-FENNEL')
print('=================================================================')
print()
while f2 !='0':
    sys.stdout.write(' Hauptmenue-Eingabe: ( "n"Neue Datei / "s"Standpunkt / "m"Messung / "0"ENDE): ')
    f2 = input()
    if f2 == 'n':
        sys.stdout.write('     Neuer Dateiname: (ohne Ext.):')
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
                    baudrate=9600,
                    bytesize=serial.EIGHTBITS,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE) as ser:
            f1 ='5'                  # f1 Messungsmenue - Steuerung Geraet
            if f3 != '0':
                f1 = '0'
                sys.stdout.write('      Keine Neue Datei geladen!!!   Weiter mit <ENTER>')
                warte = input()
            while f1 !='0':
                sys.stdout.write('      Messung-Eingabe: ( "m"Messungsauslösung/ "0"ENDE): ')
                f1 = input()
                if f1 == 'm':
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
                    aufrufasciistring='\x11\x0D\x0A'
                    aufrufbytestring = aufrufasciistring.encode(encoding='ascii')
                    ser.write(aufrufbytestring)
                    datenbytestring = ser.readline()
                    while (f1 == 'm') or (f1 == 'f'):
                        datenasciistring = str(datenbytestring.decode(encoding='ascii'))
                        messdaten = datenasciistring
                        datenteil = messdaten.split()
                        schraeg = datenteil[0]
                        schraeg = float(schraeg)
                        schraeg = schraeg/1000
                        hzgrad = datenteil[2][0:3]
                        hzminuten = datenteil[2][3:5]
                        hzsekunden = datenteil[2][5:7]
                        hzgrad = float(hzgrad)
                        hzminuten = float(hzminuten)
                        hzsekunden = float(hzsekunden)
                        hzminuten = hzminuten/60
                        hzsekunden = hzsekunden/3600
                        hzgr = hzgrad+hzminuten+hzsekunden
                        hzgon = hzgr/9*10
                        vgrad = datenteil[1][0:3]
                        vminuten = datenteil[1][3:5]
                        vsekunden = datenteil[1][5:7]
                        vgrad = float(vgrad)
                        vgrad = vgrad
                        vminuten = float(vminuten)
                        vsekunden = float(vsekunden)
                        vminuten = vminuten/60
                        vsekunden = vsekunden/3600
                        vertgr = vgrad+vminuten+vsekunden
                        vertgon=vertgr/9*10
                        dist = schraeg
                        hz = hzgon
                        v = vertgon
                        print()
                        print(zwischennr,' Hz=',"%.4f"%hz,' SD=',"%.3f"%dist,' V=',"%.4f"%v)
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
                                    fragesh = '0'
                            if fragesh == 'n':
                                zwischennr = zwischennr-1
                                f1 = '1'
                                print()
                            if fragesh == 's':
                                datzeile = str(zwischennr)+'  '+"%.4f"%hz+'  '+"%.3f"%dist+'  '+"%.4f"%v+'  '+"%.3f"%zwischensh+'  '+zwischencode+' \r'
                                datendatei = open(dateiname,'a')
                                datendatei.write(datzeile)
                                datendatei.close()
                                f1 = '1'
                                print()
                            if fragesh == 'f':
                                datzeile = str(zwischennr)+'  '+"%.4f"%hz+'  '+"%.3f"%dist+'  '+"%.4f"%v+'  '+"%.3f"%zwischensh+'  '+zwischencode+' \r'
                                datendatei = open(dateiname,'a')
                                datendatei.write(datzeile)
                                datendatei.close()
                                zwischennr = zwischennr+1
                                f1 = 'f'
                                aufrufasciistring='\x11\x0D\x0A'
                                aufrufbytestring = aufrufasciistring.encode(encoding='ascii')
                                ser.write(aufrufbytestring)
                                datenbytestring = ser.readline()
print('durch')
