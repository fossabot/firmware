# Readme Powahome Firmware
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fpowahome%2Ffirmware.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2Fpowahome%2Ffirmware?ref=badge_shield)

Questo firmware è sviluppato per [ESP-8266](https://www.espressif.com/en/products/hardware/esp8266ex/overview) utilizzando [Non-OS SDK](https://github.com/espressif/ESP8266_NONOS_SDK).

### Immagine Docker

Per semplificare lo sviluppo è possibile utilizzare un container Docker contenente le varie toolchain necessarie, come per esempio [questa immagine](https://hub.docker.com/r/vowstar/esp8266/).

### Compilazione

È disponibile lo script `gen_misch.sh` che permette di compilare il firmware andando a specificare passo passo la configurazione da utilizzare.

Per compilare con la configurazione standard di **Powahome** è presente lo script `autogen.sh`. Occorrerà passare il parametro `1` o `2` (o entrambi) per compilare il firmware per la prima parte o la seconda parte di memoria rispettivamente.

Per esempio per compilare entrambe le parti:
```sh
./autogen.sh 1 2 
```
E questo genererà i file `user1.2048.new.5.bin` e `user2.2048.new.5.bin` nella cartella `bin/upgrade`.

### Flash

>Attenzione: si consiglia di aggiornare i dispositivi utilizzando la procedura di aggiornamento firmware over-the-air (FOTA)

È disponibile lo script `autoflash.sh` che permette di flashare il firmware specificando se flashare la prima o la seconda parte semplicemente passando il parametro `1` o `2` (o entrambi).

Per esempio per flashare entrambi le parti:
```sh
./autoflash.sh 1 2
```

## Descrizione architettura firmware

**NOTA**: la dicitura *macaddress* indica l'indirizzo MAC del dispositivo.

### Modalità "AccessPoint"

Al primo avvio il dispositivo è in modalità *AccessPoint*, ovvero mostra una rete Wi-Fi del tipo "Powahome-*macaddress*" . Lo scopo di questa modalità è dare la possibilità di connettersi al dispositivo per fargli memorizzare dei parametri di configurazione.

Per configurare il dispositivo, collegarsi alla rete Wi-Fi esposta dal dispositivo e mandare la stringa di configurazione, impostando opportunamente i campi all’IP 192.168.5.1 sulla porta 2000. Ad esempio, da ambiente Linux, è possibile mandare la stringa usando il seguente comando (assicurandosi di avere **netcat** installato)

```
echo -e "SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r4\r0\r0\r5\r10\r4\r0\r100\r4\r0\r100\r0\r15000\r10000\r8000\r7000" | netcat 192.168.5.1 2000
```

**Esempi di configurazioni**
Relay a 4 stati:
``` 
SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r4\r0\r0\r5\r10\r4\r0\r100\r4\r0\r100\r0\r15000\r10000\r8000\r7000
```

Relay a 2 stati:
```
SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r4\r0\r0\r5\r10\r2\r0\r100\r2\r0\r100\r0\r15000\r10000\r8000\r7000
```
Luci Normali:
```
SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r0\r0\r0\r5\r10\r2\r0\r100\r2\r0\r100\r0\r15000\r10000\r8000\r7000
```
Roller-Avvolgibile:
```
SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r2\r0\r0\r5\r10\r2\r0\r100\r2\r0\r100\r0\r15000\r10000\r8000\r7000
```
Latched:
```
SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r6\r0\r0\r5\r10\r2\r0\r100\r2\r0\r100\r0\r15000\r10000\r8000\r7000
```
Dimmer:
```
SET\r6\rSSID\rPASS\r0\rtest.mosquitto.org\r8883\r0\r0\r42\r8\r0\r0\r5\r10\r2\r0\r100\r2\r0\r100\r0\r15000\r10000\r8000\r7000
```

#### Descrizione campi

|**Posizione** |**Nome** |**Valore Esempio**|**Valore min**|**Valore max**|
| :- | :- | :- | :- | :- |
|1|Config version|6|-|-|
|2|Nome rete wifi|SSID|-|-|
|3|Password rete wifi|PASS|-|-|
|4|Wifi type (Don’t care)|0|-|-|
|5|Mqtt Host|test.mosquitto.org|-|-|
|6|Mqtt Port|8883|-|-|
|7|Mqtt User (Don’t care)|0|-|-|
|8|Mqtt Passw (Don’t care)|0|-|-|
|9|MQTT KeepAlive /s|42|-|-|
|10|Device type |[0 = Luci con interruttore; 2 = Avvolgibili; 4 = Relay a stati; 6 = Latched (Powahome sostituisce un relè quindi pulsanti collegati su SW1,SW2 e luce su L1,L2; 8 = Dimmer per controllare alimentatori LED gestiti con un pulsante(clic on/off, pressione continua dimming o reset]|-|-|
|11|Device id remoto (Don’t care)|0|-|-|
|12|HTTPS (attivo se =1)|0|-|-|
|13|Tempo chiusura avvolgibile (poi sostituito da tempi apertura e chiusura, campi 22 e 23)|10000|5|60000|
|14|Percentuale apertura corrente avvolgibile|10|0|100|
|15|Numero di stati possibili per relay L1 (sequenze relay ad impulsi)|2|-|-|
|16|Stato iniziale per luci con relay a stati (canale 1)|0|0|campo 15|
|17|Tempo in ms da aspettare tra ON e OFF per Luci con relay a stati (canale 1)|100|-|-|
|18|Numero di stati possibili per relay L2 (sequenze relay ad impulsi)|2|-|-|
|19|Stato iniziale per luci con relay a stati (canale 2)|0|0|campo 18|
|20|Tempo in ms da aspettare tra ON e OFF per Luci con relay a stati (canale 2)|100|-|-|
|21|Tempo in ms da aspettare prima di cominciare a considerare il movimento della tapparella|0|-|-|
|22|Tempo in ms per avere la tapparella completamente aperta|15000|5|600000|
|23|Tempo in ms per avere la tapparella completamente chiusa|12000|5|600000|
|24|Tempo in ms per avere l’accensione completa del dimmer [canale 1]|8000|-|-|
|25|Tempo in ms per avere l’accensione completa del dimmer [canale 2]|7000|-|-|


### Modalità di funzionamento dopo la configurazione
In caso di una configurazione corretta, il dispositivo si riavvierà e tenterà di connettersi alla rete con i parametri specificati nella stringa di configurazione. Il dispositivo utilizza il protocollo MQTT per ricevere comandi ed inviare messaggi.

#### Comandi azionamenti
Di seguito una lista dei messaggi che l'utente può mandare per interrogare/controllare il dispositivo

|#|Descrizione effetto|Topic|Payload (ASCII)|Risposta #|
| :- | :- | :- | :- | :- |
|CA1|Accende luce su L1|/powa/macaddress/sw1/cmd|1|RA1|
|CA2|Accende luce su L2|/powa/macaddress/sw2/cmd|1|RA2|
|CA3|Spegne luce su L1|/powa/macaddress/sw1/cmd|0|RA3|
|CA4|Spegne luce su L2|/powa/macaddress/sw2/cmd|0|RA4|
|CA5|Richiesta stato L1|/powa/macaddress/sw1/cmd|?|RA5|
|CA6|Richiesta stato L2|/powa/macaddress/sw2/cmd|?|RA6|
|CA7|Apre completamente avvolgibile|/powa/macaddress/rb/cmd|100 |RA7|
|CA8|Chiude completamente avvolgibile|/powa/macaddress/rb/cmd|0 |RA8|




#### Risposte azionamenti
Di seguito, una lista dei messaggi che il dispositivo manda in risposta ai precedenti comandi.

|#|Evento scatenante|Topic|Payload (ASCII)|Comando #|
| :- | :- | :- | :- | :- |
|RA1|Luce su L1 accesa da MQTT o fisico|/powa/macaddress/sw1/alert|1|CA1|
|RA2|Luce su L2 accesa da MQTT o fisico|/powa/macaddress/sw2/alert|1|CA2|
|RA3|Luce su L1 spenta da MQTT o fisico|/powa/macaddress/sw1/alert|0|CA3|
|RA4|Luce su L2 spenta da MQTT o fisico|/powa/macaddress/sw2/alert|0|CA4|
|RA5|Stato luce L1|/powa/macaddress/sw1/alert|0 oppure 1|CA5|
|RA6|Stato luce L2|/powa/macaddress/sw2/alert|0 oppure 1|CA6|
|RA7|Avvolgibile aperto|/powa/macaddress/rb/alert|100:100|CA7|
|RA8|Avvolgibile chiuso|/powa/macaddress/rb/alert|0:0|CA8|

#### Aggiornamento firmware over the air (FOTA)

Specificare da quale sito scaricare l’aggiornamento con:

```
Topic: /powa/macaddress/fota/update/user1

Topic: /powa/macaddress/fota/update/user2

Payload: {"link": "https://miosito.it/binario.bin"}
```

#### Codici riavvio dispositivo
Di seguito, una lista dei codici usati dal dispositivo per indicare i motivi dell'ultimo riavvio

|#|Descrizione|Topic|Payload ASCII|
| :- | :- | :- | :- |
|RR1|Riavvio fisico (Power Reboot) (perdita di alimentazione)|/powa/macaddress/diag/rebootreason|0|
|RR2|Riavvio per aggiornamento OTA|/powa/macaddress/diag/rebootreason|1|
|RR3|Riavvio per configurazione device|/powa/macaddress/diag/rebootreason|2|
|RR4|Riavvio dovuto a perdita segnale Wifi |/powa/macaddress/diag/rebootreason|3|
|RR5|Riavvio dovuto a una FW reset|/powa/macaddress/diag/rebootreason|4|
|RR6|Riavvio dovuto a FW Watchdog reset|/powa/macaddress/diag/rebootreason|5|
|RR7|Riavvio dovuto a HW Watchdog|/powa/macaddress/diag/rebootreason|6|
|RR8|Riavvio per Fatal Exception|/powa/macaddress/diag/rebootreason|7|
|RR9|Riavvio per Deep-Sleep|/powa/macaddress/diag/rebootreason|8|
|RR10|Riavvio per mancata comunicazione con Broker MQTT|/powa/macaddress/diag/rebootreason|9|
|RR11|Riavvio dispositivo da  remoto con comando MQTT|/powa/macaddress/diag/rebootreason|10|
|RR12|FOTA fallito|/powa/macaddress/diag/rebootreason|11|
|RR13|Aggiornamento ca\_cert e private\_key|/powa/macaddress/diag/rebootreason|12|
|RR14|Firmware rollback|/powa/macaddress/diag/rebootreason|13|


#### Comandi di diagnosi verso il dispositivo
Di seguito una lista dei messaggi (principalmente di diagnostica) che l'utente può mandare per interrogare/controllare il dispositivo

|#|Descrizione effetto|Topic|Payload (ASCII)|Risposta #|
| :- | :- | :- | :- | :- | 
|CD1|Disabilita PAM (Possibilità di Access Point)|/powa/macaddress/diag/cmd|1|-|
|CD2|Abilita PAM|/powa/macaddress/diag/cmd|2|-|
|CD3|Richiesta Versione e PAM|/powa/macaddress/diag/cmd|3|RD2|
|CD4|Riavvio in Access Point (assicurarsi che la PAM sia abilitata)|/powa/macaddress/diag/cmd|4|-|
|CD5|Richiesta RSSI|/powa/macaddress/diag/cmd|5|RD3|
|CD6|Riavvio HW da remoto|/powa/macaddress/diag/cmd|6|RR11|
|CD7|Richiesta Versione FW|/powa/macaddress/diag/cmd|7|-|
|CD8|Richiesta PAM|/powa/macaddress/diag/cmd|8|-|
|CD9|Richiesta ultima RebootReason|/powa/macaddress/diag/cmd|9|-|


#### Risposte diagnosi

|#|Evento scatenante|Topic|Payload (ASCII)|Comando #|
| :- | :- | :- | :- | :- |
|RD1|Aggiornamento FW completato|/powa/macaddress/diag/rebootreason|1|CD1|
|RD2|Risposta Versione e PAM|/powa/macaddress/diag/alert|{Version:”1.0.6” Pam:”0/1”} |CD4|
|RD3|Risposta RSSI (dBm)|/powa/macaddress/diag/rssi|Int < 10 |CD6|


# License
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
Copyright (C) 2022 Powahome srl
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fpowahome%2Ffirmware.svg?type=large)](https://app.fossa.com/projects/git%2Bgithub.com%2Fpowahome%2Ffirmware?ref=badge_large)

## Third party software
Any portion of the Software that constitutes third party software, including software provided under a public license is licensed to You subject to the terms and conditions of the software license agreements accompanying such third party software.